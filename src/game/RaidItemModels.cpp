// RaidItemModels.cpp - loading and drawing the pickups' real models.
//
// CUSTOM. See RaidItemModels.h for what this is for.
//
// THE PATH, AND WHERE IT CAME FROM
//
// This is the item examine screen's own load sequence (MainMenu.cpp's
// menu_load_item_model / FUN_004841f0), with two differences that matter:
//
//   1. It keeps EIGHT models resident instead of one. The examine screen shows
//      one item at a time into a shared buffer; a room has several kinds of
//      pickup lying about at once, so each distinct type gets its own TMD slot
//      and its own texture page.
//
//   2. It owns its storage outright - the slots and the pages are arrays in
//      this file, outside g_tmdObjectBuffer. That is not a preference: the
//      entity allocator's cleanup sweeps slots 0..249 of that buffer, and a
//      model parked inside it gets destroyed by the next room load. Every
//      port-added subsystem that keeps a model alive across frames does this
//      (the door system, the item viewer); TmdRenderer.h's comment block says
//      why at length.
//
//      The matching obligation is in TmdRenderer.cpp: g_raidItemTmdSlots is
//      listed in kSlotRegions. Without that, TmdQueueObject cannot work out
//      which slot an object belongs to and silently drops it - the model loads,
//      the draw call is made, and nothing appears.
//
// SCALE
//
// An .ivm is authored for the examine viewer, where the model fills the screen
// on its own. The Beretta is 4952 units end to end; Jill is 2808 tall. Drawn at
// 1:1 in the room it is a gun the size of a car.
//
// So a pickup is normalised: its longest axis becomes RAID_ITEM_MODEL_SIZE.
// That number is a decision, not a measurement, and the browser editor applies
// exactly the same rule - which is the only way the two can agree about what
// you are going to see.
//
// The scale rides in the GTE rotation matrix. ApplyLVAndMul0Matrix multiplies
// two 4096-fixed rotations and FUN_00483080's tail divides by 4096, so a world
// rotation built at k*4096 comes out k times the size. It stays inside an
// int16 for any k <= 1.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"
#include "RaidLevel.h"
#include "RaidItemModels.h"
#include "TmdRenderer.h"
#include "FileLoader.h"
#include "../system/AssetPath.h"
#include "../marni/Marni3DObject.h"
#include "../marni/PSXTexture.h"
#include <cstdio>
#include <cstring>

extern void LoadPSXImage(PSXTexture* tex, void* buf, int mode);   // TextureLoader.cpp
extern void ResolveAnimPointers(unsigned char* data);             // TmdAnimation.cpp

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------
#define RAID_ITEM_MODEL_SIZE   460      // world units along the model's longest axis
#define RAID_ITEM_HOVER        620      // how high it floats (Y is negative upwards)

// It does NOT spin. A turning pickup reads well in an arena, but it would make
// the level file's `angle` field mean nothing and put the editor permanently
// out of step with the game - and the whole reason this file exists is that the
// two should show the same thing.

// The examine screen's own ceiling on an .ivm, from docs/IVM_MODEL_FORMAT.md.
#define RAID_IVM_MAX           187160

// ---------------------------------------------------------------------------
// Storage. One entry per distinct item type in the level.
// ---------------------------------------------------------------------------
alignas(16) unsigned char g_raidItemTmdSlots[TMD_RAID_ITEM_SLOT_COUNT * TMD_SLOT_STRIDE];
alignas(16) static unsigned char s_pages[TMD_RAID_ITEM_SLOT_COUNT][0x36C];

// The load buffer is shared and transient: PSXObject_Store copies the geometry
// into the slot and LoadPSXImage (copyData = 1) copies the pixels, so nothing
// points back here once a model is built.
static unsigned char s_loadBuf[RAID_IVM_MAX + 64];

struct RaidItemModel {
    unsigned char type;      // ITEM_* id, 0 = this entry is free
    unsigned char ready;
    short         scale;     // the GTE rotation magnitude: 4096 would be 1:1
    int           centreY;   // model-space Y of its middle, for hovering it flat
};

