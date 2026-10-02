// RaidDoorModels.cpp - the game's own 3D doors, standing in the RAID room.
//
// CUSTOM. See RaidDoorModels.h for what this is for.
//
// WHAT A .DOR HOLDS, AND WHAT IS TAKEN FROM IT
//
// A .dor (DoorSystem.cpp's header has the layout) is a small scene: a TMD of
// up to 12 objects - the leaf, and several handles and plates - a TIM for all
// of them, and the 38-opcode scripts that set the scene up and play it. Which
// objects are the leaf and which the handles, and where a handle sits on the
// leaf, is not a table anywhere: the scripts say it, with ORDER_SETUP (order,
// model, parent), ORDER_POS and ORDER_ROT, under IF_BYTE tests on the door
// record's bytes - byte var 0 the record's +0x08 (which way it opens, single or
// double), byte var 3 its +0x0B & 0x3F (which handle). DOOR15.DOR, the door
// between room 003 and its bathroom (type 33, handle 1), puts object 0 at the
// root as the leaf and object 1 on it twice, at (+-130, -3240, -3430), the far
// side turned round.
//
// So this runs exactly those three opcodes - and IF_BYTE and ACTIVATE, to get
// to them - over the scripts, with var 0 = 0 (a single door) and var 3 = the
// `doormodel` handle, and keeps the last placement each order was given. The
// animation itself (the camera flight, the swing) is not run: the RAID door
// swings by its own angle (RaidArena.cpp), the way the RE2 (2019) recording
// showed. The root order is the leaf; its children are the handles.
//
// SCALE
//
// The scene is built big for its close-up: DOOR15's leaf is 3600 wide and 6600
// tall where the room's doorway is 1400 by 3195. The leaf is scaled to the
// doorway on each axis (width, height, a fixed thickness), and the handles by
// the width's factor, at their spot on the leaf scaled the leaf's way - in the
// vertices themselves, at load (see rd_load for why not in the matrix).
//
// STORAGE
//
// Like the pickups (RaidItemModels.cpp, whose comment says why at length):
// own slots, outside g_tmdObjectBuffer, listed in TmdRenderer.cpp's
// kSlotRegions; own texture pages; a transient load buffer.
#include "../Globals.h"
#include "Types.h"
#include "RaidLevel.h"
#include "RaidDoorModels.h"
#include "TmdRenderer.h"
#include "FileLoader.h"
#include "../marni/Marni3DObject.h"
#include "../marni/PSXTexture.h"
#include <cmath>
#include <cstring>

extern void LoadPSXImage(PSXTexture* tex, void* buf, int mode);   // TextureLoader.cpp
extern void ResolveAnimPointers(unsigned char* data);             // TmdAnimation.cpp
extern int  PSXObject_Store(CMarniDirect3DTMD* self, int* tmdHdr, int objIndex,
                            int bankOrTpage, int texRef);         // Marni3DObject.cpp
extern const char* Door_FileName(unsigned int type);              // DoorSystem.cpp

#define RD_LEAF_THICK   70          // world units: the leaf's thickness in the room
#define RD_FILE_MAX     0x18000     // the largest .dor is 78348 bytes
#define RD_ORDERS       12          // the scene's order table (DoorSystem: 12 x 0x80)
#define RD_PARTS        4           // per door: the leaf and up to three handle placements
#define PAGE_CREATED(p) (*(DWORD*)((BYTE*)(p) + 0x348))

alignas(16) unsigned char g_raidDoorTmdSlots[TMD_RAID_DOOR_SLOT_COUNT * TMD_SLOT_STRIDE];
alignas(16) static unsigned char s_pages[RAID_MAX_DOOR][0x36C];
static unsigned char s_file[RD_FILE_MAX];

struct RdPart {
    int   slot;                     // into g_raidDoorTmdSlots
    short pos[3];                   // on the leaf, in the scene's units
    short rot[3];
};

struct RdDoor {
    int    state;                   // 0 not tried, 1 ready, -1 failed
    int    model, knob;             // what it was loaded for (RaidDoor::model / knob)
    int    nparts;                  // part[0] is the leaf
    RdPart part[RD_PARTS];
    float  sx, sy, sz;              // scene -> room: thickness, height, width
};
static RdDoor s_door[RAID_MAX_DOOR];

// ---------------------------------------------------------------------------
// The scripts, for placement only.
// ---------------------------------------------------------------------------
// Each opcode's length, from the interpreter (tools/dor_disasm.py's LEN).
static const unsigned char kOpLen[0x26] = {
    2, 2, 2, 2, 6, 6, 4, 2,   4, 2, 4, 4, 4, 4, 6, 6,
    6, 0xE, 0xE, 2, 4, 8, 8, 2,   4, 8, 6, 2, 4, 10, 10, 4,
    4, 4, 4, 4, 2, 2,
};

