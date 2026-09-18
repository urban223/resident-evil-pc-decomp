# RE1 EDITOR — one native program, built the way Unreal's editor is

The RAID level editor is part of the game executable. One process, one window,
one renderer. The viewport is the game drawn into part of that window; the
panels around it are drawn by the same renderer in the same frame; Play runs
the level inside that same viewport without loading anything.

This replaces the browser editor — a `tools/raid_editor.html` page served by
`tools/raid_editor_server.py`. That is superseded and no longer developed, and
the page itself has since been deleted, so the server can no longer serve
anything (it answers 404 and says so). `tools/raid_editor.bat` only says where
the editor went.

## Why it is arranged this way

An outside program can only agree with the engine by transcription. It cannot
show what `update_entity_lighting` does to a character, or what an item model
really looks like at its real size, because those come out of the engine's
pipeline. So the editor is in the engine, and nothing has to agree with
anything — there is only one of everything.

## The three pieces

### 1. The viewport is a renderer transform, not a render target

`MarniDX` gained one seam:

```
void SetViewportTransform(float ox, float oy, float sx, float sy);
void SetScissor(int x, int y, int w, int h);
```

Every screen-space draw path in the backend builds its orthographic matrix
from one helper (`BuildScreenMatrix`), fed by four call sites — sprites,
rects, lines and the two triangle paths. Setting an offset and a scale there
retargets the WHOLE renderer into a sub-rectangle of the window: room
geometry, character models, the sprite queue, the HUD. The scissor stops what
falls outside from painting over the panels.

`MarniSetViewport(x, y, w, h, fit)` in MarniSystem wraps it.
`MARNI_FIT_COVER` for editing (fill the rectangle, you are flying the camera),
`MARNI_FIT_CONTAIN` for Play (letterbox, the HUD is part of what is being
tested). Implemented in both backends — D3D11 and the GL one; GL's `Clear`
temporarily drops the scissor, because `glClear` is masked by it and
`ClearRenderTargetView` is not.

The rectangle comes from the interface's layout, which runs at the END of a
frame, so it is one frame behind. That is 33ms and invisible; laying the
interface out twice per frame to avoid it would buy nothing.

### 2. A widget toolkit on one textured quad

`src/game/editor/ui/` — Slate's role, at this project's scale.

- `tools/build_editor_ui.py` bakes `assets/USA/Data/edui.bin` (1024×576 RGBA,
  2.3 MB) and generates `EditorUIData.h`: four Saira Condensed faces at 17 /
  17 / 14 / 22 design px, all baked at **2×** and sampled linearly; rounded
  9-slice plates; 60 icons (most from the Space GUI pack, the editor-specific
  ones — cube, bulb, camera, cursor, floppy, dashed zone — drawn in the script
  at 4× and downsampled).
- `EditorUIDraw.cpp` — the only file that knows about backbuffer pixels.
  Clipping is done in software (shrink the rect, move the UVs by the same
  fraction) because the hardware scissor belongs to the viewport.
- `EditorUICore.cpp` — frame, hot/active/focus ids (hashes, never pointers:
  the level is C arrays a reload replaces), clip stack, scroll slots,
  splitters, deferred menu/combo/tooltip layers drawn last.
- `EditorUIWidgets.cpp` — button, checkbox, drag-int (scrub **or** click and
  type, like Unreal's), slider, text field, combo, tree row, section, field row.
- `EditorUITheme.h` — every colour and metric in one place.

Layout is in **design pixels**: the interface is laid out as though the window
were 1920×1080 and `EdUI_Scale()` (backbufferH/1080, clamped 0.8–2.5) maps it
to the real backbuffer at the last moment.

### 3. The shell

`src/game/editor/panels/` — bars carved off the window edges, viewport is the
remainder:

menu bar · toolbar (Save, Reload, Play/Stop, gizmo modes, snap, view toggles,
camera speed) · left dock (Place Actors over a searchable Content browser of
every item and enemy) · centre viewport · right dock (World Outliner over
Details) · status bar. Draggable splitters, F11 immersive mode.

Details writes straight into `g_raidLevel` and calls whatever rebuilds the
cache derived from it — `RaidLevel_Apply` for collision/lights/cameras,
`RaidItems_Reset`, `RaidItemModels_Sync` when an item type changes.

## Modes

| | frozen | camera | pad |
|---|---|---|---|
| off | no | level | game's |
| edit | yes | free | blanked |
| play | no | level | game's |

`Editor_IsOpen()` means EDITING (what GameLoop and InputSystem freeze on);
`Editor_Active()` means the interface is up at all. Play does what
`Raid_EnterRoom` does minus the file read: apply the level, stand the player
on the spawn, reset and re-give items, clear and respawn enemies,
`check_camera_switch(1)`. Stop clears the enemies and hands the room back to
the free camera. Nothing is reloaded either way.

## Keys

F2 editor · F5 play · Esc stop · F11 immersive · 1/2 move/rotate · F focus ·
Home frame all · G grid · C camera-from-view · Ctrl+S save · Ctrl+D duplicate ·
Del delete · RMB look · WASD fly · Q/E world down/up · Shift fine

## Seams into the engine (one line each)

`window_proc.cpp` mouse + WM_CHAR + F2 · `GameLoop.cpp` `Editor_Tick()` and
the freeze · `InputSystem.cpp` blanks the pad while editing ·
`Rendering.cpp` `Editor_Draw()` (world overlay, mid-scene) and
`Editor_DrawUI()` (the interface, last before the flip).

## How it is verified

The tree has a non-Windows path, so
`g++ -fsyntax-only -fpermissive -std=c++17 -Isrc` type-checks everything
except the four Windows-only translation units. Beyond that, the toolkit and
every panel are compiled against a software rasteriser offline and the
interface is rendered to a PNG at 1366×768, 1920×1080 and 2560×1440 — the real
layout code, which is what catches a panel overlapping its neighbour. The
viewport matrix is checked numerically in isolation (screen origin must land
on the viewport's corner).