static RaidItemModel s_models[TMD_RAID_ITEM_SLOT_COUNT];

#define PAGE_CREATED(p)  (*(DWORD*)((BYTE*)(p) + 0x348))

// ---------------------------------------------------------------------------
// Which file holds an item's model.
//
// The same two-stage lookup menu_load_item_model does: a byte out of
// g_ItemImageLookupTable indexes g_ItemModelFileNames, except for the five ids
// that have no row in it at all and carry an explicit name instead. Getting
// this wrong is not a cosmetic error - id 0x71 fell through to the empty row
// once and loaded "item_m2/.ivm", which left a null TMD base for the parser to
// dereference a few frames later.
// ---------------------------------------------------------------------------
static const char* raid_item_model_name(unsigned char id)
{
    if (id == ITEM_INGRAM)         return (const char*)g_ItemModelFileNameING;
    if (id == ITEM_MINIMI)         return (const char*)g_ItemModelFileNameMINI;
    if (id == ITEM_GRENADE_PISTOL) return (const char*)g_GrenadePistolModelFileName;
    if (id == ITEM_ACID_PISTOL)    return (const char*)g_AcidPistolModelFileName;
    if (id == ITEM_FREEZE_PISTOL)  return (const char*)g_FreezePistolModelFileName;
    if (id == ITEM_NONE || id > ITEM_MAP_LABORATORY) return NULL;

    const unsigned char idx = g_ItemImageLookupTable[(unsigned int)id * 4];
    if (idx == 0 || idx >= 75) return NULL;           // the empty row, or off the end
    const char* name = (const char*)g_ItemModelFileNames + (unsigned int)idx * 8;
    return (name[0] != '\0') ? name : NULL;
}

// ---------------------------------------------------------------------------
// The model's extent, so it can be scaled to something believable on a floor.
//
// WHEN THIS RUNS IS PART OF THE ANSWER.
//
// A TMD object table entry is 0x1C bytes and its first field is the vertex
// block. In the file that field is an OFFSET from the table's own start
// (tmdBase + 0x0C) - but ResolveAnimPointers rewrites all three offsets in
// every entry into absolute addresses and sets FIXP, bit 0 of the flags word
// at tmdBase + 4. So "table + ent[0]" reads vertices before that call and
// base-plus-a-heap-address after it: a wild pointer about twice as large as
// either, which is exactly what a read access violation on a 0x03xxxxxx
// address from a buffer at 0x019xxxxx is.
//
// This therefore runs BEFORE the resolve, on the offsets, and refuses outright
// if it is ever handed a table that has already been resolved. It could read
// the resolved pointer instead - but that field is int-sized and the pointer
// ResolveAnimPointers writes into it is pointer-sized, which is only the same
// thing because this game is 32-bit. Refusing costs nothing (no shipped .ivm
// arrives with FIXP set; the game resolves every one of them itself) and keeps
// one width assumption out of a file parser.
//
// Everything below is read out of a file, so every step is bounded against the
// end of the loaded buffer. The rest of this loader validates carefully; this
// used to walk a vertex count it had never looked at.
// ---------------------------------------------------------------------------
static void raid_model_extent(const unsigned char* tmdBase,
                              const unsigned char* fileEnd,
                              int* outSpan, int* outCentreY)
{
    // The fallback is "draw it at its natural size", not "span of 1" - that
    // would divide into a scale of four million and put a clip the size of the
    // room on the floor.
    *outSpan = RAID_ITEM_MODEL_SIZE;
    *outCentreY = 0;

    if ((tmdBase[4] & 1) != 0) return;          // already resolved: see above

    const unsigned char* table = tmdBase + 0x0C;
    if (table + 0x1C > fileEnd) return;

    // Object 0 only, because object 0 is the only one drawn (see the store
    // below). An .ivm may carry a second, transparent half, and scaling the
    // pickup by an extent no part of the picture has would make every item
    // that has one too small.
    const int* ent = (const int*)table;
    const int off = ent[0];
    const int n   = ent[1];

    if (n <= 0 || n > 0x4000) return;
    if (off < 0 || (size_t)off + (size_t)n * 8 > (size_t)(fileEnd - table)) return;

    const short* v = (const short*)(table + off);
    int lo[3] = { 32767, 32767, 32767 };
    int hi[3] = { -32768, -32768, -32768 };

    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) {
            const int c = v[i * 4 + k];
            if (c < lo[k]) lo[k] = c;
            if (c > hi[k]) hi[k] = c;
        }
    }

    int span = hi[0] - lo[0];
    if (hi[1] - lo[1] > span) span = hi[1] - lo[1];
    if (hi[2] - lo[2] > span) span = hi[2] - lo[2];
    if (span < 1) span = 1;

    *outSpan = span;
    *outCentreY = (lo[1] + hi[1]) / 2;
}

