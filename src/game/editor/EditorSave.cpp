// EditorSave.cpp - writing the level back out from inside the game.
//
// CUSTOM.
//
// The text this produces is the same grammar RaidLevel.cpp reads, so a level
// saved here opens in the browser editor and the other way round. It keeps the
// comment header, because a file that loses its documentation every time
// someone nudges a box is a file nobody hand-edits twice.
//
// WHERE IT WRITES
//
// The exe knows where its own data is - GetAssetRoot() - and that is the copy
// F7 will re-read, so that one is written first and is the one that matters.
// The other two are guesses relative to the working directory, which for a
// build launched from bin\<config> puts the repository two levels up. They are
// attempted and counted, never required: a copy that does not land is reported,
// not fatal.
#include "EditorState.h"
#include "../../Globals.h"
#include "../Types.h"
#include "../RaidLevel.h"
#include "../../system/AssetPath.h"
#include <cstdio>
#include <cstring>

static char s_status[96] = "";

const char* EditorSave_Status(void) { return s_status; }

// The header the file opens with. Kept in one place so the two editors cannot
// drift about what a .lvl says it is.
static const char* const ED_HEADER =
"# ---------------------------------------------------------------------------\n"
"# RAID arena\n"
"#\n"
"# Read by src/game/RaidLevel.cpp at room load, and again on F7 without leaving\n"
"# the room. Written by tools/raid_editor.html and by the in-game editor (F2).\n"
"#\n"
"# Units are the game's own. X and Z are the floor plane; Y is NEGATIVE UPWARDS,\n"
"# so a ceiling is a negative number. Every coordinate is read back UNSIGNED by\n"
"# the collision and camera-zone tests, so keep the whole level at positive X/Z\n"
"# below 32768.\n"
"#\n"
"#   ambient <r> <g> <b>                          12-bit channels, 0..4095\n"
"#   light   <x> <y> <z> <r> <g> <b> <radius>     up to 3; radius 0 is a BLACK light\n"
"#   cam     <fx> <fy> <fz> <tx> <ty> <tz> <fov>  fov is a focal length, not an angle\n"
"#   camzone <cam> <x0> <z0> <x1> <z1>            walk in here, switch to that camera\n"
"#   spawn   <x> <z> <angle>                      0 = +X, 0x400 = +Z, 4096 = a turn\n"
"#   box     <x0> <y0> <z0> <x1> <y1> <z1> <flags> <shade> <r> <g> <b>\n"
"#   item    <x> <z> <angle> <type> <amount>      a pickup lying in the room\n"
"#   give    <type> <amount>                      one slot of the starting inventory\n"
"#   enemy   <x> <z> <angle> <type>\n"
"#\n"
"# box flags: 1 draw, 2 collide, 4 checkerboard.   shade is in hundredths.\n"
"# ---------------------------------------------------------------------------\n";

