// RaidItemModels.h - the pickups' real models, in the room.
//
// CUSTOM. Not in the original game.
//
// A story room's item models come out of that room's own PAK, loaded by its
// script. The RAID arena has neither, which is why its pickups were marker
// boxes. This loads the game's own .ivm files instead - the same ones the
// examine screen shows - and draws them where the pickups lie.
//
// ONE MODEL PER DISTINCT TYPE, not per pickup: four clips on the floor are one
// parsed mesh drawn four times, from four matrices.
#pragma once

// Forget everything and reload for the current level. Safe to call repeatedly;
// it early-outs when the set of item types has not changed.
void RaidItemModels_Sync(void);

// Release every slot and texture page. Called when leaving the mode.
void RaidItemModels_Reset(void);

// Queue every un-taken pickup for this frame. Called from RaidArena_Draw, in
// the same slot the marker boxes used.
void RaidItemModels_Draw(void);

// Does this item type have a model loaded and ready? RaidArena falls back to
// the marker box when it does not, so a missing file costs a look, not a crash.
int RaidItemModels_Have(unsigned char itemType);
