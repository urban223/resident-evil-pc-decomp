// RaidShoulderCam.cpp - hold L2 in RAID: aim, with the camera over the shoulder.
//
// CUSTOM. The way Resident Evil 2 (2019) does it: holding L2 raises the gun
// and the camera swings in behind the right shoulder; letting go lowers the
// gun and the camera swings back to the room's own.
//
// RAID ONLY, and for a reason that is not a choice: an ordinary room is a
// pre-rendered picture taken from one fixed eye, and there is nothing to see
// from anywhere else. The RAID arena is real geometry (RaidArena_Draw), which
// is why the editor can fly a camera around it - and this uses the editor's
// trick (EditorCamera.cpp): write the eye into the camera record the room is
// already using and call Room_SetupCamera(). The arena, the models and the
// 3D sound pan all read that one record, so all of them follow.
//
// THE INPUT. Raw PSX L2 (0x01) is a bit no gameplay code tests. In RAID the
// left trigger - or Q on the keyboard - produces it (InputSystem.cpp), and
// PlayerPad_Update adds the aim bit 0x100 alongside it. Every aim test in the
// player state machine reads that one bit, so the stance, turning while aimed,
// firing and lowering the gun on release are all the game's own code.
//
// THE CAMERA follows player 1 (g_players[0]), as the room camera always has
// in co-op - check_camera_switch tests only him. A second player's L2 still
// aims, it just does not move the camera.
#include "../Globals.h"
#include "Types.h"
#include "RaidShoulderCam.h"
#include "CoopPlayer.h"
#include "CoopNet.h"
#include "editor/Editor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>   // rand: an unfocused shot's spread

extern int g_scaled_down_dist;   // 0x00be0de8 - apply_weapon_damage: the shot's weaponAdj

// Where the eye sits, in world units relative to the player's feet and facing.
// Read off the rigs: char11's shoulders are 2190 above the feet and her head
// joint 2232, char10's 2501 / 2511.
//
// The eye is well out to the right of him, which is what keeps his own
// shoulder from hiding the gun arm. It looks almost straight ahead - turned
// only a few degrees in, at a point still to his right - so he stands in the
// left part of the frame with the space he is aiming into on the right, the
// way Resident Evil 2 (2019) frames it.
#define SC_BACK          1900.0f   // behind him
#define SC_RIGHT          800.0f   // the eye, out past the right shoulder
#define SC_TARGET_RIGHT   300.0f   // the look-at point, right of him: he sits left of centre
#define SC_EYE_Y        -2450.0f   // above the shoulder, looking over it
#define SC_AHEAD         4000.0f   // what the eye looks at, in front of him
#define SC_TARGET_Y     -2000.0f   // gun height
#define SC_FOV            240      // focal length in 320-wide space; the
                                   // editor's 207 is wider than this wants
#define SC_BLEND_FRAMES     6      // swing in / out, at 30 frames a second
#define SC_AIM_REACH     6000.0f   // how far down the gun line the crosshair sits
#define SC_CAM_PITCH_SHARE  1.0f   // of the aim pitch the camera swings by

// RE2 (2019)'s aim settings are a distance and a field-of-view multiplier:
// raising the gun brings the camera in and narrows the view. Ours arrives at
// the walking-distance framing and then closes in over a third of a second.
#define SC_BACK_AIM      1300.0f   // the eye's distance behind him, gun fully up
#define SC_FOV_AIM         300     // and the narrower view (focal length, 320-wide)
#define SC_ZOOM_FRAMES      10

// The reticle FOCUS, as RE2 (2019) has it: standing still with the gun up,
// the reticle closes in; moving, turning or firing opens it again. A shot is
// thrown off its line by up to SC_SPREAD at an open reticle and not at all at
// a closed one, and a fully closed reticle's shot does SC_FOCUS_BONUS more
// damage - RE2's own reward for waiting, there as better odds of a critical.
#define SC_FOCUS_FRAMES     45     // to close fully: a second and a half
                                   // (s_focus counts half-frames: up to 2x this)
#define SC_SPREAD         0.030f   // radians at an open reticle: about 1.7 degrees.
                                   // The crosshair's ticks are drawn ON this cone
                                   // (RaDrawCrosshair), so a shot can land anywhere
                                   // inside them and never outside.
#define SC_FOCUS_BONUS      1.5f

static unsigned char s_l2Held[RAID_PLAYERS];
static int           s_pitch;          // free-aim pitch (FREE AIM below): + up, 4096 to the turn

static int         s_active;       // s_saved holds a record we have to restore
static int         s_savedId;      // which camera slot it belongs to
static RDT_Camera  s_saved;        // that slot as the level wrote it
static int         s_blend;        // 0 = the room's camera, SC_BLEND_FRAMES = ours
static int         s_zoom;         // 0 = walking distance, SC_ZOOM_FRAMES = the aim one

void RaidShoulderCam_NoteRaw(unsigned int raw)
{
    const int who = (g_coopPadSource > 0 && g_coopPadSource < RAID_PLAYERS) ? g_coopPadSource : 0;
    s_l2Held[who] = (g_raidMode && (raw & RAID_RAW_L2)) ? 1 : 0;
}

unsigned short RaidShoulderCam_AddAim(unsigned short dpad, unsigned int raw)
{
    if (g_raidMode && (raw & RAID_RAW_L2)) {
        dpad |= RAID_DPAD_AIM;
        // The remap tables turn raw L2 into dpad 0x2000. No PLAYER code reads
        // that bit, but Coop_DriveZombie does - as "turn left" - so a dead
        // player holding L2 spun on the spot. L2 means aim and nothing else.
        dpad &= (unsigned short)~0x2000;
    }
    return dpad;
}

static RDT_Camera* sc_cameras(void)
{
    if (g_RdtPointer == NULL) return NULL;
    if ((unsigned int)g_roomCameraId >= (unsigned int)g_RdtPointer->cameras_count) return NULL;
    return (RDT_Camera*)((char*)g_RdtPointer + sizeof(RDT));
}

static int sc_wanted(void)
{
    const PlayerEntity* p = &g_players[0];
    if (!s_l2Held[0]) return 0;
    if (g_coopActive && !Coop_IsAuthority()) return 0;   // a client runs no player
    if (Coop_IsZombie(0) || p->health <= 0) return 0;
    // The same two tests game_loop uses to take the pad away from the player
    // (a message is up, or he is on a ladder / in a scripted zone): the gun
    // cannot come up then, so neither should the camera.
    if ((p->zoneFlags & 0x20) != 0 || (g_message_flags & 0x100) == 0) return 0;
    return 1;
}

