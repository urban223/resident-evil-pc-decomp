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
| L2 / Q | raise the gun, shoulder camera |
| R2 | fire |
| left stick | walk with the gun up (step animation), strafe sideways - it does not turn her |
| right stick | aim: yaw turns her, pitch raises and lowers the gun, the arms and torso lean with it, the camera follows |

With no weapon equipped the camera still swings in, but stays farther back.

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
  through the crosshair (the camera is off the gun line, so a ray from the gun
  would miss what the crosshair covers), tested against each enemy's SCA
  volumes. The ray's height picks the hit bucket the three fixed poses used to
  (high 0x80, middle 0x40, low 0x20). An unfocused shot spreads by up to the
  drawn reticle; holding still closes it over 1.5 s.
- **The body.** `RaidShoulderCam_PoseArms` bends the torso by half the pitch
  and gives the arms the rest; `RaidShoulderCam_Walk` runs the walk cycle on
  the legs under the aim stance, before collision, and turns the hips toward a
  strafe.
- **Crosshair.** `RaidArena.cpp`, `RaDrawCrosshair`: where the gun line meets
  the screen, not the screen centre; it fades in with the camera swing.
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
