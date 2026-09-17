// EditorGizmo.cpp - the manipulator.
//
// CUSTOM.
//
// Move and rotate, as far as the level format can honestly go. A box is an
// axis-aligned min/max pair, so it has no rotation to give and no rotate ring
// appears on one; a pickup is a point with an angle, so it has no size. A
// handle that quietly does nothing is worse than no handle.
//
// The handles are drawn with MarniDrawLine, which is immediate-mode and depth
// DISABLED, so they are always on top. That is the point: a manipulator you
// cannot see because the floor is in front of it is not a manipulator.
//
// Hit testing is in SCREEN space - distance from the cursor to the projected
// axis segment - because that is where the user is aiming. Dragging is in WORLD
// space, solving for the closest point between the mouse ray and the axis line,
// so the handle stays under the cursor at any angle and distance.
#include "EditorState.h"
#include "../../Globals.h"
#include "../Types.h"
#include "../RaidLevel.h"
#include "../../platform/platform.h"
#include <cmath>

int g_edGizmoMode = ED_GIZMO_MOVE;

// The axes, as Unreal colours them: X red, Y green, Z blue. Unreal's vertical
// is Z; here the format's vertical is Y, so green is the one that goes up.
#define ED_COL_X   0xFFE0564E
#define ED_COL_Y   0xFF7FD06A
#define ED_COL_Z   0xFF5D8FD6
#define ED_COL_HOT 0xFFFFFFFF

#define ED_AXIS_NONE  (-1)
#define ED_AXIS_X      0
#define ED_AXIS_Y      1
#define ED_AXIS_Z      2
#define ED_AXIS_RY     3      // the yaw ring

// Y is negative upwards, so the "up" axis points along -Y.
static const float ED_AXIS_VEC[3][3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f,-1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f },
};

struct EdDrag {
    int   axis;
    float t0;                 // where along the axis the grab started
    float a0;                 // or, for the ring, the angle it started at
    int   ox, oy, oz;         // the object's position when the drag began
    int   turn0;
};
static EdDrag s_drag = { ED_AXIS_NONE, 0, 0, 0, 0, 0, 0 };

int EditorGizmo_Busy(void) { return s_drag.axis != ED_AXIS_NONE; }

// Which handles this selection can actually use.
static int ed_has_axis(int axis)
{
    if (!EditorSelect_Valid()) return 0;
    const int k = g_edSel.kind;

    if (g_edGizmoMode == ED_GIZMO_ROTATE)
        return axis == ED_AXIS_RY &&
               (k == ED_ITEM || k == ED_ENEMY || k == ED_SPAWN || k == ED_CAM);

    if (axis == ED_AXIS_RY) return 0;
    if (axis == ED_AXIS_Y)  return k == ED_BOX || k == ED_LIGHT || k == ED_CAM;
    return 1;                         // X and Z move anything
}

// One screen size whatever the distance, the way a manipulator has to behave.
static float ed_gizmo_len(float x, float y, float z)
{
    const float dx = x - g_edView.fromX;
    const float dy = y - g_edView.fromY;
    const float dz = z - g_edView.fromZ;
    float d = g_edView.n[0]*dx + g_edView.n[1]*dy + g_edView.n[2]*dz;
    if (d < 96.0f) d = 96.0f;
    return 52.0f * d / g_edView.f;    // ~52 game pixels, which is ~104 at 640x480
}

// Where the mouse ray comes closest to the infinite line through `g` along `v`.
//
//   t = ((r.v)(d.d) - (r.d)(v.d)) / ((v.v)(d.d) - (v.d)^2),  r = rayOrigin - g
//
// Worth stating in full because the sign is easy to lose: an earlier draft of
// this in the browser editor negated it, and the handle ran away from the
// cursor in a way that looks like a scale problem rather than a sign one.
static int ed_ray_axis_t(const float* o, const float* d,
                         const float* g, const float* v, float* out)
{
    const float r[3] = { o[0]-g[0], o[1]-g[1], o[2]-g[2] };
    const float vv = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    const float vd = v[0]*d[0] + v[1]*d[1] + v[2]*d[2];
    const float dd = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
    const float rv = r[0]*v[0] + r[1]*v[1] + r[2]*v[2];
    const float rd = r[0]*d[0] + r[1]*d[1] + r[2]*d[2];
    const float den = vv*dd - vd*vd;
    if (fabsf(den) < 1.0e-9f) return 0;      // the ray runs along the axis
    *out = (rv*dd - rd*vd) / den;
    return 1;
}

