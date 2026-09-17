// EditorShell.cpp - the window: bars cut off the edges, viewport in the hole.
//
// CUSTOM (port-only).
//
// Layout is done by carving. Each bar takes its strip off the rectangle that
// is left, in the order the eye reads them, and whatever survives is the
// viewport. That is why no panel needs to know the window size and why the
// viewport never has to be resized by hand: it is defined as the remainder.
//
// The splitters are the only stateful part - they write dock sizes into
// g_edShell, which is also where the view toggles and the snap settings live,
// so the whole interface's arrangement is one struct that could be written to
// a config file the day someone wants it to persist.
#include "EditorShell.h"
#include "EditorState.h"
#include "../RaidLevel.h"
#include "../../Globals.h"

#include <stdio.h>
#include <string.h>

EditorShell g_edShell;

void EditorShell_Init(void)
{
    EditorShell* S = &g_edShell;
    if (S->leftW > 0.0f) return;              // already set up

    S->leftW = 236.0f;
    S->rightW = 312.0f;
    S->placeH = 300.0f;
    S->outlinerH = 330.0f;
    S->showGrid = 1;
    S->showFloor = 1;
    S->showStats = 1;
    S->showZones = 1;
    S->showLights = 1;
    S->snapMove = 100;
    S->snapAngle = 512;                        // 45 degrees, 4096 to a turn
    S->contentPick = 11;                       // a clip: something is selected
    for (int i = 0; i < 8; i++) S->groupOpen[i] = 1;
    for (int i = 0; i < 5; i++) S->detailOpen[i] = 1;
    S->search[0] = '\0';
}

EdRect EdPanel_Header(EdRect r, int icon, const char* title, const char* note)
{
    const EdRect head = EdR(r.x, r.y, r.w, EDM_HEADER_H);
    EdUI_Fill(head, EDC_HEADER);
    EdUI_Fill(EdR(head.x, head.y + head.h - 1.0f, head.w, 1.0f), EDC_BORDER);
    // The accent tab down the left of the strip is what tells two stacked
    // panels apart at a glance when neither has focus.
    EdUI_Fill(EdR(head.x, head.y + 4.0f, 2.0f, head.h - 9.0f), EDC_ACCENT_DIM);

    float x = head.x + EDM_PAD;
    if (icon >= 0) {
        EdUI_Icon(EdR(x, head.y + (head.h - 14.0f) * 0.5f, 14.0f, 14.0f),
                  icon, EDC_TEXT_DIM);
        x += 20.0f;
    }
    EdUI_TextIn(EDUI_FONT_BOLD, title,
                EdR(x, head.y, head.w - (x - head.x) - EDM_PAD, head.h),
                ED_ALIGN_LEFT, EDC_TEXT);
    if (note && *note) {
        EdUI_TextIn(EDUI_FONT_SMALL, note,
                    EdR(head.x, head.y, head.w - EDM_PAD, head.h),
                    ED_ALIGN_RIGHT, EDC_TEXT_FAINT);
    }
    return EdR(r.x, r.y + EDM_HEADER_H, r.w, r.h - EDM_HEADER_H);
}

void EditorShell_Draw(void)
{
    EditorShell* S = &g_edShell;
    EditorShell_Init();

    EdRect win = EdR(0.0f, 0.0f, EdUI_Width(), EdUI_Height());

    if (S->immersive) {
        // F11. The viewport and a single line telling you how to get back:
        // an editor you cannot leave is a bug, not a mode.
        S->viewport = win;
        EdPanel_Viewport(win);
        return;
    }

    EdPanel_MenuBar(EdR_Cut(&win, EDM_MENUBAR_H, ED_SIDE_TOP));
    EdPanel_Toolbar(EdR_Cut(&win, EDM_TOOLBAR_H, ED_SIDE_TOP));
    EdPanel_StatusBar(EdR_Cut(&win, EDM_STATUS_H, ED_SIDE_BOTTOM));

    // The docks are clamped against the window so a narrow window keeps a
    // viewport rather than losing it between two panels that no longer fit.
    const float maxDock = (win.w - 360.0f) * 0.5f;
    if (S->leftW  > maxDock) S->leftW  = maxDock;
    if (S->rightW > maxDock) S->rightW = maxDock;
    if (S->leftW  < 150.0f)  S->leftW  = 150.0f;
    if (S->rightW < 180.0f)  S->rightW = 180.0f;

    EdRect left = EdR_Cut(&win, S->leftW, ED_SIDE_LEFT);
    EdRect leftSplit = EdR_Cut(&win, EDM_SPLITTER, ED_SIDE_LEFT);
    EdRect right = EdR_Cut(&win, S->rightW, ED_SIDE_RIGHT);
    EdRect rightSplit = EdR_Cut(&win, EDM_SPLITTER, ED_SIDE_RIGHT);

    S->viewport = win;
    EdPanel_Viewport(win);

    // --- left dock: Place Actors over the Content browser ------------------
    {
        // The bound is a HEIGHT, not a coordinate: the top pane may grow until
        // the bottom one is down to a header and a couple of rows.
        const float total = left.h - EDM_SPLITTER;
        float hi = total - 140.0f;
        if (hi < 120.0f) hi = 120.0f;
        if (S->placeH > hi)     S->placeH = hi;
        if (S->placeH < 120.0f) S->placeH = 120.0f;

        EdRect place = EdR_Cut(&left, S->placeH, ED_SIDE_TOP);
        EdRect bar = EdR_Cut(&left, EDM_SPLITTER, ED_SIDE_TOP);
        EdPanel_Place(place);
        EdUI_SplitterH(EdUI_Id("split.place"), bar, &S->placeH, 120.0f, hi);
        EdPanel_Content(left);
    }

    // --- right dock: Outliner over Details ---------------------------------
    {
        const float total = right.h - EDM_SPLITTER;
        float hi = total - 160.0f;
        if (hi < 120.0f) hi = 120.0f;
        if (S->outlinerH > hi)     S->outlinerH = hi;
        if (S->outlinerH < 120.0f) S->outlinerH = 120.0f;

        EdRect out = EdR_Cut(&right, S->outlinerH, ED_SIDE_TOP);
        EdRect bar = EdR_Cut(&right, EDM_SPLITTER, ED_SIDE_TOP);
        EdPanel_Outliner(out);
        EdUI_SplitterH(EdUI_Id("split.outliner"), bar, &S->outlinerH, 120.0f, hi);
        EdPanel_Details(right);
    }

    EdUI_SplitterV(EdUI_Id("split.left"), leftSplit, &S->leftW, 150.0f, maxDock);
    // The right dock grows leftwards, so the splitter's drag has to be
    // inverted: dragging it left must make the dock wider, not narrower.
    {
        float mirrored = -S->rightW;
        if (EdUI_SplitterV(EdUI_Id("split.right"), rightSplit, &mirrored,
                           -maxDock, -180.0f)) {
            S->rightW = -mirrored;
        }
    }
}
