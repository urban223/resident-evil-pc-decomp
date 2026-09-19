// CoopPlayer.cpp - two players in the RAID arena. CUSTOM; see CoopPlayer.h.
#include "CoopPlayer.h"
#include "CoopNet.h"
#include "entities/EntityCommon.h"
// benddown_and_eat is zombie_action_tbl[4] (Zombie.cpp:2831), reached through
// action_behavior - NOT behavior_flags. The table's own comment calls it
// zombie_move_behavior_tbl "indexed by behavior_flags & 0x0F" and it does serve
// that too, but the eat is the action-dispatch entry. Measured, not read off
// the comment: at the moment it tints, behavior_flags is 00 and
// action_behavior is 4.
#define ZOMBIE_ACTION_EAT 4
#include "Entities.h"
#include <cstring>
#include <cstdio>
#include "../platform/platform.h"
#include "../DebugPrint.h"

int g_coopActive = 0;
unsigned char g_enemyTarget[30] = {};

// Enemy slots held for the zombies the players will become; see Coop_ReserveZombies.
static int s_reservedSlot[RAID_PLAYERS] = { -1, -1 };
// The model loaders, declared the way RaidEnemies.cpp declares them
// (RaidEnemies.cpp:40-43): the unsigned-int form of SetupJointStructures is the
// one that returns the advanced arena pointer, and it is deliberately NOT the
// void* overload in Globals.h - see the overload note in CharacterNpc.h.
extern void load_character_sfx(unsigned char charId);          // SoundSystem.cpp
extern void LoadEntityEMD(Entity* em, unsigned char entity_id);
extern void ResetJointTransforms(void);                          // 0x0048bad0
extern void Entity_SetJoints(Entity* em, unsigned int stride);
extern void InitAnimStructure(void* animHeaderValue);
extern unsigned int SetupJointStructures(unsigned int base);


// Player 2's SCA hit-data block. Player 1 uses g_entityDataBlock (0x200 bytes,
// 0x00d211d0); this is the same size for the same reason.
static BYTE s_player2ScaHitData[0x200] = {};

// ---------------------------------------------------------------------------
// Pad state, swapped rather than duplicated
//
// PlayerPad_Update (0x0044e000) keeps its edge detection in nine globals, and
// two of them - g_PlayerDpadHeld / g_PlayerDpadPressed - are BioCard macros
// (BioCard.h:114-115), i.e. fields inside the 0x41C save block whose size is
// pinned by static_assert. There is nowhere to put a second copy, so instead
// each player's block is swapped in before its update and back out after.
//
// The consequence worth naming: between Coop_BeginPlayer(1) and Coop_EndPlayer()
// the save block briefly holds player 2's dpad words. Nothing saves mid-frame,
// and RAID has no saving at all, so this is inert - but it is why the swap is
// bracketed tightly around the update and not held open across the frame.
// ---------------------------------------------------------------------------
struct CoopPadState {
    DWORD rawHeld;
    DWORD padPressed;
    DWORD buttonPressedId;
    DWORD padHeld;
    DWORD padHeldPrev;
    WORD  rawState;
    WORD  dpadHeld;
    WORD  dpadPressed;
    WORD  dpadHeldPrev;
};

static CoopPadState s_pad[RAID_PLAYERS];
static int s_padOwner = 0;      // whose block is live in the globals right now

static void pad_save(int i)
{
    CoopPadState* p = &s_pad[i];
    p->rawHeld         = g_RawPadHeld;
    p->padPressed      = g_PlayerPadPressed;
    p->buttonPressedId = g_button_pressed_id;
    p->padHeld         = g_PlayerPadHeld;
    p->padHeldPrev     = g_PlayerPadHeldPrev;
    p->rawState        = g_RawPadState;
    p->dpadHeld        = g_PlayerDpadHeld;
    p->dpadPressed     = g_PlayerDpadPressed;
    p->dpadHeldPrev    = g_PlayerDpadHeldPrev;
}

static void pad_load(int i)
{
    const CoopPadState* p = &s_pad[i];
    g_RawPadHeld        = p->rawHeld;
    g_PlayerPadPressed  = p->padPressed;
    g_button_pressed_id = p->buttonPressedId;
    g_PlayerPadHeld     = p->padHeld;
    g_PlayerPadHeldPrev = p->padHeldPrev;
    g_RawPadState       = p->rawState;
    g_PlayerDpadHeld    = p->dpadHeld;
    g_PlayerDpadPressed = p->dpadPressed;
    g_PlayerDpadHeldPrev= p->dpadHeldPrev;
}

int Coop_CharSfxBase(void)
{
    return (g_coopActive && g_pCurPlayer == &g_players[1]) ? 32 : 0;
}

unsigned char Coop_PlayerTexBank(void)
{
    return (g_coopActive && g_pCurPlayer == &g_players[1]) ? 0x18 : 0x16;
}

unsigned char Coop_PlayerTexPage(void)
{
    return (g_coopActive && g_pCurPlayer == &g_players[1]) ? 8 : 7;
}

int Coop_PlayerCount(void)
{
    return g_coopActive ? RAID_PLAYERS : 1;
}

// ---------------------------------------------------------------------------
// Making a player current
// ---------------------------------------------------------------------------
static PlayerEntity* s_prevPlayer = 0;
static Entity*       s_prevEntity = 0;

