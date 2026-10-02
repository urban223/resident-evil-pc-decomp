// RaidLevel.cpp - reading a RAID arena out of a text file and into the engine.
//
// CUSTOM. See RaidLevel.h for why this exists at all.
//
// THE FORMAT
//
// One directive per line, whitespace separated, '#' to end of line is a
// comment. Everything is integers in the game's own world units, and the
// engine's conventions apply unchanged: X and Z are the floor plane, and Y is
// NEGATIVE UPWARDS, so a ceiling is a negative number.
//
//   ver     1
//   ambient <r> <g> <b>                          12-bit channels, 0..4095
//   light   <x> <y> <z> <r> <g> <b> <radius>     up to 8; radius 0 is a BLACK light
//   cam     <fx> <fy> <fz> <tx> <ty> <tz> <fov>  fov is a focal length, not an angle
//   camzone <cam> <x0> <z0> <x1> <z1>            walk in here, switch to that camera
//   spawn   <x> <z> <angle>                      angle: 0 = +X, 0x400 = +Z, 4096 = a turn
//   box     <x0> <y0> <z0> <x1> <y1> <z1> <flags> <shade> <r> <g> <b>
//   enemy   <x> <z> <angle> <type>
//   item    <x> <z> <angle> <type> <amount>     a pickup lying in the room
//   give    <type> <amount>                     one slot of the starting inventory
//   bgsrc   <stage> <room> <cam> <fx> <fy> <fz> <tx> <ty> <tz> <fov>
//   mesh    <id> <x> <y> <z> <yaw> <scale> <flags> <tex> <tile>
//   tbox    <x0> <y0> <z0> <x1> <y1> <z1> <flags> <tex> <tile> <shade>
//   mirror  <axis> <plane> <min> <max> <ytop> <ybot>
//   door    <hx> <hz> <width> <depth>
//
// `bgsrc` names one of the game's pre-rendered backgrounds (StageS\RCSRRC.pak,
// stage 1-based, room and camera as in the file name) and the camera it was
// rendered from, copied out of that room's RDT. Boxes with flag 8 are
// textured by projecting those images back onto them from those cameras.
//
// `mesh` places Data\raidmesh\m<id>.obj (game units, Y negative up) at
// (x, y, z), turned by yaw (4096 to the turn) and scaled by scale percent,
// and covers it with material <tex> repeating every <tile> units (tex 0: the
// model's own colour). flags as a box's: 1 draw, 8 projected - and 32: a lamp,
// which shows its own light instead of taking the room's. It never
// collides - boxes do that.
//
// `tbox` is a box covered with a material instead of a flat tint:
// Data\raidtex\t<tex>.bin, repeating every <tile> world units across each
// face, laid from the box's own corner (so a one-off picture - a door - sits
// in its box exactly), or from the world origin with flag 64 (so the pieces
// of a wall cut round a hole still line up).
//
// `mirror` arms the game's own planar reflection (SCD opcode 0x0F's): the
// plane <axis> 1 is X = <plane> (0: Z = <plane>), the glass spanning <min>..
// <max> along the other floor axis and <ytop>..<ybot> up the wall. The
// characters are reflected by the original code; the arena draws the room's
// reflection itself, which shows through a hole the level must leave in the
// wall there. One per level.
//
// `door` is a door that swings open as you walk into it, instead of cutting
// to another room: hinged at (hx, hz), `width` along +X when shut, its
// doorway `depth` thick across the wall. The loader makes the doorway's solid
// box itself; every `mesh` placed exactly at the hinge swings with the door.
// `light` lines may number up to 8: the arena is lit by all of them, the
// characters (the RDT's three light slots) by the three nearest player 1.
//
// `type` in both is an ITEM_* id from Types.h. `give` lines are taken in order
// and fill the eight slots; a level with no `give` line at all keeps the mode's
// built-in loadout rather than starting the player empty-handed.
//
// flags is the bitmask from RaidLevel.h: 1 draw, 2 collide, 4 checkerboard,
// 8 projected (textured from the bgsrc backdrops), 64 a tbox's material laid
// from the world origin.
// shade is brightness in hundredths (72 = 0.72), so the whole file stays
// integers and an editor never has to think about locales and decimal points.
//
// Everything is bounded and a bad line is skipped rather than fatal: this file
// is going to be written by an editor and hand-edited, and a level that loses
// one wall is a better failure than a level that crashes.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"
#include "RaidLevel.h"
#include "RaidItemModels.h"
#include "CoopPlayer.h"
#include "FileLoader.h"
#include "../system/AssetPath.h"
#include <cstring>
#include <cstdlib>

