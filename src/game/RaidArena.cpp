// RaidArena.cpp - the RAID mode's arena, drawn as real 3D geometry.
//
// CUSTOM. Not part of the original game, and deliberately not part of the
// story's room pipeline either.
//
// Every room in Resident Evil is a PHOTOGRAPH with a collision map behind it:
// display_image puts a pre-rendered 320x240 picture in the framebuffer and the
// models are composited on top of it. RAID mode does not have a photograph.
// Its room (stage 0, room 0x10 - see tools/build_raid_room.py) ships no
// background pak at all, and the background loaders are skipped for it, so
// what would be the picture is just the clear colour.
//
// This file draws what is there instead: a floor and four walls, in world
// space, every frame.
//
// WHY IT IS DRAWN HERE RATHER THAN BUILT AS MODELS
//
// The engine's own way to put geometry in a room is a TMD registered as a room
// object, and that route has a gate this arena cannot pass: the queue drops any
// model whose FindMinClutDepth comes back 0, and that function only looks at
// TEXTURED primitives - an untextured model never draws at all. It would also
// mean authoring TMD and TIM files, a texture page, and the script that
// registers them, for a box.
//
// So the arena takes the same road CollisionDebug.cpp already takes: build the
// view from the room's own camera, project the corners here, and hand finished
// triangles to the backend. That file is the proof the approach works - its
// outlines land on the pre-rendered photograph exactly where the collision
// records are.
//
// The one thing this adds over that overlay is DEPTH. CollisionDebug draws with
// DrawTriangles, which has no z, because it only ever wanted to sit between the
// background and the models. An arena has to be something the player can stand
// behind, so every vertex carries an NDC depth from TmdViewZToNdc - the same
// mapping the model flush uses - and the draw happens before FlushTmdObjects,
// so the characters depth-test against the room they are standing in.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"
#include "SpriteRenderer.h"   // g_SubpixelOffsetX/Y - the screen centre
#include "TmdRenderer.h"      // TmdViewZToNdc - the model pass's depth mapping
#include "../marni/MarniDX.h"
#include "../marni/MarniSystem.h"
#include <cmath>

// ---------------------------------------------------------------------------
// The room, in world units, and its palette.
//
// These numbers are the SAME box tools/build_raid_room.py writes the collision
// for. They are duplicated rather than read out of the RDT on purpose: the RDT
// stores where the WALLS are (solid slabs outside the play area, which is what
// a push-out test wants), not where the SURFACES are, and reconstructing one
// from the other would be guessing. If the arena changes shape, both change.
// ---------------------------------------------------------------------------
#define RA_X0   2000.0f
#define RA_X1  10000.0f
#define RA_Z0   2000.0f
#define RA_Z1  10000.0f
#define RA_CEIL (-3600.0f)      // RE1 is -Y up
#define RA_FLOOR    0.0f

#define RA_CELLS    8           // floor grid, per axis
#define RA_WALL_H   4           // wall rows
#define RA_NEAR    96.0f        // near plane for this pass, world units

// Fog: the far side of the room sinks toward the clear colour instead of
// ending at a hard edge, which is what keeps a box from reading as a box.
#define RA_FOG_NEAR  3500.0f
#define RA_FOG_FAR  13500.0f
#define RA_FOG_MAX   0.62f

// ---------------------------------------------------------------------------
// Batch
// ---------------------------------------------------------------------------
#define RA_MAX_TRIS 1024
#define RA_FLOATS   10          // x, y, z, w, u, v, r, g, b, a

static float    s_tris[RA_MAX_TRIS * 3 * RA_FLOATS];
static int      s_triCount  = 0;
static int      s_cursor    = 0;
static MarniDX* s_dx        = NULL;

struct RaView {
    float cx, cy, f;
    float fromX, fromY, fromZ;
    float n[3], r[3], u[3];
};

// A world point that has already been through the view: kept together because
// the near clip interpolates the colour along with the position.
struct RaVert {
    float x, y, z;              // world
    float cr, cg, cb;
};