static int sc_lerp(int a, float b, float t)
{
    return (int)((float)a + (b - (float)a) * t);
}

void RaidShoulderCam_Update(void)
{
    RDT_Camera* cams = sc_cameras();

    // Out of RAID, no room, or the editor owns the record: forget the saved
    // copy without writing it back. Leaving RAID drops the room, and the
    // editor's own restore re-applies the whole level.
    if (!g_raidMode || cams == NULL || Editor_IsOpen()) {
        s_active = 0;
        s_blend = 0;
        return;
    }

    const int want = sc_wanted();
    if (!s_active && !want) return;

    if (!s_active) {
        s_saved   = cams[g_roomCameraId];
        s_savedId = g_roomCameraId;
        s_active  = 1;
        s_blend   = 0;
    } else if (s_savedId != g_roomCameraId) {
        // The zones switched camera under us. Put back the slot we were
        // writing into and take a copy of the new one; cut_set has already
        // set the new one up from the untouched record.
        cams[s_savedId] = s_saved;
        s_saved   = cams[g_roomCameraId];
        s_savedId = g_roomCameraId;
    }

    s_blend += want ? 1 : -1;
    if (s_blend > SC_BLEND_FRAMES) s_blend = SC_BLEND_FRAMES;
    // Only with a weapon in hand: without one L2 raises nothing, and the
    // close aim framing just puts the camera in his back.
    const int armed = g_players[0].equippedWeaponId != 0;
    s_zoom += (want && armed) ? 1 : -2;
    if (s_zoom > SC_ZOOM_FRAMES) s_zoom = SC_ZOOM_FRAMES;
    if (s_zoom < 0) s_zoom = 0;
    float zt = (float)s_zoom / (float)SC_ZOOM_FRAMES;
    zt = zt * zt * (3.0f - 2.0f * zt);
    const float back = SC_BACK + (SC_BACK_AIM - SC_BACK) * zt;
    const float fov  = (float)SC_FOV + (float)(SC_FOV_AIM - SC_FOV) * zt;

    RDT_Camera* C = &cams[g_roomCameraId];
    if (s_blend <= 0) {
        *C = s_saved;
        Room_SetupCamera();
        s_active = 0;
        s_blend  = 0;
        return;
    }

    // Facing: Add_speedXZ moves a player along RotMatrixY(angle) * (v, 0, 0),
    // i.e. (cos a, 0, -sin a), 4096 to the turn. Right is the perpendicular on
    // the floor, and it is the SCREEN's right: RaBuildView (RaidArena.cpp)
    // derives r = (dz, 0, -dx) / |d| from the look direction, which for this
    // facing is exactly (-sin a, 0, -cos a). Y is negative upwards.
    const PlayerEntity* p = &g_players[0];
    const float a  = (float)(p->directionAngle & 0xFFF) * (6.2831853f / 4096.0f);
    const float fx = cosf(a), fz = -sinf(a);
    const float rx = -sinf(a), rz = -cosf(a);
    const float px = (float)p->scaMatrixData.localMatrix.t[0];
    const float py = (float)p->scaMatrixData.localMatrix.t[1];
    const float pz = (float)p->scaMatrixData.localMatrix.t[2];

    // The camera follows the pitch: eye and look-at point swing together
    // about a pivot at the eye's height over his feet, in the plane of his
    // facing - so aiming up the camera drops behind him and tips up, aiming
    // down it rises and tips down, and he stays where he is in the frame.
    // Positions in that plane are (along the facing, up), up = -Y.
    //
    // Only part of the pitch, though. Swung by all of it the camera turns
    // exactly as much as the body does, so on screen nothing moves but the
    // room - the arms, the torso and the crosshair all sit still. With most
    // of it, the view follows the aim and the aim still visibly rises in it.
    const float pt = (float)s_pitch * (6.2831853f / 4096.0f) * SC_CAM_PITCH_SHARE;
    const float cp = cosf(pt), sp = sinf(pt);
    float eA = -back,     eU = 0.0f;                        // eye, from the pivot
    float tA =  SC_AHEAD, tU = -(SC_TARGET_Y - SC_EYE_Y);   // look-at point
    {
        const float a1 = eA * cp - eU * sp, u1 = eA * sp + eU * cp;
        const float a2 = tA * cp - tU * sp, u2 = tA * sp + tU * cp;
        eA = a1; eU = u1; tA = a2; tU = u2;
    }

    const float ex = px + fx * eA + rx * SC_RIGHT;
    const float ez = pz + fz * eA + rz * SC_RIGHT;
    const float tx = px + fx * tA + rx * SC_TARGET_RIGHT;
    const float tz = pz + fz * tA + rz * SC_TARGET_RIGHT;

    // Smoothstep, so the swing eases out of the room camera and into ours.
    float t = (float)s_blend / (float)SC_BLEND_FRAMES;
    t = t * t * (3.0f - 2.0f * t);

    C->cam_from_x = sc_lerp(s_saved.cam_from_x, ex, t);
    C->cam_from_y = sc_lerp(s_saved.cam_from_y, py + SC_EYE_Y - eU, t);
    C->cam_from_z = sc_lerp(s_saved.cam_from_z, ez, t);
    C->cam_to_x   = sc_lerp(s_saved.cam_to_x, tx, t);
    C->cam_to_y   = sc_lerp(s_saved.cam_to_y, py + SC_EYE_Y - tU, t);
    C->cam_to_z   = sc_lerp(s_saved.cam_to_z, tz, t);
    C->roll       = sc_lerp(s_saved.roll, 0.0f, t);
    C->fov        = sc_lerp(s_saved.fov, fov, t);
    Room_SetupCamera();
}

float RaidShoulderCam_Amount(void)
{
    if (!s_active || s_blend <= 0) return 0.0f;
    const float t = (float)s_blend / (float)SC_BLEND_FRAMES;
    return t * t * (3.0f - 2.0f * t);
}

