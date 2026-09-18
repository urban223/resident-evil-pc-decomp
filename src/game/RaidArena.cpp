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
#include "../marni/MarniSystem.h"
#include <cmath>

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

// ===========================================================================
// RaidArena_Draw - once per frame, from the render flush, before the models.
// ===========================================================================
void RaidArena_Draw(void)
{
    if (g_raidMode == 0) return;
    if (g_RdtPointer == NULL) return;

    // The reload key (window_proc). Taken here rather than in the input path
    // because here the room is certainly loaded and certainly this one, so a
    // file that turns out to be broken leaves the level that is already up.
    if (g_raidReloadRequest) {
        g_raidReloadRequest = 0;
        if (RaidLevel_Load()) {
            RaidLevel_Apply();
            Room_SetupCamera();     // just the matrix and the FOV, no room reload
        }
    }

    if (!g_raidLevel.loaded) return;

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

    RaDrawItems(V);

    RaFlush();

    RaDrawNames(V);   // CUSTOM: co-op nameplates, after the batch, before the models

    // The pickups' real models. AFTER the flush, because these do not go
    // through this file's own triangle batch at all - they are TMD objects,
    // queued into the renderer's model pass the same way a character is, and
    // that pass runs later in the frame.
    RaidItemModels_Sync();
    RaidItemModels_Draw();
}