void Coop_BeginPlayer(int i)
{
    s_prevPlayer = g_pCurPlayer;
    s_prevEntity = ENTITY;

    if (g_coopActive) {
        if (s_padOwner != i) {
            pad_save(s_padOwner);
            pad_load(i);
            s_padOwner = i;
        }
        g_pCurPlayer = &g_players[i];
    }
    // ENTITY follows the player the way every player-facing call already
    // expects it to (GameLoop.cpp:351, EntityModelLoader.cpp:725).
    ENTITY = (Entity*)g_pCurPlayer;
}

void Coop_EndPlayer(void)
{
    g_pCurPlayer = s_prevPlayer ? s_prevPlayer : &g_players[0];
    ENTITY       = s_prevEntity;
}

// ---------------------------------------------------------------------------
// Input: one source per player
//
// ReadPadBoth merges the keyboard and joystick[0] into one word with |=, which
// is right for the original - they are two ways to drive one character. In
// co-op they have to come apart, and the split the original's own comment
// already describes is the one used here: the keyboard is remap table 0 and a
// joystick is remap table 1 (JoyToPSX's `player` argument, InputSystem.cpp:104).
//
// g_coopPadSource tells ReadPadBoth which one to answer with; -1 restores the
// merge. Player 1 takes joystick 0 if one is present and the keyboard
// otherwise, so a single pad plus the keyboard is a playable pair on one
// machine - which is what the debug mode is for.
// ---------------------------------------------------------------------------
int g_coopPadSource = -1;

void Coop_UpdatePads(void)
{
    const int count = Coop_PlayerCount();

    for (int i = 0; i < count; i++) {
        if (s_padOwner != i) {
            pad_save(s_padOwner);
            pad_load(i);
            s_padOwner = i;
        }
        g_coopPadSource = g_coopActive ? i : -1;
        PlayerPad_Update();
    }
    g_coopPadSource = -1;

    // Leave player 1's block live: everything outside the per-player update
    // (menus, the pause path, the debug menu) means player 1 when it says
    // g_PlayerDpadHeld.
    if (s_padOwner != 0) {
        pad_save(s_padOwner);
        pad_load(0);
        s_padOwner = 0;
    }
}

// ---------------------------------------------------------------------------
// Per-enemy targeting
//
// Every enemy's range and facing helper reads g_playerEntity directly - e.g.
// entity_check_visual_range (EntityCommon.cpp:287) subtracts its own matrix
// translation from g_playerEntity's. Since that name is a macro, pointing
// g_pCurPlayer at an enemy's own target before dispatching it gives each enemy
// its own player with NO edit to any enemy file. That is the whole reason this
// is affordable.
//
// The rule is nearest-player with hysteresis: an enemy only switches when the
// other player is meaningfully closer, or when its current target is dead.
// Without the margin two players at similar range make every enemy in the room
// oscillate, which reads as the AI twitching rather than choosing.
// ---------------------------------------------------------------------------
#define COOP_SWITCH_MARGIN 1200

static int coop_dist_to(const PlayerEntity* p, const Entity* e)
{
    int dx = (int)p->scaMatrixData.localMatrix.t[0] - (int)e->scaMatrixData.localMatrix.t[0];
    int dz = (int)p->scaMatrixData.localMatrix.t[2] - (int)e->scaMatrixData.localMatrix.t[2];
    return (int)SquareRoot0(dx * dx + dz * dz);
}

void Coop_ChooseTargets(void)
{
    if (!g_coopActive) {
        for (int s = 0; s < 30; s++) g_enemyTarget[s] = 0;
        return;
    }

    for (int s = 0; s < 30; s++) {
        Entity* e = &g_EnemiesList[s];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;

        const unsigned char cur = (g_enemyTarget[s] < RAID_PLAYERS) ? g_enemyTarget[s] : 0;
        const unsigned char other = (unsigned char)(cur ^ 1);

        // A zombie bent over a corpse keeps the corpse. benddown_and_eat is
        // zombie_move_behavior_tbl[4] - "bend down over a corpse and feed" -
        // and it addresses its meal as g_playerEntity, i.e. whoever is current.
        // Hand the target over mid-meal and the rest of that animation acts on
        // the OTHER player: on animation frame 0x0E it tints its victim's
        // joints 0 and 2 dark red (Zombie.cpp:2648), so the moment Jill died
        // the zombie still chewing on her painted the wound onto Chris,
        // untouched at full health across the room. The log is unambiguous -
        // one entity, tgt flipping 0/1 frame to frame, hp1=140 throughout.
        //
        // Gated on the BEHAVIOUR, not the state: the eat runs with state 1
        // (the idle dispatch) and action_state 5, so an earlier attempt to gate
        // on ZOMBIE_STATE_ATTACK matched nothing at all.
        if (e->id == ENEMY_ZOMBIE && e->action_behavior == ZOMBIE_ACTION_EAT) {
            // Feeding: the meal is a CORPSE, so the target has to be a dead
            // player - the nearest one if both are down. Freezing the target
            // instead was not enough, and the log said so: the dead-target rule
            // had already moved the zombie onto Chris a frame BEFORE it bent
            // down, so the freeze just locked in the wrong man and every wound
            // went to him. Choosing the corpse is the rule; keeping whatever
            // was there was a guess.
            int pick = -1, best = 0;
            for (int q = 0; q < RAID_PLAYERS; q++) {
                if (g_players[q].health >= 0) continue;
                const int d = coop_dist_to(&g_players[q], e);
                if (pick < 0 || d < best) { pick = q; best = d; }
            }
            if (pick >= 0) {
                g_enemyTarget[s] = (unsigned char)pick;
                continue;
            }
            // Nobody is down: this is not really a meal, fall through.
        }

        // Never switch TO a corpse. Without this the two rules below fight each
        // other: the dead-target rule moves the zombie onto the living player,
        // then the distance rule moves it straight back because the body it is
        // standing over is nearer, and the target oscillates every frame.
        if (g_players[other].health < 0) continue;

        // A dead player stops being worth chasing; take the other one whatever
        // the distance. Health below zero is the engine's own dead test.
        if (g_players[cur].health < 0 && g_players[other].health >= 0) {
            g_enemyTarget[s] = other;
            continue;
        }

        const int dCur   = coop_dist_to(&g_players[cur], e);
        const int dOther = coop_dist_to(&g_players[other], e);
        if (dOther + COOP_SWITCH_MARGIN < dCur) {
            g_enemyTarget[s] = other;
        }
    }
}

