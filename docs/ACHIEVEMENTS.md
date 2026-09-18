# Achievement toasts (Space GUI) — implementation notes

## What was added
A port-only achievement system: a panel drops in from the top of the screen
when something is unlocked, or when a counter achievement passes a fifth of
its target. Round 1 ships the engine plus **three test achievements**; Round 2
replaced the plain slide with the two-movement open/close described below;
Round 3 added the sound cue. The full list is meant to grow on top of it.

| id | name | condition | icon |
|---|---|---|---|
| `ACHV_FIRST_BLOOD` | FIRST BLOOD | first enemy killed | Skull |
| `ACHV_KEEPING_A_RECORD` | KEEPING A RECORD | first save written | Pencil |
| `ACHV_EXTERMINATOR` | EXTERMINATOR | kill 25 enemies (progress bar) | Medal |

Files: `src/game/Achievements.{h,cpp}`, the generated
`src/game/AchievementAtlasData.h`, `tools/build_achievement_ui.py`,
`tools/build_achievement_sfx.py`, and the binary assets `Data/achvui.bin` and
`Sound/achv.wav`.
Touched: `Rendering.cpp`, `WeaponDamage.cpp`, `SaveLoadScreen.cpp`, `MainMenu.cpp`,
`marni/MarniSystem.{h,cpp}`, `CMakeLists.txt`, `Game.vcxproj`.

