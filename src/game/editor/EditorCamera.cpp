// EditorCamera.cpp - flying around the real room.
//
// CUSTOM.
//
// THE TRICK, AND IT IS THE WHOLE REASON THIS WORKS
//
// The editor does not add a camera. It writes its own eye into the RDT camera
// record the room is already using and calls Room_SetupCamera(), which is the
// same two-line function the game calls when it switches camera angles:
//
//     set_scene_render_param(cameras[g_roomCameraId].fov);
//     MatrixToCamera((MATRIX*)&cameras[g_roomCameraId].cam_from_x);
//
// Everything downstream reads that one record or the g_RoomCameraData matrix it
// produces - the arena geometry, the entity models, the collision overlay, the
// lighting's camera-space rotation. So all of them follow a moving eye without
// knowing an editor exists, and what you fly around is the room the game draws,
// not a second drawing of it.
//
// The level's own camera is put back on the way out by re-applying the level,
// which is cheaper and more honest than caching a copy that a reload could
// silently invalidate.
//
// Controls follow Unreal's perspective viewport, because that is what hands
// already know. Q and E go up and down in WORLD space - not along the camera's
// own up vector, which walks sideways the moment the view is tilted.
#include "EditorState.h"
#include "../../Globals.h"
#include "../Types.h"
#include "../RaidLevel.h"
#include "../../marni/MarniSystem.h"
#include "../SpriteRenderer.h"      // g_SubpixelOffsetX/Y - the projection centre
#include "../../platform/platform.h"
#include <cmath>

EditorCamera g_edCam;
EditorView   g_edView;

#define ED_PITCH_LIMIT   1.45f      // a straight-down view has no right vector
#define ED_MIN_DIST      120.0f
#define ED_MAX_DIST    120000.0f

static int ed_key(int vk) { return (plat_key_state(vk) & 0x8000) != 0; }

void EditorCamera_Eye(float* x, float* y, float* z)
{
    const float ch = cosf(g_edCam.pitch), sh = sinf(g_edCam.pitch);
    *x = g_edCam.tx - cosf(g_edCam.yaw) * ch * g_edCam.dist;
    *y = g_edCam.ty - sh * g_edCam.dist;       // Y is negative upwards
    *z = g_edCam.tz - sinf(g_edCam.yaw) * ch * g_edCam.dist;
}

// Put the selection in the middle of the frame, from wherever the camera
// happens to be looking - the angles are kept, only the pivot and the distance
// move, because "F" is a request to look at something and not to be moved to a
// particular side of it.
void EditorCamera_FrameSelection(void)
{
    if (!EditorSelect_Valid() && g_edSel.kind != ED_SPAWN) {
        EditorCamera_FrameLevel();
        return;
    }

    float x0, y0, z0, x1, y1, z1;
    EditorSelect_Bounds(&x0, &y0, &z0, &x1, &y1, &z1);

    g_edCam.tx = (x0 + x1) * 0.5f;
    g_edCam.ty = (y0 + y1) * 0.5f;
    g_edCam.tz = (z0 + z1) * 0.5f;

    const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    float span = sqrtf(dx * dx + dy * dy + dz * dz);
    if (span < 400.0f) span = 400.0f;      // a pickup is a few hundred units
    g_edCam.dist = span * 2.2f;
    if (g_edCam.dist < 900.0f) g_edCam.dist = 900.0f;
}

void EditorCamera_FrameLevel(void)
{
    float x0 = 0.0f, x1 = 12000.0f, z0 = 0.0f, z1 = 12000.0f;
    if (g_raidLevel.loaded && g_raidLevel.nbox > 0) {
        x0 = 32767.0f; x1 = -32767.0f; z0 = 32767.0f; z1 = -32767.0f;
        for (int i = 0; i < g_raidLevel.nbox; i++) {
            const RaidBox* B = &g_raidLevel.box[i];
            if ((float)B->x0 < x0) x0 = (float)B->x0;
            if ((float)B->x1 > x1) x1 = (float)B->x1;
            if ((float)B->z0 < z0) z0 = (float)B->z0;
            if ((float)B->z1 > z1) z1 = (float)B->z1;
        }
    }
    g_edCam.tx = (x0 + x1) * 0.5f;
    g_edCam.tz = (z0 + z1) * 0.5f;
    g_edCam.ty = -900.0f;
    const float dx = x1 - x0, dz = z1 - z0;
    g_edCam.dist = sqrtf(dx*dx + dz*dz) * 0.85f;
    if (g_edCam.dist < 3000.0f) g_edCam.dist = 3000.0f;
    g_edCam.yaw   = 0.9f;
    g_edCam.pitch = 0.58f;
    if (g_edCam.fov <= 0)   g_edCam.fov = 207;
    if (g_edCam.speed <= 0) g_edCam.speed = 220.0f;
}