// ---------------------------------------------------------------------------
// Player 2's body
//
// Placed beside player 1 rather than on top of him: two spawns at the same
// point start inside each other, and the first collision resolution throws one
// of them somewhere unhelpful.
// ---------------------------------------------------------------------------
#define COOP_SPAWN_OFFSET 900

// A slot nothing is using. Deliberately does NOT extend g_enemy_count past what
// is already there if a free slot exists below it: every enemy loop is bounded
// by that count and the draw loop has no 30-slot clamp of its own
// (GameLoop.cpp:337), so growing it is the riskier half of this.
static int coop_free_enemy_slot(void)
{
    for (int s = 0; s < 30; s++) {
        if ((g_EnemiesList[s].status_flags & ENTITY_STATUS_ACTIVE) != 0) continue;

        // A reserved slot is asleep - status_flags 0 - so the liveness bit
        // cannot distinguish it from a free one. Without this, reserving the
        // second player's zombie picked the SAME slot the first had just been
        // given, both players ended up sharing one body, and the second death
        // wrote over the first one's.
        int taken = 0;
        for (int q = 0; q < RAID_PLAYERS; q++) {
            if (s_reservedSlot[q] == s) { taken = 1; break; }
        }
        if (!taken) return s;
    }
    return -1;
}

// Reserve one enemy slot per player for the zombie he will become, and load its
// model NOW - at room entry, with the arena allocator where RaidEnemies_Spawn
// leaves it. The slots stay inactive (status_flags 0), so nothing draws or
// updates them until somebody dies.

void Coop_ReserveZombies(void)
{
    for (int i = 0; i < RAID_PLAYERS; i++) { g_coopZombieSlot[i] = -1; s_reservedSlot[i] = -1; }
    if (!g_coopActive) return;

    Entity* saveEntity = ENTITY;

    // Same reason as in Coop_SpawnPlayer2: these loads must not move the page
    // the room left current, or whatever loads next lands somewhere unexpected.
    const unsigned char texBankSave = g_TextureBankID;
    const unsigned char texPageSave = g_TextureCurrentPage;

    for (int i = 0; i < RAID_PLAYERS; i++) {
        const int slot = coop_free_enemy_slot();
        if (slot < 0) break;

        Entity* z = &g_EnemiesList[slot];
        memset(z, 0, sizeof(Entity));
        z->id = ENEMY_ZOMBIE;

        // The SCA collision volumes, the way RaidEnemies_Spawn gives them to
        // every enemy it places (RaidEnemies.cpp:116-118). Without them
        // pSca_hit_data is 0, and ResolveEntityScaCollision reads its world
        // geometry straight off that pointer (EntityCommon.cpp:625) - a null
        // deref the first time this zombie touches anybody, which is what
        // "volAWorld was nullptr" is. Sca_info is the VALUE from the table, not
        // the table's address.
        z->Sca_info = g_scaDataTable[0];
        z->pSca_hit_data = g_scaPoolPtr;
        g_scaPoolPtr += 8 * 6;          // the same 8 parts RAID enemies reserve
        z->death_event_id = 0xFF;       // no script event to raise on its death

        // Marked active only for the duration of the load, because
        // coop_free_enemy_slot picks by that bit and the second reservation
        // must not pick the same slot.
        z->status_flags = ENTITY_STATUS_ACTIVE;

        ENTITY = z;
        LoadEntityEMD(z, (unsigned char)(z->id + 4));
        Entity_SetJoints(z, 0x7c);
        InitAnimStructure((void*)z->modelLoadBuffer);
        g_loadDataDestPointer =
            (void*)SetupJointStructures((unsigned int)g_loadDataDestPointer);

        // The skeleton's REST POSE. SetupJointStructures fills each joint's
        // flags and mesh slot but never touches joint->transform, so the
        // translations stay whatever the model arena happened to hold. An
        // arena enemy gets them from its own init state - zombie_init calls
        // this (Zombie.cpp:336) on its first update_entities dispatch - but a
        // reserved zombie is asleep and never reaches state 0, so it woke with
        // joint 0 correct and joints 1-14 pointing into the far distance. That
        // is the body that renders as a pair of shorts hanging in the air:
        //   j00 w=(6685,-1855,3936)   j01 w=(389842459,242564028,113253608)
        // against a healthy arena zombie's j00=(6782,-1856,3450) j07=(6597,-2206,3034).
        // Done here rather than at the wake-up so nothing allocates mid-game.
        ResetJointTransforms();

        z->status_flags = 0;          // asleep until its owner dies
        s_reservedSlot[i] = slot;
    }

    ENTITY = saveEntity;
    g_TextureBankID      = texBankSave;
    g_TextureCurrentPage = texPageSave;

}

