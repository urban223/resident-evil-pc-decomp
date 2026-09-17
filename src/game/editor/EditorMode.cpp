// EditorMode.cpp - the mode: what is frozen, where the renderer points, and
// what Play means.
//
// CUSTOM (port-only).
//
// THREE STATES, ONE PROCESS
//
//   off    the game, filling the window. Nothing here runs.
//   edit   the interface is up, the simulation is frozen, the free camera has
//          the room, and the renderer is pointed at the viewport rectangle.
//   play   the interface is still up and the renderer is still pointed at the
//          same rectangle - but the simulation is running, the level's own
//          camera is back, and what is in that rectangle is the game.
//
// Play is not a second program and not a second window. It is this one, with
// the freeze lifted: the room, the models, the lighting and the loaded assets
// are the ones that were already there, and Stop puts the free camera back
// without reloading anything. That is what makes "what you see is what runs"
// true here rather than a claim.
//
// WHERE THE RENDERER POINTS
//
// One call, once a frame, before the scene is drawn: MarniSetViewport with the
// rectangle the interface laid out. Everything downstream - the arena
// geometry, the character models, the sprite queue, the HUD - lands inside it
// without knowing an editor exists, because the mapping is applied in the one
// place screen coordinates become clip space.
#include "EditorState.h"
#include "EditorShell.h"
#include "EditorContent.h"
#include "ui/EditorUI.h"
#include "../../Globals.h"
#include "../Types.h"
#include "../Entities.h"
#include "../RaidLevel.h"
#include "../RaidEnemies.h"
#include "../RaidItemModels.h"
#include "../../marni/MarniSystem.h"
#include "../../platform/platform.h"

#include <cmath>
#include <cstring>

#define ED_MODE_OFF   0
#define ED_MODE_EDIT  1
#define ED_MODE_PLAY  2

static int s_mode = ED_MODE_OFF;
static int s_viewportApplied = 0;

int Editor_IsOpen(void)   { return s_mode == ED_MODE_EDIT; }
int Editor_Active(void)   { return s_mode != ED_MODE_OFF; }
int EdAct_Playing(void)   { return s_mode == ED_MODE_PLAY; }

// ---------------------------------------------------------------------------
// Standing the player on the spawn. Both the matrix translation and the
// position field, because the matrix is what the renderer uses and the position
// is what the collision rolls back to - the same pairing Raid_EnterRoom does,
// and for the same reason.
// ---------------------------------------------------------------------------
static void Editor_PlacePlayerAtSpawn(void)
{
    if (!g_raidLevel.loaded) return;
    const int x = g_raidLevel.spawnX;
    const int z = g_raidLevel.spawnZ;

    g_playerEntity.scaMatrixData.localMatrix.t[0] = x;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = z;
    g_playerEntity.position.x = (short)x;
    g_playerEntity.position.y = 0;
    g_playerEntity.position.z = (short)z;
    g_playerEntity.posY = 0;
    g_playerEntity.directionAngle = (short)g_raidLevel.spawnAngle;
}

// ---------------------------------------------------------------------------
// Play / Stop
// ---------------------------------------------------------------------------
void EdAct_Play(void)
{
    if (s_mode != ED_MODE_EDIT) return;

    // Everything the level says, pushed into the engine as if the room had
    // just been entered - but with no file read and no asset reload, because
    // the level in memory IS the one being edited.
    RaidLevel_Apply();
    Editor_PlacePlayerAtSpawn();

    RaidItems_Reset();
    RaidItems_Give();
    RaidItemModels_Sync();

    g_enemy_count = 0;
    for (int i = 0; i < 30; i++) g_EnemiesList[i].status_flags = 0;
    RaidEnemies_Spawn();

    EditorCamera_Restore();     // the level's camera, not the free one
    check_camera_switch(1);     // and the cut that reinstalls its matrix

    EditorSelect_Clear();
    s_mode = ED_MODE_PLAY;
}

void EdAct_Stop(void)
{
    if (s_mode != ED_MODE_PLAY) return;

    RaidEnemies_Clear();
    g_enemy_count = 0;
    for (int i = 0; i < 30; i++) g_EnemiesList[i].status_flags = 0;

    RaidItems_Reset();
    Editor_PlacePlayerAtSpawn();

    s_mode = ED_MODE_EDIT;
    EditorView_Build();
    EditorCamera_Apply();       // the free camera takes the room back
}

