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
#include "RaidLevel.h"       // g_raidLevel, and the pickups' state
#include "RaidItemModels.h"  // the pickups' real models
#include "SpriteRenderer.h"   // g_SubpixelOffsetX/Y - the screen centre
#include "TmdRenderer.h"      // TmdViewZToNdc - the model pass's depth mapping
#include "../marni/MarniDX.h"
#include "CoopPlayer.h"      // CUSTOM: co-op nameplates
#include "UiAtlas.h"         // CUSTOM: the baked font the nameplates draw with
#include "RaidShoulderCam.h"  // CUSTOM: the L2 aim crosshair
#include "../marni/MarniSystem.h"
#include <cmath>
#include <cstdio>             // CUSTOM: backdrop paths
#include <cstdlib>            // CUSTOM: backdrop pixel conversion
#include "FileLoader.h"       // CUSTOM: backdrop .pak loading
#include "../system/AssetPath.h"   // CUSTOM: GAME_DATA_ROOT for the backdrop paths
#include "../platform/platform.h"  // CUSTOM: plat_file_read_all for the HD backdrops
#include <cstring>

// ---------------------------------------------------------------------------
// The room is DATA now (RaidLevel.h, read from Data\raid1.lvl). What is left
// in this file is how a level is DRAWN: the projection, the near clip, the fog
// and the shading. Nothing here knows the shape of a room, which is what makes
// an editor possible.
// ---------------------------------------------------------------------------
#define RA_CELL    1000.0f      // face subdivision, world units
#define RA_NEAR      96.0f      // near plane for this pass, world units

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
// CUSTOM: the mirror pass's clip. While the room is drawn a second time through
// the reflected eye (RaDrawMirror), only what lies on the ROOM side of the
// glass may take part - anything behind it (the wall the mirror hangs on)
// would reflect out into the room in front of the glass. -1 is off.
// ---------------------------------------------------------------------------
static int   s_clipAxis = -1;       // 0 = X, 2 = Z
static float s_clipK    = 0.0f;
static float s_clipSide = 1.0f;     // keep (p[axis] - k) * side >= 0

static int RaClipMirror(const RaVert* in, int n, RaVert* out)
{
    int m = 0;
    for (int i = 0; i < n && m < 9; i++) {
        const RaVert& a = in[i];
        const RaVert& b = in[(i + 1) % n];
        const float da = ((&a.x)[s_clipAxis] - s_clipK) * s_clipSide;
        const float db = ((&b.x)[s_clipAxis] - s_clipK) * s_clipSide;
        if (da >= 0.0f) out[m++] = a;
        if ((da >= 0.0f) != (db >= 0.0f) && m < 9) {
            const float t = da / (da - db);
            RaVert c;
            c.x  = a.x  + (b.x  - a.x)  * t;
            c.y  = a.y  + (b.y  - a.y)  * t;
            c.z  = a.z  + (b.z  - a.z)  * t;
            c.cr = a.cr + (b.cr - a.cr) * t;
            c.cg = a.cg + (b.cg - a.cg) * t;
            c.cb = a.cb + (b.cb - a.cb) * t;
            out[m++] = c;
        }
    }
    return m;
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

    RaVert cut[10];                                   // CUSTOM: the mirror pass
    if (s_clipAxis >= 0) {
        n = RaClipMirror(in, n, cut);
        if (n < 3) return;
        in = cut;
    }

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
// The level's footprint, refreshed whenever it is (re)loaded. The light pool
// below needs a centre, and the only honest one is the level's own.
static float s_bx0 = 0.0f, s_bx1 = 1.0f, s_bz0 = 0.0f, s_bz1 = 1.0f;

static void RaBounds(void)
{
    if (g_raidLevel.nbox <= 0) return;
    float x0 = 32767.0f, x1 = -32767.0f, z0 = 32767.0f, z1 = -32767.0f;
    for (int i = 0; i < g_raidLevel.nbox; i++) {
        const RaidBox* B = &g_raidLevel.box[i];
        if (B->x0 < x0) x0 = (float)B->x0;
        if (B->x1 > x1) x1 = (float)B->x1;
        if (B->z0 < z0) z0 = (float)B->z0;
        if (B->z1 > z1) z1 = (float)B->z1;
    }
    if (x1 <= x0) x1 = x0 + 1.0f;
    if (z1 <= z0) z1 = z0 + 1.0f;
    s_bx0 = x0; s_bx1 = x1; s_bz0 = z0; s_bz1 = z1;
}

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
    float mx = (p.x - (s_bx0 + s_bx1) * 0.5f) / ((s_bx1 - s_bx0) * 0.5f);
    float mz = (p.z - (s_bz0 + s_bz1) * 0.5f) / ((s_bz1 - s_bz0) * 0.5f);
    float pool = 1.0f - 0.38f * (mx*mx + mz*mz);
    if (pool < 0.42f) pool = 0.42f;

    // Height. The light in these rooms is on the floor, so a surface loses it
    // going up - which is what used to be a hand-written per-row term on the
    // walls and is now true of anything, at any height, for free.
    float up = 1.0f + p.y * (0.35f / 4000.0f);     // p.y is negative upwards
    if (up < 0.45f) up = 0.45f;
    if (up > 1.0f)  up = 1.0f;

    float k = br * pool * up * (1.0f - fog);
    p.cr = tr * k;
    p.cg = tg * k;
    p.cb = tb * k;
}

// The modelled surfaces (materials, textured models) are lit for real: the
// level's own `ambient` and `light` lines, per vertex, with a normal - a lamp
// lights what faces it and leaves the underside of a bath in shade. Fog as
// above. `n` is the outward unit normal.
static float RaContactShadow(const float* p);

static void RaShadeLit(const RaView& V, RaVert& p, const float* n, float br, int shadows = 0)
{
    float dx = p.x - V.fromX, dy = p.y - V.fromY, dz = p.z - V.fromZ;
    float d  = sqrtf(dx*dx + dy*dy + dz*dz);
    float fog = (d - RA_FOG_NEAR) / (RA_FOG_FAR - RA_FOG_NEAR);
    if (fog < 0.0f) fog = 0.0f;
    if (fog > 1.0f) fog = 1.0f;
    fog *= RA_FOG_MAX * 0.6f;

    const RaidLevel* L = &g_raidLevel;
    float r = (float)L->ambR * (1.45f / 4095.0f);
    float g = (float)L->ambG * (1.45f / 4095.0f);
    float b = (float)L->ambB * (1.45f / 4095.0f);
    for (int i = 0; i < L->nlight && i < RAID_MAX_LIGHT; i++) {
        const RaidLight* l = &L->light[i];
        if (l->radius <= 0) continue;
        const float lx = (float)l->x - p.x, ly = (float)l->y - p.y, lz = (float)l->z - p.z;
        const float ld = sqrtf(lx*lx + ly*ly + lz*lz);
        float att = 1.0f - ld / (float)l->radius;
        if (att <= 0.0f) continue;
        att *= att;
        float lam = ld > 1.0f ? (n[0]*lx + n[1]*ly + n[2]*lz) / ld : 1.0f;
        if (lam < 0.0f) lam = 0.0f;
        // A little wrap, so a face turned from the lamp is dim, not black:
        // the room's own walls bounce it.
        const float k = att * (0.22f + 0.78f * lam) * (1.0f / 255.0f) * 1.6f;
        r += (float)l->r * k; g += (float)l->g * k; b += (float)l->b * k;
    }
    const float m = br * (1.0f - fog);
    p.cr = r * m > 1.0f ? 1.0f : r * m;
    p.cg = g * m > 1.0f ? 1.0f : g * m;
    p.cb = b * m > 1.0f ? 1.0f : b * m;
    // The shadow AFTER the clamp: the floor under the lamp is lit past white,
    // and a shadow multiplied in before the clamp is clamped away with it.
    if (shadows) {
        const float pp[3] = { p.x, p.y, p.z };
        const float k = RaContactShadow(pp);
        p.cr *= k; p.cg *= k; p.cb *= k;
    }
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

// Does this axis-aligned face point at the eye? `axis` is 0/1/2 for X/Y/Z and
// `sign` is which way its outward normal runs along it.
static int RaFacing(const RaView& V, float sign, int axis, float cx, float cy, float cz)
{
    const float d[3] = { cx - V.fromX, cy - V.fromY, cz - V.fromZ };
    return (d[axis] * sign) < 0.0f;
}

// One flat face, cut into cells. `u` and `v` are the face's two full edges, so
// a caller states a face as a corner and two vectors and never has to think
// about winding.
static void RaGrid(const RaView& V,
                   float ax, float ay, float az,
                   float ux, float uy, float uz,
                   float vx, float vy, float vz,
                   float br, float tr, float tg, float tb, int checker)
{
    const float ul = sqrtf(ux*ux + uy*uy + uz*uz);
    const float vl = sqrtf(vx*vx + vy*vy + vz*vz);
    int nu = (int)(ul / RA_CELL) + 1;
    int nv = (int)(vl / RA_CELL) + 1;
    if (nu > 12) nu = 12;
    if (nv > 12) nv = 12;

    for (int i = 0; i < nu; i++) {
        const float a0 = (float)i / nu, a1 = (float)(i + 1) / nu;
        for (int j = 0; j < nv; j++) {
            const float b0 = (float)j / nv, b1 = (float)(j + 1) / nv;
            float s = br;
            if (checker && ((i ^ j) & 1)) s *= 0.80f;
            RaQuad(V,
                   ax + ux*a0 + vx*b0, ay + uy*a0 + vy*b0, az + uz*a0 + vz*b0,
                   ax + ux*a1 + vx*b0, ay + uy*a1 + vy*b0, az + uz*a1 + vz*b0,
                   ax + ux*a1 + vx*b1, ay + uy*a1 + vy*b1, az + uz*a1 + vz*b1,
                   ax + ux*a0 + vx*b1, ay + uy*a0 + vy*b1, az + uz*a0 + vz*b1,
                   s, tr, tg, tb);
        }
    }
}


// ---------------------------------------------------------------------------
// BACKDROPS: the game's own pre-rendered rooms, projected back into 3D.
//
// A background is a picture taken from one camera. Put the room's real
// geometry where the picture says it is (floor, walls and furniture as boxes
// - their footprints come from the room's own collision records) and give
// every point of it the colour the picture has at the spot that point lands
// on from that camera, and the room stands up in 3D: seen from the original
// camera it IS the background, and from anywhere else it is the background
// laid onto the surfaces it was painted of.
//
// What one picture cannot give is what its camera did not see - the back of
// the bath, the wall behind the camera, the floor under the toilet. A room
// usually has more than one camera, so a level may name several backdrops
// (bgsrc), and every small cell of every surface takes the one that sees it
// best: facing it most squarely, entirely inside its frame, and not hidden
// behind another box. A cell no backdrop sees takes the box's plain tint.
//
// The pixels come from the stock .pak through the game's own decoder
// (unpack_pakfile_) and the same 555 -> RGBA conversion display_image uses, so
// they are exactly the colours the room has in the game - and nothing derived
// from Capcom's art is written to disk or tracked.
//
// One honest limit: the UVs are exact at the cells' corners and interpolated
// between them by the renderer for the CURRENT view, not the source one, so a
// big cell would warp. The cells are small for that reason (RA_PCELL).
// ---------------------------------------------------------------------------
#define RA_PCELL      250.0f     // projected-surface cell, world units
#define RA_PCELL_MAX  64         // per edge
#define RA_BG_W       320
#define RA_BG_H       240

struct RaSrc {
    float fx, fy, fz;
    float n[3], r[3], u[3];
    float f;
    int   ok;
};

static RaSrc       s_src[RAID_MAX_BGSRC];
static MarniHandle s_bgTex[RAID_MAX_BGSRC];
static unsigned char s_bgKey[RAID_MAX_BGSRC][3];   // which image s_bgTex holds
static int         s_bgHave[RAID_MAX_BGSRC];       // 1 loaded, -1 tried and failed
static unsigned int* s_bgPix[RAID_MAX_BGSRC];      // the same pixels, kept for colour sampling
static float       s_ttris[RAID_MAX_BGSRC][RA_MAX_TRIS * 3 * RA_FLOATS];
static int         s_tcount[RAID_MAX_BGSRC];
static float       s_otris[RAID_MAX_BGSRC][RA_MAX_TRIS * 3 * RA_FLOATS];   // blended second pictures
static int         s_ocount[RAID_MAX_BGSRC];

static void RaSrcView(RaSrc* S, const RaidCam* c)
{
    const float dx = (float)(c->tx - c->fx), dy = (float)(c->ty - c->fy), dz = (float)(c->tz - c->fz);
    const float L = sqrtf(dx*dx + dy*dy + dz*dz), h = sqrtf(dx*dx + dz*dz);
    S->ok = (L >= 1.0f && h >= 1.0f && c->fov > 0);
    if (!S->ok) return;
    S->fx = (float)c->fx; S->fy = (float)c->fy; S->fz = (float)c->fz;
    S->n[0] = dx / L; S->n[1] = dy / L; S->n[2] = dz / L;
    S->r[0] = dz / h; S->r[1] = 0.0f;   S->r[2] = -dx / h;
    S->u[0] = S->n[1]*S->r[2] - S->n[2]*S->r[1];
    S->u[1] = S->n[2]*S->r[0] - S->n[0]*S->r[2];
    S->u[2] = S->n[0]*S->r[1] - S->n[1]*S->r[0];
    S->f = (float)c->fov;
}

// Where a world point lands in the 320x240 background - the game's own
// projection (MatrixToCamera, centre 160,120). 0 if behind the camera.
static int RaSrcProject(const RaSrc* S, float x, float y, float z, float* sx, float* sy)
{
    const float px = x - S->fx, py = y - S->fy, pz = z - S->fz;
    const float vz = S->n[0]*px + S->n[1]*py + S->n[2]*pz;
    if (vz < 64.0f) return 0;
    *sx = 160.0f + (S->r[0]*px + S->r[1]*py + S->r[2]*pz) * S->f / vz;
    *sy = 120.0f + (S->u[0]*px + S->u[1]*py + S->u[2]*pz) * S->f / vz;
    return 1;
}


// ---------------------------------------------------------------------------
// Making a 320x240 background bear being walked up to.
//
// The picture was made to be seen at 320x240 from one place. Projected onto
// walls a metre away it fills the screen many times over, and two things that
// were invisible at its own size show: the 5-bit colour (32 levels a channel,
// so every soft gradient is a staircase of bands), and the pixels themselves.
// So, once per image at load:
//
//   1. deband - each pixel takes the average of its 5x5 neighbours that differ
//      from it by no more than about one 5-bit step, so a band edge (a 1-step
//      jump) is smoothed and a real edge (many steps) is left alone;
//   2. upscale x4 with Catmull-Rom bicubic, which keeps edges where bilinear
//      sampling of the small image would smear them;
//   3. sharpen lightly (unsharp mask), because any upscale softens.
//
// The texture is then 1280x960. The UVs do not change - they are 0..1 over the
// picture whatever its size.
// ---------------------------------------------------------------------------
#define RA_BG_SCALE   4
#define RA_BG_TW      (RA_BG_W * RA_BG_SCALE)
#define RA_BG_TH      (RA_BG_H * RA_BG_SCALE)

static inline float RaClamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void RaDeband(const float* in, float* out, int w, int h)
{
    const float tol = 1.25f / 31.0f;                 // about one 5-bit step
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float* c = &in[(y * w + x) * 3];
            float sum[3] = { 0, 0, 0 };
            int n = 0;
            for (int dy = -2; dy <= 2; dy++) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = -2; dx <= 2; dx++) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w) continue;
                    const float* q = &in[(yy * w + xx) * 3];
                    if (fabsf(q[0] - c[0]) <= tol && fabsf(q[1] - c[1]) <= tol && fabsf(q[2] - c[2]) <= tol) {
                        sum[0] += q[0]; sum[1] += q[1]; sum[2] += q[2]; n++;
                    }
                }
            }
            float* o = &out[(y * w + x) * 3];
            o[0] = sum[0] / n; o[1] = sum[1] / n; o[2] = sum[2] / n;   // n >= 1: the pixel itself
        }
    }
}