// ---------------------------------------------------------------------------
// The view, built from the room's own camera - the same construction
// CollisionDebug uses, and for the same reason: g_RoomCameraData's rows live in
// a Y-flipped frame and its translation mixes conventions, so feeding raw world
// points into it puts geometry at the wrong height in a depth-dependent way.
// Deriving the basis here is both simpler and checkable - the camera's look-at
// point lands on the screen centre by construction.
// ---------------------------------------------------------------------------
static bool RaBuildView(RaView& V, float scaleX)
{
    if (g_RdtPointer == NULL) return false;
    if ((unsigned int)g_roomCameraId >= (unsigned int)g_RdtPointer->cameras_count) return false;

    RDT_Camera* cams = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    RDT_Camera* C = &cams[g_roomCameraId];

    float dx = (float)(C->cam_to_x - C->cam_from_x);
    float dy = (float)(C->cam_to_y - C->cam_from_y);
    float dz = (float)(C->cam_to_z - C->cam_from_z);

    float L = sqrtf(dx*dx + dy*dy + dz*dz);
    float h = sqrtf(dx*dx + dz*dz);
    if (L < 1.0f || h < 1.0f) return false;

    V.fromX = (float)C->cam_from_x;
    V.fromY = (float)C->cam_from_y;
    V.fromZ = (float)C->cam_from_z;

    V.n[0] = dx / L;   V.n[1] = dy / L;   V.n[2] = dz / L;
    V.r[0] = dz / h;   V.r[1] = 0.0f;     V.r[2] = -dx / h;
    V.u[0] = V.n[1]*V.r[2] - V.n[2]*V.r[1];
    V.u[1] = V.n[2]*V.r[0] - V.n[0]*V.r[2];
    V.u[2] = V.n[0]*V.r[1] - V.n[1]*V.r[0];

    V.f = (float)C->fov * scaleX;
    return true;
}

static float RaDepth(const RaView& V, const RaVert& p)
{
    return V.n[0]*(p.x - V.fromX) + V.n[1]*(p.y - V.fromY) + V.n[2]*(p.z - V.fromZ);
}

static void RaProject(const RaView& V, const RaVert& p, float vz,
                      float* outX, float* outY)
{
    float dx = p.x - V.fromX, dy = p.y - V.fromY, dz = p.z - V.fromZ;
    float iz = V.f / vz;
    *outX = V.cx + (V.r[0]*dx + V.r[1]*dy + V.r[2]*dz) * iz;
    // u points down in PS1 coordinates and screen Y grows down, so this adds.
    *outY = V.cy + (V.u[0]*dx + V.u[1]*dy + V.u[2]*dz) * iz;
}

// ---------------------------------------------------------------------------
// Submission
// ---------------------------------------------------------------------------
static void RaFlush(void)
{
    if (s_triCount > 0 && s_dx != NULL) {
        // Opaque, depth-writing: this IS the room, so everything else in the
        // frame has to be able to hide behind it.
        s_dx->DrawTriangles3D(s_tris, s_triCount, MARNI_NULL_HANDLE,
                              MARNI_SAMPLER_POINT, MARNI_BLEND_DISABLE, true);
    }
    s_triCount = 0;
    s_cursor   = 0;
}

static void RaVtx(float sx, float sy, float vz, float r, float g, float b)
{
    float* p = &s_tris[s_cursor];
    p[0] = sx;
    p[1] = sy;
    p[2] = TmdViewZToNdc(vz);   // the mapping the model flush uses
    p[3] = vz;
    p[4] = 0.0f;                // no texture: the backend substitutes a white
    p[5] = 0.0f;                // 1x1, so the vertex colour is the whole shade
    p[6] = r; p[7] = g; p[8] = b; p[9] = 1.0f;
    s_cursor += RA_FLOATS;
}