// The gun line, from the pose rather than from the stick: from between his
// shoulders to between his hands. The player rig's arms are joints 9-11 and
// 12-14 (shoulder, elbow, hand - see the rig table in CoopPlayer.cpp), and
// both hands are on the gun when it is up, so the midpoints give the barrel's
// direction without knowing which side is which. It follows the pitch below
// because the pitch turns the arms.
//
// The world matrices are filled by the draw pass, so this is last frame's
// pose - one frame behind, which neither a crosshair nor a shot shows.
static int sc_gun_ray(float o[3], float d[3], float* raised)
{
    const PlayerEntity* p = &g_players[0];
    const JointStruct* j = p->jointsStructs;
    if (j == 0 || p->jointCount != 15) return 0;
    if (j[9].world.t[1] == 0 || j[11].world.t[1] == 0) return 0;  // not drawn yet

    float sh[3], hd[3];
    for (int k = 0; k < 3; k++) {
        sh[k] = (j[9].world.t[k]  + j[12].world.t[k]) * 0.5f;
        hd[k] = (j[11].world.t[k] + j[14].world.t[k]) * 0.5f;
    }
    float dx = hd[0] - sh[0], dy = hd[1] - sh[1], dz = hd[2] - sh[2];
    const float L = sqrtf(dx * dx + dy * dy + dz * dz);
    if (L < 100.0f) return 0;
    dx /= L; dy /= L; dz /= L;

    // How far the arms are up: 0 hanging at his sides, 1 level. Aiming up or
    // down tilts them, which still reads as raised. This is ALL the arm bones
    // are used for.
    float r = (sqrtf(dx * dx + dz * dz) - 0.5f) / 0.35f;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;

    // The DIRECTION is not the bones'. The arm is short and the aim pose
    // holds the hands above the shoulders, so shoulders-to-hands read about
    // 18 degrees high at zero pitch (logged: d.y = -0.31) - close up it still
    // clipped the zombie, from further away every shot went over its head.
    // So the line is the one the game itself shoots along - his facing, the
    // same yaw weapon_hit_detect_gun uses - tilted by our pitch, from his
    // hands.
    const PlayerEntity* pl = &g_players[0];
    const float a  = (float)(pl->directionAngle & 0xFFF) * (6.2831853f / 4096.0f);
    const float pt = (float)s_pitch * (6.2831853f / 4096.0f);
    const float cp = cosf(pt);

    o[0] = hd[0]; o[1] = hd[1]; o[2] = hd[2];
    d[0] =  cosf(a) * cp;     // facing (cos a, 0, -sin a), see the camera
    d[1] = -sinf(pt);         // Y is negative upwards
    d[2] = -sinf(a) * cp;
    *raised = r;
    return 1;
}

int RaidShoulderCam_AimPoint(float* x, float* y, float* z, float* raised)
{
    float o[3], d[3];
    if (!sc_gun_ray(o, d, raised)) return 0;
    *x = o[0] + d[0] * SC_AIM_REACH;
    *y = o[1] + d[1] * SC_AIM_REACH;
    *z = o[2] + d[2] * SC_AIM_REACH;
    return 1;
}

// ---------------------------------------------------------------------------
// FREE AIM
//
// The game aims for you. Behaviour 0x12 picks the nearest enemy
// (player_reticle_enemy) and turns the player onto it; the run/cancel button
// in behaviour 0x13 cycles that lock (behaviour 0x1A); up and down pick one
// of three fixed poses, each a separate animation, and switching between them
// plays a quick-fire (0x15/0x16) on the way. A hit is a flat 2D strip in
// front of the player (weapon_hit_detect_gun), with height decided only by
// which of the three poses is up.
//
// Over the shoulder all of that is wrong, so while it is held:
//   - the target pick is off (g_aimReticleEnabled, around his update only),
//     any lock already held is dropped, and the lock-cycle press is eaten;
//   - up and down never reach the state machine; the right stick moves a
//     continuous pitch instead, the pose stays the level one (0x40), and
//     the pitch turns both arms at the shoulder after the animation has
//     posed them - the gun is on joint 14, a child of the arm, so it follows;
//   - a shot is a ray down the same gun line the crosshair is drawn on,
//     tested against each enemy's own collision cylinders (RaidShoulderCam_
//     HitTest, called from apply_weapon_damage in place of the strip test).
//
// Player 1 only, because the camera is his. The pitch is not sent to a
// co-op client, which sees the level pose.
// ---------------------------------------------------------------------------
#define SC_PITCH_UP_MAX    0x2AA    // 60 degrees, 4096 to the turn
#define SC_PITCH_DOWN_MAX  0x1C7    // 40 degrees
#define SC_PITCH_RETURN    0x38     // per frame, easing back when let go
#define SC_STICK_DEADZONE  7849     // XInput's own right-stick deadzone
#define SC_STICK_RATE      0x40     // per frame at full deflection: ~175 deg/s
#define SC_YAW_RATE        0x70     // turning, per frame at full deflection: ~295 deg/s

static int           s_stickY;          // player 1's right stick, up positive
static int           s_stickX;          // ...and right positive
static int           s_walkDir;         // left stick: +1 forward, -1 back, 0 neither
static int           s_walkSide;        // left stick: +1 right, -1 left, 0 neither
static int           s_hipYaw;          // the hips turned toward the step, 4096 to the turn
static int           s_walkShown;       // the walk direction the legs are posed for
static int           s_walkFrame;       // frame of the walk cycle on the legs
static int           s_walkHold;        // ticks left on that frame
static int           s_preX, s_preZ;    // where he stood before his update
static int           s_legBlend;        // 0 = the aim pose's legs, SC_LEG_IN = the walk's
static int           s_focus;           // 0 open .. SC_FOCUS_FRAMES closed
static unsigned char s_prevBehavior;    // to see a shot begin
static float         s_shotFocus;       // the focus the current shot was fired at
static float         s_spreadYaw, s_spreadPitch;   // that shot's error, radians

void RaidShoulderCam_NoteStick(int x, int y)
{
    s_stickX = x;
    s_stickY = y;
}
static int           s_pitchPosed;      // the arms carry a pitch from us
static unsigned char s_savedReticle;
static int           s_reticleSaved;

int RaidShoulderCam_FreeAim(void)
{
    return g_pCurPlayer == &g_players[0] && g_raidMode && sc_wanted();
}