static inline float RaCubic(float a, float b, float c, float d, float t)
{
    // Catmull-Rom through b and c.
    return b + 0.5f * t * (c - a + t * (2.0f*a - 5.0f*b + 4.0f*c - d + t * (3.0f*(b - c) + d - a)));
}

static void RaUpscale(const float* in, float* out, int w, int h, int k)
{
    const int W = w * k, H = h * k;
    for (int Y = 0; Y < H; Y++) {
        const float sy = (Y + 0.5f) / k - 0.5f;
        const int y1 = (int)floorf(sy);
        const float ty = sy - y1;
        int ys[4];
        for (int i = 0; i < 4; i++) { int v = y1 - 1 + i; ys[i] = v < 0 ? 0 : (v >= h ? h - 1 : v); }
        for (int X = 0; X < W; X++) {
            const float sx = (X + 0.5f) / k - 0.5f;
            const int x1 = (int)floorf(sx);
            const float tx = sx - x1;
            int xs[4];
            for (int i = 0; i < 4; i++) { int v = x1 - 1 + i; xs[i] = v < 0 ? 0 : (v >= w ? w - 1 : v); }
            for (int ch = 0; ch < 3; ch++) {
                float col[4];
                for (int j = 0; j < 4; j++) {
                    const float* r = &in[(ys[j] * w) * 3 + ch];
                    col[j] = RaCubic(r[xs[0] * 3], r[xs[1] * 3], r[xs[2] * 3], r[xs[3] * 3], tx);
                }
                out[(Y * W + X) * 3 + ch] = RaCubic(col[0], col[1], col[2], col[3], ty);
            }
        }
    }
}

// Unsharp mask with a 3x3 box blur, written straight into the RGBA texture.
static void RaSharpenToRGBA(const float* in, unsigned int* out, int w, int h, float amount)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float blur[3] = { 0, 0, 0 };
            int n = 0;
            for (int dy = -1; dy <= 1; dy++) {
                const int yy = y + dy < 0 ? 0 : (y + dy >= h ? h - 1 : y + dy);
                for (int dx = -1; dx <= 1; dx++) {
                    const int xx = x + dx < 0 ? 0 : (x + dx >= w ? w - 1 : x + dx);
                    const float* q = &in[(yy * w + xx) * 3];
                    blur[0] += q[0]; blur[1] += q[1]; blur[2] += q[2]; n++;
                }
            }
            const float* c = &in[(y * w + x) * 3];
            unsigned int rgb[3];
            for (int ch = 0; ch < 3; ch++) {
                const float v = RaClamp01(c[ch] + amount * (c[ch] - blur[ch] / n));
                rgb[ch] = (unsigned int)(v * 255.0f + 0.5f);
            }
            out[y * w + x] = (0xFFu << 24) | (rgb[2] << 16) | (rgb[1] << 8) | rgb[0];
        }
    }
}

// The enhanced texture from the plain RGBA picture; NULL if out of memory.
static unsigned int* RaEnhance(const unsigned int* rgba)
{
    const int n = RA_BG_W * RA_BG_H, N = RA_BG_TW * RA_BG_TH;
    float* a = (float*)malloc(n * 3 * sizeof(float));
    float* b = (float*)malloc(n * 3 * sizeof(float));
    float* big = (float*)malloc(N * 3 * sizeof(float));
    unsigned int* out = (unsigned int*)malloc(N * 4);
    if (a == NULL || b == NULL || big == NULL || out == NULL) {
        free(a); free(b); free(big); free(out);
        return NULL;
    }
    for (int p = 0; p < n; p++) {
        a[p*3 + 0] = (float)( rgba[p]        & 0xFF) / 255.0f;
        a[p*3 + 1] = (float)((rgba[p] >> 8)  & 0xFF) / 255.0f;
        a[p*3 + 2] = (float)((rgba[p] >> 16) & 0xFF) / 255.0f;
    }
    RaDeband(a, b, RA_BG_W, RA_BG_H);
    RaUpscale(b, big, RA_BG_W, RA_BG_H, RA_BG_SCALE);
    RaSharpenToRGBA(big, out, RA_BG_TW, RA_BG_TH, 0.45f);
    free(a); free(b); free(big);
    return out;
}

// Load (or keep) backdrop i's texture. Through the game's own path and
// decoder; the buffers are the room-background ones, which RAID never uses
// (load_room_bg returns early in it).
static void RaBgLoad(int i)
{
    const RaidBgSrc* B = &g_raidLevel.bgsrc[i];
    if (s_bgHave[i] != 0 && s_bgKey[i][0] == B->stage && s_bgKey[i][1] == B->room
        && s_bgKey[i][2] == B->cam) {
        return;                                  // this image, already loaded (or failed)
    }
    if (s_bgTex[i] != MARNI_NULL_HANDLE && s_dx != NULL) {
        s_dx->DestroyTexture(s_bgTex[i]);
    }
    s_bgTex[i] = MARNI_NULL_HANDLE;
    free(s_bgPix[i]);
    s_bgPix[i] = NULL;
    s_bgHave[i] = -1;
    s_bgKey[i][0] = B->stage; s_bgKey[i][1] = B->room; s_bgKey[i][2] = B->cam;

    char path[64];
    sprintf(path, GAME_DATA_ROOT "stage%d\\rc%d%02x%x.pak",
            B->stage, B->stage, B->room, B->cam & 0x0F);
    const size_t got = LoadFile(path, g_bgPakLoadBuffer, 2);
    if (got == (size_t)-1 || got == 0 || got > sizeof(g_bgPakLoadBuffer)) return;
    unpack_pakfile_(g_bgPakLoadBuffer, g_TimImageBuffer);

    const unsigned short* src = (const unsigned short*)g_TimImageBuffer__bitmap;
    unsigned int* rgba = (unsigned int*)malloc(RA_BG_W * RA_BG_H * 4);
    if (rgba == NULL) return;
    for (int p = 0; p < RA_BG_W * RA_BG_H; p++) {          // display_image's conversion
        const unsigned short px = src[p];
        const unsigned int r = ((px >> 0)  & 0x1F) * 255 / 31;
        const unsigned int g = ((px >> 5)  & 0x1F) * 255 / 31;
        const unsigned int b = ((px >> 10) & 0x1F) * 255 / 31;
        rgba[p] = (0xFFu << 24) | (b << 16) | (g << 8) | r;
    }
    // The texture. First choice is a high-resolution version of this very
    // picture, if tools/build_bg_hd.py has made one (a neural-network x4 -
    // the only thing that adds detail the 320x240 never had):
    //     Data/bghd/rc<S><RR><C>.bin  =  'BGHD' + u32 w + u32 h + RGBA
    // read whole into a buffer of its own size, so a bad file cannot overrun
    // anything. Otherwise the stock picture, debanded and upscaled here
    // (RaEnhance). Either way the plain picture is kept for RaBoxColours,
    // which wants the original colours. The UVs are 0..1 over the picture,
    // so its size does not matter to anything else.
    BOOL made = FALSE;
    {
        char hd[96], rooted[260];
        sprintf(hd, GAME_DATA_ROOT "Data/bghd/rc%d%02x%x.bin", B->stage, B->room, B->cam & 0x0F);
        size_t size = 0;
        unsigned char* blob = (unsigned char*)plat_file_read_all(
            ResolveAssetRoot(hd, rooted, sizeof(rooted)), &size);
        if (blob != NULL && size >= 12 && memcmp(blob, "BGHD", 4) == 0) {
            const unsigned int w = *(const unsigned int*)(blob + 4);
            const unsigned int h = *(const unsigned int*)(blob + 8);
            if (w > 0 && h > 0 && w <= 8192 && h <= 8192 && size >= 12 + (size_t)w * h * 4) {
                made = MarniCreateTexture((int)w, (int)h, 32, blob + 12, &s_bgTex[i]);
            }
        }
        free(blob);
    }
    if (!made) {
        unsigned int* tex = RaEnhance(rgba);
        made = (tex != NULL)
            ? MarniCreateTexture(RA_BG_TW, RA_BG_TH, 32, tex, &s_bgTex[i])
            : MarniCreateTexture(RA_BG_W, RA_BG_H, 32, rgba, &s_bgTex[i]);
        free(tex);
    }
    if (made && s_bgTex[i] != MARNI_NULL_HANDLE) {
        s_bgHave[i] = 1;
        s_bgPix[i] = rgba;        // kept: RaBoxColours samples it
        return;
    }
    free(rgba);
}

static void RaBoxColours(void);
static void RaMeshPrepare(void);

static void RaBgPrepare(void)
{
    for (int i = 0; i < RAID_MAX_BGSRC; i++) {
        s_tcount[i] = 0;
        s_ocount[i] = 0;
        if (i >= g_raidLevel.nbgsrc) { s_src[i].ok = 0; continue; }
        RaSrcView(&s_src[i], &g_raidLevel.bgsrc[i].view);
        RaBgLoad(i);
        if (s_bgHave[i] != 1) s_src[i].ok = 0;
    }
    RaMeshPrepare();      // the models' bounds, which hide things too
    RaBoxColours();
}

static void RaTexFlush(int k)
{
    if (s_tcount[k] > 0 && s_dx != NULL) {
        s_dx->DrawTriangles3D(s_ttris[k], s_tcount[k], s_bgTex[k],
                              MARNI_SAMPLER_LINEAR, MARNI_BLEND_DISABLE, true);
    }
    s_tcount[k] = 0;
}

// The second picture of a blended cell: over the first, at the same depth
// (the same vertices, so LESS_EQUAL passes), alpha from the vertex, no depth
// write. Flushed after every opaque batch.
static void RaOverFlush(int k)
{
    if (s_ocount[k] > 0 && s_dx != NULL) {
        s_dx->DrawTriangles3D(s_otris[k], s_ocount[k], s_bgTex[k],
                              MARNI_SAMPLER_LINEAR, MARNI_BLEND_ALPHA, false);
    }
    s_ocount[k] = 0;
}

// Does the segment from a to b pass through any projected box other than
// `self`? The boxes are shrunk a little so a surface is not hidden by the box
// it touches (walls overlap at the corners; furniture stands on the floor).
// The placed models' world bounds this frame, for the occlusion test below
// (RaMeshPrepare fills them). A bound is coarser than the shape, which errs
// toward calling a surface hidden - the safe side: hidden means the surface's
// own colour, not some other object's picture.
static float s_meshLo[RAID_MAX_MESH][3], s_meshHi[RAID_MAX_MESH][3];
static int   s_meshLive[RAID_MAX_MESH];

// ---------------------------------------------------------------------------
// CUSTOM: shadows - the contact kind, which is what the pictures show: the
// floor darkening round the foot of the bath, the pan, the basin, the towel
// hanging over the rim. Each model flagged RAID_MESH_SHADOW that reaches down
// to the floor darkens it by its distance from the model's footprint, pushed
// a little away from the room's main light so the shadow has a side.
//
// Smooth by construction, and that is the point: a shadow cast by a ray test
// is a hard edge, and an edge evaluated per vertex - which is how this arena
// lights - lands mid-cell and draws a long straight seam across the face.
// That is what the first try did on the walls. So: floors only, no edges.
// ---------------------------------------------------------------------------
static float RaContactShadow(const float* p)
{
    float lx = 0.0f, lz = 0.0f;
    if (g_raidLevel.nlight > 0) { lx = (float)g_raidLevel.light[0].x; lz = (float)g_raidLevel.light[0].z; }
    float k = 1.0f;
    for (int m = 0; m < g_raidLevel.nmesh && m < RAID_MAX_MESH; m++) {
        if (!s_meshLive[m] || (g_raidLevel.mesh[m].flags & RAID_MESH_SHADOW) == 0) continue;
        const float* lo = s_meshLo[m];
        const float* hi = s_meshHi[m];
        if (hi[1] < -250.0f) continue;                       // does not reach the floor
        // the footprint, nudged away from the light
        float cx = (lo[0] + hi[0]) * 0.5f, cz = (lo[2] + hi[2]) * 0.5f;
        float ox = cx - lx, oz = cz - lz;
        const float ol = sqrtf(ox * ox + oz * oz);
        if (ol > 1.0f) { ox = ox / ol * 140.0f; oz = oz / ol * 140.0f; } else { ox = oz = 0.0f; }
        float dx = 0.0f, dz = 0.0f;
        if (p[0] < lo[0] + ox) dx = lo[0] + ox - p[0]; else if (p[0] > hi[0] + ox) dx = p[0] - hi[0] - ox;
        if (p[2] < lo[2] + oz) dz = lo[2] + oz - p[2]; else if (p[2] > hi[2] + oz) dz = p[2] - hi[2] - oz;
        const float d = sqrtf(dx * dx + dz * dz);
        k *= 1.0f - 0.55f * expf(-d / 380.0f);
    }
    return k;
}
static float s_meshCol[RAID_MAX_MESH][3];        // a model's own colour (RaMeshColour)
static int   s_meshColValid[RAID_MAX_MESH];

static int RaSegHitsBox(const float a[3], const float b[3], const float lo[3], const float hi[3])
{
    float t0 = 0.0f, t1 = 1.0f;
    for (int ax = 0; ax < 3; ax++) {
        const float d = b[ax] - a[ax];
        if (fabsf(d) < 1e-4f) {
            if (a[ax] < lo[ax] || a[ax] > hi[ax]) return 0;
        } else {
            float ta = (lo[ax] - a[ax]) / d, tb = (hi[ax] - a[ax]) / d;
            if (ta > tb) { const float s = ta; ta = tb; tb = s; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) return 0;
        }
    }
    return 1;
}

