// CoopNet.h - host-authoritative transport for RAID co-op.
//
// CUSTOM. Talks to the world only through plat_net_* (src/platform/platform.h);
// there is no socket call in this file or any other under src/game, and
// tests/check_platform_boundary.py fails the build if one appears.
//
// WHY HOST-AUTHORITATIVE SNAPSHOTS AND NOT LOCKSTEP
//
// Lockstep needs both machines to simulate identically, and this engine will not
// do that cheaply. apply_weapon_damage reads and writes shared scratch
// (g_playerDisplacement, g_svecScratch, g_playerPosScratch) and picks its target
// by a swap-down walk whose order depends on that state; the task scheduler
// switches contexts in naked assembly and is the reason Release builds with
// whole-program optimisation off; and the port targets both MSVC and GCC, so
// the two ends are not even the same compiler. Any divergence would be
// permanent, because there is no rollback here and no notion of a remote input
// to re-simulate from.
//
// So the host simulates everything and the client draws what it is told. The
// client sends one pad word per tick and applies what comes back. The cost is
// honest and worth stating: the client sees the host's world about RTT/2 late,
// so at 150 ms a shot at a moving teammate will feel like it misses. With a
// fixed camera and RE1's pace that is acceptable; with rollback it would not
// have to be, and rollback is not affordable here.
//
// The wire format assumes both ends are this same 32-bit build. It is not
// versioned across releases and does not try to be: a mismatched build is a
// mismatched protocol, and the magic word is there to say so rather than to
// negotiate.
#pragma once

#include "../Globals.h"

// How this machine is taking part.
#define COOP_ROLE_OFF     0   // single player
#define COOP_ROLE_LOCAL   1   // DEBUG: both players on this machine, no socket
#define COOP_ROLE_HOST    2   // simulates, sends snapshots
#define COOP_ROLE_CLIENT  3   // sends input, draws what it is told

extern int g_coopRole;

// True when this machine decides what happens. The host and the local debug
// mode both do; a client never does, and every place that would advance the
// world has to ask.
int Coop_IsAuthority(void);

// Start / stop. `hostText` is "1.2.3.4:5000" for a client and is ignored for a
// host. Both return 1 on success; on failure the role is left at COOP_ROLE_OFF
// so a failed connect degrades to single player rather than to a half-open one.
int  CoopNet_StartHost(unsigned short port);
int  CoopNet_StartClient(const char* hostText);
void CoopNet_Stop(void);

// Once per tick, before anything advances: a client applies the newest snapshot
// it has, a host drains the client's input into player 2's pad.
void CoopNet_Receive(void);

// Once per tick, after the world has moved: a host sends the snapshot, a client
// sends its pad.
void CoopNet_Send(void);