void RaidShoulderCam_BeforePlayer(int i)
{
    if (i != 0) return;

    s_preX = g_playerEntity.scaMatrixData.localMatrix.t[0];
    s_preZ = g_playerEntity.scaMatrixData.localMatrix.t[2];

    if (!RaidShoulderCam_FreeAim()) {
        s_walkDir = 0;
        s_walkSide = 0;
        s_focus = 0;
        s_prevBehavior = g_playerEntity.action_behavior;
        // Let the arms come back down to the level pose rather than snap.
        if (s_pitch > 0) { s_pitch -= SC_PITCH_RETURN; if (s_pitch < 0) s_pitch = 0; }
        if (s_pitch < 0) { s_pitch += SC_PITCH_RETURN; if (s_pitch > 0) s_pitch = 0; }
        return;
    }

    // Dpad 0x01 / 0x10 are up, 0x04 / 0x20 down (PlayerPad_Update's remap;
    // gun_hold_input turns 0x10 into the high pose and 0x20 into the low).
    // They are the LEFT stick and the D-pad, and over the shoulder the left
    // stick belongs to walking, so they do not pitch the gun - they are only
    // kept away from the state machine below, which would otherwise switch
    // poses through a quick-fire. The pitch is the right stick's alone.
    const unsigned short up   = 0x01 | 0x10;
    const unsigned short down = 0x04 | 0x20;

    // The right stick's X turns him, analogue, the way it turns the view in
    // Resident Evil 2 (2019): the camera is built from his facing, so turning
    // him IS turning the view. Right adds to directionAngle, as the D-pad's
    // right does in the aim behaviours.
    {
        int x = s_stickX;
        const int sign = x < 0 ? -1 : 1;
        x = x * sign;
        if (x > SC_STICK_DEADZONE) {
            const float k = (float)(x - SC_STICK_DEADZONE) / (float)(32767 - SC_STICK_DEADZONE);
            g_playerEntity.directionAngle = (short)(g_playerEntity.directionAngle
                + sign * (int)((0.5f * k + 0.5f * k * k) * (float)SC_YAW_RATE + 0.5f));
        }
    }

    // The right stick, analogue: past the deadzone, the further it is pushed
    // the faster the gun climbs, so a small push is a fine adjustment. Pushed
    // up is aim up (not inverted).
    {
        int y = s_stickY;
        const int sign = y < 0 ? -1 : 1;
        y = y * sign;
        if (y > SC_STICK_DEADZONE) {
            const float k = (float)(y - SC_STICK_DEADZONE) / (float)(32767 - SC_STICK_DEADZONE);
            s_pitch += sign * (int)((0.5f * k + 0.5f * k * k) * (float)SC_STICK_RATE + 0.5f);
        }
    }
    if (s_pitch >  SC_PITCH_UP_MAX)   s_pitch =  SC_PITCH_UP_MAX;
    if (s_pitch < -SC_PITCH_DOWN_MAX) s_pitch = -SC_PITCH_DOWN_MAX;

    // Focus. A shot begins the frame the behaviour becomes 0x14 (the fire
    // states); that is when its focus and its error are fixed - the damage
    // lands a few frames later, and by then the reticle has already opened.
    {
        const unsigned char ab = g_playerEntity.action_behavior;
        if (ab == 0x14 && s_prevBehavior != 0x14) {
            s_shotFocus = (float)s_focus / (float)(SC_FOCUS_FRAMES * 2);
            const float open = SC_SPREAD * (1.0f - s_shotFocus);
            // A point in the disc, not the square, so the corners are not
            // likelier than the edges.
            float ux, uy;
            do {
                ux = (float)(rand() % 2001 - 1000) / 1000.0f;
                uy = (float)(rand() % 2001 - 1000) / 1000.0f;
            } while (ux * ux + uy * uy > 1.0f);
            s_spreadYaw   = ux * open;
            s_spreadPitch = uy * open;
        }
        s_prevBehavior = ab;

        const int dz = SC_STICK_DEADZONE;
        const int turning  = (s_stickX > dz || s_stickX < -dz)
                          || (g_PlayerDpadHeld & (0x02 | 0x08)) != 0;   // (a strafe: moving)
        const int tilting  = (s_stickY > dz || s_stickY < -dz);
        if (s_walkDir != 0 || turning || ab == 0x14) {
            s_focus = 0;                       // moving, turning or firing
        } else if (ab == 0x13) {               // gun up and holding
            s_focus += tilting ? 1 : 2;        // fine adjustment only slows it
            if (s_focus > SC_FOCUS_FRAMES * 2) s_focus = SC_FOCUS_FRAMES * 2;
        }
    }

    // ...but they are what walks him: sample them before they go. Left and
    // right too - over the shoulder the left stick STRAFES, the way it does
    // in Resident Evil 2 (2019); turning is the right stick's. Kept from the
    // state machine for the same reason as up and down: the aim behaviours
    // would turn him on them (gun_aim / gun_hold_input).
    const unsigned short side = 0x02 | 0x08;          // right, left
    s_walkDir  = (g_PlayerDpadHeld & up) ? 1 : ((g_PlayerDpadHeld & down) ? -1 : 0);
    s_walkSide = (g_PlayerDpadHeld & 0x02) ? 1 : ((g_PlayerDpadHeld & 0x08) ? -1 : 0);
    g_PlayerDpadHeld    &= (unsigned short)~(up | down | side);
    g_PlayerDpadPressed &= (unsigned short)~(up | down | side);
    g_PlayerPadHeld     &= ~0x40u;           // the lock-cycle press (0x1A)

    g_playerEntity.weaponAimFlags = 0x40;    // the level pose; the pitch is ours
    g_playerEntity.weaponAimState = 0;       // no lock to turn onto

    s_savedReticle = g_aimReticleEnabled;
    s_reticleSaved = 1;
    g_aimReticleEnabled = 0;
}

void RaidShoulderCam_AfterPlayer(int i)
{
    if (i != 0 || !s_reticleSaved) return;
    g_aimReticleEnabled = s_savedReticle;
    s_reticleSaved = 0;
}