// ---------------------------------------------------------------------------
// Entering and leaving
// ---------------------------------------------------------------------------
void Editor_Toggle(void)
{
    // Only in the arena. There is no reason to fly around a story room with a
    // tool that can only write a RAID level, and a story room's camera zones
    // would fight the free camera every step.
    if (g_raidMode == 0) return;

    if (s_mode == ED_MODE_OFF) {
        s_mode = ED_MODE_EDIT;
        EditorShell_Init();
        EditorCamera_FrameLevel();
        EditorView_Build();
        EditorSelect_Clear();
        plat_cursor_show(TRUE);     // an interface you cannot point at is not one
    } else {
        if (s_mode == ED_MODE_PLAY) EdAct_Stop();
        s_mode = ED_MODE_OFF;
        EditorCamera_Restore();
        Editor_PlacePlayerAtSpawn();
        MarniResetViewport();       // the game gets the whole window back
        s_viewportApplied = 0;
        plat_cursor_show(FALSE);
    }
}

// ---------------------------------------------------------------------------
// Keyboard commands.
//
// plat_key_state reports "is down"; a command wants "just went down", and the
// editor cannot use the game's own edge machinery because that is exactly what
// it blanks while it is up. A field with the caret has the keyboard, so none of
// this fires while a number is being typed.
// ---------------------------------------------------------------------------
#define ED_KEYS 16
static const int s_watch[ED_KEYS] = {
    'S', 'D', '1', '2', 'C', 'G', 'F', VK_DELETE, VK_BACK, VK_HOME,
    VK_ESCAPE, VK_F5, VK_F11, 0, 0, 0
};
static unsigned char s_wasDown[ED_KEYS];

static int ed_pressed(int vk)
{
    for (int i = 0; i < ED_KEYS; i++) {
        if (s_watch[i] != vk) continue;
        return (plat_key_state(vk) & 0x8000) != 0 && !s_wasDown[i];
    }
    return 0;
}

static void ed_keys_latch(void)
{
    for (int i = 0; i < ED_KEYS; i++) {
        if (s_watch[i] == 0) continue;
        s_wasDown[i] = (unsigned char)((plat_key_state(s_watch[i]) & 0x8000) != 0);
    }
}

static void Editor_Commands(void)
{
    const int ctrl = (plat_key_state(VK_CONTROL) & 0x8000) != 0;

    if (EdUI_WantsKeyboard()) { ed_keys_latch(); return; }

    if (ed_pressed(VK_F11)) g_edShell.immersive = !g_edShell.immersive;

    if (s_mode == ED_MODE_PLAY) {
        if (ed_pressed(VK_ESCAPE)) EdAct_Stop();
        ed_keys_latch();
        return;
    }

    if (ctrl && ed_pressed('S')) EdAct_Save();
    if (ctrl && ed_pressed('D')) EdAct_Duplicate();
    if (ed_pressed(VK_F5))       EdAct_Play();
    if (ed_pressed('1'))         g_edGizmoMode = ED_GIZMO_MOVE;
    if (ed_pressed('2'))         g_edGizmoMode = ED_GIZMO_ROTATE;
    if (ed_pressed('C') && !ctrl) EdAct_CameraFromView();
    if (ed_pressed('F'))         EdAct_Focus();
    if (ed_pressed('G'))         g_edShell.showGrid = !g_edShell.showGrid;
    if (ed_pressed(VK_HOME))     EdAct_FrameAll();
    if (ed_pressed(VK_DELETE) || ed_pressed(VK_BACK)) EdAct_Delete();

    ed_keys_latch();
}

