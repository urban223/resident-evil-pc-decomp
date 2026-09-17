// RaidLevel.h - the RAID arena, as data.
//
// CUSTOM. The arena used to be two hardcoded places: the geometry as #defines
// in RaidArena.cpp and the collision, camera and spawn baked into the room's
// RDT by tools/build_raid_room.py. Changing the room meant editing both and
// rebuilding an asset.
//
// It is one text file now (Data\raid1.lvl), read at room load and rebuilt into
// the engine's own structures IN MEMORY. The RDT stays a fixed, empty shell.
// Nothing about a level needs a bake, which is what makes an editor and a
// reload key possible at all.
#pragma once
#include "../platform/types.h"

#define RAID_MAX_BOX    128
#define RAID_MAX_CAM      8
#define RAID_MAX_ZONE    32
#define RAID_MAX_ENEMY   32
#define RAID_MAX_LIGHT    3
#define RAID_MAX_ITEM    32
#define RAID_MAX_GIVE     8   // Jill's whole inventory; there is nowhere to put a ninth

// Box flags. A box can be any combination: scenery that is walked through,
// an invisible wall, or the usual both.
#define RAID_BOX_DRAW     0x01
#define RAID_BOX_SOLID    0x02
#define RAID_BOX_CHECKER  0x04   // alternate the two shades cell by cell

struct RaidBox {
    short x0, y0, z0;
    short x1, y1, z1;
    unsigned short flags;
    float shade;                 // base brightness, 0..1
    unsigned char tr, tg, tb;    // tint, 0..255
};

struct RaidCam {
    int fx, fy, fz;              // eye
    int tx, ty, tz;              // look-at
    int fov;                     // focal length in 320-wide space, NOT an angle
};

// Walking into this rectangle switches to camera `cam`. Empty table = the
// level has one camera and never switches.
struct RaidZone {
    short cam;
    short x0, z0, x1, z1;
};

// A pickup lying in the room. `type` is an ITEM_* id and `amount` is what the
// slot gets when it is taken - rounds for ammo, uses for a spray, and ignored
// by anything that does not stack.
struct RaidItem {
    short x, z;
    short angle;
    unsigned char type;
    unsigned char amount;
};

// One line of the mode's starting inventory, in slot order.
struct RaidGive {
    unsigned char type;
    unsigned char amount;
};

struct RaidEnemy {
    short x, z;
    short angle;
    unsigned char type;
};

struct RaidLight {
    int x, y, z;
    unsigned char r, g, b;
    short radius;
};

struct RaidLevel {
    int  loaded;
    int  nbox, ncam, nzone, nlight, nenemy, nitem, ngive;
    short ambR, ambG, ambB;
    int  spawnX, spawnZ, spawnAngle;
    RaidBox   box[RAID_MAX_BOX];
    RaidCam   cam[RAID_MAX_CAM];
    RaidZone  zone[RAID_MAX_ZONE];
    RaidLight light[RAID_MAX_LIGHT];
    RaidEnemy enemy[RAID_MAX_ENEMY];
    RaidItem  item[RAID_MAX_ITEM];
    RaidGive  give[RAID_MAX_GIVE];
};

extern RaidLevel g_raidLevel;
extern int g_raidReloadRequest;   // set by the reload key, consumed at draw time

int  RaidLevel_Load(void);        // read the file; 1 on success, level untouched on failure
void RaidLevel_Apply(void);       // push camera, lights and collision into the loaded RDT

// RaidItems.cpp - the pickups lying in the room.
void RaidItems_Reset(void);       // a (re)loaded level has taken nothing
void RaidItems_Update(void);      // once a frame: what is in reach, and the take button
int  RaidItems_Taken(int i);      // 1 once the player has it; the renderer skips those
int  RaidItems_Reach(int i);      // 1 while it is close enough to take - the marker lights up
int  RaidItems_Give(void);        // fill the inventory from g_raidLevel.give[]; 0 if it is empty
