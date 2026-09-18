// CoopPlayer.cpp - two players in the RAID arena. CUSTOM; see CoopPlayer.h.
#include "CoopPlayer.h"
#include "entities/EntityCommon.h"
#include "Entities.h"
#include <cstring>

int g_coopActive = 0;
unsigned char g_enemyTarget[30] = {};

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

    Coop_BeginPlayer(1);
    SetupCharacterData();
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

// A slot nothing is using. Deliberately does NOT extend g_enemy_count past what
// is already there if a free slot exists below it: every enemy loop is bounded
// by that count and the draw loop has no 30-slot clamp of its own
// (GameLoop.cpp:337), so growing it is the riskier half of this.
static int coop_free_enemy_slot(void)
{
    for (int s = 0; s < 30; s++) {
        if ((g_EnemiesList[s].status_flags & ENTITY_STATUS_ACTIVE) == 0) return s;
    }
    return -1;
}

void Coop_CheckDeaths(void)
{
    if (!g_coopActive) return;

    for (int i = 0; i < RAID_PLAYERS; i++) {
        if (Coop_IsZombie(i)) continue;
        if (g_players[i].health >= 0) continue;

        const int slot = coop_free_enemy_slot();
        if (slot < 0) continue;           // nowhere to put him; stays a corpse

        Entity* z = &g_EnemiesList[slot];
        memset(z, 0, sizeof(Entity));

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