// `self` is what the surface belongs to, so it is not hidden by itself: a box
// index (>= 0), or a placed model as -2 - its index.
static int RaBgOccluded(const float a[3], const float b[3], int self)
{
    for (int m = 0; m < g_raidLevel.nmesh && m < RAID_MAX_MESH; m++) {
        if (!s_meshLive[m] || self == -2 - m) continue;
        const float lo[3] = { s_meshLo[m][0] + 30.0f, s_meshLo[m][1] + 30.0f, s_meshLo[m][2] + 30.0f };
        const float hi[3] = { s_meshHi[m][0] - 30.0f, s_meshHi[m][1] - 30.0f, s_meshHi[m][2] - 30.0f };
        if (lo[0] < hi[0] && lo[1] < hi[1] && lo[2] < hi[2] && RaSegHitsBox(a, b, lo, hi)) return 1;
    }
    for (int i = 0; i < g_raidLevel.nbox; i++) {
        if (i == self) continue;
        const RaidBox* B = &g_raidLevel.box[i];
        if ((B->flags & RAID_BOX_PROJ) == 0) continue;
        if (B->y0 == B->y1) continue;                     // a plate hides nothing
        const float lo[3] = { (float)B->x0 + 40.0f, (float)B->y0 + 40.0f, (float)B->z0 + 40.0f };
        const float hi[3] = { (float)B->x1 - 40.0f, (float)B->y1 - 40.0f, (float)B->z1 - 40.0f };
        float t0 = 0.0f, t1 = 1.0f;
        int miss = 0;
        for (int ax = 0; ax < 3 && !miss; ax++) {
            const float d = b[ax] - a[ax];
            if (fabsf(d) < 1e-4f) {
                if (a[ax] < lo[ax] || a[ax] > hi[ax]) miss = 1;
            } else {
                float ta = (lo[ax] - a[ax]) / d, tb = (hi[ax] - a[ax]) / d;
                if (ta > tb) { const float s = ta; ta = tb; tb = s; }
                if (ta > t0) t0 = ta;
                if (tb < t1) t1 = tb;
                if (t0 > t1) miss = 1;
            }
        }
        if (!miss) return 1;
    }
    return 0;
}

// The backdrop that sees this cell best, or -1.
//
// "Best" is two things. Mostly how squarely the source camera faces the cell -
// a picture taken edge-on is a smear. But also how close the source's view of
// the cell is to the CURRENT one: a picture is only exactly right when seen
// from where it was taken, so between two cameras that both see a cell well,
// the one looking from the viewer's side wins. Seen from a room camera that is
// the room's own background; walking about, each surface takes the picture
// that was painted from nearest where you stand.
#define RA_VIEW_BIAS  4.0f      // weight of the agreement term below
#define RA_VIEW_SHARP 8         // ...and how sharply it peaks at "same direction"
#define RA_VIEW_NEAR  600.0f    // ...and only this close to that source's own eye

// How much each backdrop should count for this cell, before framing (which is
// per VERTEX, RaFrameSoft): facing x the viewer-agreement peak, 0 for a
// backdrop that does not face it, has a corner behind its camera, or sees it
// only through another box. A cell every backdrop scores 0 is one nobody saw.
static void RaBgWeights(const RaView& V, const RaVert* q, const float nrm[3], int self,
                        float cw[RAID_MAX_BGSRC])
{
    float c[3] = { 0, 0, 0 };
    for (int i = 0; i < 4; i++) { c[0] += q[i].x * 0.25f; c[1] += q[i].y * 0.25f; c[2] += q[i].z * 0.25f; }

    for (int k = 0; k < RAID_MAX_BGSRC; k++) {
        cw[k] = 0.0f;
        if (k >= g_raidLevel.nbgsrc) continue;
        const RaSrc* S = &s_src[k];
        if (!S->ok) continue;
        float d[3] = { S->fx - c[0], S->fy - c[1], S->fz - c[2] };
        const float L = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (L < 1.0f) continue;
        const float facing = (nrm[0]*d[0] + nrm[1]*d[1] + nrm[2]*d[2]) / L;
        if (facing < 0.08f) continue;                    // edge-on or from behind

        int inFront = 1;
        for (int i = 0; i < 4 && inFront; i++) {
            float sx, sy;
            if (!RaSrcProject(S, q[i].x, q[i].y, q[i].z, &sx, &sy)) inFront = 0;
        }
        if (!inFront) continue;

        const float from[3] = { c[0] + nrm[0] * 30.0f, c[1] + nrm[1] * 30.0f, c[2] + nrm[2] * 30.0f };
        const float eye[3]  = { S->fx, S->fy, S->fz };
        if (RaBgOccluded(from, eye, self)) continue;     // its picture there is something else

        float w[3] = { V.fromX - c[0], V.fromY - c[1], V.fromZ - c[2] };
        const float wl = sqrtf(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
        float agree = 0.0f;
        if (wl > 1.0f) {
            agree = (w[0]*d[0] + w[1]*d[1] + w[2]*d[2]) / (wl * L);   // -1..1
            if (agree < 0.0f) agree = 0.0f;
        }
        // Peaked, and only near the source's own eye: seen from a room camera
        // the room is EXACTLY its background; anywhere else the weights are a
        // function of the cell alone and hold still while the viewer moves
        // (a view-dependent choice made the floor flicker).
        float peak = agree;
        for (int e = 1; e < RA_VIEW_SHARP; e++) peak *= agree;
        {
            const float ex = V.fromX - S->fx, ey = V.fromY - S->fy, ez = V.fromZ - S->fz;
            if (ex*ex + ey*ey + ez*ez > RA_VIEW_NEAR * RA_VIEW_NEAR) peak = 0.0f;
        }
        cw[k] = facing * (1.0f + RA_VIEW_BIAS * peak);
    }
}

// Framing, per vertex: 1 inside the picture, fading over RA_FRAME_FADE pixels
// outside it to a small floor. Soft, so the hand-over at a picture's edge is a
// gradient rather than a step at a cell boundary - the "staircase".
#define RA_FRAME_FADE  20.0f
#define RA_BLEND_SOURCES 0     // see RaGridProj
static float RaFrameSoft(const RaSrc* S, float x, float y, float z)
{
    float sx, sy;
    if (!RaSrcProject(S, x, y, z, &sx, &sy)) return 0.03f;
    float out = 0.0f;
    if (-sx > out) out = -sx;
    if (sx - RA_BG_W > out) out = sx - RA_BG_W;
    if (-sy > out) out = -sy;
    if (sy - RA_BG_H > out) out = sy - RA_BG_H;
    const float f = 1.0f - out / RA_FRAME_FADE;
    return f < 0.03f ? 0.03f : f;
}


// ---------------------------------------------------------------------------
// Each projected box's own colour, sampled out of the backdrops: the average
// of the pixels its faces land on, from every camera that sees them unhidden.
//
// It paints what no camera saw. Before, that was the projection of whatever DID
// stand in the way - the wall behind the sink got the sink, a "ghost" of it in
// the wrong place and at the wrong size - or the level's hand-picked tint, a
// flat brown that matched nothing. The wall's own average is the colour the
// wall would most likely have been there.
// ---------------------------------------------------------------------------
static float        s_boxCol[RAID_MAX_BOX][3];
static unsigned int s_boxColKey = 0;

static void RaBoxColourOf(int self, const RaidBox* B)
{
    const float x0 = B->x0, x1 = B->x1, y0 = B->y0, y1 = B->y1, z0 = B->z0, z1 = B->z1;
    struct Face { float a[3], u[3], v[3], n[3]; };
    const Face faces[6] = {
        { { x0, y0, z0 }, { x1 - x0, 0, 0 }, { 0, 0, z1 - z0 }, { 0, -1, 0 } },
        { { x0, y1, z0 }, { x1 - x0, 0, 0 }, { 0, 0, z1 - z0 }, { 0,  1, 0 } },
        { { x0, y0, z0 }, { 0, y1 - y0, 0 }, { 0, 0, z1 - z0 }, { -1, 0, 0 } },
        { { x1, y0, z0 }, { 0, y1 - y0, 0 }, { 0, 0, z1 - z0 }, {  1, 0, 0 } },
        { { x0, y0, z0 }, { x1 - x0, 0, 0 }, { 0, y1 - y0, 0 }, { 0, 0, -1 } },
        { { x0, y0, z1 }, { x1 - x0, 0, 0 }, { 0, y1 - y0, 0 }, { 0, 0,  1 } },
    };
    const int nf = (y0 == y1) ? 1 : 6;
    double sum[3] = { 0, 0, 0 };
    int count = 0;
    for (int f = 0; f < nf; f++) {
        const Face& F = faces[f];
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                const float a = (i + 0.5f) / 8.0f, b = (j + 0.5f) / 8.0f;
                const float p[3] = { F.a[0] + F.u[0]*a + F.v[0]*b,
                                     F.a[1] + F.u[1]*a + F.v[1]*b,
                                     F.a[2] + F.u[2]*a + F.v[2]*b };
                for (int k = 0; k < g_raidLevel.nbgsrc && k < RAID_MAX_BGSRC; k++) {
                    const RaSrc* S = &s_src[k];
                    if (!S->ok || s_bgPix[k] == NULL) continue;
                    const float d[3] = { S->fx - p[0], S->fy - p[1], S->fz - p[2] };
                    if (F.n[0]*d[0] + F.n[1]*d[1] + F.n[2]*d[2] <= 0.0f) continue;
                    float sx, sy;
                    if (!RaSrcProject(S, p[0], p[1], p[2], &sx, &sy)) continue;
                    if (sx < 0.0f || sx >= RA_BG_W || sy < 0.0f || sy >= RA_BG_H) continue;
                    const float from[3] = { p[0] + F.n[0] * 30.0f, p[1] + F.n[1] * 30.0f, p[2] + F.n[2] * 30.0f };
                    const float eye[3]  = { S->fx, S->fy, S->fz };
                    if (RaBgOccluded(from, eye, self)) continue;
                    const unsigned int px = s_bgPix[k][(int)sy * RA_BG_W + (int)sx];
                    sum[0] += (px & 0xFF); sum[1] += ((px >> 8) & 0xFF); sum[2] += ((px >> 16) & 0xFF);
                    count++;
                }
            }
        }
    }
    if (count > 0) {
        s_boxCol[self][0] = (float)(sum[0] / count / 255.0);
        s_boxCol[self][1] = (float)(sum[1] / count / 255.0);
        s_boxCol[self][2] = (float)(sum[2] / count / 255.0);
    } else {
        s_boxCol[self][0] = B->shade * B->tr / 255.0f;
        s_boxCol[self][1] = B->shade * B->tg / 255.0f;
        s_boxCol[self][2] = B->shade * B->tb / 255.0f;
    }
}

// Recomputed only when the level's boxes or the loaded images change.
static void RaBoxColours(void)
{
    unsigned int key = 2166136261u;
    for (int i = 0; i < g_raidLevel.nbox; i++) {
        const RaidBox* B = &g_raidLevel.box[i];
        const short v[7] = { B->x0, B->y0, B->z0, B->x1, B->y1, B->z1, (short)B->flags };
        for (int k = 0; k < 7; k++) key = (key ^ (unsigned short)v[k]) * 16777619u;
    }
    for (int k = 0; k < RAID_MAX_BGSRC; k++) key = (key ^ (unsigned int)s_bgTex[k]) * 16777619u;
    for (int m = 0; m < g_raidLevel.nmesh; m++) {
        const RaidMesh* M = &g_raidLevel.mesh[m];
        const int v[6] = { M->id, M->x, M->y, M->z, M->yaw, M->scale };
        for (int k = 0; k < 6; k++) key = (key ^ (unsigned int)v[k]) * 16777619u;
    }
    if (key == s_boxColKey) return;
    for (int m = 0; m < RAID_MAX_MESH; m++) s_meshColValid[m] = 0;
    s_boxColKey = key;
    for (int i = 0; i < g_raidLevel.nbox && i < RAID_MAX_BOX; i++) {
        if (g_raidLevel.box[i].flags & RAID_BOX_PROJ) RaBoxColourOf(i, &g_raidLevel.box[i]);
    }
}

// One cell, clipped to the near plane and textured from backdrop k. The UVs
// are computed from each OUTPUT vertex's world position after clipping - a
// UV is a function of where the point is, so it is never interpolated by
// the clip.
static void RaPolyTex(const RaView& V, const RaVert* in, int n, int k, int kb,
                      float cwa, float cwb)
{
    RaVert out[8];
    float  vz[8];
    int    m = 0;
    for (int i = 0; i < n && m < 8; i++) {
        const RaVert& a = in[i];
        const RaVert& b = in[(i + 1) % n];
        const float da = RaDepth(V, a), db = RaDepth(V, b);
        if (da >= RA_NEAR) { out[m] = a; vz[m] = da; m++; }
        if ((da >= RA_NEAR) != (db >= RA_NEAR) && m < 8) {
            const float t = (RA_NEAR - da) / (db - da);
            RaVert c;
            c.x = a.x + (b.x - a.x) * t;
            c.y = a.y + (b.y - a.y) * t;
            c.z = a.z + (b.z - a.z) * t;
            c.cr = c.cg = c.cb = 1.0f;
            out[m] = c; vz[m] = RA_NEAR; m++;
        }
    }
    if (m < 3) return;
    if (s_tcount[k] + (m - 2) > RA_MAX_TRIS) RaTexFlush(k);

    float sx[8], sy[8], tu[8], tv[8], ou[8], ov[8], oa[8];
    int over = 0;
    for (int i = 0; i < m; i++) {
        RaProject(V, out[i], vz[i], &sx[i], &sy[i]);
        float bx = 0.0f, by = 0.0f;
        RaSrcProject(&s_src[k], out[i].x, out[i].y, out[i].z, &bx, &by);
        tu[i] = bx / (float)RA_BG_W;
        tv[i] = by / (float)RA_BG_H;
        oa[i] = 0.0f;
        if (kb >= 0) {
            // The second picture's share at this vertex. Over the first at
            // alpha wb/(wa+wb) the result is (wa*A + wb*B)/(wa+wb) - the same
            // whichever of the two a neighbouring cell drew first, so two
            // cells sharing an edge agree along it.
            const float wa = cwa * RaFrameSoft(&s_src[k],  out[i].x, out[i].y, out[i].z);
            const float wb = cwb * RaFrameSoft(&s_src[kb], out[i].x, out[i].y, out[i].z);
            oa[i] = (wa + wb > 1e-6f) ? wb / (wa + wb) : 0.0f;
            if (oa[i] > 0.01f) over = 1;
            float cx = 0.0f, cy = 0.0f;
            RaSrcProject(&s_src[kb], out[i].x, out[i].y, out[i].z, &cx, &cy);
            ou[i] = cx / (float)RA_BG_W;
            ov[i] = cy / (float)RA_BG_H;
        }
    }
    if (over && s_ocount[kb] + (m - 2) > RA_MAX_TRIS) {
        // An overlay must never reach the screen before the opaque pictures
        // it blends over: flush those first.
        for (int t = 0; t < RAID_MAX_BGSRC; t++) RaTexFlush(t);
        RaOverFlush(kb);
    }
    for (int i = 1; i + 1 < m; i++) {
        const int idx[3] = { 0, i, i + 1 };
        for (int e = 0; e < 3; e++) {
            const int j = idx[e];
            float* p = &s_ttris[k][s_tcount[k] * 3 * RA_FLOATS + e * RA_FLOATS];
            p[0] = sx[j]; p[1] = sy[j];
            p[2] = TmdViewZToNdc(vz[j]); p[3] = vz[j];
            p[4] = tu[j]; p[5] = tv[j];
            p[6] = 1.0f; p[7] = 1.0f; p[8] = 1.0f; p[9] = 1.0f;
            if (over) {
                float* o = &s_otris[kb][s_ocount[kb] * 3 * RA_FLOATS + e * RA_FLOATS];
                o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = p[3];
                o[4] = ou[j]; o[5] = ov[j];
                o[6] = 1.0f; o[7] = 1.0f; o[8] = 1.0f; o[9] = oa[j];
            }
        }
        s_tcount[k]++;
        if (over) s_ocount[kb]++;
    }
}