// ---------------------------------------------------------------------------
// One convex polygon, clipped to the near plane and fanned.
//
// Clipping in WORLD space rather than dropping whole faces matters here for the
// same reason it mattered in the collision overlay: the floor spans the room, so
// one of its corners is usually behind the camera. Dropping the face would take
// the floor with it.
// ---------------------------------------------------------------------------
static void RaPoly(const RaView& V, const RaVert* in, int n)
{
    RaVert out[8];
    float  vz[8];
    int    m = 0;

    for (int i = 0; i < n && m < 8; i++) {
        const RaVert& a = in[i];
        const RaVert& b = in[(i + 1) % n];
        float da = RaDepth(V, a);
        float db = RaDepth(V, b);

        if (da >= RA_NEAR) {
            out[m] = a; vz[m] = da; m++;
        }
        if ((da >= RA_NEAR) != (db >= RA_NEAR) && m < 8) {
            float t = (RA_NEAR - da) / (db - da);
            RaVert c;
            c.x  = a.x  + (b.x  - a.x)  * t;
            c.y  = a.y  + (b.y  - a.y)  * t;
            c.z  = a.z  + (b.z  - a.z)  * t;
            c.cr = a.cr + (b.cr - a.cr) * t;
            c.cg = a.cg + (b.cg - a.cg) * t;
            c.cb = a.cb + (b.cb - a.cb) * t;
            out[m] = c; vz[m] = RA_NEAR; m++;
        }
    }
    if (m < 3) return;

    if (s_triCount + (m - 2) > RA_MAX_TRIS) RaFlush();

    float sx[8], sy[8];
    for (int i = 0; i < m; i++) {
        RaProject(V, out[i], vz[i], &sx[i], &sy[i]);
    }

    for (int i = 1; i + 1 < m; i++) {
        RaVtx(sx[0],   sy[0],   vz[0],   out[0].cr,   out[0].cg,   out[0].cb);
        RaVtx(sx[i],   sy[i],   vz[i],   out[i].cr,   out[i].cg,   out[i].cb);
        RaVtx(sx[i+1], sy[i+1], vz[i+1], out[i+1].cr, out[i+1].cg, out[i+1].cb);
        s_triCount++;
    }
}

// ---------------------------------------------------------------------------
// Shading. There is no light rig here - the RDT's lights are for the CHARACTER
// models, which go through update_entity_lighting. The arena shades itself:
// a fixed tint per surface, a lift toward the lamp end of the room, and fog
// with distance from the camera.
// ---------------------------------------------------------------------------
static void RaShade(const RaView& V, RaVert& p, float br, float tr, float tg, float tb)
{
    float dx = p.x - V.fromX, dy = p.y - V.fromY, dz = p.z - V.fromZ;
    float d  = sqrtf(dx*dx + dy*dy + dz*dz);

    float fog = (d - RA_FOG_NEAR) / (RA_FOG_FAR - RA_FOG_NEAR);
    if (fog < 0.0f) fog = 0.0f;
    if (fog > 1.0f) fog = 1.0f;
    fog *= RA_FOG_MAX;

    // A soft pool of light toward the middle of the floor, so the box has a
    // centre to stand in rather than being evenly grey.
    float mx = (p.x - (RA_X0 + RA_X1) * 0.5f) / ((RA_X1 - RA_X0) * 0.5f);
    float mz = (p.z - (RA_Z0 + RA_Z1) * 0.5f) / ((RA_Z1 - RA_Z0) * 0.5f);
    float pool = 1.0f - 0.38f * (mx*mx + mz*mz);
    if (pool < 0.42f) pool = 0.42f;

    float k = br * pool * (1.0f - fog);
    p.cr = tr * k;
    p.cg = tg * k;
    p.cb = tb * k;
}

static void RaQuad(const RaView& V,
                   float ax, float ay, float az, float bx, float by, float bz,
                   float cx, float cy, float cz, float dx, float dy, float dz,
                   float br, float tr, float tg, float tb)
{
    RaVert q[4];
    q[0].x = ax; q[0].y = ay; q[0].z = az;
    q[1].x = bx; q[1].y = by; q[1].z = bz;
    q[2].x = cx; q[2].y = cy; q[2].z = cz;
    q[3].x = dx; q[3].y = dy; q[3].z = dz;
    for (int i = 0; i < 4; i++) RaShade(V, q[i], br, tr, tg, tb);
    RaPoly(V, q, 4);
}

