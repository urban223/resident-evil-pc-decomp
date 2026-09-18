# RAID arena levels: the `.lvl` format

The RAID mode's room used to be hardcoded in two places — the geometry as
`#define`s in `RaidArena.cpp`, the collision/camera/spawn baked into the RDT by
`tools/build_raid_room.py`. Changing the room meant editing both and rebuilding
an asset.

It is one text file now. The RDT is a fixed, empty shell; everything that makes
a level is read from `Data\raid1.lvl` at room load and rebuilt into the
engine's own structures **in memory**. Nothing about a level needs a bake, which
is what makes an editor and a reload key possible at all.

The editor is now in the game — see `docs/RE1_EDITOR.md`. The browser editor
described in earlier revisions of this file is superseded.

## Files

| file | what it is |
|---|---|
| `src/game/RaidLevel.h` | the structs and the limits |
| `src/game/RaidLevel.cpp` | the text parser, plus `RaidLevel_Apply()` which pushes cameras, lights, collision and camera-switch zones into the loaded RDT |
| `src/game/RaidArena.cpp` | the renderer — fully data-driven, iterates `g_raidLevel.box[]` |
| `src/game/RaidItems.cpp` | the pickups: what is in reach, the take button, and putting an item in a slot |
| `src/game/RaidItemModels.cpp` | the pickups' real `.ivm` models |
| `src/game/RaidEnemies.cpp` | filling entity slots from the level's `enemy` lines |
| `src/game/editor/` | the in-game editor |
| `tools/build_raid_room.py` | builds the empty shell RDT: 8 camera slots, empty tables |
| `portdata/USA/Data/raid1.lvl` | the level, deployed into the three data trees |

## The format

One directive per line, whitespace separated, `#` to end of line is a comment.
Everything is integers in the game's own world units. **Y is negative upwards**,
so a ceiling is a negative number, and a box's `y0` is its TOP.

```
ver     1
ambient <r> <g> <b>                          12-bit channels, 0..4095
light   <x> <y> <z> <r> <g> <b> <radius>     up to 3; radius 0 is a BLACK light
cam     <fx> <fy> <fz> <tx> <ty> <tz> <fov>  fov is a focal length, not an angle
camzone <cam> <x0> <z0> <x1> <z1>            walk in here, switch to that camera
spawn   <x> <z> <angle>                      0 = +X, 0x400 = +Z, 4096 = a turn
box     <x0> <y0> <z0> <x1> <y1> <z1> <flags> <shade> <r> <g> <b>
item    <x> <z> <angle> <type> <amount>      a pickup lying in the room
give    <type> <amount>                      one slot of the starting inventory
enemy   <x> <z> <angle> <type>
```

`box` flags: `1` draw, `2` collide, `4` checkerboard. `shade` is brightness in
hundredths (72 = 0.72), so the whole file stays integers and an editor never has
to think about locales and decimal points. `type` on `item` and `give` is an
`ITEM_*` id from `Types.h`.

Limits, from `RaidLevel.h`: 128 boxes, 8 cameras, 32 zones, 3 lights, 32
enemies, 32 pickups, 8 `give` slots. A bad line is skipped rather than fatal — a
level that loses one wall is a better failure than a level that crashes. A file
with no camera or no box is rejected outright and the level already loaded is
kept.

## Constraints worth knowing

- **Everything positive, below 32768.** The collision records and the camera
  switch zones are read back **unsigned** by the engine, so a level that
  straddles the origin does not work.
- **Walls are solid slabs outside the play area, not a hollow frame.** The
  collision pushes the player *out* of a box, so a wall has to be a box she is
  never inside.
- **A box with `y0 == y1` is a plate**: one horizontal face, always drawn, no
  backface test. That is what a floor is.
- **Lights fall on the character models, not the room.** The arena shades
  itself (fog, a light pool toward the middle, a height falloff). The three
  `light` slots go into the RDT for `update_entity_lighting`. A zeroed
  `RDT_Light` is a *black* light, not an absent one, so unused slots are filled
  from the last one rather than cleared.
- **`camzone` leads away from a camera.** `RaidLevel_Apply` emits a group header
  per camera carrying that camera's own full-level quad, then every zone that
  does not point back at it, then a `0xFFFF` terminator — without which
  `check_camera_switch` runs off into the collision table.

## Pickups

A story room's items are armed by its SCD script: the script sets up an AOT, the
AOT posts message `0xc0`, and `handle_message_post_action` eventually reaches
`room_event_item_pickup`, which reads the item id back out of the armed record
via `g_pRoomActionEntry`. Every step of that needs a compiled script in the RDT,
and the RAID room's RDT is a deliberately empty shell. So `RaidItems.cpp` runs
them instead: a list of points, a distance test, and one function that puts an
item in a slot.

- **Reach is 900 units, the button is ACTION (dpad `0x80`).** The nearest pickup
  in reach wins, so two lying together are taken one press at a time.
- **The insert is the careful version of `room_event_item_pickup`.** The
  original tests `g_selectedItemId` rather than the item it is awarding, has no
  capacity check, and writes past the last slot when the inventory is full.
  Reproducing that faithfully would mean reproducing a memory stomp, so this one
  tests the item it is holding and refuses when there is nowhere to put it — the
  pickup stays on the floor, which is a state the player can see.
- **A slot holds up to 250, not `g_ItemMaxQty`.** That table is the MAGAZINE
  size — the reload reads it as `g_ItemMaxQty[(weaponId + 9) * 4]`, which is 15
  for a clip. A pile of 60 rounds is one slot holding 60.
- **Either the whole pile or none of it.** Leaving four rounds of a clip on the
  floor is a state nothing in this mode can show.
- **F7 un-takes everything**, because a reloaded level is a new room.

`give` lines replace the built-in loadout in `GameStart.cpp`. They are applied in
`Raid_EnterRoom`, not `Raid_SetItems` — that runs before `init_room` and the
level file is not read until later. A level with no `give` line keeps the
built-in table: saying nothing is not the same as saying "none".

## The pickups' models

Item models come out of a room's own PAK and this room has none, so
`RaidItemModels.cpp` loads the game's own `.ivm` files directly — the same ones
the examine screen shows — and draws them where the pickups lie. One model per
distinct TYPE, not per pickup: four clips on the floor are one parsed mesh drawn
four times.

Item models are authored for the examine viewer and are three to five thousand
units long, so they are normalised to `RAID_ITEM_MODEL_SIZE` (460) units on the
floor. Measuring that extent must happen **before** `ResolveAnimPointers`, which
rewrites the TMD object table's offsets into absolute pointers and sets FIXP —
see the comment block on `raid_model_extent`, and `docs/IVM_MODEL_FORMAT.md` for
the format itself.