// ---------------------------------------------------------------------------
// WALKING WITH THE GUN UP
//
// The game cannot: every aim behaviour stands still. So the arms stay the
// aim behaviour's (Joint_move poses the whole skeleton from the weapon file,
// and the pitch above turns the arms), and afterwards the hips and both legs
// (joints 2-8) are re-posed from the player's own walk cycle, on a frame
// counter of ours. Joint_move cannot be used for that: on the player its
// frame and timing fields ARE the aim pose's.
//
// Which cycle, read out of the walk behaviours:
//   forward  player_ctrl_behavior_walk: weapon file (jointMoveData0/1), anim 2
//   back     player_ctrl_behavior_run (misnamed): body file (animHeader/
//            animBase), anim 3 - its own animation, not anim 2 reversed
// emdScratchPtr1/2 are no use: they are the last ENEMY file loaded.
//
// Moving him has one trap. Every gun behaviour ends with
// EntityUpdateWeaponJoint(0), which is a FOOT LOCK: it pins the aim pose's
// foot in place by moving localMatrix.t, so any step taken inside the aim
// behaviour is undone in the same frame. The step is therefore taken after
// the state machine (RaidShoulderCam_Walk, from update_player_anim), from the
// position he had BEFORE his update, and before wall collision runs - so the
// walls still stop him, through the same check_room_collision as always.
// ---------------------------------------------------------------------------

// One frame of an animation: its rotations (3 shorts per joint, after the
// root's six) and how many ticks it holds. The same arithmetic as Joint_move.
static const short* sc_anim_frame(unsigned int hdr, unsigned int base, int id,
                                  int frame, int* count, int* hold)
{
    if (hdr == 0 || base == 0) return 0;
    const unsigned short* slot = (const unsigned short*)(base + (unsigned int)id * 4);
    const int n = slot[0];
    if (n <= 0) return 0;
    if (count) *count = n;
    frame %= n;
    const unsigned short* entry = (const unsigned short*)(base + (slot[1] & ~3u) + (unsigned int)frame * 4);
    if (hold) *hold = entry[1] & 0xFF;
    const int strideShorts = *(const short*)(hdr + 6) / 2;
    const short* data = (const short*)(hdr + (*(const short*)(hdr + 2) & ~3)
                                       + (unsigned int)entry[0] * strideShorts * 2);
    return data + 6;
}

static const short* sc_walk_frame(int dir, int frame, int* count, int* hold)
{
    const PlayerEntity* p = &g_players[0];
    return dir > 0
        ? sc_anim_frame(p->jointMoveData0, p->jointMoveData1, 2, frame, count, hold)
        : sc_anim_frame(p->animHeader,     p->animBase,       3, frame, count, hold);
}

// The forward walk's speed, frame by frame - player_ctrl_behavior_walk's own
// table, so the feet do not skate: it slows on each footfall.
static int sc_walk_speed(int frame)
{
    static const unsigned char kWalkSpeedTable[8] = {
        0x15, 0x17, 0x0D, 0x0E,   // Chris
        0x14, 0x16, 0x0F, 0x0F,   // Jill
    };
    const unsigned int t = (unsigned int)(g_players[0].id & 1) * 4;
    const unsigned char f = (unsigned char)frame;
    int s = 0x5d;
    if ((unsigned char)(f - kWalkSpeedTable[t]) < 7) s = 0x5d - kWalkSpeedTable[t + 2];
    if ((unsigned char)(f - 7) < 7) s -= kWalkSpeedTable[t + 2];
    if ((unsigned char)(f - kWalkSpeedTable[t + 1]) < 3) s -= kWalkSpeedTable[t + 3];
    if ((unsigned char)(f - 9) < 3) s -= kWalkSpeedTable[t + 3];
    return s;
}

// Starting and stopping are blended, not cut. A cut was a visible jerk on
// every stop, from two things at once: the legs jumped from mid-stride to the
// aim stance in one frame, and on that same frame the foot lock (see above)
// found the foot where the STRIDE had left it and dragged the whole body over
// to pin it. So the legs ease between the two poses, and while they do the
// foot lock is cancelled the same way the step cancels it - he simply stays
// where he stood.
#define SC_LEG_IN    3       // frames to ease into the stride
#define SC_LEG_OUT   6       // and out of it, onto the stance
#define SC_LEG_FULL  (SC_LEG_IN * SC_LEG_OUT)   // common scale for both rates

static void sc_settle(void)
{
    if (s_legBlend <= 0) { s_walkShown = 0; return; }
    if (g_playerEntity.animationId == 1) {               // in control: hold still
        g_playerEntity.scaMatrixData.localMatrix.t[0] = s_preX;
        g_playerEntity.scaMatrixData.localMatrix.t[2] = s_preZ;
    }
    s_legBlend -= SC_LEG_FULL / SC_LEG_OUT;
    if (s_legBlend <= 0) { s_legBlend = 0; s_walkShown = 0; }
}

// Eight ways to step, from the two stick axes. The direction is an offset
// from his facing (Add_speedXZ's argument: + is to his right), and the cycle
// on the legs is the forward walk for anything not going backwards and the
// back walk otherwise. There is no sidestep animation in the game, so a
// strafe is the walk cycle with the HIPS turned toward the step - the legs
// carry him where he is going while the torso and the gun stay on the aim.
// How far the hips turn is capped: a full quarter turn at the waist reads as
// broken rather than as a strafe.
#define SC_HIP_MAX     0x2E0     // about 65 degrees
#define SC_HIP_RATE    0x40      // per frame, easing toward it
#define SC_STRAFE_SPEED 0.8f     // of the walk's speed, stepping sideways

static int sc_step_offset(int f, int s)
{
    static const short kOffset[3][3] = {          // [f+1][s+1]
        { 0xA00, 0x800, 0x600 },                  // back:    left, -, right
        { 0xC00, 0,     0x400 },                  // neither: left, -, right
        { 0xE00, 0,     0x200 },                  // forward: left, -, right
    };
    return kOffset[f + 1][s + 1];
}

static int sc_wrap(int a)                          // to -0x800..0x7FF
{
    a &= 0xFFF;
    return a >= 0x800 ? a - 0x1000 : a;
}

static void sc_ease_hips(int target)
{
    if (s_hipYaw < target) { s_hipYaw += SC_HIP_RATE; if (s_hipYaw > target) s_hipYaw = target; }
    if (s_hipYaw > target) { s_hipYaw -= SC_HIP_RATE; if (s_hipYaw < target) s_hipYaw = target; }
}

