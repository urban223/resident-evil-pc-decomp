# Title screen menu and the EXTRA/RAID screen (port-only)

Replaces the original two-item title with a four-item, left-aligned menu that
opens only after the game has spoken its own name, plus the EXTRA screen behind
it. Lives in `src/game/TitleScreen.cpp`; the word art, marks and grain are baked
by `tools/build_achievement_ui.py` into the shared atlas, the eye by
`tools/build_raid_eye.py`, and the two sounds by `tools/build_raid_sfx.py` and
`tools/build_raid_bgm.py`.

## The flow

1. **PRESS ANY BUTTON** — unchanged, `g_titleMode = 0`, step 2.
2. **Press** → `play_sfx(SFX_BANKS, SFX_TITLE_EVIL01)` starts the title phrase,
   the prompt stops being drawn, and a white flash is armed.
3. **The phrase plays** over the title art. Further presses are ignored on
   purpose — the phrase is the opening beat, not something to click past.
4. **Handover** — the title art fades out to black, the backdrop is swapped
   under full black, and the fade back in is armed in the same frame.
5. **Menu** — `g_titleMode = 1`, step 1 holds until the black clears, step 2 is
   the live list.

### Voice state machine (`s_titleVoiceState`)

| state | meaning |
|---|---|
| 0 | nothing started |
| 1 | asked for it, waiting for the mixer to actually begin |
| 2 | seen it start, waiting for it to end |
| 3 | it never started — hold `TITLE_VOICE_NOMINAL` frames instead |
| 5 | phrase over, fading the title art down to black |
| 4 | done — the menu has it from here |

**Never conclude "finished" from `getSndStat` before the sound has been SEEN
playing.** `play_sfx` is asynchronous; XAudio2 has not queued the buffer a few
frames in. The first version asked six frames after `play_sfx`, read "not
playing", opened the menu instantly while the line played on underneath, and
the still-held pad was taken as a confirm — the screen flashed straight into
character select. Hence states 1/2 and `TITLE_VOICE_START_FRAMES = 90` of
patience. `s_titleMenuGuard` (8 frames, re-armed while the pad is still down)
covers the same press on the menu side.

## Fades: the trap

`g_fade_type_id` is **not a brightness** — `draw_rect` reads it as a variant:

- `1` → the **white** overlay
- `2` → the **black** overlay

Setting it to 2 and expecting a flash gives a black veil instead, which is what
happened the first time round and produced "no white flash".

`fade_update()` only arms from a level of **0 or less**, and what the level is
at any given moment is left over from the previous fade — so set
`g_fading_state` explicitly before calling it rather than assuming. A negative
counter makes `fade_update` start at full (`0x7FFF`) and thin out — a fade IN;
a positive counter starts at 0 and builds — a fade OUT. `0x400` a frame ≈ 32
frames ≈ 1 s at the 30 Hz tick.

The handover arms the fade-in in the **same frame** as the backdrop swap. Going
through menu step 0 instead would arm it a frame later, and that one frame
shows the new backdrop at full brightness — a flash of the menu before its own
entrance.

## Layout

Coordinates are in the ORIGINAL's centre-relative space (its 256-wide block
sits at `screenX -130`), and `title_blit` adds `g_ScreenOffsetX` the way the
engine's own 2D does — so the menu cannot drift away from the art behind it.

```
TITLE_MENU_X     (-132)   // shared left edge
TITLE_MENU_Y     (-48)    // first row at y 72
TITLE_MENU_STEP  (24)
TITLE_COPY_X/Y   (-128) / (96)
```

Backdrop is `data\SEL_BACK.PIX` (the file-select plate) via
`title_menu_backdrop()`; the title art is a full-bleed wordmark with nowhere for
a list to live.

## The title art: backdrop and wordmark, split

The stock `data\title.pix` is ONE picture — the RESIDENT EVIL wordmark burnt
into an eye — so the room and the name are the same pixels and neither can be
changed without the other. `tools/build_title_bg.py` splits them:

| file | what | drawn by |
|---|---|---|
| `data\titlebg.pix` | the backdrop, 320×240 ABGR1555 | `display_image` in `init_title_screen` |
| `data\titlelogo.bin` | the wordmark, `'RLG1'` + w + h + RGBA | `title_draw_logo()` |

The wordmark is the game's **own lettering, moved** — the baker separates the
pixels already in the player's `title.pix` from the eye behind them. Nothing
about it is redrawn, and no logo art comes in from outside the install. Both
outputs are therefore derived from Capcom's art and are **not** tracked in the
repository; run the baker against your own install (see `docs/ASSETS.md`).

**The matte.** The lettering is the only strongly red thing in the picture and
the eye behind it is blue-grey, so `r - max(g, b)` separates them on its own,
with a soft ramp (14 → 60) that keeps the original's antialiased edges instead
of stair-stepping them. Edge pixels are the letter already blended with the eye,
so taking their colour as-is drags the eye into the new composite; the colour
comes from the nearest **fully inked** pixel instead (`distance_transform_edt`)
and alpha does the blending against whatever is behind it now.

`TITLE_LOGO_X/Y` (−146, −44) put it back exactly where it was: the crop starts
at 14,76 in a 320×240 picture whose centre is 160,120.

**The backdrop** is `RC1121.PIX`, the mansion's four-poster bedroom, graded for
the job — a lit room is a busy room and a wordmark needs somewhere quiet to sit.
80% of the saturation out, a cold cast back in (the wordmark is the only red
allowed on this screen), down to 60%, a lifted black so the room still reads as
a room rather than a silhouette, an oval vignette, a scrim rising off the bottom
edge to put the floor pattern away without cropping it out, a soft band across
the middle where the lettering lands, and a breath of bloom off what is still
bright so the grade does not read as a flat curve pulled over a photograph.

**5 bits per channel bands.** A dark graded picture is mostly gradient, and
quantised straight to 1555 it breaks into visible plates — the first cut was
obviously blocky in the shadows. `save_pix` adds a half-step of ordered (Bayer
4×4) noise before rounding, which trades the banding for grain.

`title_draw_logo` is called unconditionally in the title-art phase at full
alpha: the screen's fades are an **overlay in front of every sprite**, so the
wordmark comes up and goes down with the room behind it without being told.

The texture is loaded lazily and guarded by `IsGraphicsSystemReadyForOperation()`
with a one-shot `s_titleLogoTried`, the same shape as the eye — the title screen
can be entered before the device is up.

## Word art

- `native_title_words()` lifts NEW GAME, LOAD GAME and the copyright block out
  of `t_start.tim` pixel for pixel. The sheet inks them in two greys (112 solid,
  56 edge), so `normalise()` converts them to white with the grey as alpha —
  otherwise the colour MULTIPLY at draw time could only dim them, never light
  them, and they would not match the baked pair.
- `title_words()` letters EXTRA and QUIT — the game has no art for either.
  Liberation Sans Bold at 15px matches the native 11px cap height and the
  tracking is matched to NEW GAME's own 105px width.
- **Both sources must be tight to the ink on the left.** All four words draw
  from one `TITLE_MENU_X`, so any transparent margin is a pure indent. The
  baked pair had a 1–2px margin and sat visibly right of the native pair;
  `title_words()` now crops horizontally with `getbbox()`. Rows are NOT
  trimmed — row 0 is the cap line, which is what keeps the baseline shared.
  Verify by reading the alpha of the atlas the game actually loads: every word's
  first non-transparent column must be 0.
- RAID is Saira Condensed Black. The RESIDENT EVIL logo in `title.pix` is a
  heavy CONDENSED GROTESQUE, not a serif — that is what "same font as the title"
  means here. Letters baked white and the shadow BLACK, because the blit applies
  a colour MULTIPLY: white takes whatever red the screen asks for and black
  stays black, so one bake gives both.

## Items

