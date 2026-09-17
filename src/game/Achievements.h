// Achievements.h - CUSTOM (port-only): achievement pop-ups.
//
// Not part of the original game. The toast that drops in from the top of the
// screen is drawn straight through the Marni layer from one RGBA atlas
// (assets/USA/Data/achvui.bin, baked by tools/build_achievement_ui.py from the
// Space GUI pack, and reached through UiAtlas.h - the same sheet and the same
// loader the status-screen skin draws from), so it shares nothing with the
// PSX VRAM-page path the rest
// of the 2D art goes through and is free of its 8bpp/CLUT limits - which is
// also why it can use a real TTF-baked font instead of fontus.tim's 8x14 one.
//
// Progress is kept in <save root>/achieve.dat, a profile file of this feature's
// own: the game's save block is ROM-shaped and has no room for it, and
// achievements are meant to span playthroughs anyway.
#pragma once

enum {
    ACHV_FIRST_BLOOD = 0,      // kill one enemy
    ACHV_KEEPING_A_RECORD,     // save the game once
    ACHV_EXTERMINATOR,         // kill 25 enemies (progress achievement)
    ACHV_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif

// Per presented frame, from FrameRateGovernor: advance the toast animation and
// draw it on top of everything else. Both are no-ops until the first unlock.
void Achievements_Tick(void);
void Achievements_Draw(void);

// Award an achievement outright. Repeat calls for one already unlocked do
// nothing, so call sites need no "have I done this" bookkeeping of their own.
void Achievements_Unlock(int id);

// Add to a counter achievement. Shows a progress toast at each fifth of the
// target and an unlock toast when it is reached.
void Achievements_AddProgress(int id, int amount);

// --- queries ----------------------------------------------------------------
// The status screen shows the profile's point total and how many of the
// achievements are unlocked; both read the same file the toasts do.
int Achievements_Points(void);
int Achievements_UnlockedCount(void);
int Achievements_Total(void);

// --- game-event hooks -------------------------------------------------------
void Achievements_OnEnemyKilled(void);   // WeaponDamage.cpp
void Achievements_OnGameSaved(void);     // SaveLoadScreen.cpp

#ifdef __cplusplus
}
#endif