// ---------------------------------------------------------------------------
// Where the renderer points.
//
// The rectangle comes from the interface's last layout, which is a frame
// behind - see the note in PanelViewport.cpp about why that is the right
// trade. Before the first layout there is no rectangle, and the whole window
// is the honest answer.
// ---------------------------------------------------------------------------
static void Editor_ApplyViewport(void)
{
    DWORD bw = 0, bh = 0;
    MarniGetBackBufferSize(&bw, &bh);
    if (bw < 320 || bh < 240) return;

    const float k = EdUI_Scale();
    EdRect v = g_edShell.viewport;
    if (v.w <= 1.0f || v.h <= 1.0f || k <= 0.0f) {
        MarniResetViewport();
        s_viewportApplied = 1;
        return;
    }

    // Play shows the whole frame, letterboxed if it has to be: the HUD is part
    // of what is being tested and cropping it would be lying about the game.
    // Editing fills the rectangle instead - you are flying the camera, so
    // "off the top of the frame" is something you fix by looking elsewhere,
    // and bars around the viewport you are working in are lost screen.
    const int fit = (s_mode == ED_MODE_PLAY) ? MARNI_FIT_CONTAIN
                                             : MARNI_FIT_COVER;
    MarniSetViewport((int)(v.x * k), (int)(v.y * k),
                     (int)(v.w * k), (int)(v.h * k), fit);
    s_viewportApplied = 1;
}

// ---------------------------------------------------------------------------
// One frame, before the scene is drawn.
//
// The order matters: build the view from the camera record as it stands, let
// the input move the camera through that view (panning needs the basis), then
// write the result back and rebuild - so what is drawn this frame is where the
// mouse just put it, with no frame of lag.
// ---------------------------------------------------------------------------
void Editor_Tick(void)
{
    if (s_mode == ED_MODE_OFF) {
        if (s_viewportApplied) { MarniResetViewport(); s_viewportApplied = 0; }
        return;
    }

    EditorInput_BeginFrame();
    Editor_ApplyViewport();
    Editor_Commands();

    if (s_mode != ED_MODE_EDIT) return;

    EditorView_Build();

    // The interface gets the mouse first when the cursor is on it. That answer
    // is from the END of the previous frame, which is the only place it can
    // come from in an immediate-mode interface - and it is right, because the
    // panels have not moved since. A gizmo already being dragged overrides it:
    // a drag that started in the viewport must not be dropped by passing over
    // a panel.
    const int uiHasMouse = EdUI_WantsMouse() && !EditorGizmo_Busy();

    if (!uiHasMouse) {
        EditorGizmo_Update();
        if (!EditorGizmo_Busy()) {
            EditorCamera_Update();
            // A click that neither grabbed a handle nor dragged the view picks.
            if (g_edMouse.released[ED_MB_LEFT] &&
                fabsf(g_edMouse.dx) < 2.0f && fabsf(g_edMouse.dy) < 2.0f) {
                EditorSelect_PickAt(g_edMouse.gameX, g_edMouse.gameY);
            }
        }
    }

    EditorCamera_Apply();
    EditorView_Build();
}

// ---------------------------------------------------------------------------
// The overlay drawn WITH the scene, inside the viewport: the grid, the
// selection outline and the manipulator. Not the panels - those are drawn last,
// by Editor_DrawUI, over everything.
// ---------------------------------------------------------------------------
static void Editor_DrawGrid(void)
{
    if (!g_edShell.showGrid) return;

    // A grid sized to the camera: one that is always 1000 units is a fog of
    // lines from far away and two lines from close up.
    float step = 1000.0f;
    while (g_edCam.dist > step * 14.0f) step *= 2.0f;
    while (step > 500.0f && g_edCam.dist < step * 5.0f) step *= 0.5f;

    const int half = 10;
    const float cx = floorf(g_edCam.tx / step) * step;
    const float cz = floorf(g_edCam.tz / step) * step;
    const float ext = step * (float)half;

    for (int i = -half; i <= half; i++) {
        const float x = cx + i * step;
        const float z = cz + i * step;
        // The lines through the origin are the axes and are coloured as such,
        // which is the cheapest possible compass.
        const unsigned int cAxisX = (fabsf(z) < 0.5f) ? 0xFF7A2E2Bu : 0x30FFFFFFu;
        const unsigned int cAxisZ = (fabsf(x) < 0.5f) ? 0xFF2E4A7Au : 0x30FFFFFFu;
        EditorDraw_WorldLine(x, 0.0f, cz - ext, x, 0.0f, cz + ext, 1.0f, cAxisZ);
        EditorDraw_WorldLine(cx - ext, 0.0f, z, cx + ext, 0.0f, z, 1.0f, cAxisX);
    }
}