void Coop_SpawnPlayer2(void)
{
    PlayerEntity* p1 = &g_players[0];
    PlayerEntity* p2 = &g_players[1];

    // NOT a byte copy of player 1. jointsStructs is a POINTER into the model
    // load arena, carved by Entity_SetJoints out of the g_loadDataDestPointer
    // bump allocator (EntityModelLoader.cpp:368) - copying it would give both
    // players the same joints, so player 2's animation would drive player 1's
    // limbs. He needs his own allocation, and he needs Chris's model anyway.
    //
    // So run the real setup path with him current. SetupCharacterData loads the
    // EMD (char10.emd for Chris: (id & 1) * 53 + id picks block 0 row 0),
    // allocates his joints, sets his SCA collision record from his own id, and
    // installs his weapon. Everything after it restores player 1 as current, so
    // the enemy models RaidEnemies_Spawn loads afterwards still come from the
    // scenario block player 1 selects.
    memset(p2, 0, sizeof(PlayerEntity));
    p2->id = CHAR_CHRIS;
    p2->health = 140;          // Chris's starting health, GameStart.cpp:516
    p2->maxHealth = 140;

    // Player 1's SCA hit-data block is g_entityDataBlock, handed to him once at
    // game start (GameStart.cpp:594); enemies carve theirs out of the pool.
    // Player 2 had neither, so his pSca_hit_data was 0 - and player_state_init
    // writes through it unconditionally (PlayerAnimations.cpp:2064), which is
    // the access violation the first co-op run logged as player_state_init+0xf5
    // with EAX=0. Give him a block of his own, the same size as player 1's,
    // rather than carving from the enemy pool: the pool's stride is per-enemy
    // and a player is not one.
    p2->pSca_hit_data = (DWORD)s_player2ScaHitData;

    // SetupCharacterData sets g_TextureBankID / g_TextureCurrentPage for the
    // player it is setting up and leaves them there. For player 1 that is
    // harmless - nothing loads a texture between it and the room. For player 2
    // it is not: everything loaded afterwards, the arena's own zombies included,
    // would land on HIS bank and overwrite him. That is a red torso on Chris and
    // a zombie wearing half a player.
    const unsigned char texBankSave = g_TextureBankID;
    const unsigned char texPageSave = g_TextureCurrentPage;

    Coop_BeginPlayer(1);
    SetupCharacterData();
    Coop_EndPlayer();

    g_TextureBankID      = texBankSave;
    g_TextureCurrentPage = texPageSave;


    // SetupCharacterData sets Sca_info from the character id but does NOT touch
    // pSca_hit_data, so the assignment above survives it. Re-asserted here
    // because the order is load-bearing and silent if it changes.
    p2->pSca_hit_data = (DWORD)s_player2ScaHitData;

    // His own voice. load_character_sfx fills whichever half of
    // g_CharacterSfxBanks the CURRENT player owns, so it has to run with him
    // current - otherwise he borrows player 1's banks, which is why Chris
    // screamed in Jill's voice. The banks themselves are handles from
    // loadSndBankFromWav, not offsets into a shared arena, so a second set
    // costs nothing but the records.
    Coop_BeginPlayer(1);
    load_character_sfx((unsigned char)(p2->id & 1));
    Coop_EndPlayer();

    const int x = (int)p1->scaMatrixData.localMatrix.t[0] + COOP_SPAWN_OFFSET;
    const int z = (int)p1->scaMatrixData.localMatrix.t[2];

    p2->scaMatrixData.localMatrix.t[0] = x;
    p2->scaMatrixData.localMatrix.t[1] = 0;
    p2->scaMatrixData.localMatrix.t[2] = z;
    p2->position.x = (short)x;
    p2->position.y = 0;
    p2->position.z = (short)z;
    p2->posY = 0;
    p2->directionAngle = p1->directionAngle;
}

// Defaults until something sets them. See the note in CoopPlayer.h.
char g_coopName[RAID_PLAYERS][COOP_NAME_MAX] = { "PLAYER 1", "PLAYER 2" };

// ---------------------------------------------------------------------------
// Death turns you into a zombie - see the note in CoopPlayer.h.
// ---------------------------------------------------------------------------
int g_coopZombieSlot[RAID_PLAYERS] = { -1, -1 };

// Turn rate and the two actions the pad selects. 0x1000 is a full turn, so 0x30
// a frame is about 2.6 degrees - a zombie is not meant to pivot like a player.
#define COOP_Z_TURN        0x30
#define COOP_Z_ACT_IDLE    0
#define COOP_Z_ACT_WALK    1     // zombie_slow_walk: waypoint 5000 ahead of angle
#define COOP_Z_STATE_RUN   1     // zombie_state_check, the behaviour dispatch
#define COOP_Z_STATE_ATK   5     // zombie_attack

int Coop_IsZombie(int i)
{
    return (i >= 0 && i < RAID_PLAYERS && g_coopZombieSlot[i] >= 0) ? 1 : 0;
}



