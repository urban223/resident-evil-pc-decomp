// CoopNet.cpp - host-authoritative transport for RAID co-op. CUSTOM.
// See CoopNet.h for why this is snapshots and not lockstep.
#include "CoopNet.h"
#include "CoopPlayer.h"
#include "Entities.h"
#include "entities/EntityCommon.h"
#include "../platform/platform.h"
#include <cstring>
#include "../DebugPrint.h"

int g_coopRole = COOP_ROLE_OFF;

int Coop_IsNetworked(void)
{
    return (g_coopRole == COOP_ROLE_HOST || g_coopRole == COOP_ROLE_CLIENT) ? 1 : 0;
}

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
    unsigned char animationId;  // PlayerEntity 0x84 - the state machine's pair
    unsigned char animFrameId;  // PlayerEntity 0x85
    unsigned char jointAnimId;  // Entity view 0xBD - the pair Joint_move reads
    unsigned char jointFrameId; // Entity view 0xBE
    unsigned char jointSrc;     // which of the four animation sources
    unsigned char jointMirror;  // Joint_move's `reverse`
    unsigned char flags;        // aim bits and the rest of PlayerEntity.flags
    unsigned char isZombie;     // playing as a zombie: his body is an entity
    unsigned char zombieSlot;   // which enemy slot that body is; 0xFF if none
} CoopNetPlayer;

typedef struct {
    int   x, y, z;
    short angle;
    short health;
    unsigned char id;           // enemy type; 0xFF means "slot empty"
    unsigned char statusFlags;  // the WHOLE byte, not just the liveness bit
    unsigned char hdr2, hdr3;   // behavior_flags and has_enter_switch_zone
    // The rest of what drives a pose. A client runs no state machine, so any
    // of these it does not receive stays at whatever its own spawn left.
    unsigned char actionBehavior;
    unsigned char actionState;
    unsigned char timingControl;
    unsigned char blendCounter;
    unsigned char hitState;
    unsigned char deathTimer;
    unsigned char state;
    unsigned char animationId;
    unsigned char animFrameId;
} CoopNetEnemy;

typedef struct {
    unsigned char type, depthGroup, lightFactor, parent;
    short         yaw, pad;
    int           x, y, z;
} CoopNetEffect;

