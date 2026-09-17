// Editor.h - the in-game level editor for RAID arenas: public surface.
//
// CUSTOM. Not in the original game.
//
// WHY THIS LIVES INSIDE THE GAME
//
// tools/raid_editor.html draws the room with the same arithmetic RaidArena.cpp
// uses, and that is as close as an outside program can honestly get: it is a
// second renderer agreeing with the first by careful transcription. It cannot
// show what the engine's own lighting does to a character, or what an item
// model really looks like at its real size, because those come out of the
// engine's pipeline and not out of a level file.
//
// So the editor is here instead. The room you fly around IS the room, drawn by
// the renderer that draws the game, lit by update_entity_lighting, holding the
// models the game loaded. Nothing agrees with anything by construction - there
// is only one thing.
//
// HOW IT ATTACHES
//
// As little as possible, in three seams, each one line:
//
//   window_proc.cpp   feeds mouse and key events in (the game had no mouse
//                     handling at all before this)
//   GameLoop.cpp      treats Editor_IsOpen() exactly like g_debugMenuOpen -
//                     scripts and entity updates are skipped while it is up
//   InputSystem.cpp   blanks the published pad the same way, so the player
//                     does not walk while you are editing
//
// The camera is not a seam at all: the editor writes its own eye into the RDT
// camera record the room is already using and calls Room_SetupCamera(). Every
// consumer - the arena geometry, the entity models, the collision overlay -
// reads that one record, so all of them follow without knowing anything about
// an editor.
#pragma once

// ---------------------------------------------------------------------------
// Input, fed from the window procedure. Coordinates are CLIENT pixels; the
// conversion to the game's 320x240 space happens inside, because it needs the
// render scale and that is not the window procedure's business.
// ---------------------------------------------------------------------------
#define ED_MB_LEFT    0
#define ED_MB_RIGHT   1
#define ED_MB_MIDDLE  2

void EditorInput_OnMouseMove(int clientX, int clientY);
void EditorInput_OnMouseButton(int button, int down);
void EditorInput_OnMouseWheel(int wheelDelta);      // WHEEL_DELTA units, +away
void EditorInput_OnFocusLost(void);                 // drop every held button

// A typed character, already through the keyboard layout - which is what a
// text field needs and what a virtual key code is not.
void EditorInput_OnChar(int ch);

// ---------------------------------------------------------------------------
// The mode.
// ---------------------------------------------------------------------------
int  Editor_IsOpen(void);       // 1 while EDITING: gameplay is frozen
int  Editor_Active(void);       // 1 while the editor is up at all, Play included
void Editor_Toggle(void);       // the hotkey, from the window procedure
void Editor_Tick(void);         // once a frame, before the scene is drawn
void Editor_Draw(void);         // the world overlay, drawn with the room
void Editor_DrawUI(void);       // the interface, last thing before the flip
