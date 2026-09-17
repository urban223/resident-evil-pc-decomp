// RaidEnemies.h - putting the level's enemies in the arena.
//
// CUSTOM. Not in the original game.
//
// RaidLevel.cpp has parsed `enemy` lines since the format existed, and nothing
// has ever consumed them. This does: it fills entity slots the way the script
// opcode cmd_enemy_set fills them, and loads each one's model the way room_set
// loads a room's.
#pragma once

// Fill g_EnemiesList from g_raidLevel and load the models. Called once, from
// Raid_EnterRoom, after the level has been applied.
void RaidEnemies_Spawn(void);

// Clear the ones this put there. Called when leaving the mode.
void RaidEnemies_Clear(void);
