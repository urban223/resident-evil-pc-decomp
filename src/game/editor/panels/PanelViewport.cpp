// PanelViewport.cpp - the hole in the middle, and what is written on it.
//
// CUSTOM (port-only).
//
// This panel draws almost nothing, and that is the point. The rectangle it
// claims is where the RENDERER has already drawn the room this frame - the
// backend was pointed at it by Editor_Tick, through MarniSetViewport - so the
// panel's whole job is to say where that rectangle is and to write the few
// things that belong on top of a viewport: what the camera is doing, what the
// level costs, and whether the game is running in it.
//
// ONE FRAME OF LAG, and why it is fine. The layout runs at the END of a frame,
// after the scene has been drawn, so the rectangle it computes is used by the
// NEXT frame's render. Dragging a splitter therefore moves the panels
// immediately and the image one frame later, which at 30fps is 33ms and
// invisible; the alternative is laying the interface out twice per frame to
// save something nobody can see.
#include "../EditorShell.h"
#include "../EditorState.h"
#include "../../RaidLevel.h"
#include "../../../Globals.h"

#include <stdio.h>
#include <string.h>

// A label that reads over anything: dark plate, light text, no border.
static void view_badge(EdRect r, const char* text, int icon,
                       unsigned int tint, unsigned int bed)
{
    EdUI_Plate(r, EDUI_TILE_ROUND3, bed);
    float x = r.x + 7.0f;
    if (icon >= 0) {
        EdUI_Icon(EdR(x, r.y + (r.h - 13.0f) * 0.5f, 13.0f, 13.0f), icon, tint);
        x += 17.0f;
    }
    EdUI_TextIn(EDUI_FONT_SMALL, text, EdR(x, r.y, r.x + r.w - x - 6.0f, r.h),
                ED_ALIGN_LEFT, tint);
}

void EdPanel_Viewport(EdRect r)
{
    EditorShell* S = &g_edShell;
    S->viewport = r;

    // The bars around the rendered frame. The scene is fitted into this
    // rectangle without distorting it, so unless the rectangle happens to be
    // 4:3 there is some of this showing - filling it explicitly is what keeps
    // it from being whatever the last frame left.
    //
    // Nothing is drawn OVER the middle: the room is already there.
    const int playing = EdAct_Playing();

    // A one-pixel frame, accented while the game is running in it, which is
    // Unreal's own signal that you are looking at play and not at the level.
    EdUI_Outline(r, playing ? EDC_PLAY : EDC_BORDER);

    if (S->immersive) {
        view_badge(EdR(r.x + 12.0f, r.y + 12.0f, 240.0f, 22.0f),
                   "Immersive mode - F11 brings the panels back", -1,
                   EDC_TEXT, 0x99000000u);
        return;
    }

    // --- top-left: what the camera is doing -------------------------------
    {
        float ex, ey, ez;
        EditorCamera_Eye(&ex, &ey, &ez);
        char t[80];
        snprintf(t, sizeof(t), "%d, %d, %d", (int)ex, (int)ey, (int)ez);
        view_badge(EdR(r.x + 10.0f, r.y + 10.0f,
                       EdUI_TextW(EDUI_FONT_SMALL, t) + 34.0f, 20.0f),
                   t, EDUI_ICON_CAMERA, EDC_TEXT_DIM, 0x88000000u);
    }

    // --- top-right: the mode ----------------------------------------------
    if (playing) {
        const char* t = "PLAYING";
        const float w = EdUI_TextW(EDUI_FONT_SMALL, t) + 34.0f;
        view_badge(EdR(r.x + r.w - w - 10.0f, r.y + 10.0f, w, 20.0f), t,
                   EDUI_ICON_PLAY, EDC_PLAY, 0x99000000u);
    } else {
        const char* t = (g_edGizmoMode == ED_GIZMO_MOVE) ? "MOVE" : "ROTATE";
        const float w = EdUI_TextW(EDUI_FONT_SMALL, t) + 34.0f;
        view_badge(EdR(r.x + r.w - w - 10.0f, r.y + 10.0f, w, 20.0f), t,
                   (g_edGizmoMode == ED_GIZMO_MOVE) ? EDUI_ICON_MOVE
                                                    : EDUI_ICON_ROTATE,
                   EDC_TEXT_DIM, 0x88000000u);
    }

    // --- bottom-left: statistics ------------------------------------------
    if (S->showStats) {
        char a[64], b[64];
        snprintf(a, sizeof(a), "%d boxes   %d actors",
                 g_raidLevel.nbox,
                 g_raidLevel.nitem + g_raidLevel.nenemy + g_raidLevel.nlight);
        snprintf(b, sizeof(b), "snap %d   angle %d deg",
                 S->snapMove, (S->snapAngle * 360) / 4096);
        const float w = 190.0f;
        view_badge(EdR(r.x + 10.0f, r.y + r.h - 54.0f, w, 20.0f), a,
                   EDUI_ICON_STATS, EDC_TEXT_DIM, 0x88000000u);
        view_badge(EdR(r.x + 10.0f, r.y + r.h - 31.0f, w, 20.0f), b,
                   EDUI_ICON_SNAP, EDC_TEXT_DIM, 0x88000000u);
    }

    // --- bottom-right: the controls, because nobody reads a manual ---------
    {
        const char* t = playing
            ? "Esc  stop"
            : "RMB look   WASD fly   Q/E down/up   F focus";
        const float w = EdUI_TextW(EDUI_FONT_SMALL, t) + 18.0f;
        view_badge(EdR(r.x + r.w - w - 10.0f, r.y + r.h - 31.0f, w, 20.0f),
                   t, -1, EDC_TEXT_FAINT, 0x77000000u);
    }
}
