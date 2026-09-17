// PanelBars.cpp - the menu bar, the toolbar and the status bar.
//
// CUSTOM (port-only).
//
// The three strips that frame the window. They are together in one file
// because they are the same kind of thing - a row of commands - and because
// every one of those commands is a call into EditorActions, so there is
// nothing here but layout and labels.
#include "../EditorShell.h"
#include "../EditorState.h"
#include "../EditorContent.h"
#include "../../RaidLevel.h"
#include "../../../Globals.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------
void EdPanel_MenuBar(EdRect r)
{
    EditorShell* S = &g_edShell;
    EdUI_MenuBarBegin(r);

    if (EdUI_MenuBegin("File")) {
        if (EdUI_MenuItem("Save Level", "Ctrl+S", EDUI_ICON_SAVE, 1)) EdAct_Save();
        if (EdUI_MenuItem("Reload Level", "F7", EDUI_ICON_UNDO, 1)) EdAct_Reload();
        EdUI_MenuSeparator();
        if (EdUI_MenuItem("Close Editor", "F2", EDUI_ICON_CLOSE, 1)) Editor_Toggle();
        EdUI_MenuEnd();
    }
    if (EdUI_MenuBegin("Edit")) {
        if (EdUI_MenuItem("Duplicate", "Ctrl+D", EDUI_ICON_PLUS,
                          EditorSelect_Valid())) EdAct_Duplicate();
        if (EdUI_MenuItem("Delete", "Del", EDUI_ICON_TRASH,
                          EditorSelect_Valid())) EdAct_Delete();
        EdUI_MenuSeparator();
        if (EdUI_MenuItem("Select Mode", "Q", EDUI_ICON_SELECT, 1))
            g_edGizmoMode = ED_GIZMO_MOVE;
        if (EdUI_MenuItem("Move Mode", "1", EDUI_ICON_MOVE, 1))
            g_edGizmoMode = ED_GIZMO_MOVE;
        if (EdUI_MenuItem("Rotate Mode", "2", EDUI_ICON_ROTATE, 1))
            g_edGizmoMode = ED_GIZMO_ROTATE;
        EdUI_MenuEnd();
    }
    if (EdUI_MenuBegin("Place")) {
        if (EdUI_MenuItem("Wall / Box", NULL, EDUI_ICON_BOX, 1)) EdAct_AddBox();
        if (EdUI_MenuItem("Item Pickup", NULL, EDUI_ICON_ITEM, 1))
            EdAct_AddItem(S->contentPick);
        if (EdUI_MenuItem("Enemy", NULL, EDUI_ICON_ENEMY, 1))
            EdAct_AddEnemy(S->contentTab == 1 ? S->contentPick : 0);
        if (EdUI_MenuItem("Light", NULL, EDUI_ICON_LIGHT,
                          g_raidLevel.nlight < RAID_MAX_LIGHT)) EdAct_AddLight();
        if (EdUI_MenuItem("Camera", NULL, EDUI_ICON_CAMERA,
                          g_raidLevel.ncam < RAID_MAX_CAM)) EdAct_AddCamera();
        if (EdUI_MenuItem("Camera Zone", NULL, EDUI_ICON_ZONE, 1)) EdAct_AddZone();
        EdUI_MenuEnd();
    }
    if (EdUI_MenuBegin("View")) {
        EdUI_MenuCheck("Grid", "G", &S->showGrid);
        EdUI_MenuCheck("Floor Plane", NULL, &S->showFloor);
        EdUI_MenuCheck("Light Markers", NULL, &S->showLights);
        EdUI_MenuCheck("Camera Zones", NULL, &S->showZones);
        EdUI_MenuCheck("Statistics", NULL, &S->showStats);
        EdUI_MenuSeparator();
        EdUI_MenuCheck("Immersive Mode", "F11", &S->immersive);
        if (EdUI_MenuItem("Frame All", "Home", EDUI_ICON_TARGET, 1))
            EdAct_FrameAll();
        if (EdUI_MenuItem("Frame Selection", "F", EDUI_ICON_TARGET,
                          EditorSelect_Valid())) EdAct_Focus();
        EdUI_MenuEnd();
    }
    if (EdUI_MenuBegin("Play")) {
        if (EdUI_MenuItem("Play In Viewport", "F5", EDUI_ICON_PLAY,
                          !EdAct_Playing())) EdAct_Play();
        if (EdUI_MenuItem("Stop", "Esc", EDUI_ICON_STOP, EdAct_Playing()))
            EdAct_Stop();
        EdUI_MenuEnd();
    }
    EdUI_MenuBarEnd();

    // The name, right-aligned on the bar, where an application puts it.
    EdUI_TextIn(EDUI_FONT_SMALL, "RE1 EDITOR",
                EdR(r.x, r.y, r.w - EDM_PAD, r.h), ED_ALIGN_RIGHT,
                EDC_TEXT_FAINT);
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
static void toolbar_sep(EdRect* row)
{
    EdR_Cut(row, 5.0f, ED_SIDE_LEFT);
    const EdRect s = EdR_Cut(row, 1.0f, ED_SIDE_LEFT);
    EdUI_Fill(EdR(s.x, s.y + 8.0f, 1.0f, s.h - 16.0f), EDC_SEP);
    EdR_Cut(row, 5.0f, ED_SIDE_LEFT);
}

static int toolbar_button(EdRect* row, float w, const char* label, int icon,
                          int flags, const char* tip)
{
    const EdRect r = EdR_Inset(EdR_Cut(row, w, ED_SIDE_LEFT), 5.0f);
    const EdId id = EdUI_Id(tip);
    const int hit = EdUI_Button(id, r, label, icon, flags);
    if (EdUI_IsHot(id) && tip) EdUI_Tooltip(tip);
    EdR_Cut(row, 2.0f, ED_SIDE_LEFT);
    return hit;
}

// The groups, left to right. Each cuts what it uses off the front of `row`
// and leaves the rest for the next one.
static void toolbar_file(EdRect* row)
{
    if (toolbar_button(row, 86.0f, "Save", EDUI_ICON_SAVE, 0,
                       "Write the level back to raid1.lvl  (Ctrl+S)"))
        EdAct_Save();
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_UNDO, ED_BTN_ICON_ONLY,
                       "Reload the level from disk  (F7)"))
        EdAct_Reload();
}

