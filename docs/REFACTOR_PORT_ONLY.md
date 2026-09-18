# Refactoring the port-only code

A worklist for Claude Code, written from a survey of the port-only sources on
2026-09-17. Work through it top to bottom: the early steps shrink the surface
the later ones have to move.

## Scope

**In scope — these files have no original binary behind them, so there is
nothing to stay faithful to:**

```
src/game/editor/**              6172 lines   the RE1 EDITOR (see docs/RE1_EDITOR.md)
src/game/UiAtlas.{h,cpp}         233         the shared Space GUI atlas
src/game/UiSkin.{h,cpp}          930         the status-screen skin
src/game/Achievements.{h,cpp}    620         the toast
src/game/Raid*.{h,cpp}          1525         arena, level format, items, models, enemies
tools/build_achievement_ui.py    753
tools/build_editor_ui.py         465
```

**Out of scope — do not touch:** anything transcribed from Ghidra, i.e. every
file carrying `0x00...` addresses in its comments. `AGENTS.md` is explicit that
this project reproduces the original binary's behaviour and is *not* a
redesign, and that the original's quirks are preserved rather than cleaned up.
In those files the structure **is** the artifact. Renaming a local variable is
fine; extracting a helper, reordering statements or "simplifying" control flow
is not.

The boundary is not always the file. `MainMenu.cpp` and `TitleScreen.cpp` are
original-derived files with port-only insertions. If a step below needs to
change one, change only the inserted block and say so in the commit message.

## Ground rules

From `AGENTS.md`, and all of them are load-bearing here:

- **Strictly 32-bit.** No 64-bit types or arithmetic assumptions. (This was
  violated once already: `PanelDetails.cpp` hashed `(unsigned long long)`
  pointers to build widget ids. Fixed — do not reintroduce the pattern.)
- **`src/game/` stays OS-agnostic.** Run `python3 tests/check_platform_boundary.py`
  before every commit.
- **`CMakeLists.txt` and `Game.vcxproj` are both source-of-truth lists.** Any
  file added, removed or renamed must be changed in both, in the same commit.
- Keep the existing comment voice: these files explain *why*, not *what*, and
  say what was tried and failed where that is the useful part. A refactor that
  drops the reasoning loses more than it gains.
- **No behaviour change.** Every step below is meant to be observationally
  identical. If a step cannot be done without changing behaviour, stop and say
  so rather than changing it quietly.

## Verification, per step

There is no automated test for game behaviour, so the protocol is the defence:

1. `build_debug.bat` — must compile clean.
2. `python3 tests/check_platform_boundary.py`.
3. Run the game and exercise the screen the step touched:
   - editor steps → RAID mode, F2, then F5/Esc
   - `UiSkin` → the status screen, including the item action menu and a
     message line
   - `Achievements` → trigger a toast
   - `Raid*` → enter the arena, take a pickup, let an enemy spawn
4. One commit per step, with the step number in the message. A single
   "cleaned everything up" commit cannot be bisected and cannot be reviewed.

If a step's screen cannot be reached quickly, say so and leave the step
undone rather than committing it unverified.

---

## Step 1 — one atlas loader instead of two

`UiAtlas.cpp:26` and `src/game/editor/ui/EditorUIDraw.cpp:27` are the same
function with two magic numbers: malloc `w*h*4 + 12`, `LoadFile`, check a
four-byte tag, check the width and height in the header against the compiled-in
constants, `MarniCreateTexture(…, 32, buf + 12, &handle)`, free. Both retry
three times and both give up afterwards.

Extract one loader — suggested `src/game/PortAtlas.{h,cpp}`:

```c
// Load a baked RGBA atlas: <tag> + w + h + rows. Answers 0 until the graphics
// device is up; gives up after a few attempts so a missing file is not
// reopened every frame.
int PortAtlas_Load(const char* path, const char* tag,
                   int width, int height, MarniHandle* outTex);
```

Both callers keep their own retry counter and their own `s_atlas`, or the
helper owns both — either is fine, but pick one and use it twice.

Watch for: the two blobs have different tags (`AUI1`, `EUI1`) and different
dimensions, and the editor's loader additionally has `EdUiDraw_Reset()` for
device loss. Keep that.

## Step 2 — one baked-font text engine instead of two

`AchvGlyph` and `EdUiGlyph` are the same seven `short`s. `UiAtlas_Text` /
`UiAtlas_TextWidth` and `EdUI_Text` / `EdUI_TextW` are the same two loops. The
editor's version additionally does ellipsising and box alignment
(`EdUI_TextIn`), which the toast could use and currently cannot.

Unify the glyph struct and the measure/draw loops in `PortAtlas` (or a
`PortText` beside it), leaving each caller its own quad sink — that is the one
genuine difference: `UiAtlas` has two (immediate `UiAtlas_Blit` and queued
`PendingSprite_Push` at a depth), the editor has one with software clipping.
A function pointer or a small sink struct is enough.

Both generated headers (`AchievementAtlasData.h`, `editor/ui/EditorUIData.h`)
must then emit the shared struct name. Change the generators, not the
generated files — see step 3, which is worth doing first.