struct RdOrder {
    int   model, parent;            // model -1: never set up
    short pos[3], rot[3];
};

struct RdRun {
    const unsigned char* buf;
    size_t               len;
    size_t               table;     // the script table's offset
    int                  var0, var3;
    unsigned int         seen;      // scripts already run
    RdOrder              order[RD_ORDERS];
};

static void rd_run(RdRun* R, int script, int depth)
{
    if (depth > 6 || script < 0 || script >= 35 || (R->seen & (1u << script))) return;
    R->seen |= 1u << script;
    if (R->table + (size_t)script * 4 + 4 > R->len) return;
    size_t pc = R->table + *(const unsigned int*)(R->buf + R->table + (size_t)script * 4);
    for (int steps = 0; steps < 4000; steps++) {
        if (pc >= R->len) return;
        const unsigned char op = R->buf[pc];
        if (op >= sizeof(kOpLen)) return;              // not an opcode: stop reading
        const size_t n = kOpLen[op];
        if (pc + n > R->len) return;
        const unsigned char* a = R->buf + pc + 1;
        if (op == 0x00 || op == 0x01) return;          // END, CLEAR_SELF
        if (op == 0x04) {                              // IF_BYTE: skip unless var == val
            const int var = a[1], val = a[3];
            const int v = var == 0 ? R->var0 : (var == 3 ? R->var3 : 0);
            if (v != val) { pc += n + a[0]; continue; }
        } else if (op == 0x08) {                       // ACTIVATE cmd, script
            rd_run(R, a[2], depth + 1);
        } else if (op == 0x10 && a[0] < RD_ORDERS) {   // ORDER_SETUP order, model, parent
            R->order[a[0]].model  = a[1];
            R->order[a[0]].parent = a[2];
        } else if ((op == 0x15 || op == 0x19) && a[0] < RD_ORDERS) {   // ORDER_POS / ORDER_ROT
            short* dst = op == 0x15 ? R->order[a[0]].pos : R->order[a[0]].rot;
            for (int k = 0; k < 3; k++) dst[k] = (short)(a[1 + k * 2] | (a[2 + k * 2] << 8));
        }
        pc += n;
    }
}

// ---------------------------------------------------------------------------
// Loading.
// ---------------------------------------------------------------------------
static void rd_release(int d)
{
    // Every slot of the door's, whether a load filled it or gave up half
    // way; cleaning an empty slot or page is what the loaders do first anyway.
    RdDoor* R = &s_door[d];
    for (int p = 0; p < RD_PARTS; p++) {
        CMarniDirect3DTMD* slot = (CMarniDirect3DTMD*)(void*)
            &g_raidDoorTmdSlots[(d * RD_PARTS + p) * TMD_SLOT_STRIDE];
        slot->CleanupObjects(g_pMarniDirect3D);
    }
    VideoDriver_ClearState348(s_pages[d], g_pMarniDirect3D);
    memset(R, 0, sizeof(*R));
}

// Scale object m's vertices in place (the file's offsets, before the resolve).
static void rd_scale_object(unsigned char* tmdBase, const unsigned char* end, int m,
                            float fx, float fy, float fz)
{
    unsigned char* table = tmdBase + 0x0C;
    const int* ent = (const int*)(table + m * 0x1C);
    const int off = ent[0], n = ent[1];
    if (n <= 0 || n > 0x1000 || off < 0 || table + off + (size_t)n * 8 > end) return;
    short* v = (short*)(table + off);
    const float f[3] = { fx, fy, fz };
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 3; k++) {
            const float c = (float)v[i * 4 + k] * f[k];
            v[i * 4 + k] = (short)(c < 0.0f ? c - 0.5f : c + 0.5f);
        }
}