// ---------------------------------------------------------------------------
// The level as text. Returns the length written, or 0 if it would not fit.
// ---------------------------------------------------------------------------
static size_t ed_serialize(char* out, size_t cap)
{
    const RaidLevel* L = &g_raidLevel;
    size_t n = 0;

    #define ED_PUT(...) do { \
        if (n >= cap) return 0; \
        const int w = snprintf(out + n, cap - n, __VA_ARGS__); \
        if (w < 0 || (size_t)w >= cap - n) return 0; \
        n += (size_t)w; \
    } while (0)

    ED_PUT("%s", ED_HEADER);
    ED_PUT("ver 1\n\n");
    ED_PUT("ambient %d %d %d\n\n", L->ambR, L->ambG, L->ambB);

    for (int i = 0; i < L->nlight; i++) {
        const RaidLight* g = &L->light[i];
        ED_PUT("light %d %d %d %d %d %d %d\n",
               g->x, g->y, g->z, g->r, g->g, g->b, g->radius);
    }
    if (L->nlight) ED_PUT("\n");

    for (int i = 0; i < L->ncam; i++) {
        const RaidCam* c = &L->cam[i];
        ED_PUT("cam %6d %6d %6d  %6d %6d %6d  %d\n",
               c->fx, c->fy, c->fz, c->tx, c->ty, c->tz, c->fov);
    }
    if (L->ncam) ED_PUT("\n");

    for (int i = 0; i < L->nzone; i++) {
        const RaidZone* z = &L->zone[i];
        ED_PUT("camzone %d %6d %6d %6d %6d\n", z->cam, z->x0, z->z0, z->x1, z->z1);
    }
    if (L->nzone) ED_PUT("\n");

    ED_PUT("spawn %d %d %d\n\n", L->spawnX, L->spawnZ, L->spawnAngle);

    for (int i = 0; i < L->nbox; i++) {
        const RaidBox* b = &L->box[i];
        ED_PUT("box %6d %6d %6d  %6d %6d %6d  %d %d %d %d %d\n",
               b->x0, b->y0, b->z0, b->x1, b->y1, b->z1,
               b->flags, (int)(b->shade * 100.0f + 0.5f), b->tr, b->tg, b->tb);
    }

    if (L->ngive) ED_PUT("\n");
    for (int i = 0; i < L->ngive; i++)
        ED_PUT("give %3d %3d\n", L->give[i].type, L->give[i].amount);

    if (L->nitem) ED_PUT("\n");
    for (int i = 0; i < L->nitem; i++) {
        const RaidItem* it = &L->item[i];
        ED_PUT("item %6d %6d %5d %3d %3d\n",
               it->x, it->z, it->angle, it->type, it->amount);
    }

    if (L->nenemy) ED_PUT("\n");
    for (int i = 0; i < L->nenemy; i++) {
        const RaidEnemy* e = &L->enemy[i];
        ED_PUT("enemy %6d %6d %5d %3d\n", e->x, e->z, e->angle, e->type);
    }

    #undef ED_PUT
    return n;
}

static int ed_write_file(const char* path, const char* text, size_t len)
{
    FILE* f = fopen(path, "wb");
    if (f == NULL) return 0;
    const size_t w = fwrite(text, 1, len, f);
    fclose(f);
    return (w == len) ? 1 : 0;
}

int EditorSave_Write(void)
{
    if (!g_raidLevel.loaded) {
        strcpy(s_status, "nothing loaded to save");
        return 0;
    }

    static char text[48 * 1024];
    const size_t len = ed_serialize(text, sizeof(text));
    if (len == 0) {
        strcpy(s_status, "level too large to serialise");
        return 0;
    }

    int n = 0;

    // The copy this build reads. GetAssetRoot() is the runtime root, which
    // config.ini can move - so asking it is the only way to be sure the file
    // lands where F7 will look for it.
    char own[300];
    snprintf(own, sizeof(own), "%sData%craid1.lvl", GetAssetRoot(),
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
             );
    if (ed_write_file(own, text, len)) n++;

    // The repository's own tree and the sibling build, relative to a working
    // directory of bin\<config>. Guesses, and treated as such.
    static const char* const others[] = {
#ifdef _WIN32
        "..\\..\\assets\\USA\\Data\\raid1.lvl",
        "..\\Debug\\USA\\Data\\raid1.lvl",
        "..\\Release\\USA\\Data\\raid1.lvl",
#else
        "../../assets/USA/Data/raid1.lvl",
        "../Debug/USA/Data/raid1.lvl",
        "../Release/USA/Data/raid1.lvl",
#endif
    };
    for (unsigned int i = 0; i < sizeof(others)/sizeof(others[0]); i++)
        if (ed_write_file(others[i], text, len)) n++;

    snprintf(s_status, sizeof(s_status), "saved %d cop%s, %d bytes",
             n, (n == 1) ? "y" : "ies", (int)len);
    return n;
}