## Step 3 — one atlas baker library

`tools/build_achievement_ui.py` (753 lines) and `tools/build_editor_ui.py`
(465) share `class Packer`, `def bake_font`, `def gui`, the blob writer and the
glyph-table emitter, with small divergences that are accidents rather than
decisions — the editor bakes at 2× and the toast does not, the editor emits
`EdUiGlyph` and the toast `AchvGlyph`.

Extract `tools/atlas_lib.py` with the packer, the font baker, the blob writer
and the header emitter, parameterised by tag, atlas size, bake factor and
struct name. Each script keeps only what is actually its own: which art it
bakes and which rects it names.

Zero runtime risk — but re-run **both** generators afterwards and confirm the
two `.bin` files are byte-identical to the committed ones before the change
(`certutil -hashfile <file> SHA256` on Windows). If they are not, the
refactor changed the art, which is a failure, not a new baseline.

Both `.bin` atlases now live in `portdata/USA/Data/` and are copied into
`assets/` and both `bin/` trees by `tools/deploy_portdata.py`. Re-run it after
any re-bake, and `--check` to confirm all three copies match — see
`docs/ASSETS.md`.

## Step 4 — split `UiSkin_DrawStatus`

213 lines, the longest port-only function in the tree, and it draws the whole
status screen: plates, cells, the info card, the condition block, the EKG
frame, the tabs, the filler. `filler()` (118 lines) and `action_menu()` (78)
are already separate — finish the job along the seams the code already has, one
static function per block, each taking the `UiSkinState*` it reads.

Keep the draw ORDER byte-identical: the skin queues at depths that interleave
with the engine's own icon draws (`UiAtlas_Push` at ~700, the scrim at ~1000),
and the comment at the top of `UiSkin.h` explains why the order is what it is.
Reordering the calls would be a behaviour change even though nothing about the
data changed.

## Step 5 — make `RaidLevel_Load` table-driven

110 lines of `if (!strcmp(kw, "box")) … else if (!strcmp(kw, "item")) …`, and
every new keyword has cost the same edit in three places (the parser here, the
writer in `editor/EditorSave.cpp`, and the format doc). A small table of
`{ keyword, min args, max args, handler }` collapses the parser and makes the
writer checkable against it.

The format is documented in `docs/RAID_LEVEL_FORMAT.md` — keep it in step.

Two properties the current parser has that must survive: **Y is negative
upwards**, and every coordinate is read back **unsigned**, so values must stay
positive and below 32768. A table-driven rewrite that quietly starts accepting
negatives will produce level files the game reads as enormous positives.

Round-trip check for this step: load `Data\raid1.lvl`, press Ctrl+S in the
editor without changing anything, and diff the file against the original. It
must be identical.

## Step 6 — the editor's own tidy-up

- `src/game/editor/EditorShell.cpp` implements what
  `src/game/editor/EditorShell.h` declares, but the two are in different
  directories and a third file, `EditorActions.cpp`, also implements part of
  that header (`g_edShell` lives there). Either move the shell's header beside
  its implementation or move `EditorShell_Draw` out of `panels/`. Pick one; the
  current split is an accident of the order things were written in.
- `EdPanel_Toolbar` (102 lines) is four groups of controls in one function:
  file, play, gizmo, snap, view, camera. One static per group, each taking and
  returning the remaining row rect — the `EdR_Cut` idiom already makes this
  natural.
- `EdPanel_Content` (98) likewise splits into the tab strip, the list and the
  footer button.
- `EditorCamera_Update` (78) is three independent gestures (orbit, pan, fly)
  sharing one function. They do not interact; separate them.

## Step 7 — item names from the game's own table *(highest value, do last)*

`src/game/editor/EditorContent.cpp` carries a hardcoded table of 88 item names,
transcribed from the browser editor. The game already has `g_ItemNamePointers[128]`
(`0x004bf0a0`, indexed by `itemId - 1`) and a JPN counterpart
`g_ItemNamePointersJpn` — so the editor currently holds a **second source of
truth** that can silently disagree with what the game itself displays, and that
is English-only in a project whose stated goal is adding the Japanese release.

Replace the table with a lookup into the game's own pointers, decoded through
whatever `MainMenu.cpp` already uses to fill `UiSkinState::itemName` (find it
before writing anything — do not write a second decoder).

Keep `EditorContent`'s `model` column: that maps an item id to its `.ivm` and
has no equivalent in the game's tables.

This is last because it is the only step that can change what the editor
*shows*, and because it reaches into an original-derived file to find the
decoder.

## Step 8 — the Linux gate does not cover the editor

`tests/compile_linux.sh` globs `src/game/*.cpp` and `src/game/entities/*.cpp`.
`src/game/editor/`, `src/game/editor/ui/` and `src/game/editor/panels/` are in
neither, so none of the editor has ever been through that gate — including the
files written before today. Add them.

Note this is a pre-existing gap, not something the refactor introduced, and
fixing it may surface real errors on the 32-bit Linux build. Those are worth
having.
