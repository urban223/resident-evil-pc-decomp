// EditorInput.cpp - the mouse.
//
// CUSTOM. The game had NO mouse handling of any kind: a grep for WM_MOUSEMOVE,
// WM_LBUTTONDOWN, GetCursorPos or a cursor global across the whole tree came
// back empty. Everything here is new, and the window procedure now forwards
// four messages into it.
//
// The one piece of real work is the coordinate conversion. A client pixel is
// not a game pixel: the backbuffer can be any size (the window is fixed at
// 640x480 but fullscreen resizes the swapchain through WM_SIZE), and the two
// axes scale INDEPENDENTLY - a 16:9 fullscreen stretch is anisotropic.
// MarniGetRenderScale is the authority on that, and the header that declares it
// warns in as many words never to derive the scale from CMarniDirect3D's own
// m_width/m_height, because SetVideoResolution stomps them.
#include "EditorState.h"
#include "../../Globals.h"
#include "../../marni/MarniSystem.h"

EditorMouse g_edMouse;

static float s_prevX = 0.0f, s_prevY = 0.0f;
static int   s_havePrev = 0;

// A client pixel is a backbuffer pixel: the swap chain is resized to the
// client area on WM_SIZE and the borderless fullscreen window is the monitor.
// Game space is a different question again, and now a harder one, because the
// scene no longer fills the window - it fills the viewport rectangle, through
// the transform the backend is drawing with. Undoing that transform is what
// turns a cursor position back into the coordinate a picking ray starts from.
void EditorInput_OnMouseMove(int clientX, int clientY)
{
    g_edMouse.bbX = (float)clientX;
    g_edMouse.bbY = (float)clientY;

    float rsx = 1.0f, rsy = 1.0f;
    MarniGetRenderScale(&rsx, &rsy);
    if (rsx <= 0.0f) rsx = 1.0f;
    if (rsy <= 0.0f) rsy = 1.0f;

    float ox = 0.0f, oy = 0.0f, vsx = 1.0f, vsy = 1.0f;
    MarniGetViewport(&ox, &oy, &vsx, &vsy);
    if (vsx == 0.0f) vsx = 1.0f;
    if (vsy == 0.0f) vsy = 1.0f;

    g_edMouse.gameX = ((float)clientX - ox) / (rsx * vsx);
    g_edMouse.gameY = ((float)clientY - oy) / (rsy * vsy);
    g_edMouse.inside = 1;
}

// ---------------------------------------------------------------------------
// Typed characters.
//
// A ring, because WM_CHAR arrives on the window thread whenever Windows feels
// like it and the editor reads it once a frame. Sixteen is more than a frame's
// worth of typing; the seventeenth in one frame is dropped rather than
// overwriting one that has not been read.
// ---------------------------------------------------------------------------
#define ED_CHAR_QUEUE 16
static int s_chars[ED_CHAR_QUEUE];
static int s_charHead = 0, s_charTail = 0;

void EditorInput_OnChar(int ch)
{
    const int next = (s_charHead + 1) % ED_CHAR_QUEUE;
    if (next == s_charTail) return;
    s_chars[s_charHead] = ch;
    s_charHead = next;
}

int EditorInput_TakeChar(void)
{
    if (s_charTail == s_charHead) return 0;
    const int ch = s_chars[s_charTail];
    s_charTail = (s_charTail + 1) % ED_CHAR_QUEUE;
    return ch;
}

void EditorInput_OnMouseButton(int button, int down)
{
    if (button < 0 || button > 2) return;
    if (down) {
        if (!g_edMouse.held[button]) g_edMouse.pressed[button] = 1;
        g_edMouse.held[button] = 1;
    } else {
        if (g_edMouse.held[button]) g_edMouse.released[button] = 1;
        g_edMouse.held[button] = 0;
    }
}

void EditorInput_OnMouseWheel(int wheelDelta)
{
    // WHEEL_DELTA is 120 per notch; anything finer accumulates as a fraction
    // of a notch, which is what a precision wheel or a touchpad sends.
    g_edMouse.wheel += wheelDelta;
}

// A button released while the window is not focused never arrives, and a drag
// would run forever. The window procedure calls this on WM_ACTIVATE out.
void EditorInput_OnFocusLost(void)
{
    for (int i = 0; i < 3; i++) {
        g_edMouse.held[i] = 0;
        g_edMouse.pressed[i] = 0;
        g_edMouse.released[i] = 0;
    }
    g_edMouse.wheel = 0;
    s_havePrev = 0;
    s_charHead = s_charTail = 0;
}

void EditorInput_BeginFrame(void)
{
    if (s_havePrev) {
        g_edMouse.dx = g_edMouse.gameX - s_prevX;
        g_edMouse.dy = g_edMouse.gameY - s_prevY;
    } else {
        g_edMouse.dx = 0.0f;
        g_edMouse.dy = 0.0f;
        s_havePrev = 1;
    }
    s_prevX = g_edMouse.gameX;
    s_prevY = g_edMouse.gameY;
}

void EditorInput_EndFrame(void)
{
    for (int i = 0; i < 3; i++) {
        g_edMouse.pressed[i] = 0;
        g_edMouse.released[i] = 0;
    }
    g_edMouse.wheel = 0;
}
