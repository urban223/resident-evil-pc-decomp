#!/usr/bin/env python3
"""build_inhand_pistol.py - the custom pistols' models in Jill's hands.

CUSTOM. Not part of the original game.

WHY THIS EXISTS

`players/{w1f,w2f,w3f}.emw` are the flare, acid and freeze pistols as the
player sees them held. Each one is Jill's `W12.EMW` with the weapon half of its
TMD replaced, which means most of every file is Capcom's animation data byte for
byte - so the files themselves cannot be tracked, and for a while nothing here
could rebuild them either. A clone got no in-hand models at all.

An .emw splits cleanly, and that is the whole trick:

    [ 0 .. tmd_off )     the animation half - Capcom's, and identical in
                         w12/w1f/w2f/w3f down to the byte
    [ tmd_off .. -8 )    the TMD - OURS, and the only part that differs
    last 8 bytes         the trailer: (anim_off, tmd_off), unchanged

So the TMD halves live in `tools/inhand/*.tmd` (ours, tracked, ~6-7 KB each) and
this grafts them onto the animation half of whichever `W12.EMW` the machine
running it already has. Nothing of Capcom's is redistributed and a clone still
ends up with the real models.

Run it AFTER the game data is in place. Order against build_beretta_barrel.py
does not matter: that tool asserts it leaves the animation half untouched, so
both the stock and the barrelled W12.EMW give the same graft.

    python3 tools/build_inhand_pistol.py
    python3 tools/build_inhand_pistol.py --check    # verify, write nothing

THE TWO PACKET LAYOUTS

Do not assume a uniform 28-byte primitive here. `w1f` is textured throughout -
206 packets of 28 bytes, the fist plus a flare pistol that samples Jill's page.
`w2f` and `w3f` are MIXED: the 34 fist packets stay textured at 28 bytes, and
their 172 pistol packets are untextured at 20, because Jill's texture page had
no green or blue left to borrow and the engine draws untextured TMD polygons
happily (see docs/GRENADE_PISTOL.md, round 12). That is exactly the 1376-byte
difference between the two file sizes. The blobs are copied whole, so this file
never has to care - but anything that walks the primitives does.
"""

import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOBS = os.path.join(ROOT, "tools", "inhand")

TREES = [
    os.path.join(ROOT, "assets", "USA", "players"),
    os.path.join(ROOT, "bin", "Debug", "USA", "players"),
    os.path.join(ROOT, "bin", "Release", "USA", "players"),
]

# (our TMD blob, the file it becomes). The names are the free w-slots picked in
# EntityModelLoader.cpp; w20.emw is a real game file, w1f/w2f/w3f are not.
PISTOLS = [
    ("w1f.tmd", "W1F.EMW"),
    ("w2f.tmd", "W2F.EMW"),
    ("w3f.tmd", "W3F.EMW"),
]

STOCK = "W12.EMW"


def split_emw(data, what):
    """Answer (animation half, tmd, anim_off, tmd_off) for an .emw."""
    if len(data) < 16:
        sys.exit("%s is too short to be an .emw (%d bytes)" % (what, len(data)))
    trailer = (len(data) & ~3) - 8
    anim_off, tmd_off = struct.unpack_from("<2I", data, trailer)
    if not (0 < tmd_off < trailer) or anim_off >= tmd_off:
        sys.exit("%s: trailer says anim=%d tmd=%d, which does not fit %d bytes"
                 % (what, anim_off, tmd_off, len(data)))
    if struct.unpack_from("<I", data, tmd_off)[0] != 0x41:
        sys.exit("%s: no TMD id 0x41 at %d - is this really an .emw?"
                 % (what, tmd_off))
    return data[:tmd_off], data[tmd_off:trailer], anim_off, tmd_off


def build(stock_path):
    with open(stock_path, "rb") as f:
        stock = f.read()
    anim, _tmd, anim_off, tmd_off = split_emw(stock, stock_path)

    out = []
    for blob_name, out_name in PISTOLS:
        blob_path = os.path.join(BLOBS, blob_name)
        if not os.path.exists(blob_path):
            sys.exit("missing %s - it is tracked, so this is a broken checkout"
                     % os.path.relpath(blob_path, ROOT))
        with open(blob_path, "rb") as f:
            blob = f.read()

        built = anim + blob + struct.pack("<2I", anim_off, tmd_off)

        # The checks from the .emw notes, before anything ships. The first is
        # the one that matters: if the animation half moved, every pose is
        # wrong and the hands are somewhere else entirely.
        assert built[:tmd_off] == anim, "animation half changed"
        assert len(built) <= 37888 - 2048, \
            "%s is %d bytes, too close to g_animationBuffer's 37888" \
            % (out_name, len(built))
        t2 = (len(built) & ~3) - 8
        assert struct.unpack_from("<2I", built, t2) == (anim_off, tmd_off), \
            "trailer did not land where the loader reads it"
        # And the graft has to parse as the loader will read it.
        _a, back, _ao, _to = split_emw(built, out_name)
        assert back == blob, "TMD did not survive the round trip"

        out.append((out_name, built))
    return out


def main():
    check = "--check" in sys.argv

    trees = [t for t in TREES if os.path.isdir(t)]
    if not trees:
        sys.exit("none of %s exists - copy the game data in and build once"
                 % ", ".join(os.path.relpath(t, ROOT) for t in TREES))

    # Any tree's W12.EMW will do: the animation half is Capcom's and identical
    # across all of them. Take the first that has one and say which.
    stock_path = None
    for t in trees:
        p = os.path.join(t, STOCK)
        if os.path.exists(p):
            stock_path = p
            break
    if stock_path is None:
        sys.exit("no %s in any tree - it is the game's own file and has to come "
                 "from your copy of the game" % STOCK)
    print("grafting onto %s" % os.path.relpath(stock_path, ROOT))

    built = build(stock_path)

    wrote = current = stale = 0
    for name, blob in built:
        for t in trees:
            dst = os.path.join(t, name)
            same = (os.path.exists(dst)
                    and os.path.getsize(dst) == len(blob)
                    and open(dst, "rb").read() == blob)
            rel = os.path.relpath(dst, ROOT)
            if same:
                current += 1
                continue
            if check:
                stale += 1
                print("STALE  %s" % rel)
                continue
            with open(dst, "wb") as f:
                f.write(blob)
            print("wrote  %s (%d bytes)" % (rel, len(blob)))
            wrote += 1

    if check:
        print("\n%d file(s) out of date, %d current, across %d tree(s)"
              % (stale, current, len(trees)))
    else:
        print("\n%d file(s) written, %d already current" % (wrote, current))


if __name__ == "__main__":
    main()