static void toolbar_play(EdRect* row)
{
    const int playing = EdAct_Playing();
    if (!playing) {
        if (toolbar_button(row, 92.0f, "Play", EDUI_ICON_PLAY, ED_BTN_ACCENT,
                           "Run the level in this viewport  (F5)"))
            EdAct_Play();
    } else {
        if (toolbar_button(row, 92.0f, "Stop", EDUI_ICON_STOP, ED_BTN_ACCENT,
                           "Stop and go back to editing  (Esc)"))
            EdAct_Stop();
    }
}

static void toolbar_gizmo(EdRect* row)
{
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_MOVE,
                       ED_BTN_ICON_ONLY | ED_BTN_IF_ON(g_edGizmoMode == ED_GIZMO_MOVE),
                       "Move  (1)"))
        g_edGizmoMode = ED_GIZMO_MOVE;
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_ROTATE,
                       ED_BTN_ICON_ONLY | ED_BTN_IF_ON(g_edGizmoMode == ED_GIZMO_ROTATE),
                       "Rotate  (2)"))
        g_edGizmoMode = ED_GIZMO_ROTATE;
}

// Snapping. The value is the step, and 0 is off, so one control says both
// whether it snaps and by how much.
static void toolbar_snap(EdRect* row)
{
    EditorShell* S = &g_edShell;
    EdUI_TextIn(EDUI_FONT_SMALL, "SNAP",
                EdR_Cut(row, 38.0f, ED_SIDE_LEFT), ED_ALIGN_CENTRE,
                EDC_TEXT_FAINT);
    {
        const EdRect box = EdR_Inset(EdR_Cut(row, 76.0f, ED_SIDE_LEFT), 10.0f);
        EdUI_DragInt(EdUI_Id("tool.snapmove"), box, &S->snapMove, 5.0f,
                     0, 2000, "");
        if (EdUI_IsHot(EdUI_Id("tool.snapmove")))
            EdUI_Tooltip("Grid step for moving, in world units. 0 is off.");
    }
    EdR_Cut(row, 4.0f, ED_SIDE_LEFT);
    EdUI_TextIn(EDUI_FONT_SMALL, "ANGLE",
                EdR_Cut(row, 42.0f, ED_SIDE_LEFT), ED_ALIGN_CENTRE,
                EDC_TEXT_FAINT);
    {
        const EdRect box = EdR_Inset(EdR_Cut(row, 92.0f, ED_SIDE_LEFT), 10.0f);
        int deg = (S->snapAngle * 360) / 4096;
        if (EdUI_DragInt(EdUI_Id("tool.snapangle"), box, &deg, 0.5f, 0, 180, " deg"))
            S->snapAngle = (deg * 4096) / 360;
        if (EdUI_IsHot(EdUI_Id("tool.snapangle")))
            EdUI_Tooltip("Rotation step in degrees. 0 is off.");
    }
}