RaidLevel g_raidLevel;
int g_raidReloadRequest = 0;

#define RAID_LEVEL_PATH  GAME_DATA_ROOT "Data\\raid1.lvl"
#define RAID_LEVEL_MAX   65536

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
static char* raid_tok(char** p)
{
    char* s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0' || *s == '#') { *p = s; return NULL; }
    char* start = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) { *s = '\0'; s++; }
    *p = s;
    return start;
}

// Reads `count` integers off the line. Returns 0 - and the caller drops the
// whole directive - if any of them is missing.
static int raid_ints(char** p, int* out, int count)
{
    for (int i = 0; i < count; i++) {
        char* t = raid_tok(p);
        if (t == NULL) return 0;
        out[i] = (int)strtol(t, NULL, 10);
    }
    return 1;
}

static unsigned char raid_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (unsigned char)v;
}

// ---------------------------------------------------------------------------
// The directives. One row per keyword: how many integers it reads, and what it
// does with them. A keyword is a row and a handler here, a line in the writer
// (editor/EditorSave.cpp) and a line in both grammar comments - this file's
// and the writer's - and tests/check_raid_level_grammar.py fails when one of
// the four is missed.
//
// `nargs` is how many integers the line must have, not how many it may:
// anything after them is ignored, as it always was, so a column added by a
// newer build does not make an older one drop the line.
//
// The handlers keep the values exactly as they come off the line - negative
// Y is how a ceiling is spelled, and nothing here clamps a coordinate. Keeping
// X/Z positive and below 32768 is the file's job (the collision and zone
// tests read them back UNSIGNED); a parser that started fixing them up would
// hide a broken level instead of showing it.
// ---------------------------------------------------------------------------
#define RAID_MAX_ARGS 12

struct RaidDirective {
    const char* key;
    int         nargs;
    void      (*apply)(RaidLevel* lv, const int* v);
};

static void raid_ambient(RaidLevel* lv, const int* v)
{
    lv->ambR = (short)v[0]; lv->ambG = (short)v[1]; lv->ambB = (short)v[2];
}

static void raid_light(RaidLevel* lv, const int* v)
{
    if (lv->nlight >= RAID_MAX_LIGHT) return;
    RaidLight* L = &lv->light[lv->nlight++];
    L->x = v[0]; L->y = v[1]; L->z = v[2];
    L->r = raid_u8(v[3]); L->g = raid_u8(v[4]); L->b = raid_u8(v[5]);
    L->radius = (short)v[6];
}

static void raid_cam(RaidLevel* lv, const int* v)
{
    if (lv->ncam >= RAID_MAX_CAM) return;
    RaidCam* C = &lv->cam[lv->ncam++];
    C->fx = v[0]; C->fy = v[1]; C->fz = v[2];
    C->tx = v[3]; C->ty = v[4]; C->tz = v[5];
    C->fov = v[6];
}

static void raid_camzone(RaidLevel* lv, const int* v)
{
    if (lv->nzone >= RAID_MAX_ZONE) return;
    RaidZone* Z = &lv->zone[lv->nzone++];
    Z->cam = (short)v[0];
    Z->x0 = (short)v[1]; Z->z0 = (short)v[2];
    Z->x1 = (short)v[3]; Z->z1 = (short)v[4];
}

static void raid_spawn(RaidLevel* lv, const int* v)
{
    lv->spawnX = v[0]; lv->spawnZ = v[1]; lv->spawnAngle = v[2];
}

static void raid_box(RaidLevel* lv, const int* v)
{
    if (lv->nbox >= RAID_MAX_BOX) return;
    RaidBox* B = &lv->box[lv->nbox++];
    // Normalise, so an editor may drag a box out in any direction and the
    // collision still sees a MAX corner and a MIN corner.
    B->x0 = (short)(v[0] < v[3] ? v[0] : v[3]);
    B->x1 = (short)(v[0] < v[3] ? v[3] : v[0]);
    B->y0 = (short)(v[1] < v[4] ? v[1] : v[4]);
    B->y1 = (short)(v[1] < v[4] ? v[4] : v[1]);
    B->z0 = (short)(v[2] < v[5] ? v[2] : v[5]);
    B->z1 = (short)(v[2] < v[5] ? v[5] : v[2]);
    B->flags = (unsigned short)v[6];
    B->shade = (float)v[7] * 0.01f;
    B->tr = raid_u8(v[8]); B->tg = raid_u8(v[9]); B->tb = raid_u8(v[10]);
}

