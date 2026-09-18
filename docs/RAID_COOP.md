# RAID co-op

Two players in RAID mode only. Player 1 is Jill, player 2 is Chris with the
Colt Python. Everything here is port-only code; no transcribed file was
restructured for it.

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
