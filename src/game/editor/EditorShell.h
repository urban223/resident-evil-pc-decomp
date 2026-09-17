// EditorShell.h - the window: what is docked where, and the commands the
// panels fire.
//
// CUSTOM (port-only).
//
// The layout is the one Unreal's editor settled on and every editor since has
// copied, for the good reason that it puts the three questions you are always
// asking next to each other: what is in the level (outliner), what is this
// thing (details), and what does it look like (viewport). The bars are cut off
// the window edges in order and the viewport is whatever is left - so it is
// never the thing that has to be resized when something else grows.
#pragma once
#include "ui/EditorUI.h"

struct EditorShell {
    // Dock sizes, design px. Dragged by the splitters, kept here so they
    // survive a mode change.
    float leftW;
    float rightW;
    float placeH;          // Place Actors' share of the left dock
    float outlinerH;       // the outliner's share of the right dock

    int   immersive;       // F11: the viewport alone, no panels
    int   showGrid;
    int   showFloor;
    int   showStats;
    int   showZones;
    int   showLights;

    int   snapMove;        // world units, 0 = off
    int   snapAngle;       // 4096 to a turn, 0 = off

    int   contentTab;      // 0 items, 1 enemies
    char  search[32];
    int   contentPick;     // the type the content browser has selected

    int   groupOpen[8];    // the outliner's category twisties
    int   detailOpen[5];   // the details panel's section twisties

    EdRect viewport;       // what the viewport panel claimed this frame
    float  fps;
};

extern EditorShell g_edShell;

void EditorShell_Init(void);
void EditorShell_Draw(void);          // the whole interface, once a frame

// The panels. Each is handed the rectangle it owns and draws inside it.
void EdPanel_MenuBar(EdRect r);
void EdPanel_Toolbar(EdRect r);
void EdPanel_Place(EdRect r);
void EdPanel_Content(EdRect r);
void EdPanel_Outliner(EdRect r);
void EdPanel_Details(EdRect r);
void EdPanel_Viewport(EdRect r);
void EdPanel_StatusBar(EdRect r);

// A dock's title strip, with an icon and an optional right-aligned note.
// Returns the body rectangle below it.
EdRect EdPanel_Header(EdRect r, int icon, const char* title, const char* note);

// ---------------------------------------------------------------------------
// Commands. The menu, the toolbar and the keyboard all end up here, so a
// command does the same thing however it was asked for.
// ---------------------------------------------------------------------------
void EdAct_AddBox(void);
void EdAct_AddItem(int type);
void EdAct_AddEnemy(int type);
void EdAct_AddLight(void);
void EdAct_AddCamera(void);
void EdAct_AddZone(void);
void EdAct_Duplicate(void);
void EdAct_Delete(void);
void EdAct_Focus(void);
void EdAct_FrameAll(void);
void EdAct_Save(void);
void EdAct_Reload(void);
void EdAct_CameraFromView(void);

void EdAct_Play(void);
void EdAct_Stop(void);
int  EdAct_Playing(void);

// Where a newly placed actor goes: the camera's pivot, snapped, which is what
// "in front of you" means when the camera is an orbit.
void EdAct_PlacePoint(int* x, int* z);
int  EdAct_Snap(int v);