typedef struct {
    unsigned int   magic;
    unsigned int   tick;
    unsigned char  enemyCount;  // g_enemy_count, which bounds the DRAW loop
    unsigned char  pad0, pad1, pad2;
    CoopNetPlayer  player[RAID_PLAYERS];
    CoopNetEnemy   enemy[COOP_NET_ENEMIES];
    unsigned char  effectCount;
    unsigned char  epad0, epad1, epad2;
    CoopNetEffect  effect[COOP_EFFECT_QUEUE];
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

// Link diagnostics. A UDP mode with no visibility cannot be tested: a silent
// link and a working one look identical from the game. Everything here is
// counters plus a heartbeat every 150 ticks (5 seconds at 30Hz), and it all
// goes through dbg_printf, which is inert unless RE1_DEBUGLOG=1.
static unsigned int s_statSent    = 0;
static unsigned int s_statRecv    = 0;
static unsigned int s_statDropped = 0;   // wrong size or bad magic
static unsigned int s_statStale   = 0;   // arrived out of order, discarded

static const char* coop_role_name(void)
{
    switch (g_coopRole) {
    case COOP_ROLE_HOST:   return "host";
    case COOP_ROLE_CLIENT: return "client";
    case COOP_ROLE_LOCAL:  return "local";
    default:               return "off";
    }
}

static void coop_heartbeat(void)
{
    if ((s_tick % 150) != 0 || s_tick == 0) return;
    dbg_printf("[coop] %s tick=%u sent=%u recv=%u dropped=%u stale=%u peer=%d sock=%d\n",
               coop_role_name(), s_tick, s_statSent, s_statRecv,
               s_statDropped, s_statStale, s_havePeer,
               s_sock != PLAT_NET_INVALID);
}

int CoopNet_StartHost(unsigned short port)
{
    CoopNet_Stop();
    s_sock = plat_net_open(port ? port : COOP_NET_PORT_DEF);
    if (s_sock == PLAT_NET_INVALID) return 0;
    s_havePeer = 0;
    s_tick = 0;
    s_lastSeen = 0;
    g_coopRole = COOP_ROLE_HOST;
    s_statSent = s_statRecv = s_statDropped = s_statStale = 0;
    dbg_printf("[coop] host listening on port %u\n",
               (unsigned int)(port ? port : COOP_NET_PORT_DEF));
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
    s_statSent = s_statRecv = s_statDropped = s_statStale = 0;
    dbg_printf("[coop] client sending to %s\n", hostText);
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
    // The draw loop is bounded by this, and a host grows it when a dead player
    // stands up as a zombie (Coop_CheckDeaths). A client that kept its own
    // count from its own spawn could never draw that body at all - the loop
    // stopped one entity short, which is exactly what "drawn=1 count=1" against
    // the host's "drawn=2 count=2" was saying.
    s->enemyCount = (unsigned char)g_enemy_count;

    // Drain this tick's billboard spawns. Events, not state: they are queued as
    // they happen and cleared once shipped, so a lost datagram loses a puff of
    // blood rather than desynchronising anything.
    s->effectCount = (unsigned char)g_coopEffectCount;
    for (int i = 0; i < g_coopEffectCount; i++) {
        const CoopEffectEvent* q = &g_coopEffectQueue[i];
        CoopNetEffect* w = &s->effect[i];
        w->type        = q->type;
        w->depthGroup  = q->depthGroup;
        w->lightFactor = q->lightFactor;
        w->parent      = q->parent;
        w->yaw         = q->yaw;
        w->x = q->x; w->y = q->y; w->z = q->z;
    }
    g_coopEffectCount = 0;

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
        // The pair the pose actually comes off. PlayerEntity carries two sets:
        // 0x84/0x85 belong to the player state machine, and 0xBD/0xBE are what
        // Joint_move reads through the Entity view. Sending only the first set
        // left a client posing from an animation frame nothing had written -
        // the rotation data read as zeroes and every body slid around the room
        // in its rest pose.
        w->jointAnimId  = ((const Entity*)p)->animationId;
        w->jointFrameId = ((const Entity*)p)->animation_frame_id;
        w->jointSrc     = g_coopJointSrc[i];
        w->jointMirror  = g_coopJointMirror[i];
        // ...and, when there is one, the pose the host last actually PUT ON the
        // skeleton, which on a turn-while-aiming tick is not what these fields
        // say. See Coop_NotePose.
        Coop_PosedPlayerPose(i, &w->jointAnimId, &w->jointFrameId,
                             &w->jointSrc, &w->jointMirror);
        w->flags       = p->flags;
        w->isZombie    = (unsigned char)Coop_IsZombie(i);
        w->zombieSlot  = (g_coopZombieSlot[i] >= 0)
                       ? (unsigned char)g_coopZombieSlot[i] : 0xFF;
    }

    for (int e = 0; e < COOP_NET_ENEMIES; e++) {
        const Entity* en = &g_EnemiesList[e];
        CoopNetEnemy* w = &s->enemy[e];
        if ((en->status_flags & ENTITY_STATUS_ACTIVE) == 0) {
            w->id = 0xFF;                 // empty
            // Whatever this slot last posed belonged to a body that is gone.
            // Leaving it would hand the next occupant of the slot - a reserved
            // player-zombie waking up, say - a corpse's final frame.
            Coop_ForgetPose(e);
            continue;
        }
        w->x = (int)en->scaMatrixData.localMatrix.t[0];
        w->y = (int)en->scaMatrixData.localMatrix.t[1];
        w->z = (int)en->scaMatrixData.localMatrix.t[2];
        w->angle       = en->angle;
        w->health      = en->health;
        w->id          = en->id;
        w->statusFlags = en->status_flags;
        w->hdr2        = en->behavior_flags;
        w->hdr3        = en->has_enter_switch_zone;
        w->actionBehavior = en->action_behavior;
        w->actionState    = en->action_state;
        w->timingControl  = en->timing_control;
        w->blendCounter   = en->blend_counter;
        w->hitState       = en->hit_state;
        w->deathTimer     = (unsigned char)en->death_timer;
        w->state       = en->state;
        // The live fields are the fallback, for an enemy that has not been
        // posed yet. Once it has, the wire carries the frame the host POSED:
        // Joint_move wraps animation_frame_id to 0 on the call that ends an
        // animation, and zombie_dead_animation stops posing on exactly that
        // return value - so a corpse's entity reads frame 0 while its joints
        // hold the last frame of the fall. A client posing the field it was
        // sent drew frame 0 of that animation, which is a zombie back on its
        // feet. See Coop_NotePose.
        w->animationId = en->animationId;
        w->animFrameId = en->animation_frame_id;
        Coop_PosedPose(e, &w->animationId, &w->animFrameId);
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
    if (s->enemyCount <= 30) g_enemy_count = s->enemyCount;

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
        ((Entity*)p)->animationId        = w->jointAnimId;
        ((Entity*)p)->animation_frame_id = w->jointFrameId;
        g_coopJointSrc[i]    = w->jointSrc;
        g_coopJointMirror[i] = w->jointMirror;
        p->flags          = w->flags;
        // Death is the host's call, and Coop_CheckDeaths only runs there. Without
        // this a client never learned a player had died: it went on drawing the
        // body frozen on the last frame of the animation that killed him, while
        // the host had already stood a zombie up in his place.
        g_coopZombieSlot[i] = w->isZombie ? (int)w->zombieSlot : -1;
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
        // The whole byte, not just the liveness bit: the upper bits gate
        // things other than the draw loop's test, and a host's enemy reads F1
        // where an OR-ed client read 01.
        en->status_flags = w->statusFlags | ENTITY_STATUS_ACTIVE;
        en->id = w->id;
        // Byte 3 decides whether the body is drawn at all. render_entity opens
        // with g_animFrameIdSave = ((entity[3] & 0x7f) == 0) and puts its whole
        // draw branch behind that being zero, so an entity whose byte 3 is 0
        // runs the loop and emits nothing. A host's arena zombie carries 1; a
        // client's copy was 0, and that one byte is why a client saw an empty
        // room while every other number - joints, objects, model, count, the
        // draw counter itself - matched the host exactly.
        en->behavior_flags        = w->hdr2;
        en->has_enter_switch_zone = w->hdr3;
        en->action_behavior = w->actionBehavior;
        en->action_state    = w->actionState;
        en->timing_control  = w->timingControl;
        en->blend_counter   = w->blendCounter;
        en->hit_state       = w->hitState;
        en->death_timer     = w->deathTimer;
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

    // Effects last: a billboard's parent is an entity matrix, and it has to be
    // the one this snapshot just installed rather than the previous tick's.
    const int fx = (s->effectCount <= COOP_EFFECT_QUEUE) ? s->effectCount : 0;
    for (int i = 0; i < fx; i++) {
        const CoopNetEffect* w = &s->effect[i];
        VECTOR at;
        at.x = w->x; at.y = w->y; at.z = w->z; at.pad = 0;
        Effect_CreateBillboard(w->type, w->depthGroup, w->yaw,
                               Coop_EffectParentPtr(w->parent), &at,
                               (char)w->lightFactor);
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
        s_statRecv++;

        if (g_coopRole == COOP_ROLE_HOST && n == (int)sizeof(CoopNetInput)) {
            CoopNetInput in;
            memcpy(&in, buf, sizeof(in));
            if (in.magic != COOP_MAGIC_INPUT) { s_statDropped++; continue; }

            // First packet from anyone is the client we are playing with. No
            // handshake: this is a two-machine game over a link the players
            // arranged themselves, not a public server.
            // Always take the sender of the newest input as the peer, not
            // just the first one ever seen. A client that restarts, or simply
            // re-enters RAID, calls plat_net_open(0) again and lands on a NEW
            // ephemeral port. Binding the peer once meant the host kept
            // answering the dead port for the rest of the session - and the
            // failure is silent from both ends, because sendto to a dead local
            // port still succeeds and the client's own inputs keep arriving.
            // The counters are what showed it: host sent=299 recv=306 while
            // the client sat at recv=178 and never moved.
            if (!s_havePeer || from.addr != s_peer.addr || from.port != s_peer.port) {
                dbg_printf("[coop] host: peer %u.%u.%u.%u:%u%s\n",
                           (from.addr >> 24) & 0xFF, (from.addr >> 16) & 0xFF,
                           (from.addr >> 8) & 0xFF, from.addr & 0xFF,
                           (unsigned int)from.port,
                           s_havePeer ? " (moved)" : "");
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
            } else {
                s_statStale++;
            }
        } else if (g_coopRole == COOP_ROLE_CLIENT && n == (int)sizeof(CoopNetSnapshot)) {
            CoopNetSnapshot sn;
            memcpy(&sn, buf, sizeof(sn));
            if (sn.magic != COOP_MAGIC_SNAP) { s_statDropped++; continue; }
            if (haveSnap && sn.tick <= bestSnap.tick) continue;
            bestSnap = sn;
            haveSnap = 1;
        }
    }

    if (g_coopRole == COOP_ROLE_CLIENT && haveSnap) {
        if (bestSnap.tick >= s_lastSeen) {
            if (s_lastSeen == 0) dbg_printf("[coop] client: first snapshot\n");
            s_lastSeen = bestSnap.tick;
            snap_apply(&bestSnap);
        } else {
            s_statStale++;
        }
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
    // The heartbeat runs even with no socket and no peer: a silent link and a
    // link that was never opened look the same from the game, and last run's
    // log could not tell "host left RAID" from "host never had a peer".
    s_tick++;
    coop_heartbeat();

    if (s_sock == PLAT_NET_INVALID || !s_havePeer) return;

    if (g_coopRole == COOP_ROLE_HOST) {
        CoopNetSnapshot sn;
        snap_build(&sn);
        if (plat_net_send(s_sock, &s_peer, &sn, (int)sizeof(sn)) > 0) s_statSent++;
    } else if (g_coopRole == COOP_ROLE_CLIENT) {
        CoopNetInput in;
        in.magic       = COOP_MAGIC_INPUT;
        in.tick        = s_tick;
        in.padHeld     = g_PlayerPadHeld;
        in.dpadHeld    = g_PlayerDpadHeld;
        in.dpadPressed = g_PlayerDpadPressed;
        if (plat_net_send(s_sock, &s_peer, &in, (int)sizeof(in)) > 0) s_statSent++;
    }
}

void CoopNet_StartFromConfig(void)
{
    g_coopRole = COOP_ROLE_OFF;

    switch (g_coopConfigMode) {
    case 1:
        // The debug mode: both players here, no socket at all.
        g_coopRole = COOP_ROLE_LOCAL;
        break;
    case 2:
        if (!CoopNet_StartHost(g_coopConfigPort)) {
            dbg_printf("[coop] host on port %u failed; single player\n",
                       (unsigned int)g_coopConfigPort);
        }
        break;
    case 3:
        if (!CoopNet_StartClient(g_coopConfigHost)) {
            dbg_printf("[coop] client to %s failed; single player\n",
                       g_coopConfigHost);
        }
        break;
    default:
        break;
    }
}