void Coop_CheckDeaths(void)
{
    if (!g_coopActive) return;


    for (int i = 0; i < RAID_PLAYERS; i++) {
        if (Coop_IsZombie(i)) continue;
        if (g_players[i].health >= 0) continue;

        const int slot = s_reservedSlot[i];
        if (slot < 0) continue;           // no slot was reserved; he stays a corpse

        Entity* z = &g_EnemiesList[slot];

        // No memset: the reservation left a loaded model in here and clearing
        // it would put animHeader back to 0, which is the whole bug this
        // reservation exists to avoid.
        z->id = ENEMY_ZOMBIE;
        z->status_flags = ENTITY_STATUS_ACTIVE;
        z->state = COOP_Z_STATE_RUN;
        z->action_behavior = COOP_Z_ACT_IDLE;
        z->action_state = 0;
        z->health = 100;

        // The rest of the field set RaidEnemies_Spawn gives every enemy it
        // places (RaidEnemies.cpp:100-110). The reservation's memset left these
        // zero, which is right for all of them but one: timing_control has to
        // be 1. At 0 the zombie's state handler never advances a frame, so his
        // skeleton keeps the unposed transforms SetupJointStructures left - all
        // fifteen parts collapsed onto the hip, which is the "only shorts"
        // body. Spawn-path parity again: everything that path does is
        // load-bearing, and none of it lives inside the struct.
        z->timing_control      = 1;
        z->animationId         = 0;
        z->animation_frame_id  = 0;
        z->behavior_flags      = 0;
        z->ignore_player_flag  = 0;
        z->hit_state           = 0;
        z->collisionFlags      = 0;
        z->lookAtFlags         = 0;
        z->position.pad        = 0;      // the rotation SVECTOR's X

        // Stand him up where he fell, facing the way he was.
        z->scaMatrixData.localMatrix.t[0] = g_players[i].scaMatrixData.localMatrix.t[0];
        z->scaMatrixData.localMatrix.t[1] = g_players[i].scaMatrixData.localMatrix.t[1];
        z->scaMatrixData.localMatrix.t[2] = g_players[i].scaMatrixData.localMatrix.t[2];
        z->position.x = g_players[i].position.x;
        z->position.y = g_players[i].position.y;
        z->position.z = g_players[i].position.z;
        z->angle = g_players[i].directionAngle;

        if (slot >= g_enemy_count) g_enemy_count = slot + 1;

        // The model is NOT loaded here. Coop_ReserveZombies did that at room
        // entry, while the arena allocator was still where RaidEnemies_Spawn
        // left it; loading one mid-game advances g_loadDataDestPointer over
        // whatever the room has already put there, and the first casualty is
        // the live zombie's own model - its animHeader goes to 0 and it faults
        // the moment it attacks. All this has to do now is wake the slot up.
        g_coopZombieSlot[i] = slot;

        // Any enemy still hunting him should look elsewhere; Coop_ChooseTargets
        // does that on its own next frame because his health is below zero.
    }
}

void Coop_DriveZombie(int i)
{
    if (!Coop_IsZombie(i)) return;

    // Read that player's pad without disturbing whoever's block is live: the
    // published words belong to one player at a time, so the zombie's owner
    // gets his own out of the saved block rather than the globals.
    const WORD dpad = (s_padOwner == i) ? g_PlayerDpadHeld : s_pad[i].dpadHeld;

    // The dpad bits are the same ones player movement reads: 0x8000 up,
    // 0x4000 down, 0x2000 left, 0x1000 right (PlayerAnimations.cpp's movement).
    if (dpad & 0x2000) ENTITY->angle = (short)(ENTITY->angle - COOP_Z_TURN);
    if (dpad & 0x1000) ENTITY->angle = (short)(ENTITY->angle + COOP_Z_TURN);

    const unsigned char want = (dpad & 0x8000) ? COOP_Z_ACT_WALK : COOP_Z_ACT_IDLE;

    // action_state is the handler's own "have I started yet" latch; clearing it
    // on a change is what makes zombie_slow_walk re-run its init branch and lay
    // down a fresh waypoint along the new angle.
    if (ENTITY->action_behavior != want) {
        ENTITY->action_behavior = want;
        ENTITY->action_state = 0;
    }
    if (ENTITY->state != COOP_Z_STATE_RUN && ENTITY->state != COOP_Z_STATE_ATK) {
        ENTITY->state = COOP_Z_STATE_RUN;
    }
}

// ---------------------------------------------------------------------------
// Posing a client
//
// A client runs neither update_player_anim nor update_entities - both are
// gated on Coop_IsAuthority, because a local simulation would fight the
// snapshot for the same fields every tick. The cost was that nothing advanced
// a skeleton: bodies slid around the room in their rest pose, because the
// snapshot carries an animation id and a frame number and nothing was reading
// them.
//
// Joint_move is the piece that turns those two numbers into joint transforms,
// and it is separable from the state machine that normally chooses them: it
// reads ENTITY->animationId and ENTITY->animation_frame_id and writes the
// skeleton. So the client sets both from the wire and calls it directly. It
// advances the frame by one on its way out, which is harmless - the next
// snapshot overwrites it, and in the gap between snapshots that extra frame is
// a free interpolation rather than a glitch.
//
// timing_control has to be 1: Joint_move returns early without posing anything
// while it is above that, and a value that arrived mid-animation would stall
// the pose for several frames.
// ---------------------------------------------------------------------------
// Which of the player's four animation sources a pose came from. The state
// machine picks between them per animation - emdScratchPtr1/2 for most of the
// movement set, jointMoveData0/1 for the weapon poses, jointMoveData2/3 and
// animHeader/animBase for the rest - and a client that always used one of them
// played a DIFFERENT animation from the host rather than none at all. The host
// records what it actually used; the wire carries the choice.
//
// Derived by comparing pointers inside Joint_move rather than by touching its
// fifty-odd call sites, none of which are port code.
unsigned char g_coopJointSrc[RAID_PLAYERS]  = { 0, 0 };
unsigned char g_coopJointMirror[RAID_PLAYERS] = { 0, 0 };