// ---------------------------------------------------------------------------
// Load one .ivm into slot `k`.
// ---------------------------------------------------------------------------
static int raid_item_model_load(int k, unsigned char type)
{
    const char* name = raid_item_model_name(type);
    if (name == NULL) return 0;

    char path[260];
    sprintf(path, GAME_DATA_ROOT "item_m2/%s.ivm", name);

    const size_t read = LoadFile(path, s_loadBuf, 0x20);
    if (read == (size_t)-1 || read == 0 || read > RAID_IVM_MAX) return 0;

    // The .ivm header, validated the way FUN_00484420 validates it: a TIM id of
    // 0x10 and a colour mode of at most 2, then the TMD begins after both
    // blocks. Anything else and g_itemModelTmdBase would be a wild pointer.
    const int* hdr = (const int*)s_loadBuf;
    if (hdr[0] != 0x10 || (hdr[1] & 7) > 2) return 0;
    const int clutLen = *(const int*)(s_loadBuf + 8);
    if (clutLen < 12 || (size_t)(8 + clutLen + 8) > read) return 0;
    const int imgLen = *(const int*)(s_loadBuf + 8 + clutLen);
    const size_t tmdOff = (size_t)(8 + clutLen + imgLen);
    if (imgLen < 12 || tmdOff + 12 > read) return 0;

    unsigned char* tmdBase = s_loadBuf + tmdOff;
    if (*(const int*)tmdBase != 0x41) return 0;
    const int nobj = *(const int*)(tmdBase + 8);
    if (nobj < 1 || nobj > 2) return 0;       // the examine screen's own bound

    // Measured first, on the offsets the file carries. The next line turns
    // them into pointers.
    int span = RAID_ITEM_MODEL_SIZE, centreY = 0;
    raid_model_extent(tmdBase, s_loadBuf + read, &span, &centreY);

    ResolveAnimPointers(tmdBase + 4);

    unsigned char* page = s_pages[k];
    VideoDriver_ClearState348(page, g_pMarniDirect3D);
    memset(page, 0, sizeof(s_pages[k]));
    LoadPSXImage((PSXTexture*)page, s_loadBuf, 1);
    Direct3DTIM_Create(page, g_pMarniDirect3D);
    if (PAGE_CREATED(page) == 0) return 0;

    // Only the first object is drawn. An .ivm may carry two - the second is the
    // transparent half the examine screen draws in a separate blended pass -
    // and a pickup on a floor does not need it.
    CMarniDirect3DTMD* slot =
        (CMarniDirect3DTMD*)(void*)&g_raidItemTmdSlots[k * TMD_SLOT_STRIDE];
    slot->CleanupObjects(g_pMarniDirect3D);
    if (PSXObject_Store(slot, (int*)tmdBase, 0, 0xffffffff, 0x100) == 0) return 0;
    if (slot->Create(g_pMarniDirect3D, page, (void*)1) == 0) return 0;

    s_models[k].type    = type;
    s_models[k].ready   = 1;
    s_models[k].scale   = (short)((RAID_ITEM_MODEL_SIZE * 4096) / span);
    s_models[k].centreY = (centreY * RAID_ITEM_MODEL_SIZE) / span;
    if (s_models[k].scale < 1) s_models[k].scale = 1;
    return 1;
}

