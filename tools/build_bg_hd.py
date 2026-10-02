#!/usr/bin/env python3
"""
CUSTOM: high-resolution versions of the backgrounds a RAID level projects.

A RAID level can rebuild one of the game's pre-rendered rooms in 3D and texture
it by projecting the room's own background back out of its camera (the `bgsrc`
lines of raid1.lvl, RaidArena.cpp "BACKDROPS"). The backgrounds are 320x240.
Seen from the camera they were made for that is enough; walked up to, one
background pixel covers dozens of screen pixels and the room turns to mush. No
filter can put back what is not in the picture, so this upscales each one with
a neural network (Real-ESRGAN, x4plus, x4) and writes the result where the game
looks first:

    USA/Data/bghd/rc<S><RR><C>.bin     'BGHD' + u32 w + u32 h + RGBA bytes

When the file is missing the game uses the stock .pak exactly as before, so
this is an optional quality step, not a dependency.

The output carries Capcom's own pixels, so it is NOT tracked (docs/ASSETS.md):
every clone regenerates it from its own install. Real-ESRGAN is fetched into
tools/.cache/ on first use (the ncnn-vulkan build: one executable, any GPU with
Vulkan, no Python packages); that folder is ignored too.

A ready-made HD pack can be used instead, where it has the picture:

    python tools/build_bg_hd.py --pack C:/path/to/RE-ENHANCE_RE1_v2.0

Packs made for the Seamless HD Project (hires/bgd/<hash>.webp or .png) name each
background by a hash of the original, computed in a way this tool does not
reproduce. So the picture is found by what it LOOKS like: every image in the
pack is shrunk to 80x60 and compared with the stock background shrunk the same
way; a mean difference under PACK_MATCH (out of 255) is the same picture, and
anything else falls back to Real-ESRGAN. The thumbnails are cached in
tools/.cache/pack_<name>.json, so the 600-odd images are read once. A pack
redoes only some rooms - RE-ENHANCE has the bathroom's camera 0 and not its
camera 1 - which is exactly the case this handles.

Usage (repo root):
    python tools/build_bg_hd.py [--pack DIR]                 every bgsrc in portdata's raid1.lvl
    python tools/build_bg_hd.py [--pack DIR] 4 7 0 4 7 1     explicit (stage room cam) triples
"""

import json, os, re, struct, subprocess, sys, urllib.request, zipfile

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", ".cache")
ESRGAN_URL = ("https://github.com/xinntao/Real-ESRGAN/releases/download/"
              "v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip")
ESRGAN_DIR = os.path.join(CACHE, "realesrgan")
ESRGAN_EXE = os.path.join(ESRGAN_DIR, "realesrgan-ncnn-vulkan.exe")
MODEL = "realesrgan-x4plus"

# Every tree the game reads its assets from - the same three deploy_portdata.py
# writes (docs/ASSETS.md: each build reads the tree next to its own exe).
TREES = [
    os.path.join(ROOT, "assets"),
    os.path.join(ROOT, "bin", "Debug"),
    os.path.join(ROOT, "bin", "Release"),
]
LEVEL = os.path.join(ROOT, "portdata", "USA", "Data", "raid1.lvl")

sys.path.insert(0, os.path.join(ROOT, "tools"))
import pak_view                                   # noqa: E402  (the LZW decoder)
from PIL import Image                             # noqa: E402

W, H = 320, 240


def esrgan():
    if os.path.exists(ESRGAN_EXE):
        return ESRGAN_EXE
    if sys.platform != "win32":
        sys.exit("Real-ESRGAN: only the Windows build is fetched automatically; put "
                 "realesrgan-ncnn-vulkan in %s" % ESRGAN_DIR)
    os.makedirs(CACHE, exist_ok=True)
    z = os.path.join(CACHE, "realesrgan.zip")
    print("fetching Real-ESRGAN (~45 MB) ...")
    urllib.request.urlretrieve(ESRGAN_URL, z)
    with zipfile.ZipFile(z) as f:
        f.extractall(ESRGAN_DIR)
    return ESRGAN_EXE


PACK_MATCH = 6.0      # mean abs difference at 80x60, 0..255; the bathroom matched at 3.0,
                      # the next-best (another room) at 16

def pack_args():
    args = sys.argv[1:]
    pack = None
    if "--pack" in args:
        i = args.index("--pack")
        pack = args[i + 1]
        del args[i:i + 2]
    return pack, args