// The ray's meeting with the horizontal plane at height `y`.
static int ed_ray_plane_y(const float* o, const float* d, float y,
                          float* outX, float* outZ)
{
    if (fabsf(d[1]) < 1.0e-6f) return 0;
    const float t = (y - o[1]) / d[1];
    if (t <= 0.0f) return 0;
    *outX = o[0] + d[0]*t;
    *outZ = o[2] + d[2]*t;
    return 1;
}

static float ed_seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx-ax, dy = by-ay;
    const float len2 = dx*dx + dy*dy;
    float t = (len2 > 0.0f) ? ((px-ax)*dx + (py-ay)*dy) / len2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return sqrtf((px - (ax+dx*t))*(px - (ax+dx*t)) + (py - (ay+dy*t))*(py - (ay+dy*t)));
}

// What the cursor is over, in game space.
static int ed_gizmo_pick(float mx, float my)
{
    float gx, gy, gz;
    EditorSelect_Origin(&gx, &gy, &gz);
    const float L = ed_gizmo_len(gx, gy, gz);

    float ox, oy;
    if (!EditorView_Project(gx, gy, gz, &ox, &oy)) return ED_AXIS_NONE;

    int best = ED_AXIS_NONE;
    float bestD = 7.0f;               // game pixels

    for (int a = 0; a < 3; a++) {
        if (!ed_has_axis(a)) continue;
        float tx, ty;
        if (!EditorView_Project(gx + ED_AXIS_VEC[a][0]*L,
                                gy + ED_AXIS_VEC[a][1]*L,
                                gz + ED_AXIS_VEC[a][2]*L, &tx, &ty)) continue;
        const float d = ed_seg_dist(mx, my, ox, oy, tx, ty);
        if (d < bestD) { bestD = d; best = a; }
    }

    if (ed_has_axis(ED_AXIS_RY)) {
        // The ring, as twelve chords - close enough to a circle for a hit test
        // and cheaper than one.
        float px = 0.0f, py = 0.0f;
        int havePrev = 0;
        for (int i = 0; i <= 12; i++) {
            const float th = (float)i / 12.0f * 6.2831853f;
            float sx, sy;
            if (!EditorView_Project(gx + cosf(th)*L*0.85f, gy,
                                    gz + sinf(th)*L*0.85f, &sx, &sy)) { havePrev = 0; continue; }
            if (havePrev) {
                const float d = ed_seg_dist(mx, my, px, py, sx, sy);
                if (d < bestD) { bestD = d; best = ED_AXIS_RY; }
            }
            px = sx; py = sy; havePrev = 1;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// One frame of the manipulator.
// ---------------------------------------------------------------------------
void EditorGizmo_Update(void)
{
    if (!g_edView.ok) { s_drag.axis = ED_AXIS_NONE; return; }

    // Every button up ends a drag, whatever the page missed.
    if (!g_edMouse.held[ED_MB_LEFT]) s_drag.axis = ED_AXIS_NONE;

    const int snap = (plat_key_state(VK_SHIFT) & 0x8000) ? 1 : 250;

    float o[3], d[3];
    EditorView_Ray(g_edMouse.gameX, g_edMouse.gameY,
                   &o[0], &o[1], &o[2], &d[0], &d[1], &d[2]);

    // ---- starting a drag ----------------------------------------------------
    if (g_edMouse.pressed[ED_MB_LEFT] && EditorSelect_Valid()) {
        const int axis = ed_gizmo_pick(g_edMouse.gameX, g_edMouse.gameY);
        if (axis != ED_AXIS_NONE) {
            float gx, gy, gz;
            EditorSelect_Origin(&gx, &gy, &gz);
            const float g[3] = { gx, gy, gz };
            s_drag.axis = ED_AXIS_NONE;

            if (axis == ED_AXIS_RY) {
                float hx, hz;
                if (ed_ray_plane_y(o, d, gy, &hx, &hz)) {
                    s_drag.axis = axis;
                    s_drag.a0 = atan2f(hz - gz, hx - gx);
                }
            } else {
                float t;
                if (ed_ray_axis_t(o, d, g, ED_AXIS_VEC[axis], &t)) {
                    s_drag.axis = axis;
                    s_drag.t0 = t;
                }
            }
            s_drag.ox = 0; s_drag.oy = 0; s_drag.oz = 0;
        }
    }

    if (s_drag.axis == ED_AXIS_NONE) return;

    // ---- carrying one on --------------------------------------------------
    float gx, gy, gz;
    EditorSelect_Origin(&gx, &gy, &gz);
    const float g[3] = { gx, gy, gz };

    if (s_drag.axis == ED_AXIS_RY) {
        float hx, hz;
        if (!ed_ray_plane_y(o, d, gy, &hx, &hz)) return;
        const float a = atan2f(hz - gz, hx - gx);
        float delta = (a - s_drag.a0) / 6.2831853f * 4096.0f;
        const int step = (snap == 1) ? 16 : 128;
        int q = (int)(delta / (float)step);
        if (q != 0) {
            EditorSelect_Turn(q * step);
            s_drag.a0 += (float)(q * step) / 4096.0f * 6.2831853f;
        }
        return;
    }

    float t;
    if (!ed_ray_axis_t(o, d, g, ED_AXIS_VEC[s_drag.axis], &t)) return;
    const float move = t - s_drag.t0;
    // Only commit whole snap steps, and keep the remainder in t0 - otherwise a
    // slow drag rounds to zero every frame and the object never moves.
    int steps = (int)(move / (float)snap);
    if (steps == 0) return;
    const int amount = steps * snap;
    s_drag.t0 += (float)amount;

    const float* v = ED_AXIS_VEC[s_drag.axis];
    EditorSelect_Move((int)(v[0] * (float)amount),
                      (int)(v[1] * (float)amount),
                      (int)(v[2] * (float)amount));
}

// ---------------------------------------------------------------------------
// Drawing it.
// ---------------------------------------------------------------------------
void EditorGizmo_Draw(void)
{
    if (!EditorSelect_Valid() || !g_edView.ok) return;

    float gx, gy, gz;
    EditorSelect_Origin(&gx, &gy, &gz);
    const float L = ed_gizmo_len(gx, gy, gz);
    const unsigned int cols[3] = { ED_COL_X, ED_COL_Y, ED_COL_Z };

    const int hover = EditorGizmo_Busy() ? s_drag.axis
                                         : ed_gizmo_pick(g_edMouse.gameX, g_edMouse.gameY);

    for (int a = 0; a < 3; a++) {
        if (!ed_has_axis(a)) continue;
        const float tx = gx + ED_AXIS_VEC[a][0]*L;
        const float ty = gy + ED_AXIS_VEC[a][1]*L;
        const float tz = gz + ED_AXIS_VEC[a][2]*L;
        const unsigned int c = (hover == a) ? ED_COL_HOT : cols[a];
        EditorDraw_WorldLine(gx, gy, gz, tx, ty, tz, (hover == a) ? 3.0f : 2.2f, c);

        // An arrowhead, drawn as two short world-space barbs along the other
        // two axes - cheap, and it turns with the view for free.
        const int b1 = (a + 1) % 3, b2 = (a + 2) % 3;
        const float back = 0.82f, wing = 0.10f;
        const float bx = gx + ED_AXIS_VEC[a][0]*L*back;
        const float by = gy + ED_AXIS_VEC[a][1]*L*back;
        const float bz = gz + ED_AXIS_VEC[a][2]*L*back;
        for (int s = -1; s <= 1; s += 2) {
            EditorDraw_WorldLine(tx, ty, tz,
                bx + ED_AXIS_VEC[b1][0]*L*wing*(float)s,
                by + ED_AXIS_VEC[b1][1]*L*wing*(float)s,
                bz + ED_AXIS_VEC[b1][2]*L*wing*(float)s, 2.0f, c);
            EditorDraw_WorldLine(tx, ty, tz,
                bx + ED_AXIS_VEC[b2][0]*L*wing*(float)s,
                by + ED_AXIS_VEC[b2][1]*L*wing*(float)s,
                bz + ED_AXIS_VEC[b2][2]*L*wing*(float)s, 2.0f, c);
        }
    }

    if (ed_has_axis(ED_AXIS_RY)) {
        const unsigned int c = (hover == ED_AXIS_RY) ? ED_COL_HOT : ED_COL_Y;
        const int N = 24;
        for (int i = 0; i < N; i++) {
            const float a0 = (float)i / (float)N * 6.2831853f;
            const float a1 = (float)(i+1) / (float)N * 6.2831853f;
            EditorDraw_WorldLine(gx + cosf(a0)*L*0.85f, gy, gz + sinf(a0)*L*0.85f,
                                 gx + cosf(a1)*L*0.85f, gy, gz + sinf(a1)*L*0.85f,
                                 (hover == ED_AXIS_RY) ? 3.0f : 2.0f, c);
        }
    }
}