static int rd_load(int d, const RaidDoor* D)
{
    RdDoor* R = &s_door[d];
    const char* name = Door_FileName((unsigned int)(D->model - 1));
    if (name == NULL) return 0;
    const size_t len = LoadFile(name, s_file, 0x20);
    if (len == (size_t)-1 || len < 0x200 || len > RD_FILE_MAX) return 0;

    const unsigned int* hdr = (const unsigned int*)s_file;
    if (hdr[0] + 35 * 4 > len || hdr[1] + 0x0C > len || hdr[2] + 8 > len) return 0;
    unsigned char* tmdBase = s_file + hdr[1];
    if (*(const int*)tmdBase != 0x41) return 0;
    const int nobj = *(const int*)(tmdBase + 8);
    if (nobj < 1 || nobj > RD_ORDERS) return 0;
    if ((tmdBase[4] & 1) != 0) return 0;              // already resolved (see RaidItemModels)

    // Where each part goes.
    static RdRun run;
    memset(&run, 0, sizeof(run));
    run.buf = s_file; run.len = len; run.table = hdr[0];
    run.var0 = 0; run.var3 = D->knob;
    for (int o = 0; o < RD_ORDERS; o++) run.order[o].model = -1;
    rd_run(&run, 0, 0);
    int leaf = -1;
    for (int o = 0; o < RD_ORDERS && leaf < 0; o++)
        if (run.order[o].model >= 0 && run.order[o].model < nobj && run.order[o].parent == 0xFF) leaf = o;
    if (leaf < 0) return 0;

    // The leaf's extent, on the file's offsets, before they are resolved.
    {
        const unsigned char* table = tmdBase + 0x0C;
        const int* ent = (const int*)(table + run.order[leaf].model * 0x1C);
        const int off = ent[0], n = ent[1];
        if (n <= 0 || n > 0x1000 || off < 0
            || (size_t)(table - s_file) + (size_t)off + (size_t)n * 8 > len) return 0;
        const short* v = (const short*)(table + off);
        int lo[3] = { 32767, 32767, 32767 }, hi[3] = { -32768, -32768, -32768 };
        for (int i = 0; i < n; i++)
            for (int k = 0; k < 3; k++) {
                const int c = v[i * 4 + k];
                if (c < lo[k]) lo[k] = c;
                if (c > hi[k]) hi[k] = c;
            }
        // Thickness along x, height up -y from 0, width from the hinge at
        // z = 0 out to one side.
        const int T = hi[0] - lo[0];
        const int H = -lo[1];
        const int W = (-lo[2] > hi[2]) ? -lo[2] : hi[2];
        if (T <= 0 || H <= 0 || W <= 0) return 0;
        R->sx = (float)RD_LEAF_THICK / (float)T;
        R->sy = (float)D->height / (float)H;
        R->sz = (float)D->width / (float)W;
    }

    // Into room units in the file itself, before anything copies the
    // geometry: the leaf on each axis, every handle by the width's factor all
    // round. A scale carried in the GTE matrix instead came out uniform on
    // screen - the leaf kept the scene's 6600:3600 and stood a tenth short of
    // the doorway's top - so the matrices below are pure turns.
    {
        unsigned int done = 0;
        for (int o = -1; o < RD_ORDERS; o++) {
            const int ord = (o < 0) ? leaf : o;
            if (o >= 0 && run.order[o].parent != leaf) continue;
            const int m = run.order[ord].model;
            if (m < 0 || m >= nobj || (done & (1u << m))) continue;
            done |= 1u << m;
            if (o < 0) rd_scale_object(tmdBase, s_file + len, m, R->sx, R->sy, R->sz);
            else       rd_scale_object(tmdBase, s_file + len, m, R->sz, R->sz, R->sz);
        }
    }

    ResolveAnimPointers(tmdBase + 4);

    // The texture: the door system's own sequence (DoorCreateTexturePage),
    // stamping every CLUT descriptor with the key the .dor primitives carry.
    unsigned char* page = s_pages[d];
    VideoDriver_ClearState348(page, g_pMarniDirect3D);
    memset(page, 0, sizeof(s_pages[d]));
    const auto bank = g_TextureBankID;
    g_TextureBankID = 0x15;
    LoadPSXImage((PSXTexture*)page, s_file + hdr[2], 1);
    {
        DWORD count = *(DWORD*)(page + 0x340);
        BYTE* desc = page;
        for (DWORD j = 0; j < count; j++, desc += 0x68) {
            *(DWORD*)(desc + 0x54) = 0;
            *(DWORD*)(desc + 0x58) = 0x1FF;
            *(DWORD*)(desc + 0x5C) = 0x140;
            *(DWORD*)(desc + 0x60) = 0x100;
        }
    }
    Direct3DTIM_Create(page, g_pMarniDirect3D);
    g_TextureBankID = bank;
    if (PAGE_CREATED(page) == 0) return 0;

    // The parts: the leaf, then every order hung on it. Page 0x15 and a UV
    // divisor of 128, as DoorAsyncCreateTmd stores a door part; created as a
    // door type whose sequence is 0 is (flag 1).
    R->nparts = 0;
    for (int o = -1; o < RD_ORDERS && R->nparts < RD_PARTS; o++) {
        const int ord = (o < 0) ? leaf : o;
        if (o >= 0 && (run.order[o].parent != leaf || run.order[o].model < 0 || run.order[o].model >= nobj))
            continue;
        const int slotIdx = d * RD_PARTS + R->nparts;
        CMarniDirect3DTMD* slot = (CMarniDirect3DTMD*)(void*)&g_raidDoorTmdSlots[slotIdx * TMD_SLOT_STRIDE];
        slot->CleanupObjects(g_pMarniDirect3D);
        const int ok = PSXObject_Store(slot, (int*)tmdBase, run.order[ord].model, 0x15, 0x80) != 0
                    && slot->Create(g_pMarniDirect3D, page, (void*)1) != 0;
        if (!ok) {
            if (o < 0) return 0;                       // no leaf, no door
            continue;                                  // a handle short is still a door
        }
        RdPart* P = &R->part[R->nparts++];
        P->slot = slotIdx;
        for (int k = 0; k < 3; k++) {
            P->pos[k] = (o < 0) ? 0 : run.order[ord].pos[k];
            P->rot[k] = (o < 0) ? 0 : run.order[ord].rot[k];
        }
    }
    R->model = D->model;
    R->knob  = D->knob;
    return 1;
}