static void raid_enemy(RaidLevel* lv, const int* v)
{
    if (lv->nenemy >= RAID_MAX_ENEMY) return;
    RaidEnemy* E = &lv->enemy[lv->nenemy++];
    E->x = (short)v[0]; E->z = (short)v[1];
    E->angle = (short)v[2]; E->type = raid_u8(v[3]);
}

static void raid_item(RaidLevel* lv, const int* v)
{
    if (lv->nitem >= RAID_MAX_ITEM) return;
    RaidItem* I = &lv->item[lv->nitem++];
    I->x = (short)v[0]; I->z = (short)v[1];
    I->angle = (short)v[2];
    I->type = raid_u8(v[3]); I->amount = raid_u8(v[4]);
}

static void raid_give(RaidLevel* lv, const int* v)
{
    if (lv->ngive >= RAID_MAX_GIVE) return;
    RaidGive* G = &lv->give[lv->ngive++];
    G->type = raid_u8(v[0]); G->amount = raid_u8(v[1]);
}

static void raid_mesh(RaidLevel* lv, const int* v)
{
    if (lv->nmesh >= RAID_MAX_MESH) return;
    RaidMesh* M = &lv->mesh[lv->nmesh++];
    M->id = raid_u8(v[0]);
    M->x = v[1]; M->y = v[2]; M->z = v[3];
    M->yaw = (short)v[4];
    M->scale = (short)(v[5] > 0 ? v[5] : 100);
    M->flags = (unsigned short)v[6];
    M->tex = raid_u8(v[7]);
    M->tile = (short)(v[8] > 0 ? v[8] : 1000);
}

static void raid_tbox(RaidLevel* lv, const int* v)
{
    // A box line with a material for a tint: the same box, so collision,
    // bounds and the editor all treat it as one.
    int b[11] = { v[0], v[1], v[2], v[3], v[4], v[5], v[6] | RAID_BOX_TEX, v[9], 255, 255, 255 };
    const int before = lv->nbox;
    raid_box(lv, b);
    if (lv->nbox > before) {
        RaidBox* B = &lv->box[lv->nbox - 1];
        B->tex  = raid_u8(v[7]);
        B->tile = (short)(v[8] > 0 ? v[8] : 1000);
    }
}

static void raid_bgsrc(RaidLevel* lv, const int* v)
{
    if (lv->nbgsrc >= RAID_MAX_BGSRC) return;
    RaidBgSrc* S = &lv->bgsrc[lv->nbgsrc++];
    S->stage = raid_u8(v[0]); S->room = raid_u8(v[1]); S->cam = raid_u8(v[2]);
    S->view.fx = v[3]; S->view.fy = v[4]; S->view.fz = v[5];
    S->view.tx = v[6]; S->view.ty = v[7]; S->view.tz = v[8];
    S->view.fov = v[9];
}

static void raid_mirror(RaidLevel* lv, const int* v)
{
    RaidMirror* M = &lv->mirror;
    M->on    = 1;
    M->axis  = (unsigned char)(v[0] != 0);
    M->plane = v[1];
    M->min   = v[2] < v[3] ? v[2] : v[3];
    M->max   = v[2] < v[3] ? v[3] : v[2];
    M->ytop  = v[4] < v[5] ? v[4] : v[5];
    M->ybot  = v[4] < v[5] ? v[5] : v[4];
}

static void raid_door(RaidLevel* lv, const int* v)
{
    if (lv->ndoor >= RAID_MAX_DOOR || v[2] <= 0) return;
    RaidDoor* D = &lv->door[lv->ndoor];
    memset(D, 0, sizeof(*D));
    D->hx = v[0]; D->hz = v[1]; D->width = v[2]; D->depth = v[3] > 0 ? v[3] : 300;
    // The doorway: solid while the door is shut. Drawn by nothing - the door
    // leaf is a mesh - and written back by nothing (EditorSave skips it).
    const int half = D->depth / 2;
    int b[11] = { D->hx, -4000, D->hz - half, D->hx + D->width, 0, D->hz + half,
                  RAID_BOX_SOLID | RAID_BOX_DOOR, 40, 0, 0, 0 };
    const int before = lv->nbox;
    raid_box(lv, b);
    if (lv->nbox == before) return;
    D->box = lv->nbox - 1;
    lv->ndoor++;
}