// ---------------------------------------------------------------------------
// One frame of camera input.
//
// LMB on empty space orbits, RMB looks around from where you are, MMB and
// Shift+LMB pan, the wheel dollies, WASD flies and Q/E go up and down. RMB plus
// the wheel sets the fly speed, which is where Unreal puts it and the right
// place for it: you find out you are too slow while you are already flying.
// ---------------------------------------------------------------------------
void EditorCamera_Update(void)
{
    const int shift = ed_key(VK_SHIFT);
    const int rmb   = g_edMouse.held[ED_MB_RIGHT];

    // ---- wheel: the fly speed while looking, the distance otherwise --------
    if (g_edMouse.wheel != 0) {
        const float notches = (float)g_edMouse.wheel / 120.0f;
        if (rmb) {
            g_edCam.speed *= powf(1.25f, notches);
            if (g_edCam.speed < 15.0f)   g_edCam.speed = 15.0f;
            if (g_edCam.speed > 4000.0f) g_edCam.speed = 4000.0f;
        } else {
            g_edCam.dist *= powf(0.88f, notches);
            if (g_edCam.dist < ED_MIN_DIST) g_edCam.dist = ED_MIN_DIST;
            if (g_edCam.dist > ED_MAX_DIST) g_edCam.dist = ED_MAX_DIST;
        }
    }

    // ---- turning ------------------------------------------------------------
    const float turn = 0.012f;
    if (rmb || (g_edMouse.held[ED_MB_LEFT] && !shift &&
                !g_edMouse.held[ED_MB_MIDDLE])) {
        // Looking (RMB) and orbiting (LMB) turn the same way vertically and
        // opposite ways horizontally, which is what the two gestures feel like
        // rather than what they are: one swings the eye, the other the head.
        const float yawSign = rmb ? -1.0f : 1.0f;
        float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
        if (rmb) EditorCamera_Eye(&eyeX, &eyeY, &eyeZ);

        g_edCam.yaw   += yawSign * g_edMouse.dx * turn;
        g_edCam.pitch += g_edMouse.dy * turn;
        if (g_edCam.pitch >  ED_PITCH_LIMIT) g_edCam.pitch =  ED_PITCH_LIMIT;
        if (g_edCam.pitch < -ED_PITCH_LIMIT) g_edCam.pitch = -ED_PITCH_LIMIT;

        if (rmb) {
            // Keep the EYE still and swing the pivot: that is what turning your
            // head is, as opposed to walking around the room.
            const float ch = cosf(g_edCam.pitch), sh = sinf(g_edCam.pitch);
            g_edCam.tx = eyeX + cosf(g_edCam.yaw) * ch * g_edCam.dist;
            g_edCam.ty = eyeY + sh * g_edCam.dist;
            g_edCam.tz = eyeZ + sinf(g_edCam.yaw) * ch * g_edCam.dist;
        }
    }

    // ---- panning ------------------------------------------------------------
    if (g_edMouse.held[ED_MB_MIDDLE] ||
        (g_edMouse.held[ED_MB_LEFT] && shift)) {
        if (g_edView.ok) {
            // A game pixel is worth dist/f world units, so panning tracks the
            // cursor at any distance instead of crawling when you are far out.
            const float k = g_edCam.dist / g_edView.f;
            const float mx = -g_edMouse.dx * k, my = -g_edMouse.dy * k;
            g_edCam.tx += g_edView.r[0]*mx + g_edView.u[0]*my;
            g_edCam.ty += g_edView.r[1]*mx + g_edView.u[1]*my;
            g_edCam.tz += g_edView.r[2]*mx + g_edView.u[2]*my;
        }
    }

    // ---- flying -------------------------------------------------------------
    if (g_edView.ok) {
        const float sp = g_edCam.speed * (shift ? 3.0f : 1.0f);
        float f = 0.0f, r = 0.0f, up = 0.0f;
        if (ed_key('W')) f += sp;
        if (ed_key('S')) f -= sp;
        if (ed_key('D')) r += sp;
        if (ed_key('A')) r -= sp;
        if (ed_key('E')) up += sp;
        if (ed_key('Q')) up -= sp;
        if (f != 0.0f || r != 0.0f || up != 0.0f) {
            // g_edView.r is horizontal by construction, so strafing never
            // drifts in Y; up and down are world-space, as Unreal's are.
            g_edCam.tx += g_edView.n[0]*f + g_edView.r[0]*r;
            g_edCam.ty += g_edView.n[1]*f - up;
            g_edCam.tz += g_edView.n[2]*f + g_edView.r[2]*r;
        }
    }
}

