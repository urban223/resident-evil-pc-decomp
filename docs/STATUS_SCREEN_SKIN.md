# Status screen reskin — layout spec and implementation notes

The Space GUI reskin of the inventory/status screen, approved from mock #9
(`menu_holo9`) and since taken well past it. Everything below is in the game's
320x240 logical space.

**Status: implemented, deployed and play-tested.** The runtime is
`src/game/UiSkin.{h,cpp}` on top of `src/game/UiAtlas.{h,cpp}`; `MainMenu.cpp`
fills its state struct and gates the original chrome off.

Visual reference: RE2 Remake's arrangement and its health gauge and item action
menu, with GF2 Exilium's holographic treatment (translucent over the frozen
room, running glints, technical filler).

## Deploy trap — read this first

`bin/*/config.ini` ships with `[Assets] Path=` **empty**, which means the game
reads its data tree from **next to the executable**: `bin/Debug/USA/…` and
`bin/Release/USA/…`, each its own copy. `assets/USA/…` is the source of truth
in a checkout but is NOT what a run loads. See `docs/ASSETS.md`.

This cost a debugging round: the build compiled a new `AchievementAtlasData.h`
with new rects while the atlas the game loaded still had transparent pixels
there, so new art simply did not appear — silently, with nothing to catch it.
The bakers now write every tree that exists. Any new runtime asset must too.

A second, unrelated trap: a source file open in Visual Studio can be written
back from its editor buffer over an external change. One edit was lost that
way. After deploying, verify rather than assume.

## Where the original geometry came from
Decoded by hand from `MenuData.cpp`'s `g_MenuFrameDataBlock[712]` and the walks
in `menu_draw_inventory`:

- `load_main_menu_frame_part_tex_area` reads a **12-byte record backwards**:
  `screenX, screenY (short) | width, height (ushort) | texU, texV (byte)`.
- Three tables use a **14-byte** form: a flags/repeat word first
  (`flags & 0xC0` = mirror bits, low byte = repeat count, bit 7 = vertical).
- `g_inventorySlotsPos` (offset 656) = 14 (x,y) pairs: entries 0-3 the tab
  buttons, 4..11 the inventory slots. Original slots are 2 columns of 40x30.

**The table was not rewritten.** `UiSkin_SlotRect`, `UiSkin_EquippedRect`,
`UiSkin_OtherRect` and `UiSkin_PortraitRect` hand the engine positions instead,
and `menu_draw_inventory` uses them while the skin is on. Turning the skin off
restores the original screen exactly, and the item box — which reads the same
table — is untouched.

## Layout

### Right column
| element | rect |
|---|---|
| tab labels (MAP / FILE / RADIO / EXIT) | y 6, x 200 + n*29, 27 wide |
| rule under the tabs | y 19, x 127..312 |
| grid | 4 cols x 4 rows, cell 44x34, gap 3, origin (127, 23) |
| cell (c,r) | (127 + c*47, 23 + r*37) |
| rule under the grid | y 173 |
| info card | x 124..313, y 175..226, hatched top-right corner |
| item name | (127, 178); id chip / code at (267, 181) / (291, 181) |
| amber rule | y 190 |
| type strip | (124..170, 193..202) |
| description | one line at y 206 (small) or 203 (examine size) |
| capacity bar | y 228..231, 16 segments of 8 from x 127, step 11 |

Eight slots live in the first two rows; the grid is laid out for 16. Cells past
`slotCount` draw the empty-slot cross.

### Left column
| element | rect |
|---|---|
| CHARACTER label / portrait cell | (8, 14) / (8, 22, 38, 34), 30x30 portrait at (12, 24) |
| name / affiliation | (50, 26) / (50, 39) |
| EQUIPPED label / cell | (8, 62) / (8, 70, 44, 34), icon at (10, 72) |
| OTHER label / cell | (8, 110) / (8, 118, 44, 34) |
| CONDITION label / block | (8, 156) / (8, 164, 94, 44) |
| EKG band inside it | (10, 168, 90, 26), baseline 19 down the band |
| status word | (13, 197) |
| POINTS | label (8, 217), value right-aligned to x 102 |
| pip rows | y 226 |
| service line | (8, 232) |

