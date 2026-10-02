# RAID: aiming over the shoulder (L2)

CUSTOM, RAID mode only. `src/game/RaidShoulderCam.{h,cpp}`.

Holding L2 (left trigger, or Q on the keyboard) in the RAID arena raises the
gun and swings the camera in behind the right shoulder, the way Resident Evil 2
(2019) does it; letting go lowers the gun and swings the camera back to the
room's own. It exists only in RAID because only there is the room real
geometry - an ordinary room is a picture taken from one fixed eye.

Status: played and confirmed in game, solo and in co-op on loopback.

## Controls

| Input | While L2 is held |
|---|---|
| L1 / E | toggle the over-the-shoulder view (below) |
| L2 / Q | raise the gun, shoulder camera |
| R2 | fire |
| left stick | walk with the gun up (step animation), strafe sideways - it does not turn her |
| right stick | aim: yaw turns her, pitch raises and lowers the gun, the arms and torso lean with it, the camera follows |

With no weapon equipped the camera still swings in, but stays farther back,
and there is no free aim: she walks and turns on her own animations (free aim
without a gun had no stance to walk under and only twisted her hips).

## The L1 view - exploring over the shoulder

L1 (the left bumper, or E) toggles the camera between the room's fixed one
(the original, the default) and an over-the-shoulder view that stays up
whether or not she aims - the way Resident Evil 2 (2019) explores:

| Input | In the L1 view, not aiming |
|---|---|
| right stick | swings the camera round her and tips it; it stays where it is put |
| left stick | moves her relative to the CAMERA, exactly where it points (analogue, any angle): her body turns fully for forward, about half way for sideways, and not at all pulled back - she steps back toward the camera with her back to it |
| L2 | raises the gun where the camera looks (she turns to it); the aim is as above |

The view sits further back and higher than the aim (3000 behind, the RE2
framing: her whole figure left of centre) and follows her with a little lag;
aiming pulls it in close with no lag. It does not swing back behind her by
itself - that was tried and was worse.

All of this was measured, not guessed: RE2 (2019) was played with the screen
and the pad recorded on one clock (frames at 6 a second, the DualShock read
through WinMM at 60), and each frame read against both sticks. The game's own
walk and back-step animate her; the step they make is sent along the stick's
direction (RaidShoulderCam_Walk) before collision, so walls still stop her.
The left stick reaches the game as an angle (MarniPadLeftStickX/Y: XInput,
WinMM, SDL) as well as the eight D-pad directions.

## The crosshair

As the recording shows it: always at the screen centre - the shot goes from the
camera through it - four ticks that open while she moves or fires and close in
about 0.6 s when she holds still, a dot in the middle once they have. Turning
the view does not open it.

## The camera and the walls

In a narrow room the eye's place behind her is often inside a wall. A line is
cast from her head to where the eye wants to be against every drawn box that
stands up (not floor and ceiling plates, not a door's doorway), and the eye
stops just short of the first one. It is pulled in at once - never a frame
inside a wall - and eased back out, so walking past a wall's end it slides back
instead of jumping.

## How it is put together

- **Input.** Raw PSX L2 (0x01) is a bit no gameplay code tests. In RAID the
  trigger produces it (`InputSystem.cpp`, `PadToPSX`), and `PlayerPad_Update`
  adds the aim bit 0x100 beside it - so the stance, firing and lowering the gun
  are the game's own code. The right stick reaches the game as
  `MarniPadRightStickX/Y` (XInput thumb, WinMM R/Z/U axes, SDL on Linux).
- **Camera.** The editor's trick (`EditorCamera.cpp`): write the eye into the
  RDT camera record the room is already using and call `Room_SetupCamera()`.
  The arena, the models and the 3D sound pan all read that record. It follows
  player 1 only, as the room camera always has in co-op.
- **Free aim, no auto-aim.** While aiming, `apply_weapon_damage` swaps the
  weapon's hit strip for `RaidShoulderCam_HitTest`: a ray from the CAMERA
  through the screen centre, where the crosshair is (the camera is off the gun
  line, so a ray from the gun would miss what the crosshair covers), tested
  against each enemy's SCA volumes. The ray's height picks the hit bucket the three fixed poses used to
  (high 0x80, middle 0x40, low 0x20). An unfocused shot spreads by up to the
  drawn reticle; holding still closes it over 1.5 s.
- **The body.** `RaidShoulderCam_PoseArms` bends the torso by half the pitch
  and gives the arms the rest; `RaidShoulderCam_Walk` runs the walk cycle on
  the legs under the aim stance, before collision, and turns the hips toward a
  strafe.
- **Crosshair.** `RaidArena.cpp`, `RaDrawCrosshair`: at the screen centre (see
  below); it fades in with the camera swing.
- **Co-op.** The snapshot carries the aim pitch, the leg cycle and the hip yaw
  (`CoopNet.cpp`), so a client sees the aiming player lean and step instead of
  standing level and sliding.

## Found on the way

`update_entities`, `update_room_objects` and `player_reticle_enemy` walked
`g_EnemiesList` by counting ACTIVE entities against `g_enemy_count`. When the
count outruns the active slots (RAID does it routinely: a reserved co-op zombie
sits inactive inside it) the walk ran off the array into `g_enemy_count` and
`ENTITY`, and a phantom zombie's init wrote through `ENTITY` - the "write access
violation, ENTITY was 0x9C3358" crash on death. All three loops are bounded to
the array now; see the note in `update_entities`.