static void coop_pair(const PlayerEntity* p, int sel,
                      unsigned int* outHeader, unsigned int* outBase)
{
    switch (sel) {
    case 1:  *outHeader = p->jointMoveData0;  *outBase = p->jointMoveData1;  break;
    case 2:  *outHeader = p->jointMoveData2;  *outBase = p->jointMoveData3;  break;
    case 3:  *outHeader = p->emdScratchPtr1;  *outBase = p->emdScratchPtr2;  break;
    default: *outHeader = p->animHeader;      *outBase = p->animBase;        break;
    }
}

void Coop_NoteJointSource(unsigned int animHeader, unsigned int animBase, char reverse)
{
    if (!g_coopActive) return;

    for (int i = 0; i < RAID_PLAYERS; i++) {
        if (ENTITY != (const Entity*)&g_players[i]) continue;
        const PlayerEntity* p = &g_players[i];
        for (int sel = 0; sel < 4; sel++) {
            unsigned int h = 0, b = 0;
            coop_pair(p, sel, &h, &b);
            if (h == animHeader && b == animBase) {
                g_coopJointSrc[i]    = (unsigned char)sel;
                g_coopJointMirror[i] = (unsigned char)(reverse != 0);
                return;
            }
        }
        return;                  // a player, but no pair matched - leave the last
    }
}

static void coop_pose_current(unsigned int animHeader, unsigned int animBase,
                              unsigned char mirror)
{
    if (animHeader == 0 || animBase == 0) return;

    // Pose the frame the wire named, and leave that frame exactly where it was.
    //
    // Joint_move advances animation_frame_id on its way out, and timing_control
    // is what normally holds it back. Forcing timing_control to 1 and letting
    // the advance stand meant the client ran the animation ON ITS OWN, one
    // frame per tick, on top of the frame the snapshot had just set: bodies
    // jittered between frame N and N+1, and a corpse - which the host holds on
    // its last frame by keeping timing_control above 1 - ran off the end of the
    // death animation and stood back up.
    //
    // So: force the pose to happen, then put the frame back. The client never
    // advances anything; it only ever shows what the host sent.
    const unsigned char frame = ENTITY->animation_frame_id;
    const unsigned char timing = ENTITY->timing_control;

    ENTITY->timing_control = 1;     // 1 poses; anything above it returns early
    ENTITY->blend_counter  = 0;     // no blend: the wire frame IS the pose
    Joint_move((char)mirror, animHeader, animBase, 0x400);

    ENTITY->animation_frame_id = frame;
    ENTITY->timing_control     = timing;
}

// ---------------------------------------------------------------------------
// The frame the host actually POSED
//
// Joint_move advances animation_frame_id on its way out, and on the call that
// finishes an animation it WRAPS IT TO ZERO and returns 1
// (PlayerAnimations.cpp, "Check for animation loop"). A state handler that
// stops posing on that return value keeps its skeleton on the last frame of the
// animation for ever, while the field on the entity reads 0.
//
// zombie_dead_animation is exactly that shape: case 1 advances to case 2 on
// Joint_move's 1, and cases 2, 3 and 4 - the whole corpse - never call it
// again. On one machine that is invisible, because nothing poses from the field
// after that point.
//
// Over a wire it is fatal. The snapshot carried animation_frame_id, a client
// poses from what the snapshot carries, and frame 0 of the fall animation is a
// zombie standing on its feet. THAT is the standing corpse: not a stale field,
// not a missing one, not the posing code - the host's 0 is correct and means
// "nothing to pose", and only the client read it as a frame to draw.
//
// So the wire carries the frame the host POSED rather than the frame it would
// pose next, recorded here where the pose happens - the same trick, and for the
// same reason, as Coop_NoteJointSource above.
// A player has the same hole, reached by a different road. The aim behaviour
// runs as two handlers in one tick, and between them there are ticks on which
// NOTHING poses: player_behavior_13_gun_hold_input answers a turn by setting
// action_state 2 and unk_8c 0, and player_behavior_13_gun_raise's case 2 then
// takes the branch that only rewrites attackAnim and zeroes the frame - no
// Joint_move at all - before case 3 poses from animHeader/animBase, which is a
// DIFFERENT pair from the jointMoveData0/1 the hold used.
//
// So on those ticks the entity's fields describe an animation nobody has posed
// yet, while the skeleton still holds the last pose. A client sent those fields
// drew a pose the host never showed, for one tick, every time the player turned
// while aiming - which is what the jitter was.
static unsigned char s_posedAnim[30];
static unsigned char s_posedFrame[30];
static unsigned char s_posedValid[30];