The left column never crosses **x 120**; x 121..126 is the gutter and the card
starts at 124.

### Decoration — and the rule that keeps it out of the way
Filler only ever draws in dead space: the band above the panels (y 2..12), the
gutter (x 104..126), the screen margins (x 1..5 and x 314..318) and the strip
under the description. It never draws where a label or a block already is —
that was the bug in the first pass, where the tick column sat on the EQUIPPED
and OTHER labels.

Present: signal bars (each breathing on its own period), a cycling segment row,
a blinking record dot, a numbered gutter ladder whose labelled ticks are
counted **from the bottom** so the run ends on a number at the card's lower
edge, diamond and ring pip rows, a capacity bar, an ID chip, corner brackets
and amber side markers on the selected cell, two edge rails with drifting ticks
and a running highlight, and a service line at the bottom left that cycles six
entries, typing each in with a cursor.

240 is the last row of the screen: a second service line at y 238 ran off the
bottom half-drawn. One line only.

## Animation
All of it is a function of the presented-frame counter (`g_menuSkinFrame`):
border glint, signal bars, segment row, blinking dot, edge rails, the service
line, and the EKG's own sweep column, which is driven by the game's real head
position (`DAT_00ae9f36` as a permille) rather than a period of its own.

## How it is put together

### Depths
Everything goes through `UiAtlas_Push` into the pending-sprite list at an
explicit depth, so the skin sorts *against* the engine's sprites. Item icons
are command sprites at `depth*16 + 500` (644-676), which sets the bands:

| depth | what |
|---|---|
| 1000 | scrim over the frozen room |
| 700 / 699 / 698 | plates and cells / the empty-slot cross / the EKG screen |
| 690 | text on those plates |
| 600 | cursor brackets, markers, glints |
| 560 / 550 | the item action menu and its rows |
| 490 | the banner under a menu message |

**Keep the draw ORDER byte-identical when refactoring.** The skin puts several
things in one depth band on purpose and assumes the later push wins; reordering
the calls is a behaviour change even when nothing about the data moved.

### Engine changes
1. **The room shows through.** `OT_InsertPrimitive`'s exception for the F1
   debug menu now also covers `UiSkin_RoomVisible()`. MainMenu sets that only
   while the status screen itself is on top — a TAB screen (map, file) draws
   its own opaque background and must have the room dropped, while the item's
   action menu is just a panel over this same screen.
2. **`PendingSprite_Push` / `PendingSprite_PushEx`** in `Rendering.cpp`, plus a
   `sampler` field. LINEAR for panels and TTF fonts; POINT for the hand-plotted
   marks — filtering a ten-texel icon up to forty screen pixels smears it.
3. **`MAX_PENDING_SPRITES` 512 -> 1024.** The skin alone emits ~510 quads.
4. **The pending sort had to become stable.** The old selection-swap version
   left equal-depth sprites in whatever order the swaps produced. Now an
   insertion sort.
5. **Line primitives carry a width** (`SpriteRenderer_SetLineWidth`, in game
   pixels, scaled at draw time) and **round caps** in both backends. A polyline
   arrives one segment at a time; flat ends leave a wedge of missing pixels on
   the outside of every direction change, which turned the thick EKG into a
   chain of notches. Measured against a true round join over all 20 wave
   tables: 0.72% of the stroke missing, 0.01% excess. Square caps left 3.8%
   missing AND visible barbs.
6. **The original chrome is gated off**: tab sprites, all five frame-part
   walks, empty-slot tiles, masking rects, `menu_draw_cursor`, the move-cursor
   arrows, the 32x8 condition strip, and `FUN_00454fd0` for mode 0.
7. **Four-column cursor navigation** (`skin_nav_grid`), on both the browse
   cursor `DAT_00ae9f23` and the move cursor `DAT_00ae9f25`. Left/right steps a
   column with wrap inside the row (the last row can be short — Chris has
   4 + 2); up/down steps a row; leaving the grid vertically lands on the tab
   above that column. The original arithmetic is kept in the `else` branch.