void RaidShoulderCam_Walk(void)
{
    if (g_pCurPlayer != &g_players[0]) return;
    if ((s_walkDir == 0 && s_walkSide == 0) || !RaidShoulderCam_FreeAim()
        || g_playerEntity.animationId != 1) {           // 1 = player in control
        // Stopping: s_walkShown and s_walkFrame are left as they were, so the
        // legs ease out of the stride they actually stopped in.
        sc_ease_hips(0);
        sc_settle();
        return;
    }

    const int offset = sc_step_offset(s_walkDir, s_walkSide);
    const int cycle  = (s_walkDir < 0) ? -1 : 1;
    // The hips face along the step for the forward cycle and AWAY from it
    // for the back one (a back walk moves toward the hips' rear).
    int hips = sc_wrap(cycle > 0 ? offset : offset - 0x800);
    if (hips >  SC_HIP_MAX) hips =  SC_HIP_MAX;
    if (hips < -SC_HIP_MAX) hips = -SC_HIP_MAX;
    sc_ease_hips(hips);

    if (s_walkShown != cycle) {          // starting, or turning round
        s_walkShown = cycle;
        s_walkFrame = 0;
        s_walkHold  = 0;
    }
    s_legBlend += SC_LEG_FULL / SC_LEG_IN;
    if (s_legBlend > SC_LEG_FULL) s_legBlend = SC_LEG_FULL;

    int count = 0, hold = 1;
    if (sc_walk_frame(cycle, s_walkFrame, &count, &hold) == 0) {
        s_walkShown = 0;
        s_legBlend  = 0;
        return;
    }

    // Undo the foot lock, then step: back 64 a frame (the back-walk's own
    // speed), forward by the walk's table.
    g_playerEntity.scaMatrixData.localMatrix.t[0] = s_preX;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = s_preZ;
    int speed = cycle > 0 ? sc_walk_speed(s_walkFrame) : 0x40;
    if (s_walkSide != 0 && s_walkDir == 0) speed = (int)(speed * SC_STRAFE_SPEED);
    g_playerEntity.move_speed_current = (unsigned short)speed;
    Add_speedXZ(offset);

    // Footsteps on the walks' own frames (player_ctrl_behavior_walk / _run).
    if (s_walkHold == 0 && (s_walkFrame == 0x08 || s_walkFrame == 0x16)) {
        PlayEntitySnd(0);
    }

    // Advance, honouring each frame's hold the way Joint_move does.
    if (++s_walkHold >= (hold > 0 ? hold : 1)) {
        s_walkHold = 0;
        if (++s_walkFrame >= count) s_walkFrame = 0;
    }
}

// The legs, from the walk cycle. Only the transforms are written - not
// joint->rotation, which Joint_move blends FROM, so the aim pose's next blend
// does not start out of a walk pose. Rewritten every frame, because on a frame
// where Joint_move holds (timing gate) nothing would put them back.
static void sc_pose_legs(void)
{
    if (s_walkShown == 0 || s_legBlend <= 0) return;
    PlayerEntity* p = &g_players[0];
    JointStruct* j = p->jointsStructs;
    if (j == 0 || p->jointCount != 15) return;

    const short* rot = sc_walk_frame(s_walkShown, s_walkFrame, 0, 0);
    if (rot == 0) return;

    // Joint angles, eased from the aim pose's (joint->rotation, Joint_move's)
    // to the stride's along the shorter way round each axis.
    const int w = s_legBlend, W = SC_LEG_FULL;
    for (int k = 2; k <= 8; k++) {
        if (j[k].flags & 0x10) continue;
        const short* aim = &j[k].rotation.x;
        short v[3];
        for (int a = 0; a < 3; a++) {
            int d = ((int)rot[k * 3 + a] - (int)aim[a]) & 0xFFF;
            if (d >= 0x800) d -= 0x1000;
            v[a] = (short)(aim[a] + d * w / W);
        }
        SVECTOR sv;
        sv.x = v[0]; sv.y = v[1]; sv.z = v[2]; sv.pad = 0;
        RotMatrix(&sv, &j[k].transform);
    }
}

// Lean the body into the pitch, the way Resident Evil 2 (2019) does it: the
// upper body bends at the waist and the arms take the rest at the shoulders,
// so the gun, the arms, the torso and the head all follow the aim and the
// legs stay where they are. Called for each player before his world matrices
// are composed.
//
// The joints' transforms are rebuilt from joint->rotation first (what
// Joint_move left there - the blended value on a blend frame), so this is
// idempotent: on a frame where Joint_move holds without rewriting them, the
// pitch is not applied on top of last frame's.
//
// The player's root (joint 0) IS the torso, and the hips (2) hang off it. So
// the torso is turned about the body's own side axis at its root - the waist
// - and the hips get exactly the inverse, which leaves the legs' world
// transform untouched:
//     T0' = [P R0 | t0]        T2' = T0'^-1 T0 T2
// Then each shoulder (9, 12) takes the arms' share, about the same body axis
// expressed in the torso's new frame. Every turn is built in the body frame
// (forward +X, up -Y, the shoulders along Z) and carried into each joint's
// own frame through the rotations above it, so no joint's local axes have to
// be guessed.
//
// The sign is the one that was SEEN, twice, not the one reasoned out. In
// W12.EMW the high aim pose holds the arms at -93 degrees about Z, level at
// -85 and low at -46, which reads as "raising is a negative turn about Z" -
// and built that way (in the textbook convention) the arms went DOWN as the
// aim went up, in two builds running. RotMatrix's own sense of a positive
// angle is evidently the opposite of the textbook's, so the matrix below
// is the textbook +up turn.
#define SC_TORSO_SHARE  0.5f      // of the pitch, bent at the waist; the arms take the rest

typedef float sc_m3[3][3];

static void sc_from(sc_m3 o, const MATRIX* m)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o[r][c] = (float)m->m[r][c] / 4096.0f;
}

static void sc_to(MATRIX* m, sc_m3 a)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            float v = a[r][c] * 4096.0f;
            if (v >  32767.0f) v =  32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            m->m[r][c] = (short)(v < 0.0f ? v - 0.5f : v + 0.5f);
        }
}

