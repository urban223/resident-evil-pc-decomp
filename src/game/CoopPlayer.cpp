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