static void Editor_DrawAxisMarker(void)
{
    // A small set of world axes at the pivot, so "which way is X" is never a
    // question. Red X, green Y (up), blue Z - Unreal's letters-to-colours.
    const float L = g_edCam.dist * 0.06f;
    const float x = g_edCam.tx, y = g_edCam.ty, z = g_edCam.tz;
    EditorDraw_WorldLine(x, y, z, x + L, y, z, 1.5f, EDC_X);
    EditorDraw_WorldLine(x, y, z, x, y - L, z, 1.5f, EDC_Y);
    EditorDraw_WorldLine(x, y, z, x, y, z + L, 1.5f, EDC_Z);
}

static void Editor_DrawLightMarkers(void)
{
    if (!g_edShell.showLights || !g_raidLevel.loaded) return;
    for (int i = 0; i < g_raidLevel.nlight; i++) {
        const RaidLight* G = &g_raidLevel.light[i];
        const float x = (float)G->x, y = (float)G->y, z = (float)G->z;
        const unsigned int c = 0xFF000000u | ((unsigned)G->r << 16)
                             | ((unsigned)G->g << 8) | G->b;
        const float s = 180.0f;
        EditorDraw_WorldLine(x - s, y, z, x + s, y, z, 1.5f, c);
        EditorDraw_WorldLine(x, y - s, z, x, y + s, z, 1.5f, c);
        EditorDraw_WorldLine(x, y, z - s, x, y, z + s, 1.5f, c);
        // The radius, on the floor, because the falloff is XZ only and a
        // sphere would say something untrue about it.
        const float r = (float)G->radius;
        const int seg = 24;
        for (int k = 0; k < seg; k++) {
            const float a0 = (float)k * 6.2831853f / (float)seg;
            const float a1 = (float)(k + 1) * 6.2831853f / (float)seg;
            EditorDraw_WorldLine(x + cosf(a0) * r, 0.0f, z + sinf(a0) * r,
                                 x + cosf(a1) * r, 0.0f, z + sinf(a1) * r,
                                 1.0f, (c & 0x00FFFFFFu) | 0x40000000u);
        }
    }
}

static void Editor_DrawZones(void)
{
    if (!g_edShell.showZones || !g_raidLevel.loaded) return;
    for (int i = 0; i < g_raidLevel.nzone; i++) {
        const RaidZone* Z = &g_raidLevel.zone[i];
        const float x0 = (float)Z->x0, x1 = (float)Z->x1;
        const float z0 = (float)Z->z0, z1 = (float)Z->z1;
        const unsigned int c = 0x66E0B23Cu;
        EditorDraw_WorldLine(x0, 0.0f, z0, x1, 0.0f, z0, 1.0f, c);
        EditorDraw_WorldLine(x1, 0.0f, z0, x1, 0.0f, z1, 1.0f, c);
        EditorDraw_WorldLine(x1, 0.0f, z1, x0, 0.0f, z1, 1.0f, c);
        EditorDraw_WorldLine(x0, 0.0f, z1, x0, 0.0f, z0, 1.0f, c);
    }
}

void Editor_Draw(void)
{
    if (s_mode != ED_MODE_EDIT) return;

    Editor_DrawGrid();
    Editor_DrawZones();
    Editor_DrawLightMarkers();
    Editor_DrawAxisMarker();
    EditorSelect_Draw();
    EditorGizmo_Draw();
}

// ---------------------------------------------------------------------------
// The interface, last thing before the flip.
//
// The viewport transform is dropped first: the panels are laid out in window
// pixels, not in the room's. Everything the renderer drew before this point
// went through the transform and is inside the viewport rectangle; everything
// after it is the interface.
// ---------------------------------------------------------------------------
void Editor_DrawUI(void)
{
    if (s_mode == ED_MODE_OFF) return;

    MarniResetViewport();

    EdUI_BeginFrame();
    if (EdUI_Ready()) {
        EditorShell_Draw();
    }
    EdUI_EndFrame();

    // The mouse edges belong to the whole frame - the viewport tools consumed
    // them in Tick, the panels just now - so they are cleared here, at the end
    // of it, and not in the middle.
    EditorInput_EndFrame();
}
