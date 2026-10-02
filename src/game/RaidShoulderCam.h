// RaidShoulderCam.h - hold L2 in RAID: aim, with the camera over the shoulder.
//
// CUSTOM. See RaidShoulderCam.cpp.
#pragma once

// Raw PSX L2. No gameplay code tests it, so it is free to mean "aim with the
// camera over the shoulder" in RAID.
#define RAID_RAW_L2      0x0001
#define RAID_DPAD_AIM    0x0100    // the dpad bit every aim test reads

// ReadPadBoth: turn the RAID L2 button on a pad mask / the keyboard into
// RAID_RAW_L2, taking it away from whatever the remap table gives it (the left
// trigger is "run" outside RAID). `table` is JoyToPSX's: 0 keyboard, 1 pad.
unsigned int RaidShoulderCam_PadToPSX(unsigned int pcMask, int table);

// ReadPadBoth: player 1's right stick, -32768..32767, X right-positive and
// Y up-positive.
void RaidShoulderCam_NoteStick(int x, int y);
// L1 (the left bumper, or E): held this frame - it toggles the shoulder
// camera on and off. `pad` 1 from the joystick path, 0 from the keyboard.
void RaidShoulderCam_NoteL1(int held, int pad);
// Player 1's left stick, for the L1 view's walking (up and right positive).
void RaidShoulderCam_NoteLeftStick(int x, int y);

// ReadPadBoth, on its way out: remember whose L2 this was.
void RaidShoulderCam_NoteRaw(unsigned int raw);

// PlayerPad_Update: L2 held also holds aim. Applied to the previous AND the
// current dpad word, from the matching raw word, so aim does not read as a
// fresh press on every frame it is held.
unsigned short RaidShoulderCam_AddAim(unsigned short dpad, unsigned int raw);

// game_loop, after the players have moved and before the scene is drawn.
void RaidShoulderCam_Update(void);

// 0 when the room's own camera is in use, 1 when ours is, eased in between.
float RaidShoulderCam_Amount(void);

// A world point down player 1's gun line, and how far his arms are raised
// (0..1). 0 if there is no pose to read yet.
int RaidShoulderCam_AimPoint(float* x, float* y, float* z, float* raised);

// Free aim (see the .cpp): 1 while the CURRENT player is player 1 holding L2
// over the shoulder.
int RaidShoulderCam_FreeAim(void);

// game_loop's player update, around update_player_anim for player i.
void RaidShoulderCam_BeforePlayer(int i);
void RaidShoulderCam_AfterPlayer(int i);

// game_loop's player draw, before his world matrices are composed.
void RaidShoulderCam_PoseArms(int i);

// apply_weapon_damage's per-enemy test while free aim is on.
struct Entity;
unsigned char RaidShoulderCam_HitTest(short range, Entity* e);

// update_player_anim, after the state machine and before wall collision:
// player 1 walking with the gun up (see the .cpp).
void RaidShoulderCam_Walk(void);

// The reticle's focus, 0 open .. 1 closed (see the .cpp): the crosshair's
// gap is drawn from it.
float RaidShoulderCam_Focus(void);

// apply_weapon_damage, on a free-aim hit: a fully focused shot's bonus.
short RaidShoulderCam_ScaleDamage(short damage);

// Co-op: the aim pose that rides on top of the animation, for the snapshot.
// Get is the host's side (zeros for a player this does not pose); Set is the
// client's, and is ignored on the host.
void RaidShoulderCam_GetPose(int i, short* pitch, signed char* walk,
                             unsigned char* walkFrame, unsigned char* legBlend,
                             short* hipYaw);
void RaidShoulderCam_SetPose(int i, short pitch, signed char walk,
                             unsigned char walkFrame, unsigned char legBlend,
                             short hipYaw);

// The angle (radians) a shot fired NOW may land off the gun line: the
// crosshair's ticks are drawn at it.
float RaidShoulderCam_Spread(void);
