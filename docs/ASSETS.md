# Assets: what a clone has, what it needs, and where it all goes

Three separate things live under the names `assets/`, `portdata/` and
`bin/*/USA/`. Confusing them is behind more than one debugging round in this
project's history, so they are spelled out here once.

## The three kinds

**1. The game's own data — `assets/USA/...`, NOT tracked.**
Capcom's files: every `.tim`, `.pix`, `.pak`, the FMV under `Movie/`, the
audio under `Sound/`, the models under `Enemy/`, `Item_m2/` and `players/`.
They come from your own copy of the game (the GOG release is the reference —
see the README). They are ignored by git and must never be committed.

**2. What this port produced — `portdata/`, tracked.**
The editor's atlas, the Space GUI atlas, the RAID level, the three custom
pistols' examine models and the three sounds this project generated. Small,
ours, and in git so a clone has them without having to rebuild anything.
`portdata/README.md` lists them.

**3. Where a build actually reads from — `bin/Debug/USA/...` and
`bin/Release/USA/...`, NOT tracked.**
`bin/*/config.ini` ships with `[Assets] Path=` **empty**, which means the game
reads the tree **next to its executable**. `assets/USA/...` is the source of
truth in a checkout and is *not* what a run loads.

(`Claude outputs/` is none of these and is tracked: it holds the preview
renders the port's features were reviewed from, not anything the game loads.)

That third point is the one that costs time. It has produced this failure twice:
the build compiles a freshly generated header whose rects point at new art,
while the atlas the running game loads is the old one — so the new art simply
does not appear, silently, with nothing to catch it. **Every runtime asset must
exist in all three trees.**

They do drift. When `portdata/` was first populated, `raid.wav` and
`raidbgm.wav` were the same length with the same timestamp in all three trees
and still differed — a few hundred bytes apart in the fade-out tail, inaudible
and invisible to every check except a hash. That is the whole argument for one
tracked copy and a deploy step: `deploy_portdata.py --check` compares contents,
not sizes or dates, and it is the only thing here that would have caught it.

## Setting up a fresh clone

```
1. git clone …
2. Copy your own game data into assets/USA/ — the whole tree, which is Data,
   Effspr, Enemy, Item_m1, Item_m2, Movie, Objspr, Players, Sound, Stage1..7
   and Voice — and assets/config.ini from config.ini.template
3. Build once — build.bat / build_debug.bat, or the CMake commands in
   docs/LINUX_PORT.md — which creates bin/Debug and bin/Release
4. Copy your game data into bin/Debug/USA/ and bin/Release/USA/ as well
5. python3 tools/deploy_portdata.py      ← the port's own assets, all 3 trees
6. Rebuild whatever is derived from the game's own art (below) — and read the
   warning there first: build_beretta_barrel.py overwrites W12.EMW in place
7. Accept that players/{w1f,w2f,w3f}.emw cannot be rebuilt at all (below)
```

`python3 tools/deploy_portdata.py --check` reports what is stale or missing
without writing anything. On Windows the interpreter is usually `py -3` or
plain `python`.

## Derived from the game's own art — regenerate, do not commit

These are produced by this project's tools but carry Capcom's own pixels or
audio, so they are not tracked either. Their generators are:

| file | generator | derived from |
|---|---|---|
| `USA/Data/titlebg.pix` | `tools/build_title_bg.py` | `RC1121.PIX`, graded |
| `USA/Data/titlelogo.bin` | `tools/build_title_bg.py` | the wordmark cut out of `title.pix` |
| `USA/Data/raideye.bin` | `tools/build_raid_eye.py` | a frame of `Movie/ou.avi` |
| `USA/players/W12.EMW` | `tools/build_beretta_barrel.py` | the stock file, **patched in place** |
| `USA/Stage1/ROOM110{0,1}.RDT` | `tools/build_raid_room.py` | nothing — it fills an empty slot |

`deploy_portdata.py --check` names the missing ones at the end of its run.

**`build_beretta_barrel.py` overwrites the game's own file and keeps no backup.**
It reads `players/W12.EMW`, adds the barrel and writes it back over itself in
all three trees. Running it twice trips its own assertions rather than doubling
the barrel, but the stock file is gone either way, so re-copy `W12.EMW` from
your install before re-running it.

`build_raid_room.py` is the safe one and worth understanding, because it looks
alarming and is not. Stage 1 room 0x10 ships as a **four-byte stub**
(`00 00 00 00`) — filename filler that keeps the `room<S><RR><V>.rdt` pattern
dense. Nothing enters it: no door leads there, no script names it, and
`DebugMenu.cpp::DebugRoomSelectable` excludes it outright, exactly as it
excludes the other stage-1 stub, 0x19. (The elevator stairway that *is* room
0x10 lives in stage 6, as `ROOM6100.RDT`, and is untouched.) You can tell which
you have by size: 4 bytes is the stub, 65536 is the RAID arena.

## The one gap a fresh clone cannot fill

`USA/players/{w1f,w2f,w3f}.emw` — the custom pistols' **in-hand** models — have
**no generator in this tree**. Each is a copy of Jill's `W12.EMW` with the
weapon TMD swapped out, so most of the file is Capcom's animation data byte for
byte and it cannot be tracked here; but the swapped-in mesh was authored by hand
and was never written back out as a script, so it cannot be rebuilt either.

Jill starts with all three pistols (`GameStart.cpp::SetInitialItems`), so on a
clone this is the first thing that happens rather than an edge case. It used to
be fatal: `LoadFile` answers `(size_t)-1` for a miss and the next line indexed
`anim_buffer - 12`, handing the animation system a pointer built out of whatever
was there — memory corruption that surfaced later and elsewhere.

`LoadEquippedWeaponAnimation` now falls back to Jill's `w12.emw`, which these
pistols already alias to, so a clone without the files gets **a Beretta in her
hands instead of the flare pistol** and keeps running. Every pose is right,
because the fallback is the file the animation half was copied from in the first
place; only the gun is wrong. That is a visible defect, not a hidden one — which
is the point. If you are looking at a Beretta where a flare pistol belongs, the
three `.emw` are missing.

The **examine** models (`Item_m2/{IFLR,IACD,IFRZ}.ivm`) are a different story
and are tracked in `portdata/`: they were built from scratch, each with its own
256x256 texture, and share no geometry or texels with any Capcom file — a byte
comparison against `I00V.IVM` matches 162 bytes out of 66080, i.e. chance.

## The Space GUI pack

`assets/SpaceGUI/` is a purchased interface kit. A licence like that covers
using the art **in a product**; it does not cover redistributing the kit's 830
source files so that others can extract them. So the pack is not tracked, and
the two atlases baked from it — `edui.bin` and `achvui.bin` — are, because they
are the product.

The practical consequence: `tools/build_editor_ui.py` and
`tools/build_achievement_ui.py` cannot be re-run from a bare clone. They do not
need to be; their output is in `portdata/`. If you own the pack, drop it back
under `assets/SpaceGUI/` and both generators run again — but **neither one
reproduces the committed blob byte for byte**, so treat `portdata/*.bin` as the
source of truth and the bakers as the way to change them, not to re-derive them.
A re-run therefore always means eyeballing the result in game.

Fonts inside the pack are OFL (`sources/fonts/OFL.txt`).

## Adding a new runtime asset

1. If it is **ours** — generated from our own art, or authored as data — put it
   in `portdata/USA/<dir>/` and add a row to `portdata/README.md`.
2. If it is **derived from the game's data**, leave it untracked and list it in
   `tools/deploy_portdata.py`, in whichever of its three lists fits: `DERIVED`
   for a file of its own that a clone can rebuild, `IN_PLACE` for a tool that
   patches one of the game's files (absence is undetectable — it can only be
   stated), `UNOBTAINABLE` for something with no generator at all.
3. Either way, make sure it reaches all three trees before testing, or the
   silent failure above is waiting for you.
