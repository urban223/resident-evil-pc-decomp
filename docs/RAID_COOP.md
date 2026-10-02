# RAID co-op

Two players in RAID mode only. Player 1 is Jill, player 2 is Chris with the
Colt Python. Everything here is port-only code; no transcribed file was
restructured for it.

If you are here because a networked client is rendering something wrong, read
[What a snapshot has to carry](#what-a-snapshot-has-to-carry) first. The short
version: **a snapshot has to carry presentation state, not just world state**,
and almost every bug in this feature has been a field the renderer reads and
the wire did not send - or the right field read at the wrong moment, which is
[a second and nastier shape](#when-a-field-is-sampled-is-part-of-what-it-means)
of the same mistake.

Configured under `[Coop]` in `config.ini`:

| `Mode`   | What it does |
|----------|--------------|
| `off`    | Single player. Every co-op path is inert and the original's is taken. |
| `local`  | Both players on this machine, for debugging without a second PC. Player 1 takes a gamepad if one is present and the keyboard otherwise; player 2 gets whichever is left. |
| `host`   | Listens on `Port`. Authoritative: it runs the world and ships snapshots. |
| `client` | Sends its pad to `Host` and renders what comes back. |

## How it is put together

`g_playerEntity` is a macro over `*g_pCurPlayer`, which points into
`g_players[RAID_PLAYERS]`. That one indirection is what made the rest
affordable: every range, facing and damage helper in the engine reads
`g_playerEntity` directly, so pointing `g_pCurPlayer` at a given player before
a call gives that call its own player with no edit to any enemy file.

Storage that used to be one fixed buffer is now per player and reached through
accessors - `Coop_ModelRegion`, `Coop_AnimBuffer`, `Coop_AnimObjBuffer`,
`Coop_CharSfxBase`, `Coop_PlayerTexBank`/`Coop_PlayerTexPage`. Outside co-op
each answers exactly what the original hardcoded, so the campaign takes the
same path it always did. **When something renders or sounds wrong for player
2 only, one of these is the first place to look**: the recurring failure has
been a site that still reaches for the raw global.

The transport is snapshots over UDP, host-authoritative, not lockstep. Lockstep
was rejected because `apply_weapon_damage` works through shared scratch, the
task scheduler contains naked assembly, and the two compilers do not agree -
none of which can be made deterministic across machines cheaply.

### Where it sits in a frame

```
main_loop
  CoopNet_Receive()          MainLoop.cpp:54     - a client applies the snapshot
  TaskScheduler_Update()     MainLoop.cpp:271/289- runs the game_start task, and
      game_loop                                    inside it game_loop:
        host  : Coop_CheckDeaths, Coop_ChooseTargets, update_entities
        client: Coop_ClientPose                   - poses, and queues shadows
        both  : DrawFadeSpr, render_entity
  CoopNet_Send()             MainLoop.cpp:511    - a host ships the snapshot
```

That split is why `snap_apply` stores rather than acts: it runs in `main_loop`,
and anything that has to happen in a particular place *inside* a frame - posing
a skeleton, queueing a shadow before `DrawFadeSpr` drains the queue - has to be
done from `game_loop`, which is a different task. See
[A trap when instrumenting this](#a-trap-when-instrumenting-this).

| Piece | Where |
|---|---|
| Wire structs, build and apply | `CoopNet.cpp` |
| Pose recording (`Coop_NotePose`, `CoopPose`) | `CoopPlayer.cpp`, called from `Joint_move` |
| Client posing and shadow queueing | `Coop_ClientPose`, `CoopPlayer.cpp` |
| Effect events | `Coop_NoteEffect` / `Coop_EffectParentPtr` |
| Per-player storage accessors | `Coop_ModelRegion`, `Coop_AnimBuffer`, ... |

## Testing it on one machine

The network mode needs a host and a client. Both can live on one desktop, but
three things in the original stand in the way, and two environment variables
plus a second folder get around them.

```
bin/Debug/     config.ini with Mode=host,   Port=27015
bin/Debug2/    residentevil.exe + config.ini with Mode=client,
               Host=127.0.0.1:27015, and [Assets] Path pointing back at
               bin/Debug so the 600 MB asset tree is not duplicated
```

Launch each with:

```
RE1_DEBUGLOG=1 RE1_ALLOW_SECOND_INSTANCE=1 ./residentevil.exe
```

- `RE1_ALLOW_SECOND_INSTANCE=1` waives the original's single-instance mutex,
  which otherwise refuses the second copy with exit code 3. Off by default.
- `RE1_DEBUGLOG=1` sends `dbg_printf` to `re1_debug.log` beside the executable.
  Each instance writes its own.

The socket opens on entry to RAID, not at the title screen, so both windows
have to reach the arena before anything appears in the logs.

### Reading the log

```
[net] socket bound to port 27015
[coop] host listening on port 27015
[coop] host: peer 127.0.0.1:60474
[coop] host tick=450 sent=447 recv=449 dropped=0 stale=0 peer=1 sock=1
```

The heartbeat is every 150 ticks, five seconds at 30 Hz, and it runs before
the no-socket early-out on purpose: without that, a host that had left RAID
could not be told from one that never had a peer.

**`[coop] off` at the title screen is normal, in every mode.** The role is taken
from the config by `CoopNet_StartFromConfig`, which `GameStart` calls on the way
into RAID, so before the arena both instances heartbeat as `off` with `sock=0`.
It is not a sign that the config was not read.

- `sent`/`recv` climbing by ~150 per window on both sides is a healthy link.
- `recv` frozen while the other end's `sent` climbs is a one-way link. Check
  the bound port against the address the host learned before anything else.
- `dropped` counts wrong size or bad magic; `stale` counts datagrams that
  arrived out of order and were discarded.
- `[net] recv error N` names anything except "nothing queued".

### Why the game does not pause

A networked session waives two things that are correct for one machine:
`main_loop` still runs when the window loses focus (it is also the socket
pump), and the renderer still clears its buffer when the window is inactive.
Without the first, the background window freezes and produces nothing - the
counters move in bursts and plateau with no socket error at all. Without the
second, the window draws without clearing and the other player smears a trail
of herself across the room.

Both waivers are gated on `Coop_IsNetworked()`, i.e. host or client only.
`local` mode and single player pause exactly as the original did.

The lesson worth carrying: **the focus pause had a second thing silently
leaning on it, and there may be a third.** Anything that assumed "unfocused
means not drawing" is now wrong in a networked session.

## What a snapshot has to carry

> **A snapshot has to carry presentation state, not just world state.**
>
> Where a body is and how much health it has is what the *simulation* needs. It
> is not what the *renderer* reads. The renderer reads a different set of fields
> - sometimes at different offsets in the same struct, sometimes a byte that
> decides whether to draw at all - and a client that runs no simulation has no
> other way to obtain them.

This is the single most expensive lesson in this work, so it is worth stating
plainly before the list. The first snapshot carried position and health, which
is the obvious thing to send and reads as complete. It produced a client showing
an empty room full of frozen people, and every missing field below was found
one at a time, each looking like "the client is broken" rather than like a
field that was never sent.

Three properties made them hard to find:

1. **They fail silently and locally.** A missing byte does not crash or log; it
   makes one body wrong while every neighbouring number matches the host.
2. **The obvious measurements exonerate them.** Joint pointers, model pointers,
   joint counts, positions and even the draw counter all agreed between host and
   client while the room was visibly empty.
3. **Name similarity hides them.** `PlayerEntity` carries two animation pairs at
   two offsets; sending the wrong one poses from a frame nothing wrote.

The practical rule: when a client renders something wrong, **do not ask what the
simulation would have computed - ask what the drawing code reads, and check
whether the wire carries that**. Diff the two ends on the exact fields
`render_entity` and `Joint_move` touch.

Everything below had to be added, and each one looked like "the client is
broken" on its own:

| Carried | Why |
|---|---|
| Two animation pairs | `PlayerEntity` has `animationId`/`animFrameId` at 0x84/0x85 for the state machine AND `animationId`/`animation_frame_id` at 0xBD/0xBE through the `Entity` view. `Joint_move` reads the second. |
| Which animation SOURCE | A player poses from four pairs - `emdScratchPtr1/2`, `jointMoveData0/1`, `jointMoveData2/3`, `animHeader`/`animBase` - and the state machine picks per animation. The host records what it used, derived by comparing pointers inside `Joint_move`. |
| The whole `status_flags` byte | `\|= ACTIVE` left a client's enemy at 01 where the host read F1. |
| Entity header bytes 2 and 3 | `render_entity` opens with `g_animFrameIdSave = ((entity[3] & 0x7f) == 0)` and puts its entire draw branch behind that being zero. A client whose byte 3 was 0 called `render_entity`, walked every joint and emitted nothing. |
| `g_enemy_count` | The draw loop is bounded by it, and a host grows it when a dead player stands up as a zombie. |
| Death | `Coop_CheckDeaths` runs only on the authority, so without this a client kept drawing a body frozen on the frame that killed it. |
| The enemy's pose fields | `action_behavior`, `action_state`, `timing_control`, `blend_counter`, `hit_state`, `death_timer`. A client runs no state machine, so whatever it does not receive keeps the value its own spawn left. |
| Effect events | A billboard is not state - it is an event, queued on the host and replayed. See `Coop_NoteEffect`. |
| The pose, as one set | Id, frame, source, mirror, blend counter and blendStep, captured together inside `Joint_move`. An id from one tick with a frame from another poses from an animation frame nothing wrote. `CoopPose`. |
| WHICH TICKS posed | A serial, bumped only when a pose happened. Without it a client cannot tell "the host posed this again" from "the host posed nothing", and a replayed blend runs extra times. |
| The shadow quad | `entity+0xE4`: tint, half extents and its own offset. Not a field the renderer reads - a quad the entity's own update SUBMITS every frame. See below. |

**A client must not animate on its own.** `Joint_move` advances
`animation_frame_id` on its way out. `Coop_ClientPose` poses the wire's frame
and then puts the frame and `timing_control` back, because letting the advance
stand played the animation a second time on top of the snapshot.

### When a field is sampled is part of what it means

The first shape of this bug is a field the wire does not carry. The second is a
field it does carry, read at the wrong moment - and it is harder, because the
value on the wire is *correct*, just not the one that produced what the host is
showing.

`Joint_move` wraps `animation_frame_id` to 0 on the call that finishes an
animation and returns 1. Handlers that stop posing on that return value -
`zombie_dead_animation` for a corpse, and `player_behavior_13_gun_raise`'s case
2 on a turn - leave the skeleton holding the last frame while the field reads
something else entirely. On one machine nobody looks at the field again. Over a
wire, a client poses from exactly that field.

The result was a killed zombie standing back up on the client (frame 0 of the
fall animation is a zombie on its feet) and a player who jittered for one tick
every time she turned while aiming. Both looked like corruption, and neither
was: host and client agreed on every number.

**So the host records what it POSED, at the moment it posed it - inside
`Joint_move`, past the timing gate - and the wire carries that.** `Coop_NotePose`
does the recording; `CoopPose` is the set; `snap_build` prefers it over the
entity's own fields. The same principle covers the blend: an interpolated pose
is only reproducible if the client starts from the same joints, which means it
must pose on the same ticks - hence the serial.

The general rule, and the reason this deserves its own heading: **for anything
the renderer reads, ask not only whether the wire carries it, but whether it
carries the value that was in effect when the host drew.**

### Some presentation is not a field at all

The ground shadow is a quad *inside* the entity, at `+0xE4`, and the death blood
pool is that same quad recoloured and resized - not a separate object and not an
effect. What puts it on screen is `entity_add_fade_sprite`, called once a frame
by the body's own update, and drained by `DrawFadeSpr`.

A client runs no entity update, so its queue was empty every frame: no shadow
under anybody, and therefore no pool. No field was missing - a per-frame
*submission* was. The wire carries the quad's state and `Coop_ClientPose` makes
the submission itself, from `game_loop`, where the queue is drained.

Three kinds, then, and it is worth knowing which one you are looking at:
state (position, health), events (billboards), and per-frame submissions
(shadows). Only the first is what a snapshot naturally is.

## Dying, and what gets up

A killed player does not leave the game: he gets up as a zombie and keeps
playing on the same pad. Three things had to be true for that to read as one
event rather than a body being swapped for a monster.

**He falls first.** `player_state_01_control` puts a player into state 3 the
moment his health goes below zero, and state 3 is his own death: the scream, the
drop, the slide, the pool of blood growing under him. `Coop_CheckDeaths` now
waits for it - promotion happens when the state machine hands over to state 4
(input blocked, body settled), with a ten-second timeout for rooms that take a
different death path. Before this the zombie stood up on the frame the health
went negative, and the player appeared to vanish.

**He rises rather than appearing upright.** The engine already owns that motion:
`zombie_falldown`'s sub-state 3 is the get-up - animation 8 played REVERSED,
which is `Joint_move`'s mirror argument - ending by clearing the laying-down
height and handing back to the idle state. The promotion starts the entity
there, lying at `t[1] = 1` where the body fell.

**And the body that rises is his own.** The entity reads its animation and its
body from two different pointers: `animHeader`/`animBase` are the rig - joint
hierarchy, rest offsets and frames - and `modelLoadBuffer` is the block each
joint takes its mesh from. The reserved slot keeps the zombie's animation and is
handed the player's model, so Jill moves like a zombie as Jill.

### The two rigs number their limbs differently

This is the part that is not guessable, and it is not in either file's header.
It was read out of the skeletons themselves - the rest pose composed through
each file's own hierarchy table (`animHeader` + the offset at its first word),
y negative being up:

| joint | `char10`/`char11` (player) | `em1000` (zombie) |
|---|---|---|
| 0 | torso (root; the arms hang here) | hips (root; the legs hang here) |
| 1 | head, a leaf on the torso | torso, child of the hips |
| 2 | hips, where the legs hang | head, child of the torso |
| 3-5 | leg, z+ side | arm, z- side, held forward |
| 6-8 | leg, z- side | arm, z+ side |
| 9-11 | arm, z+ side | leg, z- side |
| 12-14 | arm, z- side | leg, z+ side |

So mesh index *j* means one body part on her and a different one on him. Handing
joint *j* mesh *j* would have driven her arms with his leg rotations. The mapping
table is `kCoopZombieMeshMap` in `CoopPlayer.cpp`, kept by side as well as by
limb so she does not come up mirrored.

What is NOT remapped is anything that could mix two skeletons into one pose: the
hierarchy, the rest offsets and the frames all stay the zombie's. Only the
meshes move. The proportions were compared at the same time and left alone - his
upper arm/forearm are 454/436 against her 422/388, his thigh/shin 663/833
against her 602/808.

### And the arms have to be turned as well

Putting the right mesh on the right joint is not enough, because a mesh is
authored in its joint's OWN frame and the two rigs do not agree on that either:
her arms run down the +Y axis, because they hang at her sides, and his run along
Z, because a zombie holds them out in front. Her arm mesh on his arm bone is
right where it should be and a quarter turn wrong in orientation - which is what
"the arms are twisted" was.

Legs and feet need none of this: both rigs run the leg down +Y and point the
foot along +X, which is why they looked right from the first build.

So the geometry is turned rather than the skeleton. A quarter turn about X is a
permutation with a sign - `(x, y, z)` becomes `(x, z, -y)` for his z- arm and
`(x, -z, y)` for his z+ one - applied to a private copy of the six arm parts'
vertices and normals. Everything else shares the player's arrays, and every part
shares his primitive data: a primitive names its vertices by index, and indices
do not change when vertices turn.

`coop_build_zombie_body` assembles all of it into a block shaped exactly like
the one the engine loads from a file - a 12-byte `AnimDataHeader` and its
`slots[]` - so `InitAnimStructure` and `SetupJointStructures` are the stock path
and nothing downstream knows any of this happened.

### The detached shoulders: found and fixed (confirmed in game)

The first version of the table above had rows 0 and 2 of the player wrong
("pelvis" / "chest"). Dumping the mesh bounds settles it: on `char11` the
root's mesh runs UP from the joint (y -714..35) and joint 2's runs down
(-77..310), so the player's root is the torso and joint 2 the hips. The
zombie's root is the hips (mesh y -3..388) and the torso is its child.
`kCoopZombieMeshMap` mapped root to root, which put her torso on his hips:
whenever he bent at the waist her chest stayed with his pelvis while her arms
went with his torso. That is what "detached at the shoulder" was.

The second half is bone length. Rest offsets (`animHeader+8`, three shorts per
joint), `char11` against `em1000`:

| bone | player (`char11`) | zombie (`em1000`) |
|---|---|---|
| head off torso | (-20, -618, 0) | (-17, -693, 0) |
| z+ shoulder off torso | (-36, -576, 276) | (-78, -576, 421) |
| z- shoulder off torso | (-37, -565, -266) | (-77, -578, -418) |
| z+ elbow / wrist | (-8, 422, 64) / (14, 388, 54) | (1, 0, 452) / (0, 0, 434) |
| z- elbow / wrist | (0, 399, -67) / (13, 358, -45) | (0, 0, -454) / (0, 0, -436) |

His shoulders are 145 units wider, so her arm meshes on his bones hung beside
her own shoulders. (`Char10`'s shoulders are at y -694 / z ±388; it differs
again, which is why nothing below is hardcoded.)

The fix, in `CoopPlayer.cpp` (64e59e8):

1. `kCoopZombieMeshMap` now starts `2, 0, 1`: hips on hips, torso on torso.
2. `coop_fit_zombie_bones`, called right after `ResetJointTransforms` in
   `Coop_ReserveZombies`, gives zombie joints 2-8 (head, both arms) the
   PLAYER's rest offsets, read from her own `animHeader`. Shoulders and head
   go in unchanged (both torsos are authored upright). Elbows and wrists take
   the same quarter turn `coop_rot_copy` gives the arm meshes, so bones and
   meshes turn together. This holds per entity because `Joint_move` writes
   only the root's `transform.t`; every other joint keeps the rest offset.
3. Legs left as the zombie's: their lengths also put the feet on the floor
   under a root height that comes from the zombie's frames. Her legs are about
   85 units shorter; if the knees or ankles show a gap, that is where it comes
   from.

Confirmed in game by the user (2026-10-02): after death the player rises
and moves as a zombie wearing his own body, with the animations right.

Three things about the two files are load-bearing and were checked rather than
assumed: `char10`, `char11` and `em1000` all carry `jointCount` 15 at
`animHeader+4`, stride 176 and a 104-byte frame record. A model that did not
match would have to be refused rather than posed into nonsense.

### A grab plays out of the attacker's file

Worth knowing before touching anything near this: `emdScratchPtr1`/`2` on a
PlayerEntity are not his own animation. They are whatever model was loaded last,
kept on the player because that is who gets posed *from* it - `zombie_attack`
writes `g_playerEntity.attackAnim` out of a table of animation ids into the
ZOMBIE's file and puts the victim into state 5, whose animation function
(`player_anim_attack_recoil`) poses him through those two pointers. The last
enemy loaded is the one that can grab you, so every enemy load overwriting them
is correct.

In co-op it was not enough. The fields live on PlayerEntity, enemy models are
loaded with player 1 current, and player 2's copy therefore stayed zero - his
half of a grab had no animation data at all. `LoadEntityEMD` now writes both to
every player. The co-op early-out at the top of `Joint_move` ("animHeader or
animBase is zero, do not pose") is what had been standing in for this.

### Two traps, both paid for

**`modelLoadBuffer` is not what it was when you set it.** `InitAnimStructure`
takes a model HEADER, and on its way through it writes the header's `slots[]`
array back into the field: `SetAnimSlot` stores through `&ENTITY->unk_0c + 8`,
and 0x0C + 8 is 0x14, which *is* `modelLoadBuffer`. So a set-up entity carries
the slots, 0xC past the header, and that is what the joint setup indexes.
Handing that value back in as a header reads the "already resolved" flag out of
the middle of slot 0, walks a garbage count and dies - which is exactly how this
crashed on entering the arena. `RaidEnemies_Spawn`'s `prev->modelLoadBuffer -
0xc` when two enemies share one model is the same fact from the other side.

**A player-zombie must be pointed at its victim BEFORE anything can return
early.** Every helper the zombie state machine uses reads `g_playerEntity`, and
for the length of the get-up that pointer was still whoever it had been - his
own owner, lying dead in the exact spot he is standing up out of. Distance zero
passes every reach test, so `zombie_chase_player` put him straight into the
grab: arms up in the bite pose, holding his own corpse, unable to move. It read
as two separate bugs ("the arms are wrong" and "I cannot move") and was one.

### The zombie's attack

State 5 (`zombie_attack`) was reachable all along and nothing ever pressed it.
It cannot simply be set: its first sub-state GRABS - it snaps the victim to the
bite position and drives his animation - so entering it with nobody in reach
would yank the other player across the room.

The fire button now enters it through the same three tests the AI uses before
entering the same state (`zombie_chase_player`): inside the cone and in reach
(`checkAngularViewAndDistance(700, 1500)`), a clear line, and a victim not
already in somebody's jaws. The AI's own grab is suppressed while a player is
driving - bit 2 of `behavior_step` is the engine's "follow, do not attack" flag -
so a player-zombie bites when its player says so.

Who the victim is matters for more than the bite: every helper the zombie state
machine uses reads `g_playerEntity`, so `Coop_DriveZombie` points it at the
other player, and a player-zombie is never measured against its own owner.

## A trap when instrumenting this

`game_loop` and `main_loop` are **different scheduler tasks**. A probe after
`update_entities` and a probe inside `snap_build` are not the same frame, and
the gap between them can be tens of ticks. Comparing the two and concluding
that the host sends stale fields is wrong - that mistake cost two rounds here.
Log both ends of the wire, or log twice in the same function, but do not
compare across those two.

## Known gaps

- `src/platform/linux/sockets.cpp` has never been compiled;
  `tests/compile_linux.sh` does not cover `src/platform`.
- No run has ever crossed two machines. Everything here is loopback.
- A client does not load models for enemy slots that become occupied after
  the room loads; see the note in `CoopNet.cpp`.
- A client does not run `update_player_anim`, so poses come off the wire as a
  frame id and nothing advances the skeleton locally.
- A client shows the RAID mirror's reflected room but not the reflected
  characters: the mirror pass lives in `update_player_anim` and
  `update_entities`, neither of which a client runs (`docs/RAID_BATHROOM.md`).
- The zombie's attack button has not been tried in game yet. The interesting
  case is the one that does nothing: with the other player out of reach the
  fire button must not start a grab, or it will drag him across the room.
- A dropped snapshot costs a client one pose: it skips that frame rather than
  interpolating through it, and a blend that was mid-flight resumes one step
  short. Harmless on loopback, untested on a real link.