// A projected box face, cut into small cells, each textured from the
// backdrop that sees it best. `nrm` is the face's outward normal.
static void RaGridProj(const RaView& V, int self,
                       float ax, float ay, float az,
                       float ux, float uy, float uz,
                       float vx, float vy, float vz,
                       const float nrm[3], float fr, float fg, float fb)
{
    const float ul = sqrtf(ux*ux + uy*uy + uz*uz);
    const float vl = sqrtf(vx*vx + vy*vy + vz*vz);
    int nu = (int)(ul / RA_PCELL) + 1;
    int nv = (int)(vl / RA_PCELL) + 1;
    if (nu > RA_PCELL_MAX) nu = RA_PCELL_MAX;
    if (nv > RA_PCELL_MAX) nv = RA_PCELL_MAX;

    for (int i = 0; i < nu; i++) {
        const float a0 = (float)i / nu, a1 = (float)(i + 1) / nu;
        for (int j = 0; j < nv; j++) {
            const float b0 = (float)j / nv, b1 = (float)(j + 1) / nv;
            RaVert q[4];
            q[0].x = ax + ux*a0 + vx*b0; q[0].y = ay + uy*a0 + vy*b0; q[0].z = az + uz*a0 + vz*b0;
            q[1].x = ax + ux*a1 + vx*b0; q[1].y = ay + uy*a1 + vy*b0; q[1].z = az + uz*a1 + vz*b0;
            q[2].x = ax + ux*a1 + vx*b1; q[2].y = ay + uy*a1 + vy*b1; q[2].z = az + uz*a1 + vz*b1;
            q[3].x = ax + ux*a0 + vx*b1; q[3].y = ay + uy*a0 + vy*b1; q[3].z = az + uz*a0 + vz*b1;
            for (int c = 0; c < 4; c++) { q[c].cr = 1.0f; q[c].cg = 1.0f; q[c].cb = 1.0f; }

            float cw[RAID_MAX_BGSRC];
            RaBgWeights(V, q, nrm, self, cw);
            int ka = -1, kb = -1;
            float sa = 0.0f, sb = 0.0f;
            for (int k = 0; k < RAID_MAX_BGSRC; k++) {
                if (cw[k] <= 0.0f) continue;
                float f = 0.0f;
                for (int c = 0; c < 4; c++) f += RaFrameSoft(&s_src[k], q[c].x, q[c].y, q[c].z);
                const float sc = cw[k] * f;
                if (sc > sa)      { kb = ka; sb = sa; ka = k; sa = sc; }
                else if (sc > sb) { kb = k; sb = sc; }
            }
            // ONE picture per cell. Blending the second one in by weight was
            // tried and looked worse: the two pictures are not the same image
            // (one may come from an HD pack that redrew the room, the other
            // from a network upscale of the original) and are registered only
            // to within ~10 px, so wherever both counted the room showed twice.
            // RaPolyTex keeps the overlay path; RA_BLEND_SOURCES turns it on.
            if (!RA_BLEND_SOURCES) kb = -1;
            if (ka >= 0) {
                RaPolyTex(V, q, 4, ka, kb, cw[ka], kb >= 0 ? cw[kb] : 0.0f);
            } else {
                // Nothing saw it, or only through something else: the box's
                // own colour (RaBoxColours), shaded by which way it faces -
                // never the projection of whatever stood in front of it.
                const float shade = (nrm[1] < -0.5f) ? 1.0f : (nrm[1] > 0.5f ? 0.6f : 0.85f);
                const float r = (self < RAID_MAX_BOX ? s_boxCol[self][0] : fr) * shade;
                const float g = (self < RAID_MAX_BOX ? s_boxCol[self][1] : fg) * shade;
                const float b = (self < RAID_MAX_BOX ? s_boxCol[self][2] : fb) * shade;
                for (int c = 0; c < 4; c++) { q[c].cr = r; q[c].cg = g; q[c].cb = b; }
                RaPoly(V, q, 4);
            }
        }
    }
}

// CUSTOM: a plate ABOVE the floor is a ceiling, and a ceiling is seen from
// below only. Seen from above it is the lid on the room - and a room camera
// from the game sits above its walls (a background is a cut-away: the bathroom's
// camera 0 hangs at y -7812 over walls 4000 high), so drawing it from there
// put a black lid over the whole view.
static int RaCeilingHidden(const RaView& V, float y)
{
    return y < -100.0f && V.fromY < y;     // Y is negative upwards: the eye is above it
}

static void RaProjBox(const RaView& V, int self, const RaidBox* B)
{
    const float x0 = (float)B->x0, x1 = (float)B->x1;
    const float y0 = (float)B->y0, y1 = (float)B->y1;
    const float z0 = (float)B->z0, z1 = (float)B->z1;
    const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const float fr = B->shade * (float)B->tr * (1.0f / 255.0f);
    const float fg = B->shade * (float)B->tg * (1.0f / 255.0f);
    const float fb = B->shade * (float)B->tb * (1.0f / 255.0f);
    static const float kUp[3]   = { 0, -1, 0 }, kDown[3] = { 0, 1, 0 };
    static const float kXneg[3] = { -1, 0, 0 }, kXpos[3] = { 1, 0, 0 };
    static const float kZneg[3] = { 0, 0, -1 }, kZpos[3] = { 0, 0, 1 };

    if (dy == 0.0f) {                       // a plate: one face
        if (RaCeilingHidden(V, y0)) return;
        RaGridProj(V, self, x0, y0, z0,  dx, 0, 0,  0, 0, dz, kUp, fr, fg, fb);
        return;
    }
    if (RaFacing(V, -1.0f, 1, (x0+x1)*0.5f, y0, (z0+z1)*0.5f))
        RaGridProj(V, self, x0, y0, z0,  dx, 0, 0,  0, 0, dz, kUp, fr, fg, fb);
    if (RaFacing(V,  1.0f, 1, (x0+x1)*0.5f, y1, (z0+z1)*0.5f))
        RaGridProj(V, self, x0, y1, z0,  dx, 0, 0,  0, 0, dz, kDown, fr, fg, fb);
    if (RaFacing(V, -1.0f, 0, x0, (y0+y1)*0.5f, (z0+z1)*0.5f))
        RaGridProj(V, self, x0, y0, z0,  0, dy, 0,  0, 0, dz, kXneg, fr, fg, fb);
    if (RaFacing(V,  1.0f, 0, x1, (y0+y1)*0.5f, (z0+z1)*0.5f))
        RaGridProj(V, self, x1, y0, z0,  0, dy, 0,  0, 0, dz, kXpos, fr, fg, fb);
    if (RaFacing(V, -1.0f, 2, (x0+x1)*0.5f, (y0+y1)*0.5f, z0))
        RaGridProj(V, self, x0, y0, z0,  dx, 0, 0,  0, dy, 0, kZneg, fr, fg, fb);
    if (RaFacing(V,  1.0f, 2, (x0+x1)*0.5f, (y0+y1)*0.5f, z1))
        RaGridProj(V, self, x0, y0, z1,  dx, 0, 0,  0, dy, 0, kZpos, fr, fg, fb);
}



// ---------------------------------------------------------------------------
// MATERIALS - surfaces covered with a tiled texture of their own.
//
// The room is modelled, not projected: a backdrop is a reference for what it
// looks like, and its surfaces are made of materials - wood boards, floor
// tiles, enamel - each a texture tools/build_raid_textures.py cut out of the
// game's own pictures, straightened and made seamless:
//     Data/raidtex/t<n>.bin  =  'RTEX' + u32 w + u32 h + RGBA
// A material repeats every `tile` world units, laid out by WORLD position
// (so the boards run on across two boxes that meet), on the two axes the face
// spans; it is sampled with repeat (MARNI_SAMPLER_LINEAR_WRAP) and lit by the
// arena's own per-vertex light (RaShade), the same as an untextured box.
// ---------------------------------------------------------------------------
#define RA_MAT_FILES  64

static MarniHandle s_matTex[RA_MAT_FILES];
static int         s_matState[RA_MAT_FILES];          // 0 not tried, 1 loaded, -1 failed
static float       s_matAspect[RA_MAT_FILES];         // h / w: a texture need not be square
static float       s_mtris[RA_MAX_TRIS * 3 * RA_FLOATS];
static int         s_mcount = 0;
static int         s_mcur   = -1;                      // the material s_mtris holds

static void RaMatForget(void)
{
    for (int i = 0; i < RA_MAT_FILES; i++) {
        if (s_matTex[i] != MARNI_NULL_HANDLE && s_dx != NULL) s_dx->DestroyTexture(s_matTex[i]);
        s_matTex[i] = MARNI_NULL_HANDLE;
        s_matState[i] = 0;
    }
}

static MarniHandle RaMatGet(int id)
{
    if (id <= 0 || id >= RA_MAT_FILES) return MARNI_NULL_HANDLE;
    if (s_matState[id] != 0) return s_matTex[id];
    s_matState[id] = -1;
    char path[96], rooted[260];
    sprintf(path, GAME_DATA_ROOT "Data/raidtex/t%d.bin", id);
    size_t size = 0;
    unsigned char* blob = (unsigned char*)plat_file_read_all(ResolveAssetRoot(path, rooted, sizeof(rooted)), &size);
    if (blob != NULL && size >= 12 && memcmp(blob, "RTEX", 4) == 0) {
        const unsigned int w = *(const unsigned int*)(blob + 4);
        const unsigned int h = *(const unsigned int*)(blob + 8);
        if (w > 0 && h > 0 && w <= 4096 && h <= 4096 && size >= 12 + (size_t)w * h * 4
            && MarniCreateTexture((int)w, (int)h, 32, blob + 12, &s_matTex[id])) {
            s_matState[id] = 1;
            s_matAspect[id] = (float)h / (float)w;
        }
    }
    free(blob);
    return s_matTex[id];
}

static void RaMatFlush(void)
{
    if (s_mcount > 0 && s_dx != NULL && s_mcur > 0) {
        s_dx->DrawTriangles3D(s_mtris, s_mcount, s_matTex[s_mcur],
                              MARNI_SAMPLER_LINEAR_WRAP, MARNI_BLEND_DISABLE, true);
    }
    s_mcount = 0;
}