def pack_index(pack):
    """{file: 80x60 RGB thumbnail as a list} for every background in the pack."""
    bgd = os.path.join(pack, "hires", "bgd")
    if not os.path.isdir(bgd):
        bgd = pack
    key = re.sub(r"[^A-Za-z0-9]+", "_", os.path.basename(os.path.normpath(pack)))
    cache = os.path.join(CACHE, "pack_%s.json" % key)
    idx = {}
    if os.path.exists(cache):
        idx = json.load(open(cache))
    changed = False
    for f in sorted(os.listdir(bgd)):
        if not f.lower().endswith((".webp", ".png", ".bmp", ".jpg")) or f in idx:
            continue
        try:
            im = Image.open(os.path.join(bgd, f)).convert("RGB").resize((80, 60), Image.BILINEAR)
        except OSError:
            continue
        idx[f] = list(im.tobytes())
        changed = True
    if changed:
        os.makedirs(CACHE, exist_ok=True)
        json.dump(idx, open(cache, "w"))
    return bgd, idx


def pack_find(bgd, idx, img):
    ref = img.convert("RGB").resize((80, 60), Image.BILINEAR).tobytes()
    best, bestf = 1e9, None
    for f, thumb in idx.items():
        d = sum(abs(a - b) for a, b in zip(ref, thumb)) / len(ref)
        if d < best:
            best, bestf = d, f
    return (os.path.join(bgd, bestf), best) if best < PACK_MATCH else (None, best)


def sources(args):
    if args:
        v = [int(a, 0) for a in args]
        if len(v) % 3:
            sys.exit("give (stage room cam) triples")
        return [tuple(v[i:i + 3]) for i in range(0, len(v), 3)]
    out = []
    for line in open(LEVEL, encoding="utf-8"):
        m = re.match(r"\s*bgsrc\s+(\d+)\s+(\d+)\s+(\d+)", line)
        if m:
            out.append(tuple(int(g) for g in m.groups()))
    return out


def name(stage, room, cam):
    # The game's own background name (g_bgPathTemplate): rc<S><RR><C>, hex.
    return "rc%d%02x%x" % (stage, room, cam)


def decode(stage, room, cam):
    """The background as the game shows it: display_image's 555 conversion."""
    pak = None
    sdir = os.path.join(ROOT, "assets", "USA", "Stage%d" % stage)
    for f in os.listdir(sdir):
        if f.lower() == name(stage, room, cam) + ".pak":
            pak = os.path.join(sdir, f)
    if pak is None:
        sys.exit("no %s.pak in %s" % (name(stage, room, cam), sdir))
    tim = pak_view.PakDecoder(open(pak, "rb").read()).run()
    px = tim[0x14:0x14 + W * H * 2]
    img = Image.new("RGB", (W, H))
    data = []
    for i in range(W * H):
        v = px[i * 2] | (px[i * 2 + 1] << 8)
        data.append(((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                     ((v >> 10) & 31) * 255 // 31))
    img.putdata(data)
    return img


def main():
    pack, args = pack_args()
    os.makedirs(CACHE, exist_ok=True)
    bgd, idx = pack_index(pack) if pack else (None, {})
    exe = None
    for stage, room, cam in sources(args):
        n = name(stage, room, cam)
        src = os.path.join(CACHE, n + "_in.png")
        dst = os.path.join(CACHE, n + "_x4.png")
        img = decode(stage, room, cam)
        img.save(src)
        hit = None
        if idx:
            hit, score = pack_find(bgd, idx, img)
            print("%s: pack %s (difference %.1f)" % (n, os.path.basename(hit) if hit else "has no match", score))
        if hit:
            big = Image.open(hit).convert("RGBA")
        else:
            exe = exe or esrgan()
            subprocess.run([exe, "-i", src, "-o", dst, "-n", MODEL, "-s", "4"],
                           cwd=ESRGAN_DIR, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            big = Image.open(dst).convert("RGBA")
        blob = b"BGHD" + struct.pack("<II", big.width, big.height) + big.tobytes()
        for tree in TREES:
            d = os.path.join(tree, "USA", "Data", "bghd")
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, n + ".bin"), "wb") as f:
                f.write(blob)
        print("%s: %dx%d -> USA/Data/bghd/%s.bin in %d trees"
              % (n, big.width, big.height, n, len(TREES)))


if __name__ == "__main__":
    main()
