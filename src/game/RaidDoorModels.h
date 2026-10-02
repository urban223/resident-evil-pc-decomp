// RaidDoorModels.h - the game's own 3D doors, standing in the RAID room.
//
// CUSTOM. Not in the original game.
//
// Every door in the game has a model: the one its full-screen opening
// animation shows on black while the next room loads (item_m1\doorNN.dor,
// DoorSystem.cpp). A RAID door (`door` line) opens in the room instead, with
// no cut - and a `doormodel` line under it draws it with that same leaf and
// handle, swung by the RAID door's own angle (RaidArena.cpp).
#pragma once

// Load what the current level's doors need. Cheap when nothing changed.
void RaidDoorModels_Sync(void);

// Release every slot and texture page (a level (re)load, leaving the mode).
void RaidDoorModels_Reset(void);

// Queue every modelled door for this frame, at its current angle. From
// RaidArena_Draw, beside the pickups.
void RaidDoorModels_Draw(void);