// "ver" is not in here, and neither is anything unknown: both are ignored on
// purpose, so an older build reads a newer file instead of refusing it.
static const RaidDirective kRaidDirectives[] = {
    { "ambient",  3, raid_ambient },
    { "light",    7, raid_light },
    { "cam",      7, raid_cam },
    { "camzone",  5, raid_camzone },
    { "spawn",    3, raid_spawn },
    { "box",     11, raid_box },
    { "enemy",    4, raid_enemy },
    { "item",     5, raid_item },
    { "give",     2, raid_give },
    { "bgsrc",   10, raid_bgsrc },
    { "mesh",     9, raid_mesh },
    { "tbox",    10, raid_tbox },
    { "mirror",   6, raid_mirror },
    { "door",     4, raid_door },
};

static const RaidDirective* raid_directive(const char* key)
{
    if (key == NULL) return NULL;               // blank or comment
    for (unsigned int i = 0; i < sizeof(kRaidDirectives) / sizeof(kRaidDirectives[0]); i++) {
        const RaidDirective* d = &kRaidDirectives[i];
        if (strcmp(key, d->key) == 0) {
            // A row wider than v[] would overrun it. The grammar check
            // catches that before it is committed; this keeps it out of memory.
            return (d->nargs <= RAID_MAX_ARGS) ? d : NULL;
        }
    }
    return NULL;
}

int RaidLevel_Load(void)
{
    static char buf[RAID_LEVEL_MAX];

    const size_t read = LoadFile(RAID_LEVEL_PATH, buf, 0);
    if (read == (size_t)-1 || read == 0 || read >= RAID_LEVEL_MAX) {
        return 0;
    }
    buf[read] = '\0';

    // Parse into a scratch level and only publish a COMPLETE one. A reload
    // that hits a half-written file must not leave the player standing in a
    // room that is half the old one and half the new.
    static RaidLevel lv;
    memset(&lv, 0, sizeof(lv));
    lv.ambR = 880; lv.ambG = 850; lv.ambB = 920;

    char* line = buf;
    while (*line) {
        char* eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char* next = eol;
        while (*next == '\n' || *next == '\r') next++;
        *eol = '\0';

        char* p = line;
        const RaidDirective* d = raid_directive(raid_tok(&p));
        int v[RAID_MAX_ARGS];
        if (d != NULL && raid_ints(&p, v, d->nargs)) {
            d->apply(&lv, v);
        }

        line = next;
    }

    // A level with no camera cannot be looked at and a level with no geometry
    // is not a level. Either one means the file is wrong; keep the old one.
    if (lv.ncam == 0 || lv.nbox == 0) return 0;

    lv.loaded = 1;
    g_raidLevel = lv;
    RaidItems_Reset();      // a reloaded level has taken nothing: the items moved
    RaidItemModels_Reset(); // and may hold different kinds of them
    return 1;
}

// ---------------------------------------------------------------------------
// Pushing it into the engine
//
// The RDT is a fixed shell with room for RAID_MAX_CAM cameras and empty
// tables. Everything below overwrites those in the loaded copy, or points the
// RDT's own pointers at static tables built here. None of it touches the file
// on disk, so a reload is just this function again.
// ---------------------------------------------------------------------------

// Collision. The engine's boundary records are 2D - a box's footprint in XZ -
// and the quadrant lists are a point test: whichever list the player's own
// quadrant selects is the only one tested that frame. Rather than work out
// which records straddle which split, every record goes in every list. It
// costs four times the table and removes the question.
static RDT_BoundaryHeader s_bounds;
static RDT_Boundary       s_boundRecs[RAID_MAX_BOX * 4];