// One polygon in material `tex`: clipped to the near plane, colours carried
// through the clip (they are the lighting), UVs taken from each output
// vertex's world position on the axes `au`, `av`.
static void RaPolyMat(const RaView& V, const RaVert* in, int n, int tex,
                      int au, int av, float tile, const float* origin)
{
    if (RaMatGet(tex) == MARNI_NULL_HANDLE) { RaPoly(V, in, n); return; }
    if (tex != s_mcur) { RaMatFlush(); s_mcur = tex; }

    RaVert cut[10];                                   // the mirror pass
    if (s_clipAxis >= 0) {
        n = RaClipMirror(in, n, cut);
        if (n < 3) return;
        in = cut;
    }

    RaVert out[8];
    float  vz[8];
    int    m = 0;
    for (int i = 0; i < n && m < 8; i++) {
        const RaVert& a = in[i];
        const RaVert& b = in[(i + 1) % n];
        const float da = RaDepth(V, a), db = RaDepth(V, b);
        if (da >= RA_NEAR) { out[m] = a; vz[m] = da; m++; }
        if ((da >= RA_NEAR) != (db >= RA_NEAR) && m < 8) {
            const float t = (RA_NEAR - da) / (db - da);
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
    if (s_mcount + (m - 2) > RA_MAX_TRIS) RaMatFlush();

    // `tile` is the world width of one repeat; its height follows the
    // texture's own proportions, so a door 1:2 is a door 1:2 on the wall.
    const float inv  = 1.0f / (tile > 1.0f ? tile : 1.0f);
    const float invV = inv / (s_matAspect[tex] > 0.01f ? s_matAspect[tex] : 1.0f);
    float sx[8], sy[8];
    for (int i = 0; i < m; i++) RaProject(V, out[i], vz[i], &sx[i], &sy[i]);
    for (int i = 1; i + 1 < m; i++) {
        const int idx[3] = { 0, i, i + 1 };
        for (int e = 0; e < 3; e++) {
            const int j = idx[e];
            const float w[3] = { out[j].x, out[j].y, out[j].z };
            float* p = &s_mtris[s_mcount * 3 * RA_FLOATS + e * RA_FLOATS];
            p[0] = sx[j]; p[1] = sy[j];
            p[2] = TmdViewZToNdc(vz[j]); p[3] = vz[j];
            p[4] = (w[au] - (origin ? origin[au] : 0.0f)) * inv;
            p[5] = (w[av] - (origin ? origin[av] : 0.0f)) * invV;
            p[6] = out[j].cr; p[7] = out[j].cg; p[8] = out[j].cb; p[9] = 1.0f;
        }
        s_mcount++;
    }
}

// CUSTOM: a material triangle whose UVs come from its own model-space corners
// (`loc`), not from the world - for a mesh that moves (a door). Clipped to the
// near plane like RaPolyMat, the model coordinates carried through the clip.
static void RaPolyMatLocal(const RaView& V, const RaVert* in, const float loc[3][3],
                           int tex, int au, int av, float tile)
{
    if (RaMatGet(tex) == MARNI_NULL_HANDLE) { RaPoly(V, in, 3); return; }
    if (tex != s_mcur) { RaMatFlush(); s_mcur = tex; }

    RaVert out[8];
    float  ol[8][3];
    float  vz[8];
    int    m = 0;
    for (int i = 0; i < 3 && m < 8; i++) {
        const int j = (i + 1) % 3;
        const RaVert& a = in[i];
        const RaVert& b = in[j];
        const float da = RaDepth(V, a), db = RaDepth(V, b);
        if (da >= RA_NEAR) {
            out[m] = a; vz[m] = da;
            ol[m][0] = loc[i][0]; ol[m][1] = loc[i][1]; ol[m][2] = loc[i][2];
            m++;
        }
        if ((da >= RA_NEAR) != (db >= RA_NEAR) && m < 8) {
            const float t = (RA_NEAR - da) / (db - da);
            RaVert c;
            c.x  = a.x  + (b.x  - a.x)  * t;
            c.y  = a.y  + (b.y  - a.y)  * t;
            c.z  = a.z  + (b.z  - a.z)  * t;
            c.cr = a.cr + (b.cr - a.cr) * t;
            c.cg = a.cg + (b.cg - a.cg) * t;
            c.cb = a.cb + (b.cb - a.cb) * t;
            out[m] = c; vz[m] = RA_NEAR;
            for (int k = 0; k < 3; k++) ol[m][k] = loc[i][k] + (loc[j][k] - loc[i][k]) * t;
            m++;
        }
    }
    if (m < 3) return;
    if (s_mcount + (m - 2) > RA_MAX_TRIS) RaMatFlush();
    const float inv  = 1.0f / (tile > 1.0f ? tile : 1.0f);
    const float invV = inv / (s_matAspect[tex] > 0.01f ? s_matAspect[tex] : 1.0f);
    float sx[8], sy[8];
    for (int i = 0; i < m; i++) RaProject(V, out[i], vz[i], &sx[i], &sy[i]);
    for (int i = 1; i + 1 < m; i++) {
        const int idx[3] = { 0, i, i + 1 };
        for (int e = 0; e < 3; e++) {
            const int j = idx[e];
            float* p = &s_mtris[s_mcount * 3 * RA_FLOATS + e * RA_FLOATS];
            p[0] = sx[j]; p[1] = sy[j];
            p[2] = TmdViewZToNdc(vz[j]); p[3] = vz[j];
            p[4] = ol[j][au] * inv;
            p[5] = ol[j][av] * invV;
            p[6] = out[j].cr; p[7] = out[j].cg; p[8] = out[j].cb; p[9] = 1.0f;
        }
        s_mcount++;
    }
}

// A material face, cut into the usual light cells (RA_CELL: the lighting is
// per vertex) and lit like any box face. `au`/`av` are the world axes the
// face spans: the texture's across and down.
// Where a face running from world coordinate a0 for `len` along one axis is
// cut: at every multiple of `cell` strictly inside it, plus its two ends, as
// fractions 0..1 in t[]. Returns the number of cells (at most maxCells).
static int RaCellCuts(float a0, float len, float* t, float cell, int maxCells)
{
    int n = 0;
    t[0] = 0.0f;
    if (len > 1.0f) {
        const float a1 = a0 + len;
        for (float w = (floorf(a0 / cell) + 1.0f) * cell; w < a1 - 1.0f && n < maxCells - 1; w += cell) {
            if (w > a0 + 1.0f) t[++n] = (w - a0) / len;
        }
    }
    t[++n] = 1.0f;
    return n;
}

static void RaGridMat(const RaView& V, int tex, float tile, int au, int av,
                      const float* origin, const float* nrm,
                      float ax, float ay, float az,
                      float ux, float uy, float uz,
                      float vx, float vy, float vz, float br)
{
    // The cells are cut on a WORLD grid, not the face's own: two
    // boxes that meet edge to edge (a wall cut round the mirror's glass) then
    // have their light evaluated at the same points along the seam, and the
    // per-vertex light runs on across it instead of stepping.
    const float ua = (ux != 0.0f) ? ax : (uy != 0.0f ? ay : az);
    const float va = (vx != 0.0f) ? ax : (vy != 0.0f ? ay : az);
    // A floor is cut finer: it is what the contact shadows fall on, and they
    // are evaluated per vertex like the rest of the light.
    // Walls too: a lamp near a wall makes its light change fast along it, and
    // a coarse grid draws that change as creases along its rows.
    const int floor_ = nrm[1] < -0.5f;
    const float cell = RA_CELL * 0.25f;
    const int maxCells = 40;
    float tu[42], tv[42];
    const int nu = RaCellCuts(ua, ux + uy + uz, tu, cell, maxCells);
    const int nv = RaCellCuts(va, vx + vy + vz, tv, cell, maxCells);
    for (int i = 0; i < nu; i++) {
        const float a0 = tu[i], a1 = tu[i + 1];
        for (int j = 0; j < nv; j++) {
            const float b0 = tv[j], b1 = tv[j + 1];
            RaVert q[4];
            q[0].x = ax + ux*a0 + vx*b0; q[0].y = ay + uy*a0 + vy*b0; q[0].z = az + uz*a0 + vz*b0;
            q[1].x = ax + ux*a1 + vx*b0; q[1].y = ay + uy*a1 + vy*b0; q[1].z = az + uz*a1 + vz*b0;
            q[2].x = ax + ux*a1 + vx*b1; q[2].y = ay + uy*a1 + vy*b1; q[2].z = az + uz*a1 + vz*b1;
            q[3].x = ax + ux*a0 + vx*b1; q[3].y = ay + uy*a0 + vy*b1; q[3].z = az + uz*a0 + vz*b1;
            for (int c = 0; c < 4; c++) RaShadeLit(V, q[c], nrm, br, floor_);
            RaPolyMat(V, q, 4, tex, au, av, tile, origin);
        }
    }
}

static int RaCeilingHidden(const RaView& V, float y);

// A tbox: each face in its material, on the axes it spans (X and Z for the
// top and bottom, Z or X with Y down for the sides), with the same per-face
// light falloff as a plain box so a room keeps its shape.
static void RaMatBox(const RaView& V, const RaidBox* B)
{
    const float x0 = (float)B->x0, x1 = (float)B->x1;
    const float y0 = (float)B->y0, y1 = (float)B->y1;
    const float z0 = (float)B->z0, z1 = (float)B->z1;
    const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const int tex = B->tex;
    const float tile = (float)B->tile;
    const float br = B->shade;
    // The material starts at the box's own corner (top, low X, low Z), so a
    // one-off picture - the door - sits in its box exactly, wherever the box is.
    const float corner[3] = { x0, y0, z0 };
    const float* o = (B->flags & RAID_BOX_WORLDUV) ? NULL : corner;
    static const float nUp[3] = { 0, -1, 0 }, nDown[3] = { 0, 1, 0 };
    static const float nXn[3] = { -1, 0, 0 }, nXp[3] = { 1, 0, 0 };
    static const float nZn[3] = { 0, 0, -1 }, nZp[3] = { 0, 0, 1 };
    if (dy == 0.0f) {
        if (RaCeilingHidden(V, y0)) return;
        RaGridMat(V, tex, tile, 0, 2, o, V.fromY < y0 ? nUp : nDown,
                  x0, y0, z0,  dx, 0, 0,  0, 0, dz, br);
        return;
    }
    if (RaFacing(V, -1.0f, 1, (x0+x1)*0.5f, y0, (z0+z1)*0.5f))
        RaGridMat(V, tex, tile, 0, 2, o, nUp,   x0, y0, z0,  dx, 0, 0,  0, 0, dz, br);
    if (RaFacing(V,  1.0f, 1, (x0+x1)*0.5f, y1, (z0+z1)*0.5f))
        RaGridMat(V, tex, tile, 0, 2, o, nDown, x0, y1, z0,  dx, 0, 0,  0, 0, dz, br);
    if (RaFacing(V, -1.0f, 0, x0, (y0+y1)*0.5f, (z0+z1)*0.5f))
        RaGridMat(V, tex, tile, 2, 1, o, nXn,   x0, y0, z0,  0, dy, 0,  0, 0, dz, br);
    if (RaFacing(V,  1.0f, 0, x1, (y0+y1)*0.5f, (z0+z1)*0.5f))
        RaGridMat(V, tex, tile, 2, 1, o, nXp,   x1, y0, z0,  0, dy, 0,  0, 0, dz, br);
    if (RaFacing(V, -1.0f, 2, (x0+x1)*0.5f, (y0+y1)*0.5f, z0))
        RaGridMat(V, tex, tile, 0, 1, o, nZn,   x0, y0, z0,  dx, 0, 0,  0, dy, 0, br);
    if (RaFacing(V,  1.0f, 2, (x0+x1)*0.5f, (y0+y1)*0.5f, z1))
        RaGridMat(V, tex, tile, 0, 1, o, nZp,   x0, y0, z1,  dx, 0, 0,  0, dy, 0, br);
}

// ---------------------------------------------------------------------------
// PLACED MODELS - the furniture, as real low-poly shapes.
//
// Boxes cannot stand in for a bath: seen from beside it, a box is a crate with
// a picture of a bath on it, and the picture of the bath spills onto the wall
// wherever the box is not where the bath is. So the furniture is modelled
// (tools/build_raid_meshes.py writes the .obj files, in game units, Y negative
// up) and placed by the level's `mesh` lines, and every triangle is textured
// the way a projected box is: from the backdrop that sees it most squarely,
// unhidden; a triangle no backdrop saw takes the model's own average colour.
//
// The .obj subset read: `v x y z` and `f a b c [d ...]` (1-based indices, any
// `/vt/vn` suffix ignored, polygons fanned). Files are read whole into a
// buffer of their own size and parsed defensively; a bad file is a missing
// model, never a crash.
// ---------------------------------------------------------------------------
#define RA_MESH_FILES   64
#define RA_MESH_VMAX    8192
#define RA_MESH_TMAX    8192

struct RaMeshFile {
    int             state;          // 0 not tried, 1 loaded, -1 failed
    int             nv, nt;
    float*          v;              // nv * 3
    unsigned short* t;              // nt * 3
    float*          n;              // nv * 3: smooth normals, for the light
};
static RaMeshFile s_meshFile[RA_MESH_FILES];

static void RaMeshForget(void)
{
    for (int i = 0; i < RA_MESH_FILES; i++) {
        free(s_meshFile[i].v);
        free(s_meshFile[i].t);
        free(s_meshFile[i].n);
        memset(&s_meshFile[i], 0, sizeof(s_meshFile[i]));
    }
    for (int m = 0; m < RAID_MAX_MESH; m++) s_meshColValid[m] = 0;
}

static RaMeshFile* RaMeshGet(int id)
{
    if (id < 0 || id >= RA_MESH_FILES) return NULL;
    RaMeshFile* F = &s_meshFile[id];
    if (F->state != 0) return F->state > 0 ? F : NULL;
    F->state = -1;

    char path[96], rooted[260];
    sprintf(path, GAME_DATA_ROOT "Data/raidmesh/m%d.obj", id);
    size_t size = 0;
    char* text = (char*)plat_file_read_all(ResolveAssetRoot(path, rooted, sizeof(rooted)), &size);
    if (text == NULL) return NULL;

    F->v = (float*)malloc(RA_MESH_VMAX * 3 * sizeof(float));
    F->t = (unsigned short*)malloc(RA_MESH_TMAX * 3 * sizeof(unsigned short));
    if (F->v == NULL || F->t == NULL) { free(text); return NULL; }

    size_t i = 0;
    while (i < size) {
        size_t e = i;
        while (e < size && text[e] != '\n' && text[e] != '\r') e++;
        char line[512];
        size_t n = e - i < sizeof(line) - 1 ? e - i : sizeof(line) - 1;
        memcpy(line, text + i, n);
        line[n] = '\0';
        if (line[0] == 'v' && line[1] == ' ' && F->nv < RA_MESH_VMAX) {
            float x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                F->v[F->nv * 3 + 0] = x; F->v[F->nv * 3 + 1] = y; F->v[F->nv * 3 + 2] = z;
                F->nv++;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            int idx[16], k = 0;
            char* p = line + 2;
            while (*p && k < 16) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                const int vi = atoi(p);
                if (vi >= 1 && vi <= F->nv) idx[k++] = vi - 1;
                while (*p && *p != ' ' && *p != '\t') p++;
            }
            for (int f = 1; f + 1 < k && F->nt < RA_MESH_TMAX; f++) {
                F->t[F->nt * 3 + 0] = (unsigned short)idx[0];
                F->t[F->nt * 3 + 1] = (unsigned short)idx[f];
                F->t[F->nt * 3 + 2] = (unsigned short)idx[f + 1];
                F->nt++;
            }
        }
        i = e;
        while (i < size && (text[i] == '\n' || text[i] == '\r')) i++;
    }
    free(text);
    if (F->nv == 0 || F->nt == 0) return NULL;

    // Smooth normals: every face's area-weighted normal summed into its
    // corners. Where a model wants a hard edge it has separate vertices.
    F->n = (float*)calloc((size_t)F->nv * 3, sizeof(float));
    if (F->n == NULL) return NULL;
    for (int t = 0; t < F->nt; t++) {
        const float* a = &F->v[F->t[t*3+0] * 3];
        const float* b = &F->v[F->t[t*3+1] * 3];
        const float* c = &F->v[F->t[t*3+2] * 3];
        const float u[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
        const float w[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
        const float fn[3] = { u[1]*w[2] - u[2]*w[1], u[2]*w[0] - u[0]*w[2], u[0]*w[1] - u[1]*w[0] };
        for (int e = 0; e < 3; e++)
            for (int k = 0; k < 3; k++) F->n[F->t[t*3+e] * 3 + k] += fn[k];
    }
    for (int v = 0; v < F->nv; v++) {
        float* q = &F->n[v * 3];
        const float L = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2]);
        if (L > 1e-6f) { q[0] /= L; q[1] /= L; q[2] /= L; } else { q[1] = -1.0f; }
    }
    F->state = 1;
    return F;
}

// A model vertex into the world: scale, turn about Y the way a facing turns
// (RotMatrixY), then move.
static void RaMeshXform(const RaidMesh* M, const float* in, float* out)
{
    const float k = (float)M->scale / 100.0f;
    const float a = (float)(M->yaw & 0xFFF) * (6.2831853f / 4096.0f);
    const float c = cosf(a), sn = sinf(a);
    const float x = in[0] * k, y = in[1] * k, z = in[2] * k;
    out[0] = (float)M->x + c * x + sn * z;
    out[1] = (float)M->y + y;
    out[2] = (float)M->z - sn * x + c * z;
}

// This frame's bounds of every placed model, for RaBgOccluded.
static void RaMeshPrepare(void)
{
    for (int m = 0; m < RAID_MAX_MESH; m++) s_meshLive[m] = 0;
    for (int m = 0; m < g_raidLevel.nmesh && m < RAID_MAX_MESH; m++) {
        const RaidMesh* M = &g_raidLevel.mesh[m];
        const RaMeshFile* F = RaMeshGet(M->id);
        if (F == NULL) continue;
        float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
        for (int v = 0; v < F->nv; v++) {
            float w[3];
            RaMeshXform(M, &F->v[v * 3], w);
            for (int a = 0; a < 3; a++) { if (w[a] < lo[a]) lo[a] = w[a]; if (w[a] > hi[a]) hi[a] = w[a]; }
        }
        for (int a = 0; a < 3; a++) { s_meshLo[m][a] = lo[a]; s_meshHi[m][a] = hi[a]; }
        s_meshLive[m] = 1;
    }
}

// The backdrop that sees this triangle best - squarely, wholly in front of its
// camera, unhidden - or -1. The same rule as a box cell, minus the viewer term:
// a model is looked at from all round, never from a room camera's own eye.
static int RaTriPick(const float* a, const float* b, const float* c, const float n[3], int self)
{
    const float ctr[3] = { (a[0]+b[0]+c[0]) / 3.0f, (a[1]+b[1]+c[1]) / 3.0f, (a[2]+b[2]+c[2]) / 3.0f };
    int best = -1;
    float bestScore = 0.0f;
    for (int k = 0; k < g_raidLevel.nbgsrc && k < RAID_MAX_BGSRC; k++) {
        const RaSrc* S = &s_src[k];
        if (!S->ok) continue;
        const float d[3] = { S->fx - ctr[0], S->fy - ctr[1], S->fz - ctr[2] };
        const float L = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (L < 1.0f) continue;
        const float facing = (n[0]*d[0] + n[1]*d[1] + n[2]*d[2]) / L;
        if (facing < 0.12f) continue;
        float sx, sy;
        if (!RaSrcProject(S, a[0], a[1], a[2], &sx, &sy) || !RaSrcProject(S, b[0], b[1], b[2], &sx, &sy)
            || !RaSrcProject(S, c[0], c[1], c[2], &sx, &sy)) continue;
        const float from[3] = { ctr[0] + n[0] * 20.0f, ctr[1] + n[1] * 20.0f, ctr[2] + n[2] * 20.0f };
        const float eye[3]  = { S->fx, S->fy, S->fz };
        if (RaBgOccluded(from, eye, self)) continue;
        const float f = (RaFrameSoft(S, a[0], a[1], a[2]) + RaFrameSoft(S, b[0], b[1], b[2])
                       + RaFrameSoft(S, c[0], c[1], c[2])) / 3.0f;
        const float score = facing * f;
        if (score > bestScore) { bestScore = score; best = k; }
    }
    return best;
}

static void RaTriNormal(const float* a, const float* b, const float* c, float n[3])
{
    const float u[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    const float v[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
    n[0] = u[1]*v[2] - u[2]*v[1];
    n[1] = u[2]*v[0] - u[0]*v[2];
    n[2] = u[0]*v[1] - u[1]*v[0];
    const float L = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (L > 1e-6f) { n[0] /= L; n[1] /= L; n[2] /= L; }
}

// The model's own colour: the average of its triangles' centres as the
// backdrops see them unhidden. Computed once per model and level.
static void RaMeshColour(int m, const RaidMesh* M, const RaMeshFile* F)
{
    double sum[3] = { 0, 0, 0 };
    int count = 0;
    for (int t = 0; t < F->nt; t++) {
        float a[3], b[3], c[3], n[3];
        RaMeshXform(M, &F->v[F->t[t*3+0] * 3], a);
        RaMeshXform(M, &F->v[F->t[t*3+1] * 3], b);
        RaMeshXform(M, &F->v[F->t[t*3+2] * 3], c);
        RaTriNormal(a, b, c, n);
        const int k = RaTriPick(a, b, c, n, -2 - m);
        if (k < 0 || s_bgPix[k] == NULL) continue;
        float sx, sy;
        if (!RaSrcProject(&s_src[k], (a[0]+b[0]+c[0]) / 3, (a[1]+b[1]+c[1]) / 3, (a[2]+b[2]+c[2]) / 3, &sx, &sy)) continue;
        if (sx < 0 || sx >= RA_BG_W || sy < 0 || sy >= RA_BG_H) continue;
        const unsigned int px = s_bgPix[k][(int)sy * RA_BG_W + (int)sx];
        sum[0] += (px & 0xFF); sum[1] += ((px >> 8) & 0xFF); sum[2] += ((px >> 16) & 0xFF);
        count++;
    }
    if (count > 0) {
        for (int i = 0; i < 3; i++) s_meshCol[m][i] = (float)(sum[i] / count / 255.0);
    } else {
        s_meshCol[m][0] = s_meshCol[m][1] = s_meshCol[m][2] = 0.35f;
    }
    s_meshColValid[m] = 1;
}

static void RaDrawMeshes(const RaView& V)
{
    for (int m = 0; m < g_raidLevel.nmesh && m < RAID_MAX_MESH; m++) {
        const RaidMesh* M = &g_raidLevel.mesh[m];
        if ((M->flags & RAID_BOX_DRAW) == 0) continue;
        const RaMeshFile* F = RaMeshGet(M->id);
        if (F == NULL) continue;
        if ((M->flags & RAID_BOX_PROJ) && !s_meshColValid[m]) RaMeshColour(m, M, F);

        for (int t = 0; t < F->nt; t++) {
            float w[3][3], n[3];
            for (int e = 0; e < 3; e++) RaMeshXform(M, &F->v[F->t[t*3+e] * 3], w[e]);
            RaTriNormal(w[0], w[1], w[2], n);
            // Back faces away from the eye are skipped (the files are closed
            // and wound outward).
            const float ev[3] = { V.fromX - w[0][0], V.fromY - w[0][1], V.fromZ - w[0][2] };
            if (n[0]*ev[0] + n[1]*ev[1] + n[2]*ev[2] <= 0.0f) continue;

            RaVert q[3];
            for (int e = 0; e < 3; e++) {
                q[e].x = w[e][0]; q[e].y = w[e][1]; q[e].z = w[e][2];
                q[e].cr = q[e].cg = q[e].cb = 1.0f;
            }
            if (M->tex > 0) {
                // Each triangle takes the material on the two world axes its
                // normal is NOT closest to - planar mapping by dominant axis,
                // so a curved shape is never stretched along its own normal.
                const float ax_ = fabsf(n[0]), ay_ = fabsf(n[1]), az_ = fabsf(n[2]);
                int au, av;
                if (ay_ >= ax_ && ay_ >= az_) { au = 0; av = 2; }
                else if (ax_ >= az_)          { au = 2; av = 1; }
                else                           { au = 0; av = 1; }
                if (M->flags & RAID_MESH_LOCALUV) {
                    // A moving model (a door) keeps its picture: the axes and
                    // the UVs come from the MODEL's own coordinates, so the
                    // material turns with it instead of sliding over it.
                    const float* a0 = &F->v[F->t[t*3+0] * 3];
                    const float* b0 = &F->v[F->t[t*3+1] * 3];
                    const float* c0 = &F->v[F->t[t*3+2] * 3];
                    float ln[3];
                    RaTriNormal(a0, b0, c0, ln);
                    const float lx = fabsf(ln[0]), ly = fabsf(ln[1]), lz = fabsf(ln[2]);
                    if (ly >= lx && ly >= lz) { au = 0; av = 2; }
                    else if (lx >= lz)        { au = 2; av = 1; }
                    else                      { au = 0; av = 1; }
                    float lv[3][3];
                    for (int e = 0; e < 3; e++)
                        for (int k = 0; k < 3; k++) lv[e][k] = F->v[F->t[t*3+e] * 3 + k];
                    RaVert lq[3];
                    for (int e = 0; e < 3; e++) {
                        const float* vn = &F->n[F->t[t*3+e] * 3];
                        const float a = (float)(M->yaw & 0xFFF) * (6.2831853f / 4096.0f);
                        const float c = cosf(a), sn = sinf(a);
                        const float wn[3] = { c * vn[0] + sn * vn[2], vn[1], -sn * vn[0] + c * vn[2] };
                        RaShadeLit(V, q[e], wn, 1.0f);
                        lq[e] = q[e];
                    }
                    RaPolyMatLocal(V, lq, lv, M->tex, au, av, (float)M->tile);
                    continue;
                }
                if (M->flags & RAID_MESH_GLOW) {
                    // A lamp: its own light, only the fog over it.
                    for (int e = 0; e < 3; e++) { q[e].cr = q[e].cg = q[e].cb = 1.0f; }
                } else {
                    const float a = (float)(M->yaw & 0xFFF) * (6.2831853f / 4096.0f);
                    const float c = cosf(a), sn = sinf(a);
                    for (int e = 0; e < 3; e++) {
                        const float* vn = &F->n[F->t[t*3+e] * 3];
                        const float wn[3] = { c * vn[0] + sn * vn[2], vn[1], -sn * vn[0] + c * vn[2] };
                        RaShadeLit(V, q[e], wn, 1.0f);
                    }
                }
                RaPolyMat(V, q, 3, M->tex, au, av, (float)M->tile, NULL);
                continue;
            }
            const int k = (M->flags & RAID_BOX_PROJ) ? RaTriPick(w[0], w[1], w[2], n, -2 - m) : -1;
            if (k >= 0) {
                RaPolyTex(V, q, 3, k, -1, 0.0f, 0.0f);
            } else {
                const float shade = (n[1] < -0.5f) ? 1.0f : (n[1] > 0.5f ? 0.6f : 0.85f);
                for (int e = 0; e < 3; e++) {
                    q[e].cr = s_meshCol[m][0] * shade;
                    q[e].cg = s_meshCol[m][1] * shade;
                    q[e].cb = s_meshCol[m][2] * shade;
                }
                RaPoly(V, q, 3);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Pickups.
//
// A pickup has no model: the item models a story room uses are loaded by that
// room's script out of its own PAK, and this room has neither. So it is drawn
// as what it is - a marker. A small box, bobbing, over a bright patch on the
// floor that says where it is from across the room, tinted by what kind of item
// it is, and brighter while it is in reach so the player knows ACTION will take
// it. Cheap, readable, and it costs the batch seven quads.
// ---------------------------------------------------------------------------
#define RA_ITEM_HALF     110.0f     // half the marker's width
#define RA_ITEM_TALL     260.0f
#define RA_ITEM_FLOAT    620.0f     // how high it hovers (Y is negative upwards)
#define RA_ITEM_BOB       70.0f
#define RA_ITEM_PATCH    460.0f     // half the floor patch under it

static int s_itemPhase = 0;         // frames, for the bob and the pulse

// The marker's colour, by what the item is. The ranges are the menu's own
// categories (MainMenu.cpp), so a weapon on the floor is the colour the
// inventory screen would give it.
static void RaItemTint(unsigned char id, float* r, float* g, float* b)
{
    if (id >= ITEM_KNIFE && id <= ITEM_ROCKET_LAUNCHER) { *r=0.78f; *g=0.84f; *b=0.95f; return; }
    if (ITEM_IS_CUSTOM_PISTOL(id) || id == ITEM_INGRAM || id == ITEM_MINIMI)
                                                        { *r=0.78f; *g=0.84f; *b=0.95f; return; }
    if (id >= ITEM_CLIP && id <= ITEM_FLAME_ROUNDS)     { *r=0.95f; *g=0.80f; *b=0.42f; return; }
    if (id >= ITEM_FIRST_AID_SPRAY && id <= ITEM_MIX_2GREEN_RED)
                                                        { *r=0.55f; *g=0.92f; *b=0.62f; return; }
    if (id >= ITEM_SWORD_KEY && id <= ITEM_DESK_KEY)    { *r=0.95f; *g=0.86f; *b=0.45f; return; }
    *r = 0.88f; *g = 0.88f; *b = 0.92f;
}

// ---------------------------------------------------------------------------
// CUSTOM: co-op nameplates.
//
// A label over each player's head, in world space, so it tracks him rather than
// sitting in a corner of the HUD.
//
// Y is NEGATIVE upwards in this engine, so the anchor is the player's matrix
// translation MINUS the head height - getting that sign wrong puts the name
// under the floor, which is the first thing to check if nothing appears.
//
// Depth 700 puts it after the 3D pass (anything below PENDING_SCENE_DEPTH,
// 0x400, draws then) and behind the game's own item icons, which sit at
// depth*16 + 500 = 644..676. Lower would draw the name over the inventory.
//
// Nothing is drawn for a player who is off camera: RaProject's caller has
// already rejected vz below the near plane, and a name whose anchor is behind
// the camera would otherwise smear across the screen.
// ---------------------------------------------------------------------------
// How far ABOVE the head joint the label floats. Y is negative upwards, so this
// is subtracted. Small, because the anchor is already the head rather than a
// guess at the model's height.
#define RA_NAME_CLEAR_Y    450.0f

// Fallback used only before the joints exist: the head joint's world matrix is
// filled by EntityComputeJointWorldMatrices during the draw pass, so on the very
// first frame it can still be zero. Measured from the feet.
#define RA_NAME_HEAD_Y    2200.0f
#define RA_NAME_DEPTH     700u
#define RA_NAME_SCALE     0.9f
#define RA_NAME_COLOR     0xFFE8E8E8u

static void RaDrawNames(const RaView& V)
{
    if (!g_coopActive) return;
    if (UiAtlas_Ready() == 0) return;

    const float k = UiAtlas_Scale() * RA_NAME_SCALE;

    for (int i = 0; i < RAID_PLAYERS; i++) {
        if (Coop_IsZombie(i)) continue;   // his body is an entity now, not here
        const PlayerEntity* p = &g_players[i];

        // Anchor on the HEAD JOINT, not on a fixed offset from the feet. The
        // first cut used the matrix translation minus a constant and the label
        // landed at chest height, because that constant was a guess at how tall
        // the model is. jointsStructs[1] is the head - the same joint
        // enemy_hit_reaction_head lifts its blood billboard to
        // (WeaponDamage.cpp:1764-1775) - and its world matrix carries where the
        // head actually IS this frame, so the label also follows a crouch or a
        // stagger instead of floating at a fixed height.
        RaVert head;
        const JointStruct* headJoint = p->jointsStructs;
        if (headJoint != 0 && headJoint[1].world.t[1] != 0) {
            head.x = (float)headJoint[1].world.t[0];
            head.y = (float)headJoint[1].world.t[1] - RA_NAME_CLEAR_Y;
            head.z = (float)headJoint[1].world.t[2];
        } else {
            head.x = (float)p->scaMatrixData.localMatrix.t[0];
            head.y = (float)p->scaMatrixData.localMatrix.t[1] - RA_NAME_HEAD_Y;
            head.z = (float)p->scaMatrixData.localMatrix.t[2];
        }

        const float vz = RaDepth(V, head);
        if (vz < 96.0f) continue;          // behind or on the near plane

        float sx, sy;
        RaProject(V, head, vz, &sx, &sy);

        const float w = UiAtlas_TextWidth(UI_FONT_BODY, g_coopName[i], k);
        UiAtlas_TextPushed(UI_FONT_BODY, g_coopName[i],
                           sx - w * 0.5f, sy, k, RA_NAME_COLOR, RA_NAME_DEPTH);
    }
}

// CUSTOM: the crosshair for RAID's L2 aim (RaidShoulderCam.cpp). It sits
// where the gun line meets the screen, not at the screen centre: the camera is
// over the shoulder, so the two are not the same point, and the gun follows
// the arms when aiming up or down. It fades in with the camera swing and with
// the arms coming up, so it never shows pointing at the floor.
#define RA_XHAIR_DEPTH   690u      // over the nameplates, under the item icons
#define RA_XHAIR_GAP     4.0f      // design px from the centre to each tick, at the closest
#define RA_XHAIR_LEN     7.0f
#define RA_XHAIR_THICK   2.0f

static void RaDrawCrosshair(const RaView& V)
{
    if (UiAtlas_Ready() == 0) return;
    const float amount = RaidShoulderCam_Amount();
    if (amount <= 0.0f) return;

    RaVert aim;
    float raised;
    if (!RaidShoulderCam_AimPoint(&aim.x, &aim.y, &aim.z, &raised)) return;

    const float a = amount * raised;
    if (a <= 0.02f) return;

    const float vz = RaDepth(V, aim);
    if (vz < 96.0f) return;
    float cx, cy;
    RaProject(V, aim, vz, &cx, &cy);

    const float k = UiAtlas_Scale();
    // The ticks sit ON the spread cone: a shot fired now can land anywhere
    // inside them and nowhere outside. V.f is the projection's focal length
    // in backbuffer pixels, so a cone of half-angle a is f * tan(a) pixels
    // across at any distance. They close in as the reticle focuses, to the
    // closed gap when it is spent; never inside that, or the dot is lost.
    float g = V.f * tanf(RaidShoulderCam_Spread());
    if (g < RA_XHAIR_GAP * k) g = RA_XHAIR_GAP * k;
    const float l = RA_XHAIR_LEN * k, t = RA_XHAIR_THICK * k;
    const unsigned int ink  = ((unsigned int)(a * 230.0f) << 24) | 0x00F2F2F2u;
    const unsigned int edge = ((unsigned int)(a * 140.0f) << 24);   // dark outline

    // An outline under each mark first, so it reads over a light wall too.
    for (int pass = 0; pass < 2; pass++) {
        const float o = pass == 0 ? k : 0.0f;
        const unsigned int c = pass == 0 ? edge : ink;
        UiAtlas_FillPushed(cx - t * 0.5f - o, cy - g - l - o, t + 2 * o, l + 2 * o, c, RA_XHAIR_DEPTH);
        UiAtlas_FillPushed(cx - t * 0.5f - o, cy + g - o,     t + 2 * o, l + 2 * o, c, RA_XHAIR_DEPTH);
        UiAtlas_FillPushed(cx - g - l - o, cy - t * 0.5f - o, l + 2 * o, t + 2 * o, c, RA_XHAIR_DEPTH);
        UiAtlas_FillPushed(cx + g - o,     cy - t * 0.5f - o, l + 2 * o, t + 2 * o, c, RA_XHAIR_DEPTH);
        UiAtlas_FillPushed(cx - t * 0.5f - o, cy - t * 0.5f - o, t + 2 * o, t + 2 * o, c, RA_XHAIR_DEPTH);
    }
}

static void RaDrawItems(const RaView& V)
{
    s_itemPhase++;

    for (int i = 0; i < g_raidLevel.nitem && i < RAID_MAX_ITEM; i++) {
        if (RaidItems_Taken(i)) continue;
        const RaidItem* I = &g_raidLevel.item[i];

        float tr, tg, tb;
        RaItemTint(I->type, &tr, &tg, &tb);

        // A 64-frame triangle wave, so the bob and the pulse need no sine and
        // no float state that a reload would have to reset.
        const int   ph   = (s_itemPhase + i * 9) & 63;
        const float wave = (ph < 32 ? (float)ph : (float)(64 - ph)) * (1.0f / 32.0f);

        const int   reach = RaidItems_Reach(i);
        const float br    = reach ? (1.05f + 0.25f * wave) : 0.80f;

        // The patch on the floor is drawn either way - it is what says where a
        // pickup is from across the room, and it carries the category colour
        // the model itself has no reason to. The floating marker box is only
        // the stand-in for a model that is not there.

        const float cx = (float)I->x, cz = (float)I->z;
        const float top = -RA_ITEM_FLOAT - RA_ITEM_BOB * wave;
        const float bot = top + RA_ITEM_TALL;

        // The floor patch. Flat, so it takes the shading a floor takes, and
        // drawn first so the marker sits over it. Twelve units clear of the
        // floor rather than one or two: the depth buffer is doing a lot of work
        // at ten thousand units out and a coplanar patch would fight with it.
        RaGrid(V, cx - RA_ITEM_PATCH, -12.0f, cz - RA_ITEM_PATCH,
               RA_ITEM_PATCH * 2.0f, 0, 0,  0, 0, RA_ITEM_PATCH * 2.0f,
               br * 0.55f, tr, tg, tb, 0);

        if (RaidItemModels_Have(I->type)) continue;   // the real model takes it from here

        // The marker. Six faces, no backface test: it is small, it floats, and
        // culling it per face would flicker as it turns.
        const float x0 = cx - RA_ITEM_HALF, x1 = cx + RA_ITEM_HALF;
        const float z0 = cz - RA_ITEM_HALF, z1 = cz + RA_ITEM_HALF;
        const float dx = x1 - x0, dz = z1 - z0, dy = bot - top;
        RaGrid(V, x0, top, z0,  dx, 0, 0,  0, 0, dz,  br,         tr, tg, tb, 0);
        RaGrid(V, x0, bot, z0,  dx, 0, 0,  0, 0, dz,  br * 0.45f, tr, tg, tb, 0);
        RaGrid(V, x0, top, z0,  0, dy, 0,  0, 0, dz,  br * 0.72f, tr, tg, tb, 0);
        RaGrid(V, x1, top, z0,  0, dy, 0,  0, 0, dz,  br * 0.72f, tr, tg, tb, 0);
        RaGrid(V, x0, top, z0,  dx, 0, 0,  0, dy, 0,  br * 0.62f, tr, tg, tb, 0);
        RaGrid(V, x0, top, z1,  dx, 0, 0,  0, dy, 0,  br * 0.62f, tr, tg, tb, 0);
    }
}

static void RaDrawBoxes(const RaView& V);
static void RaDrawMirror(const RaView& V);
static void RaDoorsUpdate(void);
static void RaDrawGlass(const RaView& V);

// This frame's view, kept for RaidArena_DrawLate - the mirror's glass goes
// over the reflected characters, and they are drawn after this function.
static RaView s_lateView;
static int    s_lateValid = 0;

// ===========================================================================
// RaidArena_Draw - once per frame, from the render flush, before the models.
// ===========================================================================
void RaidArena_Draw(void)
{
    s_lateValid = 0;
    if (g_raidMode == 0) return;
    if (g_RdtPointer == NULL) return;

    // The reload key (window_proc). Taken here rather than in the input path
    // because here the room is certainly loaded and certainly this one, so a
    // file that turns out to be broken leaves the level that is already up.
    if (g_raidReloadRequest) {
        g_raidReloadRequest = 0;
        RaMeshForget();     // CUSTOM: F7 re-reads the model and material files too
        RaMatForget();
        if (RaidLevel_Load()) {
            RaidLevel_Apply();
            Room_SetupCamera();     // just the matrix and the FOV, no room reload
        }
    }

    if (!g_raidLevel.loaded) return;

    RaDoorsUpdate();      // CUSTOM: the doors swing before anything is drawn
    RaidLevel_LightsNear(g_players[0].scaMatrixData.localMatrix.t[0],
                         g_players[0].scaMatrixData.localMatrix.t[2]);

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

    RaBounds();
    RaBgPrepare();      // CUSTOM: backdrop textures and their source cameras

    RaDrawBoxes(V);
    RaDrawMeshes(V);      // CUSTOM: the furniture models
    RaDrawMirror(V);      // CUSTOM: the room again, as the mirror shows it

    RaDrawItems(V);

    RaFlush();
    for (int k = 0; k < RAID_MAX_BGSRC; k++) RaTexFlush(k);   // CUSTOM: backdrops
    for (int k = 0; k < RAID_MAX_BGSRC; k++) RaOverFlush(k);  // ...then their blended second pictures
    RaMatFlush();                                             // CUSTOM: the last material batch
    s_lateView = V;                                           // CUSTOM: the glass waits for the models
    s_lateValid = 1;

    RaDrawNames(V);   // CUSTOM: co-op nameplates, after the batch, before the models
    RaDrawCrosshair(V);   // CUSTOM: RAID's L2 aim

    // The pickups' real models. AFTER the flush, because these do not go
    // through this file's own triangle batch at all - they are TMD objects,
    // queued into the renderer's model pass the same way a character is, and
    // that pass runs later in the frame.
    RaidItemModels_Sync();
    RaidItemModels_Draw();
}

// ---------------------------------------------------------------------------
// The level's boxes.
// ---------------------------------------------------------------------------
static void RaDrawBoxes(const RaView& V)
{
    // ---- the level ---------------------------------------------------------
    // Every box, six faces, each subdivided into cells. The subdivision is not
    // decoration: the fog and the light pool are evaluated PER VERTEX, so a
    // single room-sized quad would interpolate both across the whole surface
    // and show neither.
    //
    // A face is skipped when its outward normal points away from the eye.
    // That is honest backface culling rather than a guess about which side of
    // a wall the player is on, and it matters: a room built from solid slabs
    // has an outer surface nobody can ever see, and drawing it doubles the
    // triangle count for nothing.
    for (int i = 0; i < g_raidLevel.nbox; i++) {
        const RaidBox* B = &g_raidLevel.box[i];
        if ((B->flags & RAID_BOX_DRAW) == 0) continue;
        if (B->flags & RAID_BOX_TEX) {                   // CUSTOM: a material surface
            RaMatBox(V, B);
            continue;
        }
        if (B->flags & RAID_BOX_PROJ) {                  // CUSTOM: a backdrop surface
            RaProjBox(V, i, B);
            continue;
        }

        const float x0 = (float)B->x0, x1 = (float)B->x1;
        const float y0 = (float)B->y0, y1 = (float)B->y1;
        const float z0 = (float)B->z0, z1 = (float)B->z1;
        const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
        const int chk = (B->flags & RAID_BOX_CHECKER) != 0;
        const float tr = (float)B->tr * (1.0f / 255.0f);
        const float tg = (float)B->tg * (1.0f / 255.0f);
        const float tb = (float)B->tb * (1.0f / 255.0f);

        // Y is negative upwards, so y0 is the TOP of the box. The top gets the
        // full shade, the sides less, the underside least - the difference is
        // what stops a box reading as a flat silhouette.
        // A plate (no thickness) is one face and is always drawn: it has no
        // outside, and a floor seen edge-on is still a floor.
        if (dy == 0.0f) {
            if (RaCeilingHidden(V, y0)) continue;
            RaGrid(V, x0, y0, z0,  dx, 0, 0,  0, 0, dz,  B->shade, tr, tg, tb, chk);
            continue;
        }

        if (RaFacing(V, -1.0f, 1, (x0+x1)*0.5f, y0, (z0+z1)*0.5f))
            RaGrid(V, x0, y0, z0,  dx, 0, 0,  0, 0, dz,  B->shade,        tr, tg, tb, chk);
        if (RaFacing(V,  1.0f, 1, (x0+x1)*0.5f, y1, (z0+z1)*0.5f))
            RaGrid(V, x0, y1, z0,  dx, 0, 0,  0, 0, dz,  B->shade * 0.30f, tr, tg, tb, chk);
        if (RaFacing(V, -1.0f, 0, x0, (y0+y1)*0.5f, (z0+z1)*0.5f))
            RaGrid(V, x0, y0, z0,  0, dy, 0,  0, 0, dz,  B->shade * 0.62f, tr, tg, tb, 0);
        if (RaFacing(V,  1.0f, 0, x1, (y0+y1)*0.5f, (z0+z1)*0.5f))
            RaGrid(V, x1, y0, z0,  0, dy, 0,  0, 0, dz,  B->shade * 0.62f, tr, tg, tb, 0);
        if (RaFacing(V, -1.0f, 2, (x0+x1)*0.5f, (y0+y1)*0.5f, z0))
            RaGrid(V, x0, y0, z0,  dx, 0, 0,  0, dy, 0,  B->shade * 0.55f, tr, tg, tb, 0);
        if (RaFacing(V,  1.0f, 2, (x0+x1)*0.5f, (y0+y1)*0.5f, z1))
            RaGrid(V, x0, y0, z1,  dx, 0, 0,  0, dy, 0,  B->shade * 0.55f, tr, tg, tb, 0);
    }
}

// ---------------------------------------------------------------------------
// CUSTOM: the mirror (`mirror` line).
//
// The characters' reflection is the game's own: with the pass armed
// (RaidMirror_Arm), update_player_anim and update_entities submit each body a
// second time through the room camera reflected about the plane
// (entity_draw_mirror_reflection). A story room needs nothing else - its
// mirror is painted into the background. The arena has no background, so it
// draws the reflected ROOM here the same way: every box and model again,
// through the reflected eye, clipped to the room side of the glass.
//
// Reflecting the view is reflecting the eye and the basis about the plane - one
// component negated in each - and the projection that comes out IS the mirror
// image, handedness and all, with depths that are the true length of the path
// through the glass. So the reflection lands BEHIND the wall the mirror hangs
// on, and the depth test hides it everywhere except where the level has left
// a hole for the glass.
// ---------------------------------------------------------------------------
static void RaDrawMirror(const RaView& V)
{
    const RaidMirror* M = &g_raidLevel.mirror;
    if (!M->on) return;

    const int a = M->axis ? 0 : 2;
    const float k = (float)M->plane;
    const float eye = a == 0 ? V.fromX : V.fromZ;
    if (fabsf(eye - k) < 1.0f) return;

    // The whole room again costs as much as the room: skip it when the glass
    // is not in front of the eye at all. (Off to one side still draws - the
    // depth test then throws it away - which is cheap next to getting this
    // wrong at the screen's edge.)
    int ahead = 0;
    for (int i = 0; i < 4; i++) {
        RaVert c;
        const float cc = (float)((i & 1) ? M->max : M->min);
        c.y = (float)((i & 2) ? M->ybot : M->ytop);
        if (M->axis) { c.x = k; c.z = cc; } else { c.z = k; c.x = cc; }
        if (RaDepth(V, c) > RA_NEAR) ahead = 1;
    }
    if (!ahead) return;

    RaView R = V;
    if (a == 0) R.fromX = 2.0f * k - V.fromX;
    else        R.fromZ = 2.0f * k - V.fromZ;
    R.n[a] = -V.n[a];
    R.r[a] = -V.r[a];
    R.u[a] = -V.u[a];

    s_clipAxis = a;
    s_clipK    = k;
    s_clipSide = eye > k ? 1.0f : -1.0f;      // the side the eye, and so the room, is on
    RaDrawBoxes(R);
    RaDrawMeshes(R);
    s_clipAxis = -1;
}

// The glass: a faint blue-grey film over the hole, blended and depth-tested
// but not depth-writing. Drawn after the model pass (RaidArena_DrawLate), so
// it tints the reflected characters as it tints the reflected room - and the
// depth test keeps it off anyone standing in FRONT of the mirror.
static void RaDrawGlass(const RaView& V)
{
    const RaidMirror* M = &g_raidLevel.mirror;
    if (!M->on || s_dx == NULL) return;

    const float k = (float)M->plane;
    const float c0 = (float)M->min, c1 = (float)M->max;
    const float y0 = (float)M->ytop, y1 = (float)M->ybot;
    RaVert q[4];
    for (int i = 0; i < 4; i++) {
        const float c = (i == 1 || i == 2) ? c1 : c0;
        const float y = (i >= 2) ? y1 : y0;
        if (M->axis) { q[i].x = k; q[i].z = c; }
        else         { q[i].z = k; q[i].x = c; }
        q[i].y = y;
        q[i].cr = 0.52f; q[i].cg = 0.64f; q[i].cb = 0.72f;
    }

    // Through the ordinary batch, then flushed blended instead of opaque.
    s_triCount = 0;
    s_cursor   = 0;
    RaPoly(V, q, 4);
    if (s_triCount > 0) {
        for (int t = 0; t < s_triCount * 3; t++) s_tris[t * RA_FLOATS + 9] = 0.24f;
        s_dx->DrawTriangles3D(s_tris, s_triCount, MARNI_NULL_HANDLE,
                              MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA, false);
    }
    s_triCount = 0;
    s_cursor   = 0;
}

// ---------------------------------------------------------------------------
// CUSTOM: dynamic shadows - the characters' (and anything else the model pass
// drew: the pickups, the reflections, which land behind the mirror's wall and
// are hidden there).
//
// The model pass leaves this frame's triangles in screen space
// (TmdRenderer_FrameTris). Each vertex is taken back to the world through the
// same view the arena projects with - x and y across, its view depth along -
// flattened onto the floor along the ray from the room's main light, and
// projected again. That is the body's real silhouette, posed this frame.
//
// It must darken once however many triangles overlap, so it is drawn as a MASK
// in the frame's alpha channel (MARNI_BLEND_ALPHA_ONLY: cleared by one quad,
// then written by the flattened triangles, depth-tested against the room and
// the bodies so nothing standing in front of the floor is shadowed), and one
// more quad darkens by it (MARNI_BLEND_DARKEN_DESTA). Only where the backend
// keeps an alpha channel (SupportsDestAlpha: D3D11 yes, the GL backend no).
// ---------------------------------------------------------------------------
#define RA_SHADOW_ALPHA   0.42f       // how dark: the share of the light taken
#define RA_SHADOW_Y     -14.0f        // just off the floor - and over the mats (-8)

static float s_stris[(RA_MAX_TRIS + 2) * 3 * RA_FLOATS];

static void RaShadowQuad(float alpha, MarniBlend blend)
{
    // The whole screen, at the nearest depth: passes every depth test.
    static const float xy[6][2] = { { -8192, -8192 }, { 8192, -8192 }, { 8192, 8192 },
                                    { -8192, -8192 }, { 8192, 8192 }, { -8192, 8192 } };
    float q[6 * RA_FLOATS];
    for (int i = 0; i < 6; i++) {
        float* p = &q[i * RA_FLOATS];
        p[0] = xy[i][0]; p[1] = xy[i][1]; p[2] = 0.0f; p[3] = 1.0f;
        p[4] = p[5] = 0.0f;
        p[6] = p[7] = p[8] = 0.0f; p[9] = alpha;
    }
    s_dx->DrawTriangles3D(q, 2, MARNI_NULL_HANDLE, MARNI_SAMPLER_POINT, blend, false);
}

static void RaDrawModelShadows(const RaView& V)
{
    if (s_dx == NULL || !s_dx->SupportsDestAlpha() || g_raidLevel.nlight == 0) return;
    const float* tri = NULL;
    int stride = 0;
    const int count = TmdRenderer_FrameTris(&tri, &stride);
    if (count <= 0 || tri == NULL) return;

    // The main light, lifted: it hangs low, and a shadow cast from it would
    // fling a head's shadow across the room, away from the body.
    const float L[3] = { (float)g_raidLevel.light[0].x, (float)g_raidLevel.light[0].y * 3.0f,
                         (float)g_raidLevel.light[0].z };

    RaShadowQuad(0.0f, MARNI_BLEND_ALPHA_ONLY);          // clear the mask

    int n = 0;
    for (int t = 0; t < count; t++, tri += stride) {
        float* out = &s_stris[n * 3 * RA_FLOATS];
        int ok = 1;
        for (int e = 0; e < 3 && ok; e++) {
            const float* v = tri + e * RA_FLOATS;
            const float sx = v[0], sy = v[1], vz = v[3];
            if (vz <= 1.0f) { ok = 0; break; }
            // back to the world: the inverse of RaProject
            const float a = (sx - V.cx) * vz / V.f;
            const float b = (sy - V.cy) * vz / V.f;
            RaVert w;
            w.x = V.fromX + V.r[0] * a + V.u[0] * b + V.n[0] * vz;
            w.y = V.fromY + V.r[1] * a + V.u[1] * b + V.n[1] * vz;
            w.z = V.fromZ + V.r[2] * a + V.u[2] * b + V.n[2] * vz;
            if (w.y <= L[1] + 50.0f) { ok = 0; break; }   // above the light
            // down the light's ray to the floor
            const float k = (RA_SHADOW_Y - L[1]) / (w.y - L[1]);
            w.x = L[0] + (w.x - L[0]) * k;
            w.z = L[2] + (w.z - L[2]) * k;
            w.y = RA_SHADOW_Y;
            const float d = RaDepth(V, w);
            if (d < RA_NEAR) { ok = 0; break; }
            float px, py;
            RaProject(V, w, d, &px, &py);
            float* p = &out[e * RA_FLOATS];
            p[0] = px; p[1] = py; p[2] = TmdViewZToNdc(d); p[3] = d;
            p[4] = p[5] = 0.0f;
            p[6] = p[7] = p[8] = 0.0f; p[9] = RA_SHADOW_ALPHA;
        }
        if (!ok) continue;
        if (++n >= RA_MAX_TRIS) {
            s_dx->DrawTriangles3D(s_stris, n, MARNI_NULL_HANDLE, MARNI_SAMPLER_POINT,
                                  MARNI_BLEND_ALPHA_ONLY, false);
            n = 0;
        }
    }
    if (n > 0) {
        s_dx->DrawTriangles3D(s_stris, n, MARNI_NULL_HANDLE, MARNI_SAMPLER_POINT,
                              MARNI_BLEND_ALPHA_ONLY, false);
    }

    RaShadowQuad(0.0f, MARNI_BLEND_DARKEN_DESTA);        // and darken by it
}

// ---------------------------------------------------------------------------
// CUSTOM: doors, the way Resident Evil 2 (2019) does them (`door` lines) -
// no cut to a loading screen, the next room is simply there behind the leaf.
//
//   - Come up to a shut door and it opens away from you: walking at it from
//     a little way off sets it ajar, and right up against it it opens all the
//     way. Nothing asks which way she faces or whether she moved: a shut door
//     is exactly what stops her moving, and a facing test only made it hard
//     to open.
//   - The action button, within reach, opens it all the way - or,
//     open, shuts it. Doors stay as you leave them.
//   - Its doorway stops you only while the leaf is still across it, and it
//     never shuts on somebody standing in it.
//
// RaidDoors_Player runs inside game_loop's per-player block (the pad and the
// facing are that player's there); RaDoorsUpdate, from the draw, swings the
// leaves and keeps the doorway boxes in step. Every mesh placed exactly at the
// hinge is posed by the angle (the leaf, its brass): a mesh's yaw is a turn
// about its own origin, which is the hinge.
// ---------------------------------------------------------------------------
#define RA_DOOR_REACH     1300.0f   // from the doorway: the action button
#define RA_DOOR_AJAR      1100.0f   // walking at it from inside this: it opens ajar
#define RA_DOOR_BUMP       700.0f   // and inside this: all the way
#define RA_DOOR_FULL      1.75f     // radians: wide open (a right angle and a little)
#define RA_DOOR_HALF      0.70f     // ajar
#define RA_DOOR_PASS      0.95f     // the doorway is clear past this
#define RA_DOOR_SPEED     0.14f     // radians a frame, opening
#define RA_DOOR_SHUT      0.10f     // and shutting
#define RA_DOOR_BUTTON   0x80       // the action button, as the pickups read it (RaidItems.cpp)
#define RA_DOOR_WALK     0x00FF     // any direction on the pad (PlayerPad_Update's remap)

static int RaDoorOccupied(const RaidDoor* D)
{
    for (int i = 0; i < Coop_PlayerCount(); i++) {
        const int* t = g_players[i].scaMatrixData.localMatrix.t;
        if (t[0] > D->hx - 250 && t[0] < D->hx + D->width + 250
            && t[2] > D->hz - D->depth && t[2] < D->hz + D->depth) return 1;
    }
    return 0;
}

void RaidDoors_Player(int i)
{
    if (g_raidMode == 0 || !g_raidLevel.loaded || i < 0 || i >= RAID_PLAYERS) return;
    const int* t = g_playerEntity.scaMatrixData.localMatrix.t;
    const float px = (float)t[0], pz = (float)t[2];
    const float a = (float)(g_playerEntity.directionAngle & 0xFFF) * (6.2831853f / 4096.0f);
    const float fx = cosf(a), fz = sinf(a);
    const int act  = (g_PlayerDpadPressed & RA_DOOR_BUTTON) != 0;
    const int walk = (g_PlayerDpadHeld & RA_DOOR_WALK) != 0;

    for (int d = 0; d < g_raidLevel.ndoor && d < RAID_MAX_DOOR; d++) {
        RaidDoor* D = &g_raidLevel.door[d];
        // To the doorway's nearest point: a wide door is walked into anywhere.
        const float x0 = (float)D->hx, x1 = (float)(D->hx + D->width);
        const float nx = px < x0 ? x0 : (px > x1 ? x1 : px);
        const float dx = nx - px, dz = (float)D->hz - pz;
        const float dist = sqrtf(dx * dx + dz * dz);
        if (dist > RA_DOOR_REACH) continue;
        (void)fx; (void)fz;    // facing is not asked: it made the door hard to open
        const float away = pz > (float)D->hz ? 1.0f : -1.0f;   // from +Z it swings toward -Z

        if (act) {
            if (fabsf(D->target) < 0.05f) D->target = away * RA_DOOR_FULL;
            else if (!RaDoorOccupied(D)) { D->target = 0.0f; D->idle = 1; }   // shut by hand:
            continue;                                    // it stays shut until she steps back
        }
        if (D->idle) continue;
        if (dist < RA_DOOR_BUMP) {
            // Up against it: open, whatever the pad says - standing at a shut
            // door is asking for it to open.
            if (fabsf(D->target) < RA_DOOR_FULL - 0.01f)
                D->target = (fabsf(D->target) > 0.05f ? (D->target > 0.0f ? 1.0f : -1.0f) : away) * RA_DOOR_FULL;
        } else if (walk && dist < RA_DOOR_AJAR && fabsf(D->target) < 0.05f) {
            D->target = away * RA_DOOR_HALF;
        }
    }
}

static void RaDoorsUpdate(void)
{
    int collisionDirty = 0;
    for (int d = 0; d < g_raidLevel.ndoor && d < RAID_MAX_DOOR; d++) {
        RaidDoor* D = &g_raidLevel.door[d];
        if (D->idle) {                                   // shut by hand: free again once
            int near_ = 0;                               // everybody has stepped back
            for (int i = 0; i < Coop_PlayerCount(); i++) {
                const int* t = g_players[i].scaMatrixData.localMatrix.t;
                const float x0 = (float)D->hx, x1 = (float)(D->hx + D->width);
                const float px = (float)t[0], nx = px < x0 ? x0 : (px > x1 ? x1 : px);
                const float dx = nx - px, dz = (float)D->hz - (float)t[2];
                if (dx * dx + dz * dz < RA_DOOR_AJAR * RA_DOOR_AJAR) near_ = 1;
            }
            if (!near_) D->idle = 0;
        }
        if (D->target == 0.0f && D->angle != 0.0f && RaDoorOccupied(D)) {
            D->target = D->angle;                        // never shut on somebody
        }
        const float step = (fabsf(D->target) > fabsf(D->angle)) ? RA_DOOR_SPEED : RA_DOOR_SHUT;
        if (D->angle < D->target) { D->angle += step; if (D->angle > D->target) D->angle = D->target; }
        else if (D->angle > D->target) { D->angle -= step; if (D->angle < D->target) D->angle = D->target; }

        // The meshes at the hinge. Yaw is 4096 to the turn and turns +X toward
        // -Z for a positive angle (RaMeshXform).
        const short yaw = (short)(int)(D->angle * (4096.0f / 6.2831853f));
        for (int m = 0; m < g_raidLevel.nmesh && m < RAID_MAX_MESH; m++) {
            RaidMesh* M = &g_raidLevel.mesh[m];
            if (M->x == D->hx && M->z == D->hz) M->yaw = yaw;
        }

        // The doorway: open once the leaf is clear of it.
        if (D->box >= 0 && D->box < g_raidLevel.nbox) {
            RaidBox* B = &g_raidLevel.box[D->box];
            const int solid = fabsf(D->angle) < RA_DOOR_PASS;
            const int was = (B->flags & RAID_BOX_SOLID) != 0;
            if (solid != was) {
                if (solid) B->flags |= RAID_BOX_SOLID; else B->flags &= ~RAID_BOX_SOLID;
                collisionDirty = 1;
            }
        }
    }
    if (collisionDirty) RaidLevel_RebuildCollision();
}

void RaidArena_DrawLate(void)
{
    if (!s_lateValid || g_raidMode == 0 || !g_raidLevel.loaded) return;
    s_lateValid = 0;
    s_dx = Marni_DX();
    RaDrawModelShadows(s_lateView);     // the characters' shadows, cast from their models
    RaDrawGlass(s_lateView);
}
