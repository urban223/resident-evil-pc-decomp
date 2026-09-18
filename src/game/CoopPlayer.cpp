// CoopPlayer.cpp - two players in the RAID arena. CUSTOM; see CoopPlayer.h.
#include "CoopPlayer.h"
#include "entities/EntityCommon.h"
#include "Entities.h"
#include <cstring>
#include <cstdio>
#include "../platform/platform.h"

int g_coopActive = 0;
unsigned char g_enemyTarget[30] = {};

// Enemy slots held for the zombies the players will become; see Coop_ReserveZombies.
static int s_reservedSlot[RAID_PLAYERS] = { -1, -1 };
// The model loaders, declared the way RaidEnemies.cpp declares them
// (RaidEnemies.cpp:40-43): the unsigned-int form of SetupJointStructures is the
// one that returns the advanced arena pointer, and it is deliberately NOT the
// void* overload in Globals.h - see the overload note in CharacterNpc.h.
extern void LoadEntityEMD(Entity* em, unsigned char entity_id);
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