// The slot this type occupies, whether or not its load succeeded. Sync needs
// the difference: a slot that was tried and failed must still be remembered, or
// the same missing file is opened again every frame for the rest of the run.
static int raid_item_slot_of(unsigned char type)
{
    for (int k = 0; k < TMD_RAID_ITEM_SLOT_COUNT; k++)
        if (s_models[k].type == type) return k;
    return -1;
}

// The slot this type can actually be DRAWN from.
static int raid_item_model_find(unsigned char type)
{
    const int k = raid_item_slot_of(type);
    return (k >= 0 && s_models[k].ready) ? k : -1;
}

int RaidItemModels_Have(unsigned char itemType)
{
    return raid_item_model_find(itemType) >= 0;
}

void RaidItemModels_Reset(void)
{
    for (int k = 0; k < TMD_RAID_ITEM_SLOT_COUNT; k++) {
        if (s_models[k].ready) {
            CMarniDirect3DTMD* slot =
                (CMarniDirect3DTMD*)(void*)&g_raidItemTmdSlots[k * TMD_SLOT_STRIDE];
            slot->CleanupObjects(g_pMarniDirect3D);
            VideoDriver_ClearState348(s_pages[k], g_pMarniDirect3D);
        }
        memset(&s_models[k], 0, sizeof(s_models[k]));
    }
}

// ---------------------------------------------------------------------------
// Make the resident set match the level. Cheap when nothing changed, which is
// every frame but the one after a load.
// ---------------------------------------------------------------------------
void RaidItemModels_Sync(void)
{
    if (!g_raidLevel.loaded) return;

    for (int i = 0; i < g_raidLevel.nitem && i < RAID_MAX_ITEM; i++) {
        const unsigned char type = g_raidLevel.item[i].type;
        if (type == ITEM_NONE) continue;
        if (raid_item_slot_of(type) >= 0) continue;     // loaded, or already tried

        int free_k = -1;
        for (int k = 0; k < TMD_RAID_ITEM_SLOT_COUNT; k++)
            if (s_models[k].type == ITEM_NONE) { free_k = k; break; }
        if (free_k < 0) break;            // eight kinds is the limit; the rest stay boxes

        // A failed load claims the slot as "tried" so the file is not read
        // again every frame, but leaves ready clear so the marker box is drawn.
        if (!raid_item_model_load(free_k, type)) {
            s_models[free_k].type = type;
            s_models[free_k].ready = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw. One matrix per pickup, all from the same parsed mesh.
// ---------------------------------------------------------------------------
void RaidItemModels_Draw(void)
{
    if (!g_raidLevel.loaded || g_RdtPointer == NULL) return;

    for (int i = 0; i < g_raidLevel.nitem && i < RAID_MAX_ITEM; i++) {
        if (RaidItems_Taken(i)) continue;
        const RaidItem* I = &g_raidLevel.item[i];
        const int k = raid_item_model_find(I->type);
        if (k < 0) continue;                    // no model: RaidArena draws the box

        // A scaled identity, turned about Y by the engine's own RotMatrixY -
        // which composes Ry(r) * m and keeps the result in 4.12, so starting
        // from `scale` on the diagonal gives a rotation AND the size in one go.
        // Writing the sines out by hand here would be a second copy of a fixed
        // point convention this file has no business knowing.
        MATRIX world;
        memset(&world, 0, sizeof(world));
        world.m[0][0] = s_models[k].scale;
        world.m[1][1] = s_models[k].scale;
        world.m[2][2] = s_models[k].scale;
        RotMatrixY(I->angle & 0xFFF, &world);
        world.t[0] = I->x;
        world.t[1] = -RAID_ITEM_HOVER - s_models[k].centreY;
        world.t[2] = I->z;

        // Lit where it lies, by the room's own lights, through the same
        // update_entity_lighting a character goes through.
        VECTOR at;
        at.x = world.t[0]; at.y = world.t[1]; at.z = world.t[2]; at.pad = 0;

        CMarniDirect3DTMD* slot =
            (CMarniDirect3DTMD*)(void*)&g_raidItemTmdSlots[k * TMD_SLOT_STRIDE];
        TmdDrawSlotAt(slot, &world, &at, 4);
    }
}