static unsigned char s_pPosedAnim[RAID_PLAYERS];
static unsigned char s_pPosedFrame[RAID_PLAYERS];
static unsigned char s_pPosedSrc[RAID_PLAYERS];
static unsigned char s_pPosedMirror[RAID_PLAYERS];
static unsigned char s_pPosedValid[RAID_PLAYERS];

void Coop_NotePose(void)
{
    if (!g_coopActive) return;

    const Entity* e = ENTITY;

    for (int i = 0; i < RAID_PLAYERS; i++) {
        if (e != (const Entity*)&g_players[i]) continue;
        // The source is already correct for THIS pose: Coop_NoteJointSource ran
        // a few lines earlier in the same Joint_move call, off the same
        // pointers. Capturing it here as well is what makes the four values one
        // consistent set rather than four fields sampled at different moments.
        s_pPosedAnim[i]   = e->animationId;
        s_pPosedFrame[i]  = e->animation_frame_id;
        s_pPosedSrc[i]    = g_coopJointSrc[i];
        s_pPosedMirror[i] = g_coopJointMirror[i];
        s_pPosedValid[i]  = 1;
        return;
    }

    if (e < &g_EnemiesList[0] || e > &g_EnemiesList[29]) return;

    const int slot = (int)(e - &g_EnemiesList[0]);
    s_posedAnim[slot]  = e->animationId;
    s_posedFrame[slot] = e->animation_frame_id;
    s_posedValid[slot] = 1;
}

int Coop_PosedPlayerPose(int i, unsigned char* anim, unsigned char* frame,
                         unsigned char* src, unsigned char* mirror)
{
    if (i < 0 || i >= RAID_PLAYERS || s_pPosedValid[i] == 0) return 0;
    *anim   = s_pPosedAnim[i];
    *frame  = s_pPosedFrame[i];
    *src    = s_pPosedSrc[i];
    *mirror = s_pPosedMirror[i];
    return 1;
}

int Coop_PosedPose(int slot, unsigned char* anim, unsigned char* frame)
{
    if (slot < 0 || slot >= 30 || s_posedValid[slot] == 0) return 0;
    *anim  = s_posedAnim[slot];
    *frame = s_posedFrame[slot];
    return 1;
}

void Coop_ForgetPose(int slot)
{
    if (slot < 0 || slot >= 30) return;
    s_posedValid[slot] = 0;
}

// ---------------------------------------------------------------------------
// The ground shadow, and the blood pool it turns into
//
// A body's shadow is not an effect and not a sprite of its own: it is a quad
// INSIDE the entity, at +0xE4, built by FUN_004565f0 and pushed into the
// fade-sprite queue once per frame by the entity's own update - for a zombie,
// by zombie_update (Zombie.cpp). The death blood pool is that same quad,
// recoloured to 0x00ffff50 and resized by zombie_dead_animation.
//
// A client runs no entity update, so nothing ever queues it: there is no
// shadow under anybody on a client, and therefore no pool either. So the wire
// carries the quad's state - its tint, its half extents and its own offset -
// and the client rebuilds it with the game's own builder and queues it in the
// same place in the frame the host does, just before DrawFadeSpr drains it.
static unsigned char s_shTint[30][3];
static short         s_shW[30], s_shH[30], s_shOx[30], s_shOz[30];
static unsigned char s_shValid[30];

void Coop_ReadShadow(const void* quadPtr, unsigned char tint[3],
                     short* w, short* h, short* ox, short* oz)
{
    const unsigned char* q = (const unsigned char*)quadPtr;
    // Byte 0x0C is the header dword FUN_004565f0 fills from the tint scratch
    // and BillboardSetColor rewrites; 0x60/0x64 are the +halfW/+halfH corner
    // the size calls patch; 0x00/0x04 are the quad's own x/z offset, which is
    // what entity_add_fade_sprite adds to the body's position.
    const unsigned int tintWord = *(const unsigned int*)(q + 0x0C);
    tint[0] = (unsigned char)(tintWord & 0xFF);
    tint[1] = (unsigned char)((tintWord >> 8) & 0xFF);
    tint[2] = (unsigned char)((tintWord >> 16) & 0xFF);
    *w  = *(const short*)(q + 0x60);
    *h  = *(const short*)(q + 0x64);
    *ox = *(const short*)(q + 0x00);
    *oz = *(const short*)(q + 0x04);
}

void Coop_SetShadow(int slot, const unsigned char tint[3],
                    short w, short h, short ox, short oz)
{
    if (slot < 0 || slot >= 30) return;
    s_shTint[slot][0] = tint[0];
    s_shTint[slot][1] = tint[1];
    s_shTint[slot][2] = tint[2];
    s_shW[slot]  = w;
    s_shH[slot]  = h;
    s_shOx[slot] = ox;
    s_shOz[slot] = oz;
    s_shValid[slot] = 1;
}

static void coop_client_shadow(Entity* e, int slot)
{
    if (slot < 0 || slot >= 30 || s_shValid[slot] == 0) return;

    SVECTOR ofs;
    ofs.x = s_shOx[slot]; ofs.y = 0; ofs.z = s_shOz[slot]; ofs.pad = 0;

    // The tint reaches the builder through the scratch global it reads rather
    // than through an argument - see the note in zombie_init about which of the
    // two scratch dwords that is.
    g_animFrameIdSave = ((unsigned int)s_shTint[slot][2] << 16)
                      | ((unsigned int)s_shTint[slot][1] << 8)
                      | (unsigned int)s_shTint[slot][0];
    FUN_004565f0(&ofs, (SVECTOR*)&e->pushVelocity, s_shW[slot], s_shH[slot]);

    // The host queues this one only when the body is in a camera switch zone,
    // and that test is already on the wire because render_entity gates drawing
    // on the same byte.
    if (e->has_enter_switch_zone == 0) return;
    entity_add_fade_sprite((VECTOR*)&e->scaMatrixData.localMatrix.t[0],
                           (short*)&e->pushVelocity, 0, e->angle);
}

