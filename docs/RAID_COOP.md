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
- The zombie a dead player becomes has no attack button bound. State 5 is
  accepted, there is just no input that reaches it.
- A dropped snapshot costs a client one pose: it skips that frame rather than
  interpolating through it, and a blend that was mid-flight resumes one step
  short. Harmless on loopback, untested on a real link.