A candidate list to grow into lives in `docs/RETROACHIEVEMENTS_REFERENCE.md`
(the RetroAchievements set for the non-Director's-Cut PS1 release).

## The decision that shapes everything: the toast bypasses the PSX 2D path
Every other 2D element in this game goes through the VRAM-page emulation —
8bpp indexed art, a CLUT picked out of `STATUS.TIM`, `TextureDesc` + the
sprite queue — and inherits its limits: 256-colour palettes, the "index 0 is
not reliably transparent" problem from Round 4 of the pistol work, and a font
(`fontus.tim`, 8x14) with ASCII only and no anti-aliasing.

None of that is required here. `MarniCreateTexture(w, h, 32, pixels, &handle)`
uploads a plain RGBA8 texture (`ConvertToRGBA8` memcpy's bpp-32 input straight
into an `R8G8B8A8_UNORM` texture, so the file's byte order **is** the
texture's), and `MarniDX::DrawSprite` already takes a sampler, a blend mode and
a per-quad tint. So the toast is one atlas texture and a handful of tinted
quads, with real alpha, real anti-aliased type and a TTF-baked font.

Two small additions to the Marni wrapper layer make that reachable from game
code, which must not touch `m_pDX` conventions itself:
- `MarniDrawSpriteEx(...)` — `MarniDrawSprite` is hardwired to
  `MARNI_SAMPLER_POINT` on purpose (that is the PS1 look for the game's own
  art); an overlay scaled to the window wants `MARNI_SAMPLER_LINEAR`.
- `MarniGetBackBufferSize()` — `MarniGetRenderScale` answers "how do I scale
  game space", which is a different question. The toast is sized against the
  **window**, not against 320x240, so it stays the same fraction of the screen
  at 480p and at 4K instead of growing into a slab.

## The atlas (`tools/build_achievement_ui.py`)
One 512x640 RGBA texture, `Data/achvui.bin` (`'AUI1'` + w + h + rows), built
from the Space GUI pack. Since the port-only refactor, the packer, the font
baker and the blob/header writers are shared with the editor's atlas through
`tools/atlas_lib.py`, and the loader is shared through
`src/game/PortAtlas.{h,cpp}` with the text engine in `src/game/PortText.{h,cpp}`.

- **Panel**, baked at 500x92 = 2x its design size: cut-corner plate (the shape
  every dialog in the kit uses), vertical gradient, 2px stroke, a cyan wash
  down the left edge, and the pack's `CornerBigTL.png` bracket mirrored into
  all four corners.
- **Icon slot**: `Panels/Frame.png` 9-sliced out to 64x64.
- **18 icons** from `Icons/128/`, downscaled to 64x64 — more than the three
  achievements need, so the next batch does not have to rebuild the atlas.
- **Two fonts** baked from `SairaCondensed-Bold` (24px, the name) and
  `SairaCondensed-SemiBold` (17px, the label/description/counter), ASCII
  32..126.
- **A 2x2 white block** for the solid fills (the progress bar's track and its
  lime fill).

The pack's art is white/greyscale by design — it is meant to be tinted — so
the atlas keeps it white and the runtime multiplies the colour in per quad.

The script also emits `src/game/AchievementAtlasData.h`: the sub-rects, the
`ACHV_ICON_*` ids and the two glyph tables. Generated file, do not hand-edit.

**Edge bleed matters here.** With a LINEAR sampler a sample taken exactly on a
sub-rect's right or bottom edge blends with whatever sits in the gap beyond
it, which fades the panel's border and the slot's frame along those two edges.
Every packed rect therefore duplicates its last column and row one pixel
further out, and the packer leaves two pixels of spacing instead of one.

## Layout
In atlas pixels (the runtime draws one atlas pixel as `k` backbuffer pixels,
`k = (backbufferHeight / 480) * 0.5`, clamped to 0.45..2.20):

```
 +-------------------------------------------------------+
 |  +------+   ACHIEVEMENT UNLOCKED     <- body font, y 8 |
 |  | icon |   FIRST BLOOD              <- title,     y 26|
 |  +------+   Killed your first enemy  <- body,      y 60|
 +-------------------------------------------------------+  92
 0         96                                           500
```

A progress toast puts `PROGRESS` on the label line and replaces the
description with the bar at y 66 (290 wide) and its `15 / 25` counter to the
right of it.

## Animation: the plate opens out of the icon capsule

| phase | frames | what moves |
|---|---|---|
| DROP | 8 | the collapsed capsule falls in from the top edge (ease-out, alpha follows) |
| OPEN | 7 | the plate opens rightward to full width (ease-out) |
| HOLD | 78 | text fades in over the first 5 frames |
| CLOSE | 6 | the plate folds back down to the capsule (ease-in) |
| RISE | 8 | the capsule leaves through the top edge |

Three things make it work:

- **The panel is drawn as a horizontal 3-slice** — `ACHV_PANEL_CAP_L` |
  stretched middle | `ACHV_PANEL_CAP_R`. Both caps (56 px each) are sized to
  contain their cut corner *and* both of their brackets, so the frame never
  distorts at any width, and their sum (112 px) IS the collapsed capsule.
  Squashing the whole 500px image instead would have smeared the corners.
- **The cyan wash had to move inside the left cap.** It used to run 160 px
  across, which is out in the stretched middle — anything that varies along x
  out there is smeared across the entire panel as it opens. The middle slice
  must be uniform in x.
- **The plate opens to the RIGHT of the capsule**, i.e. the panel's left edge
  is placed where the finished toast's left edge will be rather than centring
  the capsule. The icon therefore lands once and stays put instead of sliding
  out from under itself while the plate grows.

**Text only draws during HOLD.** This draw path has no scissor rectangle, so
text drawn while the plate is still opening would hang off its right edge.

Up to 8 toasts queue; a flood drops the extras.

## Sound
**One cue, `Sound/achv.wav`, and only an unlock plays it.** A progress toast
is deliberately silent: it is an interim note, and a chime every fifth of a
counter turns the reward into a nag. Finishing a counter arrives as an
ordinary unlock (`Achievements_AddProgress` calls `Achievements_Unlock` at the
target), so the sound lands on completion for counters too. It fires as the
capsule starts to fall — the IDLE → DROP transition in `Achievements_Tick`.

The cue is prepared by `tools/build_achievement_sfx.py <input audio>`, which
converts anything ffmpeg reads into what the engine's loader expects:
**mono, 22050 Hz, 16-bit PCM** (the format every shipped RE1 sound uses;
`DirectSound::CreateSound` walks the RIFF chunks and hands the PCM straight to
XAudio2), silence trimmed below -50 dBFS at both ends keeping 5 ms of pre-roll
and 10 ms of tail, peak-normalised to -1 dBFS.

An earlier revision had a second, synthesized blip for progress steps
(`Sound/achvp.wav`). It is no longer loaded or referenced and can be deleted.

**Why its own sound rather than a gameplay SFX.** The game's effects live in
banks that the room loader brings in and throws away; an achievement can fire
in any room, so borrowing `item01.wav`'s bank would mean depending on a bank
that may not be loaded. Loading our own is the same mechanism the game already
uses — `loadSndBankFromWav` → `playSnd` — and a bank allocated this way is
never one the game destroys: `sounds_reset` and its siblings walk their own
tables and only free ids they put there.

The load is lazy and retried at most three times: the sound device is not up
during the first frames, and `loadSndBankFromWav` simply answers 0 then.

Volume rides `g_SfxVolume`, the options screen's effects volume (−1 dB under
it). Pan is forced to centre — this is UI, not a sound in the room.

## Where it runs from
`FrameRateGovernor` (`Rendering.cpp`), as the last thing before
`MarniPresent()`. That placement is deliberate on both counts:
- **Drawing last** puts the toast above the screen fades, the pause menu and
  every sprite pass — which is the whole point of a notification. (The editor's
  interface, when it is up, draws after even this.)
- **Ticking there** ties its clock to presented frames rather than to the task
  scheduler, so it does not advance on dropped frames.

## Triggers
- **Kills** — `apply_weapon_damage`, after the post-hit callback:
  `g_entity_bkp` is the pre-shot health snapshot the function already takes,
  so `was >= 0 && is now < 0` is exactly the shot that killed it. It has to be
  read *after* the callback, because the callback is what turns a point-blank
  shotgun hit into an instant kill. Two more kill paths get their own call:
  `weapon_shatter_enemy` and the fatal branch of `weapon_update_status_effects`.
- **Saves** — `LoadSaveGameState`, right after `FileWrite(g_saveFileName, ...)`
  rather than at the typewriter check, because that is the point the save
  actually exists.

## Persistence
`<save root>/achieve.dat` — `'ACH1'`, an entry count, then the unlocked flags
and the counters. Its own profile file, not part of the game's save block:
that block is ROM-shaped with no spare room, and achievements are meant to
span playthroughs anyway. A file written by an older build is short; the
loader reads the entries it has and leaves the rest at zero, so the table can
grow without invalidating anyone's progress.

Note the save root is wherever `config.ini [Save] Path` points — in a source
checkout that is `assets/`, not `bin/<cfg>/SAVE/`. To reset progress for
testing, overwrite the file with `'ACH1'`, the count, and zeroed payload
(23 bytes at three achievements), with the game closed.

## Adding an achievement
1. Add an id to the enum in `Achievements.h`.
2. Add its row to `s_defs` in `Achievements.cpp` (name, description, icon,
   target — `0` for a plain unlock, anything else makes it a counter with a
   progress bar).
3. Call `Achievements_Unlock(id)` or `Achievements_AddProgress(id, n)` from
   wherever the event happens.

No atlas rebuild is needed unless the icon is not one of the 18 already baked.

## Known gaps
- **English only.** The font atlas is ASCII 32..126. Cyrillic is a one-line
  change in the generator — nothing in the runtime assumes ASCII beyond the
  glyph-table index — but it was not built.
- **USA tree only.** `achvui.bin` and `achv.wav` are deployed under `USA/`;
  with `config.ini [Assets] Version=JPN` they are missing, so the toast
  silently does not draw and makes no sound.
- Long names are not measured against the panel width — they will run past the
  right edge rather than being ellipsised.
- No achievement list screen: a toast is the only way to see one.