static void raid_build_collision(void)
{
    int n = 0;
    RDT_Boundary* rec = s_boundRecs;

    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < g_raidLevel.nbox; i++) {
            const RaidBox* B = &g_raidLevel.box[i];
            if ((B->flags & RAID_BOX_SOLID) == 0) continue;
            rec->xMax = (unsigned short)B->x1;
            rec->zMax = (unsigned short)B->z1;
            rec->xMin = (unsigned short)B->x0;
            rec->zMin = (unsigned short)B->z0;
            rec->type  = 0x0001;   // collision_push_rect
            rec->flags = 0x0300;   // 0x100 blocks, 0x200 joins the re-test pass
            rec++;
            if (pass == 0) n++;
        }
    }

    // The cell split decides which quadrant a point is in. Every list holds
    // every record, so the split itself does not matter - centre it on the
    // level and be done.
    int cx = 0, cz = 0;
    if (g_raidLevel.nbox > 0) {
        int xlo = 32767, xhi = 0, zlo = 32767, zhi = 0;
        for (int i = 0; i < g_raidLevel.nbox; i++) {
            const RaidBox* B = &g_raidLevel.box[i];
            if (B->x0 < xlo) xlo = B->x0;
            if (B->x1 > xhi) xhi = B->x1;
            if (B->z0 < zlo) zlo = B->z0;
            if (B->z1 > zhi) zhi = B->z1;
        }
        cx = (xlo + xhi) / 2;
        cz = (zlo + zhi) / 2;
    }
    s_bounds.cellX = (short)cx;
    s_bounds.cellZ = (short)cz;

    // Room_SetupCollisionCallbacks turns counts into pointers, but it has
    // already run for this room by the time we get here - so write the
    // POINTERS, which is the state it would have left behind.
    for (int q = 0; q <= 4; q++) {
        s_bounds.group[q] = s_boundRecs + q * n;
    }

    g_RdtPointer->boundaries = (unsigned char*)&s_bounds;
}

// Camera switch zones, in the shape display_room_camera_bg and
// check_camera_switch walk: one GROUP HEADER per camera carrying that camera's
// own quad, its zones after it, and a 0xFFFF terminator at the very end
// without which check_camera_switch runs off into the collision table.
static CAM_SWITCH_ZONE s_zones[RAID_MAX_CAM + RAID_MAX_ZONE + 1];

static void raid_fill_zone(CAM_SWITCH_ZONE* z, int to, int from,
                           int x0, int z0, int x1, int z1)
{
    // Corner order is (xMax,zMax) (xMax,zMin) (xMin,zMin) (xMin,zMax) - the
    // winding is_entity_in_switch_zone's four cross products expect. All of
    // them are read back UNSIGNED, so a level below the origin does not work.
    z->camTo = (short)to;
    z->camFrom = (short)from;
    z->x0 = (short)x1; z->y0 = (short)z1;
    z->x1 = (short)x1; z->y1 = (short)z0;
    z->x2 = (short)x0; z->y2 = (short)z0;
    z->x3 = (short)x0; z->y3 = (short)z1;
}

static void raid_build_zones(void)
{
    int xlo = 32767, xhi = 0, zlo = 32767, zhi = 0;
    for (int i = 0; i < g_raidLevel.nbox; i++) {
        const RaidBox* B = &g_raidLevel.box[i];
        if (B->x0 < xlo) xlo = B->x0;
        if (B->x1 > xhi) xhi = B->x1;
        if (B->z0 < zlo) zlo = B->z0;
        if (B->z1 > zhi) zhi = B->z1;
    }

    int n = 0;
    for (int c = 0; c < g_raidLevel.ncam; c++) {
        // The header's own quad is live data: the player is tested against it
        // every frame and bit 0 of zoneFlags gates her shadow, so it covers
        // the whole level.
        raid_fill_zone(&s_zones[n++], c, c, xlo, zlo, xhi, zhi);
        for (int i = 0; i < g_raidLevel.nzone && n < RAID_MAX_CAM + RAID_MAX_ZONE; i++) {
            const RaidZone* Z = &g_raidLevel.zone[i];
            if (Z->cam == c) continue;              // a zone leads AWAY from a camera
            raid_fill_zone(&s_zones[n++], Z->cam, c, Z->x0, Z->z0, Z->x1, Z->z1);
        }
    }
    memset(&s_zones[n], 0, sizeof(s_zones[n]));
    s_zones[n].camTo = (short)0xFFFF;
    s_zones[n].camFrom = (short)0xFFFF;

    g_RdtPointer->cam_switch_zones = (unsigned char*)s_zones;
    g_CurrentRdtDataTypePtr = s_zones;
}