`NEW GAME` / `LOAD GAME` keep the original's exit codes (`g_titleSelectionId`
1 = character select, 2 = load). `EXTRA` opens the RAID screen below, which
starts a run. `QUIT` calls `plat_window_destroy(g_hWnd)`, the same close the
window's X button takes.

### Exit dialog

`RunMessageLoop`'s WM_QUIT branch had `ShowMessageBox(NULL, "", "RESIDENT EVIL",
MB_OK)` — an empty dialog on **every** normal exit, unnoticed while the only way
out was the X. Now gated on `g_szExitMessage[0]` (`src/platform/win32/main.cpp`,
declared in `src/Globals.h`), so an error path can set text and a clean exit
shows nothing.

## The EXTRA screen (RAID)

`g_titleMode = 2`, its own mode rather than another step in the menu's fade
counter. Both crossings are arranged like the title's handover: the menu fades
to black, the swap happens under full black, the fade back is armed in the same
frame. `s_titleExtraPhase` is 0 off / 1 leaving the menu / 2 on it / 3 leaving
it; `s_titleExtraGuard` covers the button that opened it.

Everything on it is a function of ONE counter (`s_titleExtraFrames`), reset on
entry. Nothing keeps its own state, so the sequence cannot end up half-played:
at any frame the screen is exactly what the frame number says it is.

### In frames (30 Hz)

| frame | what |
|---|---|
| 0–47 | dark, with the sting's riser under it |
| 48 | the rule opens out of the centre |
| 60 | RAID rises; the sting's hit lands here; the eye fades in |
| 78 | the flare comes in along the rule |
| 90 | the prompt fades up |
| 118 | the flare has reached the last letter and starts to grow |

Half again as slow as the GF2 reference it started from: what works at 60 fps on
a 1080p screen reads as a twitch at 30 on a 320×240 one.

### The rule, the word, the sweep

**The rule parts around the word.** Left whole it reads as a strike through the
lettering — it passes behind the strokes, but the gaps between them are exactly
where a rule at that height shows. The parting looks like the word pushing it
apart.

**The light sweep** is a vertical slice of the same atlas rect drawn white over
the word, so the highlight follows the letterforms instead of a mask.

### The flare

Anatomy taken off the reference frame by frame, because at speed it is not what
it looks like: a **compact hot core**, a **wide and very dim halo** around it,
and a **ghost arc** thrown across the frame. The first version here was one big
soft bright blob, which is why it read as fog. The core is what makes it a
light; the halo is only what the light throws, and is held to a third of the
core's level.

- It travels along the rule and **stops on the last letter** — the D inks
  columns 88..117 of the 118-wide word, so its middle is `TITLE_EXTRA_D_CX`,
  44 px right of centre. It does not move again. The arrival is the event; a
  light that then wanders off takes the eye with it.
- After arriving it **grows, up and to the right**: the right edge runs out 84
  px and the top 52, while the left and bottom barely move. An even expansion
  would drag the bright middle with it. The core is drawn as its **own quad
  about the letter**, not about the middle of the halo.
- The halo **gains alpha as it spreads** (69 → 132). A radial falloff stretched
  over a bigger quad puts less light through each of its pixels, so holding the
  alpha while the quad grows makes the light *fade* — the opposite of growing.
- It **whitens** as it grows (`title_warm`, 85% of the way to white). A light
  getting brighter loses its colour.
- The **ghost arc** sits on the far side of the screen centre from the core, on
  the line through it (`-cx * 1.45`) — a lens throws one opposite whatever is
  blowing it out. Baked as a RING with the brightness falling away round the
  circumference, so what shows is an arc and not a hoop, and drawn twice at
  slightly different sizes in two tints for the chromatic fringe.
- Straight rays from the core were tried and removed.

### The eye

Two layers, from `tools/build_raid_eye.py`, loaded as `Data/raideye.bin`
(`'REY2'` + base w/h + iris w/h + the iris's home in the base + both layers as
RGBA rows, which `MarniCreateTexture` takes unchanged at bpp 32). Derived from
the game's own FMV, so it is not tracked in the repository — see
`docs/ASSETS.md`.

**The source is `ou.avi`, not `title.pix`.** The logo is baked across the middle
of title.pix (x 24..307, y 85..155, measured) with a shadow under every stroke,
and the strokes are too wide to reconstruct what is behind them. The game's own
opening has the same shot before the logo arrives; the frame at 6.9 s is the eye
open and clean, found by counting red pixels across the sequence.

**The eyeball holds still and the iris moves on it.** Sliding the whole picture
instead reads as the camera drifting, which is a different thing entirely. The
baker cuts the iris out as its own sprite and fills the hole by extending the
sclera inwards along the radius.

Two traps in that fill, both found in game:

- Fill only to the iris's own edge and the **original rim stays in the base**
  just outside it; the moment the sprite slides off, an arc of the old circle
  stands on the sclera. So the fill is wider than the iris by `IRIS_TRAVEL`.
- Extending along the radius needs sclera to extend FROM, and at the **bottom**
  there is none — the iris meets the lower lid, so the ray picks up lashes and
  drags them inward as streaks. The fill is therefore blurred, and only the
  fill, with the swap feathered so the blurred patch does not show its own edge.

Movement is **saccades**: hold, then snap over 6 frames, holding most of the
time. Every offset is EVEN, because the scanlines are baked into both layers and
an odd shift puts the iris's comb half a line out of phase with the eyeball's.

The eye arrives WITH the word (frame 60) — before that the screen is dark, and
something watching from behind it would give the reveal away. It needed
`QueueTexturedSpriteTinted`, a port-only sibling of `QueueTexturedSprite` that
takes a colour; the original hardcoded opaque white, and without alpha there is
no fading it in.

### Grain

One 168×128 tile and a 160×120 WINDOW of it moved every frame by an LCG off the
counter: a screenful of moving noise for a single quad. Stretched 2× and
point-sampled, so a grain is a 2×2 block — film, not fizz. Alpha ended at **4**
after several passes; 15 and 8 both read as snow over the picture.

### Sound

Two files, two banks, both loaded with `loadSndBankFromWav` — a bank the game's
own code never allocated is one it never throws away, and this screen opens at
any point in the title loop. Both stopped with `setSndStop` on the way out.

**`Sound/raid.wav`** — the sting, one shot, `tools/build_raid_sfx.py`. It has to
land on the animation's beats, and the only way to keep a sound and a frame
counter in step is to generate the sound from the same numbers; the frame marks
are duplicated at the top of that script. A low drone from frame 0, a noise
riser that **ducks out over 80 ms before the hit** (without that gap the hit has
nothing to punch through), the hit at frame 60, then four delay taps **off the
strike only** — feeding the whole mix back smears the riser across the impact.

**`Sound/raidbgm.wav`** — the bed, an 8-bar loop, `tools/build_raid_bgm.py`,
played with `playSnd(bank, 1)`. **A non-zero slot is what sets
`XAUDIO2_LOOP_INFINITE`**, which repeats the WHOLE buffer; this build has no
loop-region mechanism anywhere, so the file itself has to be the loop.

Its shape was measured off the two reference tracks rather than guessed
(onset-comb for tempo, Krumhansl profiles on a chroma sum for key, per-bar triad
matching for harmony, band RMS for balance, and a note detector in the lead
register for the melody):

| | tempo | key | sub / low / mid / hi / air |
|---|---|---|---|
| Frontier Conquest menu theme | 100.0 | C minor | 53 / 32 / 11 / 3.5 / 1 |
| One Hit Kill | 115.0 | D minor | 57 / 26 / 10 / 6 / 2 |
| `raidbgm.wav` | 100.0 | C minor | 50 / 33 / 12 / 3.5 / 1.6 |

The menu theme is the base — its tempo, its key, its harmonic family. Two bars
each measured as G minor rather than C: a bass hammering roots puts the chroma
weight wherever it spends its time. Three bars of tonic fix it. One Hit Kill
contributes its STATICNESS and drive — four bars on a chord under a sixteenth
bass, which is what makes a loop this long bearable.

The lead is written to the reference melody's measured HABITS, not its notes:
2.4 notes a bar, sounding 39% of the time, median note 0.4 beats, register
MIDI 65–74, 59% steps of two semitones or less and 31% fourths-to-fifths.

Running the same note detector over the mix then found, three times, that the
melody was not the most salient thing in its own register — first the arp
(moved an octave up), then the bass's harmonics (one-pole rolls off only 6 dB an
octave; two passes), then the pad (moved an octave down). Pad under, line in the
middle, arp above.

The loop is rendered at DOUBLE length and folded (`out[:L] += out[L:]`) so every
reverb and delay tail wraps to the front, and both master filters run on two
copies (`lp1_wrap`) because a causal filter's startup transient IS the seam.
Even then the two ends meet cold, which measured at the 99.97th percentile of
the track's own sample-to-sample steps, so each end gets a 2 ms ramp.

### The prompt, without the copyright

`UpdateTitleTextSprite` draws a 54-tall block with the two CAPCOM lines baked in
under the prompt. This screen draws the same page at the same place and colour
with the height cut to 20 — rows 5..15 carry the lettering and nothing else
until row 32, in BOTH `t_start.tim` (pad) and `t_press.tim` (keyboard).

### Atlas size

The sheet is **512×640**, not 512×512: the two fonts fill it to row 508 and
the grain tile had to go somewhere. Nothing hardcodes the size — the blob
carries it in its header and both loaders read `ACHV_ATLAS_W/H` from the
generated header — so it grows by a row band rather than by a power of two.

A mark's name becomes a C symbol, so two marks may not share one: the flare's
ring is `flarering` because `skin_marks()` already bakes a `ring`.

## RAID mode

A mode, not a variation on the story. Its own room, its own loadout, its own
renderer, and no data shared with a playthrough. See `docs/RAID_LEVEL_FORMAT.md`
for the level format and `docs/RE1_EDITOR.md` for the editor.

### Starting a run

A press on the RAID screen no longer bounces back to the menu. `s_titleExtraPhase
== 3` still fades to black, but under full black it now sets `g_raidMode`, forces
the character, takes a title exit id of its own and calls `title_exit_loop()`:

```
g_raidMode = 1;
g_SelectedCharactedId = CHAR_JILL;
g_titleSelectionId = TITLE_SEL_RAID;   // 4
```

`title_state`'s switch gets a `case TITLE_SEL_RAID` that chains `game_start`
**directly**. The original's ids are 0 attract demo, 1 character select, 2/3 load
a save; RAID needs neither, so it skips both. No backdrop swap and no fade back
in on the way out: the screen stays black and the loading takes it from there.

`g_raidMode` is cleared at the TOP of `title_state`, not at the end of a run —
that way it cannot survive one however the run ended, and a NEW GAME after a
RAID run is an ordinary new game.

### The room

**Stage 0, room 0x10.** That slot ships as a FOUR-BYTE stub (`00 00 00 00`) in
both scenario variants — filename filler so the `room<S><RR><V>.rdt` pattern
stays dense — and nothing in the game enters it. No door leads there, no script
names it, no save can be standing in it. That is what makes it takeable.

(The stubs also answer a tempting question: no, the engine does NOT tolerate a
degenerate RDT. `LoadRoomRdt` would write only four bytes over the previous
room's header and then relocate nineteen stale pointers.)

`tools/build_raid_room.py` writes it. What it had to get right:

- **There is no -3 pointer bias.** `docs/RDT_FILE_FORMAT.md` says header pointers
  are stored as `target - (file_base + 3)`. The reader is a plain
  `*ptrField += (int)g_RdtPointer` over 0x48..0x90 (`RoomInit.cpp:630`), and the
  shipped files agree. **A stored 0 is not a null pointer either**; it relocates
  to the file base like anything else.
- **Two camera-switch records for one camera.** rec0 is camera 0's group header
  and its quad is live data — PlayerAnimations tests the player against it every
  frame and bit 0 of `zoneFlags` gates her shadow — so it covers the whole floor.
  rec1 is `camTo/camFrom = 0xFFFF`. Without it `check_camera_switch` runs off the
  end and starts reading collision records as camera zones.
- **Walls are solid slabs OUTSIDE the play area**, not a hollow frame:
  `collision_push_rect` pushes the player out of a box. `type = 0x0001` (shape 5
  is the same push but is skipped once a Z resolve has happened that frame);
  `flags = 0x0300`, where 0x100 blocks and 0x200 joins the second pass.
- **All four walls go in all four quadrant lists.** The quadrant split is a point
  test and only the player's own list is consulted.
- **Everything is at positive X/Z below 32768.** Both the switch-zone corners and
  the collision extents are read back UNSIGNED.
- **`vab_sound_file` is not sound data** — it is the room load's bump-allocator
  cursor (`g_loadDataDestPointer`), so it points past every real block.
- **`effect_anim_sprite` is read BACKWARDS** (`base-0, -4 … -28`), so it points at
  the LAST dword of its block.
- Empty tables that still must exist: mask block `00000000`, init SCD
  `04 00 00 00 00 00`, main SCD `00 00`, event table `00000000`, one walk zone,
  one catch-all footstep zone (that walker has NO terminator).

### No background

The room has no `RC1100.pak` and never asks for one. `load_room_bg` and
`load_room_bg_image` early-return on `g_raidMode` (`Room.cpp`), because neither
checks its load: a missing file leaves `LoadFile` returning -1 without touching
the buffer, and `unpack_pakfile_` then runs LZW over whatever is already there
into a fixed-size destination **with no bound on either side**. The failure mode
is a memory write, not a black screen.

With `display_image` never called, `OT_InsertPrimitive`'s background quad
self-gates on `g_displayImageSRV` being null. But that global OUTLIVES the screen
that set it, so `Raid_EnterRoom` calls `display_image_drop()` or the arena is
built in front of the RESIDENT EVIL logo.

### Drawing the arena

`src/game/RaidArena.cpp`, called from the render flush between the background
sprites and `FlushTmdObjects`.

The engine's own way to put geometry in a room is a TMD registered as a room
object, and that route has a gate this arena cannot pass: the queue drops any
model whose `FindMinClutDepth` comes back 0, and that function only looks at
TEXTURED primitives — **an untextured model never draws at all**.

So the arena takes the road `CollisionDebug.cpp` already proves: build the view
from the room's own camera, project the corners in this file, hand finished
triangles to the backend. The view is derived here rather than taken from
`g_RoomCameraData` — that matrix's rows live in a Y-flipped frame and its
translation mixes conventions.

What this adds over the overlay is **depth**. Every vertex carries an NDC depth
from `TmdViewZToNdc` — the same mapping the model flush uses — and submits
through `DrawTriangles3D` with depth writes on.

Faces are clipped to a near plane **in world space** and fanned, rather than
dropped whole. The floor spans the room, so one of its corners is usually behind
the camera; dropping the face would take the floor with it.

### Reloading — AIM + CANCEL

The original has no reload action at all: ammunition goes into a weapon through
the INVENTORY. `weapon_reload()` (PlayerAnimations.cpp) is that same transfer on
a button combination.

It is the menu's arithmetic, not a new rule. Capacity comes from
`g_ItemMaxQty[weaponId * 4]` and the weapon→ammo pairing is the game's own:
`weapon_fire_check` already asks for `equippedWeaponId + 9` (BERETTA 0x02 →
CLIP 0x0B, up to the acid bazooka 0x09 → 0x12). **Bit 7 of the quantity byte is
a flag, not part of the count**, so it is masked out and put back.

**Why AIM + CANCEL is free.** The gameplay-facing input is `g_PlayerDpadHeld`,
built from the raw word through `g_padRemapTable[g_controllerConfig & 3]`:

| dpad bit | function | raw source |
|---|---|---|
| `0x40` | fire | action / confirm (`0x0080`) |
| `0x80` | action / confirm | action / confirm |
| `0x100` | aim | aim (`0x0008`) |
| `0x200` | run | cancel / run (`0x0040`) |

So in RE1 you aim with AIM and fire with ACTION, and the cancel/run button is
unused while the weapon is up. Nothing can be shadowed by taking it.

The test is on the PRESS, not the hold: reloading is one event.

**The animation is the game's own two motions, back to back.** There is no
reload animation anywhere in the data. The press routes into behaviour `0x17` —
the holster — whose bottom is where the rounds move and the sound plays; it then
hands to `0x12`, the ordinary raise. Lower, click, raise, every frame shipped.

That is also why `weapon_reload` takes a `commit` flag: the press has to know
whether a reload is *possible* before committing to the motion, but the rounds
must not move until the bottom. The aim button decides — let go during the
descent and this is simply a holster.

### The Beretta's slide

`src/game/WeaponSlide.cpp`, called once per frame from the render flush. The
slide travels back on every shot and stays locked back while the magazine is
empty.

**Why this is cheap.** The in-hand weapon model is the gloved fist and the
pistol modelled as ONE mesh, and the pistol half is a plain eight-vertex box —
see `docs/EMW_INHAND_WEAPON_FORMAT.md`. The part that should move on every shot
is already the only part modelled, in its own connected component.

**Why writing vertices is legal.** A TMD is parsed ONCE, by `PSXObject_Store`,
into a plain heap array of 11-float vertices. After that `CreateTmdObjectInternal`
short-circuits on a cached slot and the TMD bytes are never read again;
`FlushTmdObjects` transforms and projects straight out of that array **on the
CPU**, every frame. There is no GPU vertex buffer to invalidate.

Two things the parse does that matter: **it expands** (each triangle gets three
fresh vertices, so the slide is 36 buffer vertices, not 8), and **it negates Y**,
so a slide travelling rearwards is a POSITIVE y offset.

**Finding the slide.** Measured off the shipped file: its TMD splits by shared
vertices into fist (34 tris, verts 0–18) and slide (12 tris, verts 19–26), which
maps to buffer vertices 102..137. The runtime test is
`x >= 47 && y <= -80 && |z| <= 32`, applied PER TRIANGLE — all three corners or
none — with the trip count checked against 12 before anything moves.

Two numbers had to be wrong once to be got right: **travel is 100 model units**
(a real Beretta's seventh-of-its-length is invisible at fifty pixels across), and
**the cycle is 6 frames**, not 4, because the muzzle smoke covers a shorter one.

**The empty hold needs a latch.** `while (ammo == 0)` is a state that exists and
is never seen: firing the last round with ammo in the inventory drops straight
into behaviour 0x18. So the hold is armed when the count reaches zero and runs
down on its own (14 frames).

**Where it has to run: the render flush, not `update_player_anim`.** That was
where it went first, and it did nothing. `render_entity` re-runs `PSXObject_Store`
for a model whose cached slot has been reclaimed, which restores every vertex.
Running immediately before `FlushTmdObjects` closes the window.

**And it needed a barrel.** With the slide moving correctly the effect still read
as nothing, because the stock weapon is a featureless box.
`tools/build_beretta_barrel.py` adds a second, thinner box through the slide and
a few units past the muzzle — invisible at rest, a barrel once the slide is back.

## Watch out

Visual Studio has twice written its stale editor buffer back over a file edited
outside it, silently reverting the last change. After an external edit, reload
the file in the IDE before building — and if a change appears not to have taken
effect in game, check the file on disk before re-diagnosing the code.

Binary assets go into **three** trees. See `docs/ASSETS.md`.

## Verifying this screen

Everything here is checked by rasterising the SAME arithmetic in Python — the
atlas the game loads, the baked eye, the frame marks — and looking at the
frames. That catches geometry, timing and balance, and it caught every visual
bug in this feature.

It cannot catch C. Moving blocks around this file once dropped two helper
definitions and the preview was perfectly happy; the build was not. So there is
also a source check worth re-running after any large edit: parse the file for
`static ... title_*(` definitions and make sure no call site precedes its own
definition.