// ---------------------------------------------------------------------------
// Effects
//
// A billboard is spawned by whoever's AI decided to spawn it, which on a
// client is nobody: it runs no state machine, so a zombie bit a player and no
// blood appeared. The snapshot carries world state; an effect is an EVENT, and
// events have to be queued and shipped rather than sampled.
//
// What travels is the call's own arguments plus a PARENT ID rather than the
// parent pointer: spriteInfo is a MATRIX* belonging to some entity, and the
// client has its own copy of that entity at the same slot. Sending the id and
// rebuilding the pointer locally keeps the parent's rotation, which resolving
// to a world position on the host would have thrown away.
// ---------------------------------------------------------------------------
#define COOP_EFFECT_NONE   0xFF
#define COOP_EFFECT_PLAYER 0x80   // | player index

CoopEffectEvent g_coopEffectQueue[COOP_EFFECT_QUEUE];
int             g_coopEffectCount = 0;

static unsigned char coop_effect_parent(const void* spriteInfo)
{
    if (spriteInfo == 0) return COOP_EFFECT_NONE;
    for (int i = 0; i < 30; i++) {
        if (spriteInfo == (const void*)&g_EnemiesList[i].scaMatrixData.localMatrix) {
            return (unsigned char)i;
        }
    }
    for (int i = 0; i < RAID_PLAYERS; i++) {
        if (spriteInfo == (const void*)&g_players[i].scaMatrixData.localMatrix) {
            return (unsigned char)(COOP_EFFECT_PLAYER | i);
        }
    }
    return COOP_EFFECT_NONE;     // something else's matrix; send it parentless
}

void* Coop_EffectParentPtr(unsigned char parent)
{
    if (parent == COOP_EFFECT_NONE) return 0;
    if (parent & COOP_EFFECT_PLAYER) {
        const int i = parent & 0x7F;
        if (i < 0 || i >= RAID_PLAYERS) return 0;
        return (void*)&g_players[i].scaMatrixData.localMatrix;
    }
    if (parent >= 30) return 0;
    return (void*)&g_EnemiesList[parent].scaMatrixData.localMatrix;
}

void Coop_NoteEffect(unsigned char type, unsigned char depthGroup, short yaw,
                     const void* spriteInfo, const void* pos, char lightFactor)
{
    // Host only. A client calls Effect_CreateBillboard while replaying these,
    // and recording those would send them back round for ever.
    if (!Coop_IsNetworked() || !Coop_IsAuthority()) return;
    if (g_coopEffectCount >= COOP_EFFECT_QUEUE) return;   // drop, do not grow
    if (pos == 0) return;

    const short* v = (const short*)pos;      // VECTOR's first three components
    CoopEffectEvent* e = &g_coopEffectQueue[g_coopEffectCount++];
    e->type        = type;
    e->depthGroup  = depthGroup;
    e->lightFactor = (unsigned char)lightFactor;
    e->parent      = coop_effect_parent(spriteInfo);
    e->yaw         = yaw;
    e->x = ((const int*)pos)[0];
    e->y = ((const int*)pos)[1];
    e->z = ((const int*)pos)[2];
    (void)v;
}

void Coop_ClientPose(void)
{
    if (!g_coopActive) return;

    Entity* saveEntity = ENTITY;

    for (int i = 0; i < Coop_PlayerCount(); i++) {
        if (Coop_IsZombie(i)) continue;        // his body is an entity now
        Coop_BeginPlayer(i);
        unsigned int h = 0, b = 0;
        coop_pair(&g_players[i], g_coopJointSrc[i], &h, &b);
        coop_pose_current(h, b, g_coopJointMirror[i]);
        Coop_EndPlayer();
    }

    // The same for whatever the arena is holding. Bounded by the slot count
    // rather than g_enemy_count, and by the active bit, because a client's
    // count comes from its own spawn and the liveness comes off the wire.
    for (int s = 0; s < 30; s++) {
        Entity* e = &g_EnemiesList[s];
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) == 0) continue;
        ENTITY = e;
        coop_pose_current(e->animHeader, e->animBase, 0);
        coop_client_shadow(e, s);
    }

    ENTITY = saveEntity;
}

void Coop_SetRemotePad(int i, unsigned int padHeld,
                       unsigned short dpadHeld, unsigned short dpadPressed)
{
    if (i < 0 || i >= RAID_PLAYERS) return;

    // Straight into the saved block, and into the live globals too when this
    // player's block happens to be the one loaded. Going through the same
    // storage the local path uses is what keeps "remote" from being a second
    // kind of input the game has to know about.
    s_pad[i].padHeld     = padHeld;
    s_pad[i].dpadHeld    = dpadHeld;
    s_pad[i].dpadPressed = dpadPressed;

    if (s_padOwner == i) {
        g_PlayerPadHeld     = padHeld;
        g_PlayerDpadHeld    = dpadHeld;
        g_PlayerDpadPressed = dpadPressed;
    }
}