void RaidDoorModels_Reset(void)
{
    for (int d = 0; d < RAID_MAX_DOOR; d++) rd_release(d);
}

void RaidDoorModels_Sync(void)
{
    if (!g_raidLevel.loaded) return;
    for (int d = 0; d < RAID_MAX_DOOR; d++) {
        const RaidDoor* D = &g_raidLevel.door[d];
        const int want = (d < g_raidLevel.ndoor) ? D->model : 0;
        RdDoor* R = &s_door[d];
        if (R->state != 0 && R->model == want && (want == 0 || R->knob == D->knob)) continue;
        rd_release(d);
        R->model = want;
        R->knob  = D->knob;
        if (want == 0) { R->state = -1; continue; }
        if (rd_load(d, D)) {
            R->state = 1;
        } else {
            rd_release(d);                             // tried: not again every frame
            R->state = -1; R->model = want; R->knob = D->knob;
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing.
// ---------------------------------------------------------------------------
void RaidDoorModels_Draw(void)
{
    if (!g_raidLevel.loaded || g_RdtPointer == NULL) return;
    for (int d = 0; d < g_raidLevel.ndoor && d < RAID_MAX_DOOR; d++) {
        const RdDoor* R = &s_door[d];
        if (R->state <= 0) continue;
        const RaidDoor* D = &g_raidLevel.door[d];

        // The scene's leaf has its width along z, out from the hinge at 0
        // toward -z; the room's along +X. A quarter turn (0xC00, the facing
        // sense RotMatrixY and the arena's meshes share) takes one to the
        // other, and the door's own angle turns it from there.
        const int yaw = ((int)(D->angle * (4096.0f / 6.2831853f)) + 0xC00) & 0xFFF;
        const float a = (float)yaw * (6.2831853f / 4096.0f);
        const float c = cosf(a), sn = sinf(a);

        VECTOR at;
        at.x = D->hx + (int)(c * (D->width * 0.5f));
        at.y = -D->height / 2;
        at.z = D->hz - (int)(sn * (D->width * 0.5f));
        at.pad = 0;

        for (int p = 0; p < R->nparts; p++) {
            const RdPart* P = &R->part[p];
            MATRIX world;
            memset(&world, 0, sizeof(world));
            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            if (p == 0) {
                world.m[0][0] = world.m[1][1] = world.m[2][2] = 4096;   // already to size
            } else {
                // A handle, turned as the script turns it, at its spot on the
                // leaf - the spot scaled the leaf's way.
                SVECTOR r;
                r.x = P->rot[0]; r.y = P->rot[1]; r.z = P->rot[2]; r.pad = 0;
                RotMatrix(&r, &world);
                tx = P->pos[0] * R->sx;
                ty = P->pos[1] * R->sy;
                tz = P->pos[2] * R->sz;
            }
            RotMatrixY(yaw, &world);
            world.t[0] = D->hx + (int)(c * tx + sn * tz);
            world.t[1] = (int)ty;
            world.t[2] = D->hz + (int)(-sn * tx + c * tz);

            // Lit by the level lights nearest the DOOR. The room's three light
            // slots hold the ones nearest player 1 (RaidLevel_LightsNear, for
            // the characters), and a door lit by those went darker as she
            // walked away from it and lighter as she came back.
            if (p == 0) RaidLevel_LightsNear(at.x, at.z);
            CMarniDirect3DTMD* slot = (CMarniDirect3DTMD*)(void*)
                &g_raidDoorTmdSlots[P->slot * TMD_SLOT_STRIDE];
            TmdDrawSlotAt(slot, &world, &at, 4);
        }
    }
    // And back to hers, for everything drawn after.
    RaidLevel_LightsNear(g_players[0].scaMatrixData.localMatrix.t[0],
                         g_players[0].scaMatrixData.localMatrix.t[2]);
}
