#!/usr/bin/env python3
"""
CUSTOM: bakes the RAID arena's room file, ROOM1100.RDT / ROOM1101.RDT.

This is a room that does not exist in the game. Stage 0 room 0x10 ships as a
FOUR BYTE stub (`00 00 00 00`) - filename filler so the room<S><RR><V>.rdt
pattern stays dense - and nothing in the game ever enters it. That makes it the
one slot a new room can take without displacing anything: no door leads to it,
no script mentions it, no save can be standing in it.

The room is a box. It has one camera, four walls, a floor, and nothing else:
no doors, no items, no enemies, no events, no messages, and NO PRE-RENDERED
BACKGROUND. The arena is drawn as real 3D geometry by src/game/RaidArena.cpp;
this file only supplies what the engine needs around that - the camera to
project from, the collision to stop the player, and the empty tables every
loader walks whether the room uses them or not.

Everything here was written against the READER, not the format doc, which is
wrong in one load-bearing place: docs/RDT_FILE_FORMAT.md says header pointers
carry a -3 bias. They do not. LoadRoomRdt (src/game/RoomInit.cpp:630) is a
plain `*ptrField += (int)g_RdtPointer` over 0x48..0x90, and the shipped files
agree - ROOM1180's tables sit at exactly the offsets its header stores.

A stored 0 is NOT a null pointer: it relocates to the file base like any other
offset. Every one of the nineteen pointers has to aim at something real.
"""

import os, struct

# ---------------------------------------------------------------------------
# The room, in world units. RE1 is -Y up, so the ceiling is negative.
#
# Every collision and camera-zone coordinate in an RDT is read back UNSIGNED
# (Room.cpp:86, RoomCollision.cpp:88), so the whole room has to live at
# positive X/Z below 32768. Hence a box that starts at 2000 rather than at the
# origin.
# ---------------------------------------------------------------------------
X0, X1 = 2000, 10000          # interior, X
Z0, Z1 = 2000, 10000          # interior, Z
WALL   = 2000                 # slab thickness outside the interior
CEIL   = -3600                # wall height (negative = up)

OUT0, OUT1 = X0 - WALL, X1 + WALL     # 0 .. 12000

# The camera lives INSIDE the box, high in the low-X/low-Z corner, looking
# across it. Outside would put a wall between the lens and the room; from in
# here the two near walls fall behind the near plane and the two far ones frame
# the shot, which is how the game's own fixed cameras are placed.
CAM_FROM = (2600, -4200, 2600)
CAM_TO   = (6400,  -600, 6600)
CAM_FOV  = 207                # ROOM1180's value. Focal length in 320-px space,
                              # NOT an angle: sx = vx*fov/vz + 160.

# Where the run starts. There is no door to arrive through, so the spawn is
# stated rather than derived. Angle: 0 = +X, 0x400 = +Z, 4096 = full turn - so
# 0x200 is the diagonal away from the camera, which puts her back to the lens
# the way a fixed-camera game frames its entrances.
SPAWN = (6000, 6000, 0x200)

# ---------------------------------------------------------------------------
# Little-endian writers
# ---------------------------------------------------------------------------
def u8(v):  return struct.pack('<B', v & 0xFF)
def u16(v): return struct.pack('<H', v & 0xFFFF)
def s16(v): return struct.pack('<h', v)
def u32(v): return struct.pack('<I', v & 0xFFFFFFFF)
def s32(v): return struct.pack('<i', v)

def pad4(b):
    return b + b'\x00' * ((-len(b)) & 3)

# ---------------------------------------------------------------------------
# The blocks
# ---------------------------------------------------------------------------

def mask_block():
    """Camera sprite/mask groups. The leading count is what every reader tests:
    zero and Room_LoadCameraSprites skips its whole loop, load_room_masks takes
    the else branch, load_room_bg_masks sets fileSize = 0. No mask pak is ever
    looked for."""
    return u32(0)

def switch_zones():
    """The shell's own table, used only between room_set and RaidLevel_Apply -
    the level's cameras replace it in memory a moment later. It still has to be
    correct, because check_camera_switch walks it during room_set.

    One camera needs TWO records.

    rec0 is camera 0's GROUP HEADER, and its quad is live data: PlayerAnimations
    tests the player against it every frame and bit 0 of zoneFlags gates her
    shadow, so it has to cover the whole floor.

    rec1 is the terminator. Without it check_camera_switch, which starts one
    record PAST the header and walks while camFrom still matches, runs straight
    off the end of the table and starts reading collision records as camera
    zones.

    Corner order is (xMax,zMax) (xMax,zMin) (xMin,zMin) (xMin,zMax) - the
    winding the four cross-product tests in is_entity_in_switch_zone expect.
    """
    b  = s16(0) + s16(0)                      # camTo, camFrom
    b += s16(OUT1) + s16(OUT1)
    b += s16(OUT1) + s16(OUT0)
    b += s16(OUT0) + s16(OUT0)
    b += s16(OUT0) + s16(OUT1)
    b += u16(0xFFFF) + u16(0xFFFF) + b'\x00' * 16
    return b

