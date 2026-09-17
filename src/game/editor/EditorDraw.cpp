// EditorDraw.cpp - the overlay's drawing primitives.
//
// CUSTOM.
//
// Two of them, and both already existed in the engine; this file is mostly
// about getting the units right.
//
//   text   PrintText8x8 takes GAME space (320x240) and plain ASCII, with the
//          string in the global PRINT_TEXT_BUFFER rather than an argument. It
//          queues at a depth below PENDING_SCENE_DEPTH, which is drawn after
//          FlushTmdObjects - so it lands on top of the 3D scene with no extra
//          work. `shadow` must be 0: it retints the whole string rather than
//          drawing a second offset pass.
//
//   lines  MarniDrawLine takes BACKBUFFER pixels and is immediate-mode with
//          depth testing disabled, so it is always on top. That is exactly what
//          a manipulator wants - a gizmo you cannot see because the floor is in
//          front of it is not a manipulator. It is one draw call per line, so
//          it is for the handful of lines a gizmo needs and not for a grid.
#include "EditorState.h"
#include "../../Globals.h"
#include "../../marni/MarniSystem.h"
#include <cstdio>
#include <cstdarg>

void EditorDraw_Text(int x, int y, unsigned char colour, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), fmt, ap);
    va_end(ap);
    PRINT_TEXT_BUFFER[sizeof(PRINT_TEXT_BUFFER) - 1] = '\0';
    PrintText8x8((short)x, (short)y, colour, 0);
}

// Game space in, backbuffer pixels out - the conversion MarniDrawLine does not
// do for you.
void EditorDraw_Line(float ax, float ay, float bx, float by,
                     float width, unsigned int argb)
{
    float sx = 1.0f, sy = 1.0f;
    MarniGetRenderScale(&sx, &sy);
    MarniDrawLine(ax * sx, ay * sy, bx * sx, by * sy,
                  width * ((sx + sy) * 0.5f), argb);
}

// A segment in the world, clipped to the near plane so a line with one end
// behind the camera is shortened rather than projected to nonsense.
void EditorDraw_WorldLine(float x0, float y0, float z0,
                          float x1, float y1, float z1,
                          float width, unsigned int argb)
{
    if (!g_edView.ok) return;

    const float NEARP = 96.0f;
    float ax = x0, ay = y0, az = z0;
    float bx = x1, by = y1, bz = z1;

    const float da = g_edView.n[0]*(ax - g_edView.fromX) +
                     g_edView.n[1]*(ay - g_edView.fromY) +
                     g_edView.n[2]*(az - g_edView.fromZ);
    const float db = g_edView.n[0]*(bx - g_edView.fromX) +
                     g_edView.n[1]*(by - g_edView.fromY) +
                     g_edView.n[2]*(bz - g_edView.fromZ);
    if (da < NEARP && db < NEARP) return;

    if (da < NEARP) {
        const float t = (NEARP - da) / (db - da);
        ax = x0 + (x1 - x0) * t; ay = y0 + (y1 - y0) * t; az = z0 + (z1 - z0) * t;
    } else if (db < NEARP) {
        const float t = (NEARP - db) / (da - db);
        bx = x1 + (x0 - x1) * t; by = y1 + (y0 - y1) * t; bz = z1 + (z0 - z1) * t;
    }

    float sax, say, sbx, sby;
    if (!EditorView_Project(ax, ay, az, &sax, &say)) return;
    if (!EditorView_Project(bx, by, bz, &sbx, &sby)) return;
    EditorDraw_Line(sax, say, sbx, sby, width, argb);
}