// ===========================================================================
// RaidArena_Draw - once per frame, from the render flush, before the models.
// ===========================================================================
void RaidArena_Draw(void)
{
    if (g_raidMode == 0) return;
    if (g_RdtPointer == NULL) return;

    s_dx = Marni_DX();
    if (s_dx == NULL) return;

    float scaleX, scaleY;
    MarniGetRenderScale(&scaleX, &scaleY);

    RaView V;
    V.cx = (float)g_SubpixelOffsetX * scaleX;
    V.cy = (float)g_SubpixelOffsetY * scaleY;
    if (!RaBuildView(V, scaleX)) return;

    s_triCount = 0;
    s_cursor   = 0;

    // ---- floor -----------------------------------------------------------
    // A grid rather than one quad, for two reasons: the fog and the light pool
    // are evaluated per vertex, so a single room-sized quad would interpolate
    // both across the whole floor and show neither; and the two tones give the
    // eye something to read the perspective against, which is most of what
    // sells a flat plane as a floor.
    {
        const float sx = (RA_X1 - RA_X0) / RA_CELLS;
        const float sz = (RA_Z1 - RA_Z0) / RA_CELLS;
        for (int iz = 0; iz < RA_CELLS; iz++) {
            for (int ix = 0; ix < RA_CELLS; ix++) {
                float x = RA_X0 + sx * ix;
                float z = RA_Z0 + sz * iz;
                const float br = ((ix ^ iz) & 1) ? 0.72f : 0.58f;
                RaQuad(V,
                       x,      RA_FLOOR, z,
                       x + sx, RA_FLOOR, z,
                       x + sx, RA_FLOOR, z + sz,
                       x,      RA_FLOOR, z + sz,
                       br, 1.00f, 0.97f, 0.88f);
            }
        }
    }

    // ---- walls -----------------------------------------------------------
    // All four, every frame. The two the camera stands between fall entirely
    // behind the near plane and cost nothing but the clip test, so there is no
    // culling to get wrong.
    {
        const float rows = (float)RA_WALL_H;
        for (int side = 0; side < 4; side++) {
            for (int c = 0; c < RA_CELLS; c++) {
                for (int rrow = 0; rrow < RA_WALL_H; rrow++) {
                    float y0 = RA_FLOOR + (RA_CEIL - RA_FLOOR) * (rrow / rows);
                    float y1 = RA_FLOOR + (RA_CEIL - RA_FLOOR) * ((rrow + 1) / rows);

                    float t0 = (float)c / RA_CELLS;
                    float t1 = (float)(c + 1) / RA_CELLS;
                    float ax, az, bx, bz;
                    switch (side) {
                        case 0:   // west, x = X0
                            ax = RA_X0; az = RA_Z0 + (RA_Z1 - RA_Z0) * t0;
                            bx = RA_X0; bz = RA_Z0 + (RA_Z1 - RA_Z0) * t1;
                            break;
                        case 1:   // east
                            ax = RA_X1; az = RA_Z0 + (RA_Z1 - RA_Z0) * t0;
                            bx = RA_X1; bz = RA_Z0 + (RA_Z1 - RA_Z0) * t1;
                            break;
                        case 2:   // north, z = Z0
                            ax = RA_X0 + (RA_X1 - RA_X0) * t0; az = RA_Z0;
                            bx = RA_X0 + (RA_X1 - RA_X0) * t1; bz = RA_Z0;
                            break;
                        default:  // south
                            ax = RA_X0 + (RA_X1 - RA_X0) * t0; az = RA_Z1;
                            bx = RA_X0 + (RA_X1 - RA_X0) * t1; bz = RA_Z1;
                            break;
                    }

                    // Darker toward the ceiling: the light in this room is on
                    // the floor, so the walls should lose it going up.
                    float up = 1.0f - 0.55f * ((rrow + 0.5f) / rows);
                    RaQuad(V,
                           ax, y0, az,  bx, y0, bz,
                           bx, y1, bz,  ax, y1, az,
                           0.46f * up, 0.82f, 0.86f, 1.00f);
                }
            }
        }
    }

    RaFlush();
}
