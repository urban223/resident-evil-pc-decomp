// CoopNet.cpp - host-authoritative transport for RAID co-op. CUSTOM.
// See CoopNet.h for why this is snapshots and not lockstep.
#include "CoopNet.h"
#include "CoopPlayer.h"
#include "Entities.h"
#include "entities/EntityCommon.h"
#include "../platform/platform.h"
#include <cstring>

int g_coopRole = COOP_ROLE_OFF;

int Coop_IsAuthority(void)
{
    return (g_coopRole == COOP_ROLE_HOST || g_coopRole == COOP_ROLE_LOCAL
            || g_coopRole == COOP_ROLE_OFF) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Wire format
//
// Fixed-size, little-endian, 32-bit - the same build on both ends. Packed so
// the layout is the struct and nothing depends on the compiler's padding
// choices, which is the one thing that could differ between MSVC and GCC here.
//
// COOP_NET_ENEMIES is 16, matching RAID_ENEMY_SLOTS in RaidEnemies.cpp rather
// than the 30-slot array: the arena never spawns more, and a snapshot that
// carried all 30 would be mostly zeroes on every tick.
// ---------------------------------------------------------------------------
#define COOP_MAGIC_SNAP   0x50414E53u   // 'SNAP'
#define COOP_MAGIC_INPUT  0x54554E49u   // 'INUT'
#define COOP_NET_ENEMIES  16
#define COOP_NET_PORT_DEF 27015

#pragma pack(push, 1)

typedef struct {
    int   x, y, z;          // localMatrix.t
    short angle;
    short health;
    unsigned char animationId;
    unsigned char animFrameId;
    unsigned char flags;        // aim bits and the rest of PlayerEntity.flags
    unsigned char isZombie;     // playing as a zombie: his body is an entity
} CoopNetPlayer;

typedef struct {
    int   x, y, z;
    short angle;
    short health;
    unsigned char id;           // enemy type; 0xFF means "slot empty"
    unsigned char state;
    unsigned char animationId;
    unsigned char animFrameId;
} CoopNetEnemy;

typedef struct {
    unsigned int   magic;
    unsigned int   tick;
    CoopNetPlayer  player[RAID_PLAYERS];
    CoopNetEnemy   enemy[COOP_NET_ENEMIES];
} CoopNetSnapshot;

typedef struct {
    unsigned int   magic;
    unsigned int   tick;
    unsigned int   padHeld;
    unsigned short dpadHeld;
    unsigned short dpadPressed;
} CoopNetInput;

#pragma pack(pop)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int         s_sock     = PLAT_NET_INVALID;
static PlatNetAddr s_peer     = { 0, 0, 0 };
static int         s_havePeer = 0;
static unsigned int s_tick    = 0;
static unsigned int s_lastSeen = 0;   // newest tick accepted, to drop reorders

// The client's pad, as the host last heard it. Applied to player 2 before the
// world moves, so an input that arrives late is simply held over rather than
// producing a frame where player 2 stands still.
static CoopNetInput s_remoteInput = { 0, 0, 0, 0, 0 };

int CoopNet_StartHost(unsigned short port)
{
    CoopNet_Stop();
    s_sock = plat_net_open(port ? port : COOP_NET_PORT_DEF);
    if (s_sock == PLAT_NET_INVALID) return 0;
    s_havePeer = 0;
    s_tick = 0;
    s_lastSeen = 0;
    g_coopRole = COOP_ROLE_HOST;
    return 1;
}

int CoopNet_StartClient(const char* hostText)
{
    CoopNet_Stop();
    PlatNetAddr to;
    if (!plat_net_parse(hostText, &to)) return 0;

    s_sock = plat_net_open(0);          // any local port
    if (s_sock == PLAT_NET_INVALID) return 0;

    s_peer = to;
    s_havePeer = 1;
    s_tick = 0;
    s_lastSeen = 0;
    g_coopRole = COOP_ROLE_CLIENT;
    return 1;
}

void CoopNet_Stop(void)
{
    if (s_sock != PLAT_NET_INVALID) {
        plat_net_close(s_sock);
        s_sock = PLAT_NET_INVALID;
    }
    s_havePeer = 0;
    if (g_coopRole == COOP_ROLE_HOST || g_coopRole == COOP_ROLE_CLIENT) {
        g_coopRole = COOP_ROLE_OFF;
    }
}

// ---------------------------------------------------------------------------
// Host: build and apply
// ---------------------------------------------------------------------------
static void snap_build(CoopNetSnapshot* s)
{
    memset(s, 0, sizeof(*s));
    s->magic = COOP_MAGIC_SNAP;
    s->tick  = s_tick;

    for (int i = 0; i < RAID_PLAYERS; i++) {
        const PlayerEntity* p = &g_players[i];
        CoopNetPlayer* w = &s->player[i];
        w->x = (int)p->scaMatrixData.localMatrix.t[0];
        w->y = (int)p->scaMatrixData.localMatrix.t[1];
        w->z = (int)p->scaMatrixData.localMatrix.t[2];
        w->angle       = p->directionAngle;
        w->health      = p->health;
        w->animationId = p->animationId;
        w->animFrameId = p->animFrameId;
        w->flags       = p->flags;
        w->isZombie    = (unsigned char)Coop_IsZombie(i);
    }

    for (int e = 0; e < COOP_NET_ENEMIES; e++) {
        const Entity* en = &g_EnemiesList[e];
        CoopNetEnemy* w = &s->enemy[e];
        if ((en->status_flags & ENTITY_STATUS_ACTIVE) == 0) {
            w->id = 0xFF;                 // empty
            continue;
        }
        w->x = (int)en->scaMatrixData.localMatrix.t[0];
        w->y = (int)en->scaMatrixData.localMatrix.t[1];
        w->z = (int)en->scaMatrixData.localMatrix.t[2];
        w->angle       = en->angle;
        w->health      = en->health;
        w->id          = en->id;
        w->state       = en->state;
        w->animationId = en->animationId;
        w->animFrameId = en->animation_frame_id;
    }
}

// ---------------------------------------------------------------------------
// Client: apply
//
// Position and pose only. The client does not run update_player_anim or
// update_entities at all (GameLoop asks Coop_IsAuthority before either), so
// nothing here has to fight a local simulation for the same fields - which is
// the whole reason this is authoritative rather than predicted.
// ---------------------------------------------------------------------------
static void snap_apply(const CoopNetSnapshot* s)
{
    for (int i = 0; i < RAID_PLAYERS; i++) {
        PlayerEntity* p = &g_players[i];
        const CoopNetPlayer* w = &s->player[i];
        p->scaMatrixData.localMatrix.t[0] = w->x;
        p->scaMatrixData.localMatrix.t[1] = w->y;
        p->scaMatrixData.localMatrix.t[2] = w->z;
        p->position.x = (short)w->x;
        p->position.y = (short)w->y;
        p->position.z = (short)w->z;
        p->directionAngle = w->angle;
        p->health         = w->health;
        p->animationId    = w->animationId;
        p->animFrameId    = w->animFrameId;
        p->flags          = w->flags;
    }

    for (int e = 0; e < COOP_NET_ENEMIES; e++) {
        Entity* en = &g_EnemiesList[e];
        const CoopNetEnemy* w = &s->enemy[e];

        if (w->id == 0xFF) {
            en->status_flags = 0;
            continue;
        }

        // A slot that has just become occupied needs its model, which only the
        // spawn path loads. Leaving that to the client's own RaidEnemies_Spawn
        // would race the snapshot, so a newly-seen slot is marked active and
        // the id is taken from the wire; the model load happens the next time
        // the room does one.
        en->status_flags |= ENTITY_STATUS_ACTIVE;
        en->id = w->id;
        en->scaMatrixData.localMatrix.t[0] = w->x;
        en->scaMatrixData.localMatrix.t[1] = w->y;
        en->scaMatrixData.localMatrix.t[2] = w->z;
        en->position.x = (short)w->x;
        en->position.y = (short)w->y;
        en->position.z = (short)w->z;
        en->angle  = w->angle;
        en->health = w->health;
        en->state  = w->state;
        en->animationId        = w->animationId;
        en->animation_frame_id = w->animFrameId;
    }
}

// ---------------------------------------------------------------------------
// Per-tick
// ---------------------------------------------------------------------------
void CoopNet_Receive(void)
{
    if (s_sock == PLAT_NET_INVALID) return;

    // Drain everything queued and keep only the newest: a datagram that arrives
    // out of order is older news than one already applied, and applying it
    // would snap the world backwards for a frame.
    CoopNetSnapshot bestSnap;
    int haveSnap = 0;

    for (;;) {
        unsigned char buf[sizeof(CoopNetSnapshot)];
        PlatNetAddr from;
        const int n = plat_net_recv(s_sock, &from, buf, (int)sizeof(buf));
        if (n <= 0) break;

        if (g_coopRole == COOP_ROLE_HOST && n == (int)sizeof(CoopNetInput)) {
            CoopNetInput in;
            memcpy(&in, buf, sizeof(in));
            if (in.magic != COOP_MAGIC_INPUT) continue;

            // First packet from anyone is the client we are playing with. No
            // handshake: this is a two-machine game over a link the players
            // arranged themselves, not a public server.
            if (!s_havePeer) {
                s_peer = from;
                s_havePeer = 1;
            }
            // Newest wins; a reordered datagram is older news than one already
            // applied. s_lastSeen means "newest input tick" on a host and
            // "newest snapshot tick" on a client - a machine is only ever one
            // of the two, so the two uses cannot collide.
            if (in.tick >= s_lastSeen) {
                s_remoteInput = in;
                s_lastSeen = in.tick;
            }
        } else if (g_coopRole == COOP_ROLE_CLIENT && n == (int)sizeof(CoopNetSnapshot)) {
            CoopNetSnapshot sn;
            memcpy(&sn, buf, sizeof(sn));
            if (sn.magic != COOP_MAGIC_SNAP) continue;
            if (haveSnap && sn.tick <= bestSnap.tick) continue;
            bestSnap = sn;
            haveSnap = 1;
        }
    }

    if (g_coopRole == COOP_ROLE_CLIENT && haveSnap && bestSnap.tick >= s_lastSeen) {
        s_lastSeen = bestSnap.tick;
        snap_apply(&bestSnap);
    }

    // The host hands the client's pad to player 2 before the world moves, so it
    // reaches update_player_anim as if it had come off a local device.
    if (g_coopRole == COOP_ROLE_HOST) {
        Coop_SetRemotePad(1, s_remoteInput.padHeld,
                          s_remoteInput.dpadHeld, s_remoteInput.dpadPressed);
    }
}

void CoopNet_Send(void)
{
    if (s_sock == PLAT_NET_INVALID || !s_havePeer) return;

    s_tick++;

    if (g_coopRole == COOP_ROLE_HOST) {
        CoopNetSnapshot sn;
        snap_build(&sn);
        plat_net_send(s_sock, &s_peer, &sn, (int)sizeof(sn));
    } else if (g_coopRole == COOP_ROLE_CLIENT) {
        CoopNetInput in;
        in.magic       = COOP_MAGIC_INPUT;
        in.tick        = s_tick;
        in.padHeld     = g_PlayerPadHeld;
        in.dpadHeld    = g_PlayerDpadHeld;
        in.dpadPressed = g_PlayerDpadPressed;
        plat_net_send(s_sock, &s_peer, &in, (int)sizeof(in));
    }
}