### The EKG is the game's own
`menu_draw_health_bar` still builds the trace from `ekg_wave_00..19`. All five
line submissions go through `skin_ekg_line`, which remaps the endpoints from
the original box (x `0x54..0x83`, baseline `0xa2`) into `UiSkin_EkgRect` and
**puts the struct back** — the caller reads X0/X1 again for the gradient.

- Vertical window `0x91..0xa8`: the wave tables' own range plus a pixel of
  slack, NOT the `0x92..0xb0` the heal sweep uses, which would squash every
  beat into the top third.
- The band is never made *shorter* than that window (23 px): shrinking
  collapses neighbouring samples onto one row, which is what made the trace a
  staircase at 15 px. Stretching is harmless.
- The amplitudes run opposite to the names: **FINE is the strong beat** (waves
  12..15, 17 px) and DANGER the weak nearly-flat one (0..3, 6 px) — a dying
  heart is what the trace describes. Poison (16..19) is widest at 20 px.
- Colour comes from the live EKG globals `DAT_00ae9f30..32`, so the word and
  the trace can never disagree. Poison is the exception: the game has no colour
  for it, so the skin supplies `A846DC` and tints the line to match.
- The sweep column's two trailing columns are clamped to the band.

### Item descriptions exist
`g_ItemDescriptions[82]` (0x004c6160), two lines per item, read by
`set_item_description_message`. They are a table of their own, which is why
`set_message_display` cannot reach them and why they were missed at first.
`skin_item_desc` decodes them back to ASCII (`encodeChar` inverted, `0x02`
newline, `0x78` opening quote, `0x05`/`0x06` tags skipped) and **joins the two
lines**: the break sits at the original's 26-column margin and means nothing at
the card's width. Verified by round-trip over all 52 strings.

Item *names* go through `ItemName_ToAscii` (declared in `Globals.h`), which the
editor's content browser also uses — so the status screen and the editor cannot
spell a name differently.

The card re-wraps to its own width (`wrap_text`). Measured: 182 px available,
longest description 83 px small / 124 px at examine size.

### The action menu
A Space GUI panel beside the selected cell, as in RE2 Remake — overlapping the
neighbouring cells is part of that look. 82x46, opens right, or left in the two
right-hand columns; on the bottom row it hangs from the cell's lower edge.
Grows and shrinks on the game's own counter `DAT_00ae9f27`, so the sound timing
still matches. Row 0 reads EQUIP or USE by `DAT_00ae9f1c`, as the original box
did. Opaque, not translucent: it covers the inventory, and the scrim look
belongs to surfaces under the icons, not to a menu over them.

### Where the data comes from
| field | source |
|---|---|
| slot / held counts | `g_totalInventorySlots`, `g_TotalHeldItems` |
| selected slot | `DAT_00ae9f23`, `slot = (c >> 1) - 4`, -1 on the tab row |
| equipped slot | `g_EquippedItemId - 1` (1-based slot index) |
| character | `g_playerEntity.id & 1` |
| condition | `DAT_00ae9f2f` -> word, `DAT_00ae9f30..32` -> colour |
| item name / description | `message_item_name_lookup` / `g_ItemDescriptions` |
| item type | derived from the id ranges in `Types.h` |
| POINTS / pips | `Achievements_Points()` / `Achievements_UnlockedCount()` |
| action menu | `DAT_00ae9f27`, `DAT_00ae9f24`, `DAT_00ae9f1c` |
| message banner | `g_menu_choice_id & 0x80`, `g_MessageScreenY` |

## Verification method
Geometry and rendering are checked by building the shipping `UiSkin.cpp`
against a stub atlas that rasterises the queue into an image (a scratch
harness, not in the repo): it links the real file, collects every quad, sorts
by depth far-first the way `FrameRateGovernor` does, samples the real
`achvui.bin` and writes a PPM. The same approach settled the cap-shape question
by rasterising the actual quad geometry against a round-join reference.

Worth keeping: judging these things from a drawing tool's output is what
produced two wrong conclusions. Rasterise what the engine will actually emit.

## Still open
- **OTHER slot** is display only: the cell and label draw, nothing fills it.
- **POINTS** totals achievement points (50/50/150).
- The item box screen (`itembox_draw_cursor`, `draw_itembox_menu`) is untouched
  and still uses the original two-column geometry.
- Long item names are not measured against the card width (descriptions are).