// ---------------------------------------------------------------------------
// Push the editor's eye into the room's own camera record.
// ---------------------------------------------------------------------------
void EditorCamera_Apply(void)
{
    if (g_RdtPointer == NULL) return;
    if ((unsigned int)g_roomCameraId >= (unsigned int)g_RdtPointer->cameras_count) return;

    RDT_Camera* cams = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    RDT_Camera* C = &cams[g_roomCameraId];

    float ex, ey, ez;
    EditorCamera_Eye(&ex, &ey, &ez);

    C->cam_from_x = (int)ex;
    C->cam_from_y = (int)ey;
    C->cam_from_z = (int)ez;
    C->cam_to_x   = (int)g_edCam.tx;
    C->cam_to_y   = (int)g_edCam.ty;
    C->cam_to_z   = (int)g_edCam.tz;
    C->roll       = 0;
    C->fov        = g_edCam.fov;

    Room_SetupCamera();
}

void EditorCamera_Restore(void)
{
    // Re-applying the level rewrites every camera slot from the file, which is
    // both the restore and a guarantee that a reload while editing cannot leave
    // a stale copy behind.
    if (g_raidLevel.loaded) {
        RaidLevel_Apply();
        Room_SetupCamera();
    }
}

// ---------------------------------------------------------------------------
// The view, rebuilt from the camera record the room is drawn with. Same
// construction as RaidArena and CollisionDebug, and for the same stated reason:
// g_RoomCameraData's rows live in a Y-flipped frame and its translation mixes
// conventions, so deriving the basis here is both simpler and checkable.
// ---------------------------------------------------------------------------
void EditorView_Build(void)
{
    g_edView.ok = 0;
    if (g_RdtPointer == NULL) return;
    if ((unsigned int)g_roomCameraId >= (unsigned int)g_RdtPointer->cameras_count) return;

    RDT_Camera* cams = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    RDT_Camera* C = &cams[g_roomCameraId];

    const float dx = (float)(C->cam_to_x - C->cam_from_x);
    const float dy = (float)(C->cam_to_y - C->cam_from_y);
    const float dz = (float)(C->cam_to_z - C->cam_from_z);
    const float L = sqrtf(dx*dx + dy*dy + dz*dz);
    const float h = sqrtf(dx*dx + dz*dz);
    if (L < 1.0f || h < 1.0f) return;

    g_edView.fromX = (float)C->cam_from_x;
    g_edView.fromY = (float)C->cam_from_y;
    g_edView.fromZ = (float)C->cam_from_z;

    g_edView.n[0] = dx / L;  g_edView.n[1] = dy / L;  g_edView.n[2] = dz / L;
    g_edView.r[0] = dz / h;  g_edView.r[1] = 0.0f;    g_edView.r[2] = -dx / h;
    g_edView.u[0] = g_edView.n[1]*g_edView.r[2] - g_edView.n[2]*g_edView.r[1];
    g_edView.u[1] = g_edView.n[2]*g_edView.r[0] - g_edView.n[0]*g_edView.r[2];
    g_edView.u[2] = g_edView.n[0]*g_edView.r[1] - g_edView.n[1]*g_edView.r[0];

    // Everything here works in GAME space (320x240), not backbuffer pixels, so
    // the mouse and the projection meet in the same units.
    g_edView.f  = (float)C->fov;
    g_edView.cx = (float)g_SubpixelOffsetX;
    g_edView.cy = (float)g_SubpixelOffsetY;
    g_edView.ok = 1;
}

int EditorView_Project(float wx, float wy, float wz, float* sx, float* sy)
{
    if (!g_edView.ok) return 0;
    const float dx = wx - g_edView.fromX;
    const float dy = wy - g_edView.fromY;
    const float dz = wz - g_edView.fromZ;
    const float vz = g_edView.n[0]*dx + g_edView.n[1]*dy + g_edView.n[2]*dz;
    if (vz < 96.0f) return 0;                    // the pass's own near plane
    const float iz = g_edView.f / vz;
    *sx = g_edView.cx + (g_edView.r[0]*dx + g_edView.r[1]*dy + g_edView.r[2]*dz) * iz;
    // u points down in PS1 coordinates and screen Y grows down, so this adds.
    *sy = g_edView.cy + (g_edView.u[0]*dx + g_edView.u[1]*dy + g_edView.u[2]*dz) * iz;
    return 1;
}

// The ray under a game-space point. Inverting the projection: a screen offset
// of (a, b) focal lengths is that many units of right and up per unit forward.
void EditorView_Ray(float gameX, float gameY, float* ox, float* oy, float* oz,
                    float* dx, float* dy, float* dz)
{
    *ox = g_edView.fromX; *oy = g_edView.fromY; *oz = g_edView.fromZ;
    if (!g_edView.ok || g_edView.f == 0.0f) { *dx = 0; *dy = 0; *dz = 1; return; }
    const float a = (gameX - g_edView.cx) / g_edView.f;
    const float b = (gameY - g_edView.cy) / g_edView.f;
    *dx = g_edView.n[0] + g_edView.r[0]*a + g_edView.u[0]*b;
    *dy = g_edView.n[1] + g_edView.r[1]*a + g_edView.u[1]*b;
    *dz = g_edView.n[2] + g_edView.r[2]*a + g_edView.u[2]*b;
}