static void toolbar_view(EdRect* row)
{
    EditorShell* S = &g_edShell;
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_GRID,
                       ED_BTN_ICON_ONLY | ED_BTN_IF_ON(S->showGrid),
                       "Show the ground grid"))
        S->showGrid = !S->showGrid;
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_LIGHT,
                       ED_BTN_ICON_ONLY | ED_BTN_IF_ON(S->showLights),
                       "Show light markers"))
        S->showLights = !S->showLights;
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_ZONE,
                       ED_BTN_ICON_ONLY | ED_BTN_IF_ON(S->showZones),
                       "Show camera zones"))
        S->showZones = !S->showZones;
    if (toolbar_button(row, 38.0f, NULL, EDUI_ICON_STATS,
                       ED_BTN_ICON_ONLY | ED_BTN_IF_ON(S->showStats),
                       "Show the statistics overlay"))
        S->showStats = !S->showStats;
}

// The right-hand end: the camera speed, where Unreal keeps it. Cut from the
// bar's right edge, not from what the left-hand groups left over.
static void toolbar_camera(EdRect r)
{
    EdRect right = r;
    EdR_Cut(&right, EDM_GAP, ED_SIDE_RIGHT);
    EdRect box = EdR_Inset(EdR_Cut(&right, 132.0f, ED_SIDE_RIGHT), 9.0f);
    EdUI_SliderFloat(EdUI_Id("tool.speed"), box, &g_edCam.speed,
                     20.0f, 900.0f, "%.0f");
    if (EdUI_IsHot(EdUI_Id("tool.speed")))
        EdUI_Tooltip("Fly speed. The wheel does this too while looking.");
    EdUI_TextIn(EDUI_FONT_SMALL, "CAMERA",
                EdR_Cut(&right, 56.0f, ED_SIDE_RIGHT), ED_ALIGN_CENTRE,
                EDC_TEXT_FAINT);
    EdUI_Icon(EdR_Cut(&right, 24.0f, ED_SIDE_RIGHT), EDUI_ICON_SPEED,
              EDC_TEXT_FAINT);
}

void EdPanel_Toolbar(EdRect r)
{
    EdUI_Fill(r, EDC_TOOLBAR);
    EdUI_Fill(EdR(r.x, r.y + r.h - 1.0f, r.w, 1.0f), EDC_BORDER);

    EdRect row = r;
    EdR_Cut(&row, EDM_GAP, ED_SIDE_LEFT);

    toolbar_file(&row);
    toolbar_sep(&row);
    toolbar_play(&row);
    toolbar_sep(&row);
    toolbar_gizmo(&row);
    toolbar_sep(&row);
    toolbar_snap(&row);
    toolbar_sep(&row);
    toolbar_view(&row);

    toolbar_camera(r);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
void EdPanel_StatusBar(EdRect r)
{
    EdUI_Fill(r, EDC_STATUS);
    EdUI_Fill(EdR(r.x, r.y, r.w, 1.0f), EDC_BORDER);

    EdRect row = r;
    EdR_Cut(&row, EDM_PAD, ED_SIDE_LEFT);

    // What is selected, first, because it is what a status bar is for.
    const char* sel = EditorSelect_Name();
    EdUI_Icon(EdR_Cut(&row, 16.0f, ED_SIDE_LEFT), EDUI_ICON_SELECT, EDC_TEXT_FAINT);
    EdR_Cut(&row, 4.0f, ED_SIDE_LEFT);
    EdUI_TextIn(EDUI_FONT_SMALL, sel,
                EdR_Cut(&row, EdUI_TextW(EDUI_FONT_SMALL, sel) + 12.0f,
                        ED_SIDE_LEFT), ED_ALIGN_LEFT, EDC_TEXT_DIM);

    // What the level is made of.
    char counts[96];
    snprintf(counts, sizeof(counts),
             "%d boxes   %d items   %d enemies   %d lights   %d cameras",
             g_raidLevel.nbox, g_raidLevel.nitem, g_raidLevel.nenemy,
             g_raidLevel.nlight, g_raidLevel.ncam);
    EdUI_TextIn(EDUI_FONT_SMALL, counts, r, ED_ALIGN_CENTRE, EDC_TEXT_FAINT);

    // What the last save did, and where it went.
    const char* st = EditorSave_Status();
    if (st && st[0]) {
        EdUI_TextIn(EDUI_FONT_SMALL, st,
                    EdR(r.x, r.y, r.w - EDM_PAD, r.h), ED_ALIGN_RIGHT,
                    EDC_TEXT_DIM);
    } else {
        EdUI_TextIn(EDUI_FONT_SMALL, "Data\\raid1.lvl",
                    EdR(r.x, r.y, r.w - EDM_PAD, r.h), ED_ALIGN_RIGHT,
                    EDC_TEXT_FAINT);
    }
}
