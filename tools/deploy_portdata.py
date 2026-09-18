#!/usr/bin/env python3
"""deploy_portdata.py - put the port's own assets where the game reads them.

`portdata/` holds every runtime asset this project produced, tracked in git and
kept apart from `assets/`, which is the player's own copy of the game. The game
does not read `portdata/`: `bin/*/config.ini` ships with an empty
`[Assets] Path`, which means each build reads the tree NEXT TO ITS EXECUTABLE.
So the same files have to exist in three places, and forgetting one fails
silently - the build compiles a new header while the run loads an old atlas.

This copies portdata/** into every tree that exists:

    assets/USA/...          the source-of-truth tree in a checkout
    bin/Debug/USA/...       what a Debug run reads
    bin/Release/USA/...     what a Release run reads

Run from the repo root:  python3 tools/deploy_portdata.py
                         python3 tools/deploy_portdata.py --check
"""

import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "portdata")
TREES = [
    os.path.join(ROOT, "assets"),
    os.path.join(ROOT, "bin", "Debug"),
    os.path.join(ROOT, "bin", "Release"),
]

# Assets this port generates but does NOT track, because they carry the game's
# own art. A clone has to rebuild them from its own install. Keep this in step
# with the table in docs/ASSETS.md.
# These have names of their own, so their absence is detectable and worth
# reporting per tree.
DERIVED = [
    ("USA/Data/titlebg.pix",   "tools/build_title_bg.py"),
    ("USA/Data/titlelogo.bin", "tools/build_title_bg.py"),
    ("USA/Data/raideye.bin",   "tools/build_raid_eye.py"),
]

# These two generators PATCH THE GAME'S OWN FILES IN PLACE - W12.EMW gains a
# barrel, ROOM110{0,1}.RDT become the RAID arena - so the path exists whether or
# not the tool has ever run and no check here can tell the two apart. They can
# only be stated.
IN_PLACE = [
    ("USA/players/W12.EMW",          "tools/build_beretta_barrel.py"),
    ("USA/Stage1/ROOM110{0,1}.RDT",  "tools/build_raid_room.py"),
]

# The one gap nothing can close: the custom pistols' in-hand models have no
# generator in this tree, and cannot be tracked either (each is Jill's W12.EMW
# with the weapon TMD swapped, so the animation half is the game's own data).
# Missing, LoadFile returns (size_t)-1 and EntityModelLoader indexes off the
# front of its buffer, so this is a crash and not a missing model. Say so.
UNOBTAINABLE = [
    "USA/players/w1f.emw",
    "USA/players/w2f.emw",
    "USA/players/w3f.emw",
]


def relative_files(root):
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if f == "README.md":
                continue
            full = os.path.join(dirpath, f)
            yield os.path.relpath(full, root).replace("\\", "/")


def main():
    check = "--check" in sys.argv

    if not os.path.isdir(SRC):
        sys.exit("no portdata/ at %s" % SRC)

    names = sorted(relative_files(SRC))
    if not names:
        sys.exit("portdata/ is empty")

    trees = [t for t in TREES if os.path.isdir(t)]
    if not trees:
        sys.exit("none of %s exists - build once first"
                 % ", ".join(os.path.relpath(t, ROOT) for t in TREES))

    copied = stale = 0
    for name in names:
        src = os.path.join(SRC, name)
        for tree in trees:
            dst = os.path.join(tree, name.replace("/", os.sep))
            same = (os.path.exists(dst)
                    and os.path.getsize(dst) == os.path.getsize(src)
                    and open(dst, "rb").read() == open(src, "rb").read())
            if same:
                continue
            stale += 1
            rel = os.path.relpath(dst, ROOT)
            if check:
                print("STALE  %s" % rel)
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            print("wrote  %s" % rel)
            copied += 1

    if check:
        print("\n%d file(s) out of date across %d tree(s)" % (stale, len(trees)))
    else:
        print("\n%d file(s) written, %d already current"
              % (copied, len(names) * len(trees) - copied))

    # The derived assets are the other half of a working clone, and nothing
    # else will tell you they are missing until the feature quietly does not
    # draw. Report them rather than pretending portdata/ is the whole story.
    missing = []
    for rel, maker in DERIVED:
        for tree in trees:
            if not os.path.exists(os.path.join(tree, rel.replace("/", os.sep))):
                missing.append((os.path.relpath(os.path.join(tree, rel), ROOT), maker))
    if missing:
        print("\nDerived from the game's own art, not tracked - run its maker:")
        for path, maker in missing:
            print("  %-44s %s" % (path, maker))

    print("\nAlso not tracked, and not detectable from here:")
    for rel, maker in IN_PLACE:
        print("  %-44s %s  (patches the game's own file)" % (rel, maker))
    gone = [u for u in UNOBTAINABLE
            if not any(os.path.exists(os.path.join(t, u.replace("/", os.sep)))
                       for t in trees)]
    if gone:
        print("\n  %s\n  are MISSING and no tool here can build them. Equipping a"
              " custom pistol\n  without them corrupts memory - see docs/ASSETS.md."
              % ", ".join(gone))


if __name__ == "__main__":
    main()
