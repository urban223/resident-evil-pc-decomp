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
#define RAID_MAX_LIGHT    8   // the arena lights by all of them; the RDT's 3 slots get the nearest
#define RAID_MAX_DOOR     4
#define RAID_MAX_ITEM    32
#define RAID_MAX_GIVE     8   // Jill's whole inventory; there is nowhere to put a ninth
#define RAID_MAX_BGSRC    4   // backdrop images a level can project
#define RAID_MAX_MESH    32   // placed models (Data/raidmesh/m<id>.obj)

// Box flags. A box can be any combination: scenery that is walked through,
// an invisible wall, or the usual both.
#define RAID_BOX_DRAW     0x01
#define RAID_BOX_SOLID    0x02
#define RAID_BOX_CHECKER  0x04   // alternate the two shades cell by cell
#define RAID_BOX_PROJ     0x08   // textured by projecting the level's backdrops (bgsrc)
#define RAID_BOX_TEX      0x10   // textured by a tiled material (a `tbox` line): tex, tile
#define RAID_MESH_GLOW    0x20   // a `mesh` that gives light rather than takes it: a lamp
#define RAID_MESH_SHADOW  0x80   // a `mesh` that casts a shadow from the level's lights
#define RAID_BOX_DOOR     0x100  // the box a `door` line made for its doorway: solid while it is shut
#define RAID_MESH_LOCALUV 0x200  // a `mesh` whose material follows the model, not the world: it moves
#define RAID_BOX_WORLDUV  0x40   // a tbox whose material is laid from the world origin, not
                                 // its own corner - pieces of one wall then line up

struct RaidBox {
    short x0, y0, z0;
    short x1, y1, z1;
    unsigned short flags;
    float shade;                 // base brightness, 0..1
    unsigned char tr, tg, tb;    // tint, 0..255
    unsigned char tex;           // RAID_BOX_TEX: Data/raidtex/t<tex>.bin
    short tile;                  // ...repeating every `tile` world units
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

// A backdrop: one of the game's own pre-rendered room backgrounds, and the
// camera it was rendered from. Boxes flagged RAID_BOX_PROJ are textured by
// projecting it back out of that camera onto them (RaidArena.cpp). The view
// is the ORIGINAL camera, kept here rather than read from the room: the live
// camera record is rewritten by the editor and the shoulder camera, and a
// projection from a moving camera would slide.
struct RaidBgSrc {
    unsigned char stage;         // 1-based, as in the file name: Stage4\RC4070.pak
    unsigned char room;          // the room id, 0x07 there
    unsigned char cam;           // the camera, 0 there
    RaidCam       view;          // from that room's RDT camera record
};

// A placed model: Data/raidmesh/m<id>.obj, scaled by scale/100, turned by yaw
// (4096 to the turn, the facing convention) and moved to (x, y, z). Only drawn;
// what stops the player is still the boxes. With RAID_BOX_PROJ in flags it is
// textured by the backdrops, like a projected box.
struct RaidMesh {
    unsigned char id;
    int   x, y, z;
    short yaw;
    short scale;                 // percent
    unsigned short flags;
    unsigned char tex;           // 0 = untextured, else Data/raidtex/t<tex>.bin
    short tile;                  // world units per repeat
};

struct RaidLight {
    int x, y, z;
    unsigned char r, g, b;
    short radius;
};

// A mirror (`mirror` line): the game's own planar reflection, the one the story
// rooms arm with SCD opcode 0x0F (cmd_mirror_set) - same plane, same extent.
// The arena adds what a pre-rendered room got for free: the reflected room
// itself, seen through a hole the level leaves in the wall.
struct RaidMirror {
    unsigned char on;
    unsigned char axis;          // 0: the plane is Z = plane, 1: X = plane (the opcode's bit 1)
    int plane;                   // the glass
    int min, max;                // its extent along the other floor axis
    int ytop, ybot;              // and up the wall (Y negative up: ytop < ybot)
};

// A door (`door` line) that swings open as you walk into it, the way
// Resident Evil 2 (2019) does it, instead of cutting to a loading screen: the
// leaf turns about a vertical hinge at (hx, hz), and its doorway is a solid
// box (made by the loader, RAID_BOX_DOOR) only while it is shut. Every mesh
// placed exactly at the hinge swings with it. Runtime state lives here too.
struct RaidDoor {
    int   hx, hz;                // the hinge
    int   width;                 // along +X from the hinge, shut
    int   depth;                 // the doorway's thickness, across the wall
    int   box;                   // its doorway box in box[]
    float angle;                 // now, radians: + opens toward -Z, - toward +Z
    float target;
    int   idle;                  // shut by hand: it will not open by itself until she steps back
    int   closeIn;               // shut by hand: frames until the leaf starts back (her hand reaching the knob)
    int   openIdle;              // frames it has stood open with nobody in its way
    int   model;                 // `doormodel`: 1 + the game's door file (DoorSystem's table); 0 none
    int   knob;                  // its handle, as a door record's +0x0B & 0x3F
    int   height;                // the leaf's height, floor up
};

struct RaidLevel {
    int  loaded;
    int  nbox, ncam, nzone, nlight, nenemy, nitem, ngive, nbgsrc, nmesh, ndoor;
    short ambR, ambG, ambB;
    int  spawnX, spawnZ, spawnAngle;
    RaidBox   box[RAID_MAX_BOX];
    RaidCam   cam[RAID_MAX_CAM];
    RaidZone  zone[RAID_MAX_ZONE];
    RaidLight light[RAID_MAX_LIGHT];
    RaidEnemy enemy[RAID_MAX_ENEMY];
    RaidItem  item[RAID_MAX_ITEM];
    RaidGive  give[RAID_MAX_GIVE];
    RaidBgSrc bgsrc[RAID_MAX_BGSRC];
    RaidMesh  mesh[RAID_MAX_MESH];
    RaidMirror mirror;
    RaidDoor  door[RAID_MAX_DOOR];
};

extern RaidLevel g_raidLevel;
extern int g_raidReloadRequest;   // set by the reload key, consumed at draw time

int  RaidLevel_Load(void);        // read the file; 1 on success, level untouched on failure
void RaidLevel_Apply(void);       // push camera, lights and collision into the loaded RDT
void RaidMirror_Arm(void);        // once per room entry, after every body is set up
void RaidLevel_RebuildCollision(void);      // after a door opened or shut
void RaidDoors_Player(int i);              // RaidArena.cpp: player i walks into / works a door
void RaidDoors_PoseArm(int i);             // RaidArena.cpp: and reaches out to it (the draw)
void RaidLevel_LightsNear(int x, int z);    // the RDT's 3 light slots <- the nearest level lights

// RaidItems.cpp - the pickups lying in the room.
void RaidItems_Reset(void);       // a (re)loaded level has taken nothing
void RaidItems_Update(void);      // once a frame: what is in reach, and the take button
int  RaidItems_Taken(int i);      // 1 once the player has it; the renderer skips those
int  RaidItems_Reach(int i);      // 1 while it is close enough to take - the marker lights up
int  RaidItems_Give(void);        // fill the inventory from g_raidLevel.give[]; 0 if it is empty
