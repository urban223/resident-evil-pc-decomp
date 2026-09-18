// CoopPlayer.h - two players in the RAID arena.
//
// CUSTOM. Not part of the original game, which is single-player to its bones:
// one PlayerEntity, one set of published pad words, one "the player" that every
// enemy measures its distance to.
//
// The whole feature rests on one indirection that the engine already had. The
// transcribed code names g_playerEntity 3370 times, and since Phase 1a that
// name is a macro for *g_pCurPlayer (see Globals.h). So "run this code for
// player N" is not a rewrite - it is repointing one pointer around the call,
// exactly the way the engine already repoints ENTITY around an entity update.
//
// That buys three things at once:
//
//   * the player state machine (update_player_anim and its 52 animation
//     functions) drives player 1 unchanged;
//   * so does every enemy's distance-to-player helper - entity_check_visual_range
//     and friends read g_playerEntity directly, so pointing g_pCurPlayer at an
//     enemy's OWN target before dispatching it gives per-enemy targeting with
//     no edit to any enemy file;
//   * and the auto-aim exclusion is structural rather than a rule: both target
//     scans walk g_EnemiesList only (PlayerAnimations.cpp:4197, :4234), and a
//     player is not in that array, so nothing can acquire a teammate. Friendly
//     fire is therefore a separate, deliberate hit test - which is the point.
//
// What it does NOT buy, and what this file has to do by hand:
//   * pad state: PlayerPad_Update keeps nine globals of edge-detection state,
//     two of which live inside the save block, so each player's is swapped in
//     and out around the call rather than duplicated in place;
//   * body-vs-body collision between the two players, because
//     HandleEnemyPlayerCollisions walks g_EnemiesList and neither player is in it.
#pragma once

#include "../Globals.h"

// Co-op is RAID-only and off by default. Nothing in the story campaign reads
// any of this, and with it off g_pCurPlayer never leaves &g_players[0].
extern int g_coopActive;

// Which player each enemy slot is hunting. Slot-indexed like the status-effect
// system (WeaponDamage.cpp:133-137), for the same reason: Entity is pinned to
// 0x18C by static_assert and has no spare byte to put it in.
extern unsigned char g_enemyTarget[30];

// Point g_pCurPlayer (and ENTITY) at player `i` for the duration of a call that
// reads "the player", restoring the previous one afterwards. Nested use is not
// supported; these are a matched pair around one call site.
void Coop_BeginPlayer(int i);
void Coop_EndPlayer(void);

// Per-frame, in order:
//   Coop_UpdatePads()     - publish each player's pad words from its own source
//   Coop_ChooseTargets()  - decide what each live enemy is hunting
void Coop_UpdatePads(void);
void Coop_ChooseTargets(void);

// Number of players actually in play: 1 outside co-op, RAID_PLAYERS inside it.
int Coop_PlayerCount(void);

// Which input source ReadPadBoth should answer with: 0 = player 1, 1 = player 2,
// -1 = the original's keyboard|joystick merge. Only Coop_UpdatePads sets it, and
// it is back to -1 before anything else runs.
extern int g_coopPadSource;

// Set up player 2 next to player 1's spawn. Called from Raid_EnterRoom once the
// level's own spawn has been applied to player 1.
void Coop_SpawnPlayer2(void);

// ---------------------------------------------------------------------------
// Friendly fire
//
// Requirement: you can shoot your teammate, deliberately, and a magnum can take
// his head off - but the aiming assist must never acquire him.
//
// The second half is already true and needs no code: both target scans walk
// g_EnemiesList (PlayerAnimations.cpp:4197, :4234) and a player is not in that
// array. Nothing can lock onto him, so every teammate hit is aimed by hand.
//
// The first half cannot reuse the gun's own cone: weapon_hit_detect_gun stages
// it in g_svecScratch and checkEntityInRangeCone mutates the shared
// g_playerDisplacement accumulator, so calling it for a second body outside that
// staging would corrupt the enemy search running in the same shot. This is a
// separate, narrower test instead - which also makes hitting a teammate feel
// deliberate rather than incidental.
//
// Returns 1 if the shot was spent on the teammate.
int Coop_FriendlyFire(unsigned char weaponId);

// ---------------------------------------------------------------------------
// Nameplates
//
// Storage only. The drawing lives in RaidArena.cpp, because that is where the
// camera view exists: RaBuildView/RaProject already run every frame in RAID and
// produce backbuffer pixels, which is the space UiAtlas_TextPushed wants.
// EditorView_Project is the same maths but is unusable here - g_edView.ok is
// only ever set by EditorView_Build, which is called from four places, all
// inside the editor, so outside it the function returns 0 and writes nothing.
//
// Names are variables, not literals, so a lobby or a config can set them later.
#define COOP_NAME_MAX 16
extern char g_coopName[RAID_PLAYERS][COOP_NAME_MAX];

// ---------------------------------------------------------------------------
// Death turns you into a zombie
//
// A killed player keeps playing, as a zombie, on the same pad. He is not a
// PlayerEntity any more: the zombie moveset lives in zombie state handlers that
// read an Entity, so his body becomes a real entity in g_EnemiesList with
// id = ENEMY_ZOMBIE. That is what makes the moveset, the hit reactions, the
// dismemberment and the magnum decapitation work natively rather than by
// imitation.
//
// The control seam is the one the freeze pistol already proved: update_entities
// can skip the AI dispatch without disturbing drawing, collision or joints
// (EntityCommon.cpp:1792). Here the AI is not skipped but STEERED - the pad
// picks the state and the action, and the engine's own handler runs. That
// matters because a zombie only moves through Add_speedXZ and only advances a
// frame through Joint_move, and both are reachable only from inside a state
// handler; driving one from outside would mean reimplementing it.
//
// Forward works because zombie_slow_walk sets its waypoint 5000 units ahead
// along ENTITY->angle (Zombie.cpp:2180) rather than at the player - so writing
// the angle from the pad steers it.

// The enemy slot each player's zombie occupies, or -1 while he is alive.
extern int g_coopZombieSlot[RAID_PLAYERS];

// Is this player currently playing as a zombie?
int  Coop_IsZombie(int i);

// Once per frame, before the entity update: promote any player whose health has
// gone below zero into a zombie.
void Coop_CheckDeaths(void);

// Steer slot `slot`'s zombie from player `i`'s pad. Called from update_entities
// in place of the AI's own targeting, immediately before the state dispatch.
void Coop_DriveZombie(int i);