def boundaries():
    """Four wall slabs, duplicated into all four quadrants.

    collision_push_rect pushes the player OUT of a box, so the walls are solid
    slabs sitting outside the play area, not a hollow frame.

    The quadrant split is a point test: whichever of the four lists the player's
    own quadrant selects is the ONLY list tested that frame. A record straddling
    a split therefore has to appear in every quadrant it touches, and these
    slabs each touch two. Putting all four in all four lists costs 192 bytes and
    removes the question.

    type 0x0001 = collision_push_rect (shape 5 is the same push but is skipped
    once a Z resolve has already happened this frame, which is not what a wall
    wants). flags 0x0300: 0x100 blocks movement, 0x200 joins the second pass
    that re-tests the pushed position - together they are what makes a corner
    stop the player instead of squeezing her through.
    """
    walls = [
        (X0,   OUT1, OUT0, OUT0),   # west   xMax zMax xMin zMin
        (OUT1, OUT1, X1,   OUT0),   # east
        (OUT1, Z0,   OUT0, OUT0),   # north
        (OUT1, OUT1, OUT0, Z1),     # south
    ]
    recs = b''.join(u16(a) + u16(b) + u16(c) + u16(d) + u16(0x0001) + u16(0x0300)
                    for (a, b, c, d) in walls)

    # header: the cell split, then the four per-quadrant counts. The fifth count
    # is read and then discarded by Room_SetupCollisionCallbacks; it is only
    # there to be the end of the running total.
    hdr  = s16((X0 + X1) // 2) + s16((Z0 + Z1) // 2)
    hdr += u32(len(walls)) * 4 + u32(0)
    return hdr + recs * 4

def walk_zones():
    """NPC navigation grid: one zone over the whole floor. Nothing walks it
    while the arena is empty, but it is what the debug spawn helper and any
    future enemy pathing read, and it costs fourteen bytes."""
    return u8(1) + u8(0) + s16(X0) + s16(Z0) + s16(X1) + s16(Z1) + u16(0x03FF) + u16(0)

def footstep_zones():
    """LookupFootstepZone walks this with NO terminator - it relies on the last
    record matching everything. So there is exactly one record, and it covers
    the whole plane."""
    return u16(1) + u16(0) + u16(0) + u16(0xFFFF) + u16(0xFFFF) + u16(45)

def init_scd():
    """One empty block: size 4, whose opcode stream is a single cmd_block_end,
    then the 0000 that ends the chain. run_command_functions dispatches 0x00,
    which clears the continue flag and returns 0, and the walk stops."""
    return u16(4) + u16(0) + u16(0)

def main_scd():
    """Runs every frame, forever. A zero block size means the dispatch loop's
    condition fails before it ever reads an opcode."""
    return u16(0)

def event_table():
    """The relocation pass adds the table base to each entry until it reads a
    zero. One zero dword is an empty table - the same thing ROOM1180 ships."""
    return u32(0)

def messages():
    """Unreachable: nothing in this room calls cmd_message_set. The table is a
    verbatim copy of a shipped one-message block rather than something derived,
    because the text state machine has enough control codes that guessing a
    terminator is not worth it."""
    return u16(2) + bytes((0x04, 0x00, 0x02, 0x04, 0x02, 0x01, 0x50))

def effect_index():
    """load_effect_sprite_data stops at the first 0xFF and never reaches the
    frame data, so eight of them is 'no effects'."""
    return b'\xFF' * 8 + b'\x00' * 8

# The shell carries a camera SLOT per RAID_MAX_CAM, because RaidLevel_Apply
# fills them from the level file at load and shrinks cameras_count to however
# many the level actually uses. Baking one slot would cap every level at one
# camera; baking eight costs 288 bytes.
N_CAM = 8


def camera(mask_off):
    b  = u32(mask_off)          # relocated; the 4-byte zero block above
    b += u32(0)                 # tim mask - relocated but never dereferenced
    b += b''.join(s32(v) for v in CAM_FROM)
    b += b''.join(s32(v) for v in CAM_TO)
    b += s32(0)                 # roll: MatrixToCamera drops it
    b += s32(0)                 # reserved
    b += s32(CAM_FOV)
    return b

def lights():
    """Three point lights. A zeroed light is a BLACK light, not an absent one:
    update_entity_lighting short-circuits every channel to 0 when radius is 0,
    and three of those leave the player unlit. Attenuation ignores Y, so the
    height only has to be somewhere sensible."""
    out = b''
    for (x, y, z, r, g, b_, rad) in (
        (4000, -2000, 4000, 210, 205, 195, 9000),
        (8000, -2000, 8000, 150, 155, 175, 9000),
        (6000, -2600, 3000, 120, 118, 130, 7000),
    ):
        out += s32(x) + s32(y) + s32(z)
        out += u8(r) + u8(g) + u8(b_) + u8(0)
        out += u16(0) + s16(rad)
    return out

# ---------------------------------------------------------------------------
# Assemble
# ---------------------------------------------------------------------------
def build():
    HEADER = 0x94
    CAM    = 0x2C

    blocks = []          # (name, bytes); laid out in order after the camera
    def add(name, data):
        blocks.append([name, pad4(data), 0])

    add('mask',     mask_block())
    add('switch',   switch_zones())
    add('bounds',   boundaries())
    add('walk',     walk_zones())
    add('steps',    footstep_zones())
    add('init',     init_scd())
    add('main',     main_scd())
    add('events',   event_table())
    add('anim',     b'\x00' * 32)      # player_anim_header / _base
    add('msg',      messages())
    add('efidx',    effect_index())
    add('efspr',    b'\x00' * 32)      # 8 dwords; see the note below
    add('tail',     b'\x00' * 64)      # object_models / item_models / icons etc.
    add('vab',      b'\x00' * 16)      # the bump allocator's first bytes

    cursor = HEADER + CAM * N_CAM
    off = {}
    for blk in blocks:
        blk[2] = cursor
        off[blk[0]] = cursor
        cursor += len(blk[1])

    # effect_anim_sprite is read BACKWARDS - base-0, base-4 ... base-28 - so it
    # points at the LAST dword of its block, which puts all eight reads inside
    # it. (Every shipped RDT stores 0 here, which relocates to the file base and
    # reads seven dwords before the buffer. That works by luck; this does not
    # need to.)
    efspr = off['efspr'] + 28

    h  = u8(0)                       # sprites_count - overwritten at load
    h += u8(N_CAM)                   # cameras_count - trimmed at load by the level
    h += u8(0)                       # omodel_slot_count
    h += u8(0)                       # item_count
    h += u16(0)                      # pad
    h += s16(880) + s16(850) + s16(920)   # ambient light, 12-bit channels
    h += lights()
    assert len(h) == 0x48, hex(len(h))

    ptrs = [
        off['switch'],   # 0x48 cam_switch_zones
        off['bounds'],   # 0x4C boundaries
        off['tail'],     # 0x50 object_models   (count 0, never dereferenced)
        off['tail'],     # 0x54 item_models     (count 0)
        off['walk'],     # 0x58 walk_zones
        off['steps'],    # 0x5C footstep_sound_zones
        off['init'],     # 0x60 initialization_scd
        off['main'],     # 0x64 scd_opcodes
        off['events'],   # 0x68 scd_opcodes2
        off['anim'],     # 0x6C player_anim_header
        off['anim'],     # 0x70 player_anim_base
        off['msg'],      # 0x74 messages
        off['tail'],     # 0x78 item_icons      (no reader anywhere)
        off['efidx'],    # 0x7C effect_anim_index
        off['tail'],     # 0x80 effect_anim_data (unreached: index starts 0xFF)
        efspr,           # 0x84 effect_anim_sprite
        off['tail'],     # 0x88 sound_attribute_table (no reader on PC)
        off['tail'],     # 0x8C vab_header_file      (no reader on PC)
        off['vab'],      # 0x90 vab_sound_file - see the note below
    ]
    assert len(ptrs) == 19
    h += b''.join(u32(p) for p in ptrs)
    assert len(h) == HEADER, hex(len(h))

    out = h + camera(off['mask']) * N_CAM
    for _, data, _ in blocks:
        out += data

    # vab_sound_file is the room load's BUMP ALLOCATOR cursor, not sound data:
    # room_set carves its per-object records forward from there and the enemy
    # model setup keeps going. Nothing in this room allocates much, but the
    # cursor must have somewhere to go, so the file ends in slack.
    out += b'\x00' * (0x10000 - len(out))
    return out

TREES = ["assets/USA", "bin/Debug/USA", "bin/Release/USA"]

def main():
    root = os.environ.get("RE_ROOT", ".")
    blob = build()
    n = 0
    for t in TREES:
        d = os.path.join(root, t, "Stage1")
        if not os.path.isdir(d):
            continue
        # Both scenario variants: the trailing digit is Chris/Jill, and the
        # loader picks it from a flag this mode does not control.
        for name in ("ROOM1100.RDT", "ROOM1101.RDT"):
            with open(os.path.join(d, name), "wb") as f:
                f.write(blob)
        n += 1
        print("wrote", d)
    print("rdt %d bytes, %d camera slots, %d trees" % (len(blob), N_CAM, n))
    print("spawn", SPAWN, "camera", CAM_FROM, "->", CAM_TO, "fov", CAM_FOV)

if __name__ == "__main__":
    main()
