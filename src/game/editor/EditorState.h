// EditorState.h - what the editor's translation units share.
//
// CUSTOM. Internal to src/game/editor; nothing outside includes this.
#pragma once
#include "Editor.h"

// ---------------------------------------------------------------------------
// Mouse.
//
// `gameX/gameY` are in the game's own 320x240 space, which is the space every
// projection in this engine works in (see g_SubpixelOffsetX/Y). `pressed` and
// `released` are single-frame edges, cleared by EditorInput_EndFrame.
// ---------------------------------------------------------------------------
struct EditorMouse {
    float bbX, bbY;            // BACKBUFFER pixels: what the interface uses
    float gameX, gameY;        // game space INSIDE the viewport
    float dx, dy;              // movement since the last frame, game units
    int   held[3];
    int   pressed[3];
    int   released[3];
    int   wheel;               // WHEEL_DELTA units this frame, + away
    int   inside;              // the cursor is over the client area
};

extern EditorMouse g_edMouse;

void EditorInput_BeginFrame(void);
void EditorInput_EndFrame(void);

// Typed characters, as the window procedure's WM_CHAR delivers them - already
// through the keyboard layout, which a virtual key code is not. Returns 0 when
// the queue is empty. Only the text fields read this.
void EditorInput_OnChar(int ch);
int  EditorInput_TakeChar(void);

// ---------------------------------------------------------------------------
// The free camera.
//
// The same pivot/yaw/pitch/distance model the browser editor uses, and for the
// same reason: orbiting a point is what you want when you are looking AT
// something, and flying is what you want when you are looking FOR something.
// Y IS NEGATIVE UPWARDS, so a positive pitch lifts the eye by SUBTRACTING.
// ---------------------------------------------------------------------------
struct EditorCamera {
    float tx, ty, tz;          // the pivot
    float yaw, pitch;
    float dist;
    int   fov;                 // a focal length in 320-wide space, not an angle
    float speed;               // fly speed, world units per frame
};

extern EditorCamera g_edCam;

void  EditorCamera_FrameLevel(void);          // put it where the whole level fits
void  EditorCamera_FrameSelection(void);      // and where the selection fills it
void  EditorCamera_Update(void);              // read the mouse and the keys
void  EditorCamera_Apply(void);               // write it into the room's camera
void  EditorCamera_Restore(void);             // put the level's own camera back
void  EditorCamera_Eye(float* x, float* y, float* z);

// The view the room is being drawn with, rebuilt every frame from the same RDT
// camera record - so a ray from the cursor lands where the cursor is pointing.
struct EditorView {
    float cx, cy, f;
    float fromX, fromY, fromZ;
    float n[3], r[3], u[3];
    int   ok;
};
extern EditorView g_edView;

void EditorView_Build(void);
int  EditorView_Project(float wx, float wy, float wz, float* sx, float* sy);
void EditorView_Ray(float gameX, float gameY, float* ox, float* oy, float* oz,
                    float* dx, float* dy, float* dz);

// ---------------------------------------------------------------------------
// Selection.
//
// A selection is a KIND and an INDEX into the matching array in g_raidLevel.
// Not a pointer: a level reload rebuilds those arrays wholesale, and a pointer
// into the old one is a crash waiting for the next F7.
// ---------------------------------------------------------------------------
enum EdKind {
    ED_NONE = 0,
    ED_BOX, ED_ITEM, ED_ENEMY, ED_SPAWN, ED_LIGHT, ED_CAM, ED_ZONE
};

struct EditorSel {
    int kind;
    int index;
};

extern EditorSel g_edSel;

void EditorSelect_Clear(void);
int  EditorSelect_Valid(void);                 // does it still point at something?
void EditorSelect_PickAt(float gameX, float gameY);
void EditorSelect_Bounds(float* x0, float* y0, float* z0,
                         float* x1, float* y1, float* z1);
void EditorSelect_Origin(float* x, float* y, float* z);
void EditorSelect_Move(int dx, int dy, int dz);   // world units, already snapped
void EditorSelect_Turn(int delta);                // 4096 to a turn
void EditorSelect_Delete(void);
const char* EditorSelect_Name(void);
void EditorSelect_Draw(void);                     // the white outline

// ---------------------------------------------------------------------------
// The manipulator.
// ---------------------------------------------------------------------------
#define ED_GIZMO_MOVE    0
#define ED_GIZMO_ROTATE  1

extern int g_edGizmoMode;

void EditorGizmo_Update(void);      // hit test and drag, once a frame
void EditorGizmo_Draw(void);
int  EditorGizmo_Busy(void);        // a handle is being dragged: do not re-pick

// ---------------------------------------------------------------------------
// Writing the level back out.
// ---------------------------------------------------------------------------
int  EditorSave_Write(void);        // returns how many copies landed
const char* EditorSave_Status(void);

// ---------------------------------------------------------------------------
// Little shared helpers.
// ---------------------------------------------------------------------------
void EditorDraw_Text(int x, int y, unsigned char colour, const char* fmt, ...);
void EditorDraw_Line(float ax, float ay, float bx, float by,
                     float width, unsigned int argb);
void EditorDraw_WorldLine(float x0, float y0, float z0,
                          float x1, float y1, float z1,
                          float width, unsigned int argb);