void RaidLevel_Apply(void)
{
    if (!g_raidLevel.loaded || g_RdtPointer == NULL) return;

    // Cameras. The shell RDT carries RAID_MAX_CAM slots; fill the ones the
    // level uses and leave the rest as they were - cameras_count is what
    // bounds every reader, so shrink that instead of clearing slots.
    RDT_Camera* cams = (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
    int nc = g_raidLevel.ncam;
    if (nc > RAID_MAX_CAM) nc = RAID_MAX_CAM;
    for (int i = 0; i < nc; i++) {
        const RaidCam* C = &g_raidLevel.cam[i];
        cams[i].cam_from_x = C->fx; cams[i].cam_from_y = C->fy; cams[i].cam_from_z = C->fz;
        cams[i].cam_to_x   = C->tx; cams[i].cam_to_y   = C->ty; cams[i].cam_to_z   = C->tz;
        cams[i].roll = 0;
        cams[i].fov  = C->fov;
    }
    g_RdtPointer->cameras_count = (unsigned char)nc;

    // Lights. A zeroed RDT_Light is a BLACK light, not an absent one -
    // update_entity_lighting short-circuits every channel when the radius is
    // 0 - so an unused slot is filled from the last one rather than cleared.
    RDT_Light* dst = g_RdtPointer->lights;
    for (int i = 0; i < 3; i++) {
        const RaidLight* L = &g_raidLevel.light[(i < g_raidLevel.nlight)
                                                ? i
                                                : (g_raidLevel.nlight ? g_raidLevel.nlight - 1 : 0)];
        if (g_raidLevel.nlight == 0) break;
        dst[i].pos_x = L->x; dst[i].pos_y = L->y; dst[i].pos_z = L->z;
        dst[i].red = L->r; dst[i].green = L->g; dst[i].blue = L->b;
        dst[i].zero1 = 0;
        dst[i].lightType = 0;
        dst[i].radius = L->radius;
    }

    setBackColor((unsigned short)g_raidLevel.ambR,
                 (unsigned short)g_raidLevel.ambG,
                 (unsigned short)g_raidLevel.ambB);

    raid_build_collision();
    raid_build_zones();

    // CUSTOM: the mirror's numbers, which a reload may have moved. Only the
    // numbers: the pass itself is armed by RaidMirror_Arm, once per room entry,
    // because arming carves the reflected joint copies out of the load arena
    // and a reload must not carve them again. A level that has lost its mirror
    // switches the pass off.
    if (g_raidLevel.mirror.on) {
        g_mirrorPlaneCoord = (unsigned short)g_raidLevel.mirror.plane;
        g_mirrorExtentMin  = (unsigned short)g_raidLevel.mirror.min;
        g_mirrorExtentMax  = (unsigned short)g_raidLevel.mirror.max;
        g_main_state_flags = (g_main_state_flags & ~MSF_MIRROR_PLANE_X)
                           | (g_raidLevel.mirror.axis ? MSF_MIRROR_PLANE_X : 0);
    } else {
        g_main_state_flags &= ~(MSF_MIRROR_ENABLE | MSF_MIRROR_PLANE_X);
    }
}

void RaidLevel_RebuildCollision(void)
{
    if (!g_raidLevel.loaded || g_RdtPointer == NULL) return;
    raid_build_collision();
}

// The RDT has three light slots, and they light the characters. A level with
// more lights than that (a second room through a door) gives them the three
// nearest the given point - player 1, every frame.
void RaidLevel_LightsNear(int x, int z)
{
    if (!g_raidLevel.loaded || g_RdtPointer == NULL || g_raidLevel.nlight <= 3) return;
    int pick[3] = { -1, -1, -1 };
    for (int k = 0; k < 3; k++) {
        double best = 1e30;
        for (int i = 0; i < g_raidLevel.nlight && i < RAID_MAX_LIGHT; i++) {
            if (i == pick[0] || i == pick[1]) continue;
            const double dx = g_raidLevel.light[i].x - x, dz = g_raidLevel.light[i].z - z;
            const double d = dx * dx + dz * dz;
            if (d < best) { best = d; pick[k] = i; }
        }
    }
    RDT_Light* dst = g_RdtPointer->lights;
    for (int k = 0; k < 3; k++) {
        if (pick[k] < 0) continue;
        const RaidLight* L = &g_raidLevel.light[pick[k]];
        dst[k].pos_x = L->x; dst[k].pos_y = L->y; dst[k].pos_z = L->z;
        dst[k].red = L->r; dst[k].green = L->g; dst[k].blue = L->b;
        dst[k].radius = L->radius;
    }
}

// ---------------------------------------------------------------------------
// RaidMirror_Arm - what cmd_mirror_set (SCD opcode 0x0F) does, for the arena.
//
// The opcode raises the pass and then gives the player the second joint array
// the reflection is drawn from (SetupEntityJointAnimation, entity +0xAC) and
// the weapon joint's copy (FUN_0048bfe0 / FUN_0048c020); a story room's
// enemies get their copy at room init, because the flag is already up when
// they are built (RoomInit.cpp). The arena's bodies are all built before this
// runs, so every one of them gets its copy here: both players, the arena's
// enemies, and the zombies held in reserve - every body whose model the copy
// can be made from. The rest (see raid_mirror_cost) go without, and the reads
// through +0xAC that JointApplyColorTint and the hit-effect setup make on any
// entity they touch are guarded for a body with no copy.
//
// The copies come out of the load arena, so the bill is added up first and a
// room that would overrun g_DataBuffer gets no mirror rather than a corrupted
// heap.
// ---------------------------------------------------------------------------
extern void SetupEntityJointAnimation(void);       // 0x0048bef0
extern void FUN_0048bfe0(void);                    // 0x0048bfe0
extern void FUN_0048c020(int param);               // 0x0048c020

// What one body's copy costs, or 0 when it cannot have one. The slot table's
// first dword is its own end pointer in every model the game loads; a body
// assembled by the port (the co-op player-zombie, coop_build_zombie_body) does
// not keep that promise, and copying "end - start" bytes of it would run off
// into the arena - so a table that does not look like one is refused.
#define RAID_MIRROR_SLOTS_MAX  0x20000

static size_t raid_mirror_cost(const Entity* e, int player)
{
    if (e->jointCount == 0 || e->jointsStructs == NULL) return 0;
    const int* slots = (const int*)e->jointsStructs->anim_slot_ptr;
    if (slots == NULL) return 0;
    const int table = *slots - (int)slots;
    if (table <= 0 || table > RAID_MIRROR_SLOTS_MAX) return 0;
    size_t n = (size_t)e->jointCount * (0x7c + 0x2d * 4);   // joints + one anim object each
    n += (size_t)(table & ~3);                              // the copied slot table
    if (player) n += 0x7A00 + 0x1A00;                       // FUN_0048bfe0's two buffers
    return n + 64;
}

void RaidMirror_Arm(void)
{
    g_main_state_flags &= ~(MSF_MIRROR_ENABLE | MSF_MIRROR_PLANE_X);
    if (!g_raidLevel.loaded || !g_raidLevel.mirror.on) return;

    const int players = Coop_PlayerCount();
    size_t need = 0;
    for (int p = 0; p < players; p++) need += raid_mirror_cost((const Entity*)&g_players[p], 1);
    for (int s = 0; s < 30; s++) need += raid_mirror_cost(&g_EnemiesList[s], 0);
    if ((unsigned char*)g_loadDataDestPointer + need > g_DataBuffer + sizeof(g_DataBuffer)) {
        return;     // no room: the mirror stays a dark glass
    }

    Entity* const save = ENTITY;
    for (int p = 0; p < players; p++) {
        Coop_BeginPlayer(p);
        ENTITY = (Entity*)&g_playerEntity;
        if (raid_mirror_cost(ENTITY, 1) != 0) {
            SetupEntityJointAnimation();
            FUN_0048bfe0();
            FUN_0048c020(0xe);
        }
        Coop_EndPlayer();
    }
    for (int s = 0; s < 30; s++) {
        Entity* e = &g_EnemiesList[s];
        if (raid_mirror_cost(e, 0) == 0) continue;     // no copy: not drawn in the glass
        ENTITY = e;
        SetupEntityJointAnimation();
    }
    ENTITY = save;

    g_mirrorPlaneCoord = (unsigned short)g_raidLevel.mirror.plane;
    g_mirrorExtentMin  = (unsigned short)g_raidLevel.mirror.min;
    g_mirrorExtentMax  = (unsigned short)g_raidLevel.mirror.max;
    g_main_state_flags |= MSF_MIRROR_ENABLE
                        | (g_raidLevel.mirror.axis ? MSF_MIRROR_PLANE_X : 0);
}