static void sc_mul(sc_m3 o, sc_m3 a, sc_m3 b)       // o = a b
{
    sc_m3 t;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            t[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o[r][c] = t[r][c];
}

static void sc_tr(sc_m3 o, sc_m3 a)                  // o = a^T
{
    sc_m3 t;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            t[r][c] = a[c][r];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o[r][c] = t[r][c];
}

// Raising by `up` (radians) in the body frame. See the note above for why
// this is +up and not -up.
static void sc_raise(sc_m3 o, float up)
{
    const float c = cosf(up), sn = sinf(up);
    o[0][0] = c;  o[0][1] = -sn; o[0][2] = 0.0f;
    o[1][0] = sn; o[1][1] = c;   o[1][2] = 0.0f;
    o[2][0] = 0;  o[2][1] = 0;   o[2][2] = 1.0f;
}

void RaidShoulderCam_PoseArms(int i)
{
    if (i != 0) return;

    PlayerEntity* p = &g_players[0];
    JointStruct* j = p->jointsStructs;
    if (j == 0 || p->jointCount != 15) return;

    const int touch = (s_pitch != 0 || s_hipYaw != 0 || s_pitchPosed);

    // The joints this touches, back to what Joint_move posed. The hips'
    // translation too: it is the rest offset (ResetJointTransforms), which
    // nothing but this rewrites.
    static const int kJoint[4] = { 0, 2, 9, 12 };
    if (touch) {
        for (int k = 0; k < 4; k++) {
            JointStruct* J = &j[kJoint[k]];
            if (J->flags & 0x10) continue;          // not posed by Joint_move
            RotMatrix(&J->rotation, &J->transform); // keeps transform.t
        }
        if (p->animHeader != 0) {
            const short* rest = (const short*)(p->animHeader + 8);
            j[2].transform.t[0] = rest[2 * 3 + 0];
            j[2].transform.t[1] = rest[2 * 3 + 1];
            j[2].transform.t[2] = rest[2 * 3 + 2];
        }
    }

    sc_pose_legs();     // walking replaces the hips and legs (2-8)

    s_pitchPosed = (s_pitch != 0 || s_hipYaw != 0);
    if (!s_pitchPosed) return;

    const float pitch = (float)s_pitch * (6.2831853f / 4096.0f);
    sc_m3 R0, R2, P, Pt, R0n, R0nt, M, Q, Y;

    // Torso: R0' = P R0, about the root.
    sc_from(R0, &j[0].transform);
    sc_raise(P, pitch * SC_TORSO_SHARE);
    sc_mul(R0n, P, R0);
    sc_to(&j[0].transform, R0n);

    // Hips: kept where they were in the world, and then turned by the strafe
    // about the body's vertical - T2' = T0'^-1 Y T0 T2, so
    //   R2' = R0'^T Y R0 R2,   t2' = R0'^T Y R0 t2.
    // Y is RotMatrixY's own form, the one directionAngle turns by, so a
    // positive yaw turns the hips the same way a positive facing does.
    {
        const float a = (float)s_hipYaw * (6.2831853f / 4096.0f);
        const float c = cosf(a), sn = sinf(a);
        Y[0][0] = c;   Y[0][1] = 0; Y[0][2] = sn;
        Y[1][0] = 0;   Y[1][1] = 1; Y[1][2] = 0;
        Y[2][0] = -sn; Y[2][1] = 0; Y[2][2] = c;
    }
    sc_tr(R0nt, R0n);
    sc_mul(M, Y, R0);
    sc_mul(M, R0nt, M);
    sc_from(R2, &j[2].transform);
    sc_mul(Q, M, R2);
    sc_to(&j[2].transform, Q);
    {
        const float x = (float)j[2].transform.t[0];
        const float y = (float)j[2].transform.t[1];
        const float z = (float)j[2].transform.t[2];
        j[2].transform.t[0] = (int)(M[0][0] * x + M[0][1] * y + M[0][2] * z);
        j[2].transform.t[1] = (int)(M[1][0] * x + M[1][1] * y + M[1][2] * z);
        j[2].transform.t[2] = (int)(M[2][0] * x + M[2][1] * y + M[2][2] * z);
    }

    if (s_pitch == 0) return;

    // Arms: the rest of the pitch, about the body's side axis carried into
    // the torso's new frame:  R' = R0'^T Pa R0' R.
    sc_raise(P, pitch * (1.0f - SC_TORSO_SHARE));
    sc_mul(M, P, R0n);
    sc_mul(M, R0nt, M);
    static const int kShoulder[2] = { 9, 12 };
    for (int k = 0; k < 2; k++) {
        JointStruct* J = &j[kShoulder[k]];
        if (J->flags & 0x10) continue;
        sc_m3 R;
        sc_from(R, &J->transform);
        sc_mul(R, M, R);
        sc_to(&J->transform, R);
    }
    (void)Pt;
}

// A shot down the gun line against one enemy. Same contract as the
// weapon_hit_detect_* functions: return 1 only when this enemy is nearer than
// the best so far, and leave its distance in g_playerDisplacement - the
// zombie's instant-kill rules read it (enemy_hit_reaction_zombie).
//
// The target is each of the enemy's collision cylinders (Sca_info: six
// shorts per volume, [4] half-height, [5] radius; pSca_hit_data: the volume's
// offset from the entity, already turned by its facing, three shorts per
// volume; the last volume has a negative first short). Where the ray meets it
// picks the hit type the three fixed poses used to: the top third is the
// high pose's 0x80, the bottom third the low pose's 0x20, the rest 0x40.
// apply_weapon_damage turns that into the enemy's damage animation.
unsigned char RaidShoulderCam_HitTest(short range, Entity* e)
{
    (void)range;    // the strip's lateral reach; a ray has none
    float o[3], d[3], raised;
    if (!sc_gun_ray(o, d, &raised)) return 0;
    if (e->Sca_info == 0 || e->pSca_hit_data == 0) return 0;

    // FROM THE CAMERA, through the crosshair. The crosshair is drawn at a
    // point far down the gun line, and the camera sits well off that line
    // (out to the right and behind), so on screen the far point can sit on a
    // NEAR zombie the gun line itself passes beside - logged as a shot 19
    // degrees off a zombie 1700 away, with the crosshair on it. So the shot
    // goes where the player sees it go: along the eye's line through that
    // point. What lies between the eye and the player is not his to hit, so
    // the ray starts where it passes him, and every distance the damage
    // rules read is measured from there.
    float tmin = 0.0f;
    {
        RDT_Camera* cams = sc_cameras();
        if (cams != NULL && s_active) {
            const RDT_Camera* C = &cams[g_roomCameraId];
            const float px = o[0] + d[0] * SC_AIM_REACH;
            const float py = o[1] + d[1] * SC_AIM_REACH;
            const float pz = o[2] + d[2] * SC_AIM_REACH;
            const float ex = (float)C->cam_from_x, ey = (float)C->cam_from_y, ez = (float)C->cam_from_z;
            float vx = px - ex, vy = py - ey, vz = pz - ez;
            const float L = sqrtf(vx * vx + vy * vy + vz * vz);
            if (L > 1.0f) {
                vx /= L; vy /= L; vz /= L;
                tmin = (o[0] - ex) * vx + (o[1] - ey) * vy + (o[2] - ez) * vz;
                if (tmin < 0.0f) tmin = 0.0f;
                o[0] = ex + vx * tmin; o[1] = ey + vy * tmin; o[2] = ez + vz * tmin;
                d[0] = vx; d[1] = vy; d[2] = vz;
            }
        }
    }

    // This shot's error (fixed when it was fired, the same for every enemy
    // it is tested against): a turn of the line by s_spreadYaw about the
    // vertical and a lift by s_spreadPitch.
    {
        const float cy = cosf(s_spreadYaw), sy = sinf(s_spreadYaw);
        const float x = d[0] * cy - d[2] * sy;
        const float z = d[0] * sy + d[2] * cy;
        d[0] = x; d[2] = z;
        d[1] -= s_spreadPitch;              // up is -Y
        const float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        d[0] /= L; d[1] /= L; d[2] /= L;
    }

    const short* vol = (const short*)e->Sca_info;
    const short* off = (const short*)e->pSca_hit_data;
    const float ex = (float)e->scaMatrixData.localMatrix.t[0];
    const float ey = (float)e->scaMatrixData.localMatrix.t[1];
    const float ez = (float)e->scaMatrixData.localMatrix.t[2];

    float best = -1.0f;
    unsigned char bucket = 0x40;
    for (int guard = 0; guard < 8; guard++) {
        const float cx = ex + off[0], cy = ey + off[1], cz = ez + off[2];
        const float half = (float)(unsigned short)vol[4];
        const float rad  = (float)(unsigned short)vol[5];

        // Ray against the vertical cylinder: the circle in XZ first, then
        // the height at the entry point.
        const float fx = o[0] - cx, fz = o[2] - cz;
        const float A = d[0] * d[0] + d[2] * d[2];
        const float B = 2.0f * (fx * d[0] + fz * d[2]);
        const float C = fx * fx + fz * fz - rad * rad;
        if (A > 1e-6f) {
            const float disc = B * B - 4.0f * A * C;
            if (disc >= 0.0f) {
                const float sq = sqrtf(disc);
                float t = (-B - sq) / (2.0f * A);
                if (t < 0.0f) t = (-B + sq) / (2.0f * A);   // starting inside
                if (t >= 0.0f) {
                    const float hy = o[1] + d[1] * t;
                    const float rel = (hy - (cy - half)) / (2.0f * half);  // 0 top, 1 bottom
                    if (rel >= 0.0f && rel <= 1.0f && (best < 0.0f || t < best)) {
                        best = t;
                        // A level shot from the hands meets a zombie about
                        // a quarter of the way down its cylinder, which the
                        // engine's level pose calls 0x40 - so the head band
                        // is the top fifth, the legs the bottom third.
                        bucket = rel < 0.20f ? 0x80 : (rel > 0.65f ? 0x20 : 0x40);
                        // The Colt Python's head explosion
                        // (enemy_hit_reaction_zombie) fires on 0x40 only -
                        // the LEVEL pose, the one the original fires it from.
                        // 0x80 is aiming up, and there it never fires. Read
                        // as a height, that would make a magnum shot to the
                        // HEAD the one shot that cannot take it off, so for
                        // the Python the head band is 0x40 too.
                        if ((g_scaled_down_dist == 3 || g_scaled_down_dist == 4)
                            && bucket == 0x80) {
                            bucket = 0x40;
                        }
                    }
                }
            }
        }
        if (vol[0] < 0) break;
        vol += 6;
        off += 3;
    }

    if (best < 0.0f || (int)best >= g_playerDisplacement) return 0;
    g_playerDisplacement = (int)best;
    g_playerEntity.flags = (unsigned char)((g_playerEntity.flags & ~0xE0) | bucket);
    return 1;
}

float RaidShoulderCam_Focus(void)
{
    if (!RaidShoulderCam_FreeAim()) return 0.0f;
    return (float)s_focus / (float)(SC_FOCUS_FRAMES * 2);
}

short RaidShoulderCam_ScaleDamage(short damage)
{
    if (s_shotFocus < 0.95f) return damage;
    return (short)((float)damage * SC_FOCUS_BONUS + 0.5f);
}

// The pose state is player 1's only, as is everything in this file - so is
// what crosses the wire. On a client nothing else writes these: it runs
// neither the player update (BeforePlayer, Walk) nor the camera, only the
// draw, whose RaidShoulderCam_PoseArms reads them exactly as it does on the
// host. The leg blend still eases a stop: it arrives already eased.
void RaidShoulderCam_GetPose(int i, short* pitch, signed char* walk,
                             unsigned char* walkFrame, unsigned char* legBlend,
                             short* hipYaw)
{
    if (i != 0) { *pitch = 0; *walk = 0; *walkFrame = 0; *legBlend = 0; *hipYaw = 0; return; }
    *hipYaw    = (short)s_hipYaw;
    *pitch     = (short)s_pitch;
    *walk      = (signed char)s_walkShown;
    *walkFrame = (unsigned char)s_walkFrame;
    *legBlend  = (unsigned char)s_legBlend;
}

void RaidShoulderCam_SetPose(int i, short pitch, signed char walk,
                             unsigned char walkFrame, unsigned char legBlend,
                             short hipYaw)
{
    if (i != 0 || Coop_IsAuthority()) return;
    s_hipYaw    = hipYaw;
    s_pitch     = pitch;
    s_walkShown = walk;
    s_walkFrame = walkFrame;
    s_legBlend  = legBlend;
}

float RaidShoulderCam_Spread(void)
{
    return SC_SPREAD * (1.0f - RaidShoulderCam_Focus());
}
