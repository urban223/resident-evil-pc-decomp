#!/usr/bin/env python3
"""
CUSTOM: the materials a modelled RAID room is built from, cut out of the
game's own pictures of that room.

The RAID bathroom (raid1.lvl) is a real 3D room - boxes and low-poly models -
and the original backgrounds are only its REFERENCE. Its surfaces are covered
with tiled materials (RaidArena.cpp, "MATERIALS"), and those materials come
from the backgrounds: a patch of wall boards, of floor tiles, of enamel, of the
dark wood rail, and the door. Each patch is

  - taken from the camera that sees it most squarely, through the projection
    the game itself uses (the RDT camera; RaidArena / fit scripts agree with
    it to the pixel on camera 0),
  - straightened: the patch is defined as a rectangle IN THE WORLD (on a wall
    or on the floor), and its four corners are projected into the picture, so
    the result looks as if photographed head-on,
  - made seamless where it repeats (the wrap seam cross-faded with the
    opposite edge), and written as
        USA/Data/raidtex/t<n>.bin   'RTEX' + u32 w + u32 h + RGBA
    into the three asset trees.

The pixels are Capcom's, so the output is NOT tracked (docs/ASSETS.md). The
sources are the high-resolution backgrounds tools/build_bg_hd.py prepares in
tools/.cache (rc4070/rc4071 at 4x), and - for camera 0, when given - the
RE-ENHANCE pack's own repaint of it, which is much the better picture:

    python tools/build_raid_textures.py [--pack C:/path/to/RE-ENHANCE_RE1_v2.0]
"""

import os, struct, sys, math
from PIL import Image, ImageFilter, ImageEnhance

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", ".cache")
TREES = [os.path.join(ROOT, "assets"), os.path.join(ROOT, "bin", "Debug"),
         os.path.join(ROOT, "bin", "Release")]
RDT = os.path.join(ROOT, "assets", "USA", "Stage4", "ROOM4070.RDT")
PACK_CAM0 = "7C41E729.webp"     # RE-ENHANCE's rc4070 (found by tools/build_bg_hd.py --pack)


# ----------------------------------------------------------------- cameras

def cameras():
    d = open(RDT, "rb").read()
    return [struct.unpack_from("<11i", d, 0x94 + i * 0x2C) for i in range(d[1])]


def view(c):
    fx, fy, fz, tx, ty, tz = c[2:8]
    f = c[10]
    dx, dy, dz = tx - fx, ty - fy, tz - fz
    L = math.sqrt(dx * dx + dy * dy + dz * dz)
    h = math.sqrt(dx * dx + dz * dz)
    n = (dx / L, dy / L, dz / L)
    r = (dz / h, 0.0, -dx / h)
    u = (n[1] * r[2] - n[2] * r[1], n[2] * r[0] - n[0] * r[2], n[0] * r[1] - n[1] * r[0])
    return (fx, fy, fz, n, r, u, f)


def project(V, p, scale):
    """The game's projection (MatrixToCamera, centre 160,120), in picture pixels."""
    fx, fy, fz, n, r, u, f = V
    d = (p[0] - fx, p[1] - fy, p[2] - fz)
    vz = sum(n[i] * d[i] for i in range(3))
    sx = 160 + sum(r[i] * d[i] for i in range(3)) * f / vz
    sy = 120 + sum(u[i] * d[i] for i in range(3)) * f / vz
    return sx * scale, sy * scale


# ----------------------------------------------------------------- patches

def rectify(img, quad, w, h):
    """The picture inside `quad` (tl, bl, br, tr) as a w x h image."""
    flat = [c for pt in quad for c in pt]
    return img.transform((w, h), Image.QUAD, flat, resample=Image.BICUBIC)


def world_quad(V, scale, o, du, dv):
    """A world rectangle o + s*du + t*dv, s,t in 0..1, as a picture quad."""
    def p(s, t):
        return project(V, (o[0] + du[0] * s + dv[0] * t,
                           o[1] + du[1] * s + dv[1] * t,
                           o[2] + du[2] * s + dv[2] * t), scale)
    return [p(0, 0), p(0, 1), p(1, 1), p(1, 0)]


def seamless(img, frac=0.18):
    """Cross-fade each edge into the opposite one so the image repeats."""
    w, h = img.size
    px = img.load()
    out = img.copy()
    po = out.load()
    bw, bh = max(1, int(w * frac)), max(1, int(h * frac))
    for y in range(h):
        for i in range(bw):
            a = i / bw                      # 0 at the edge -> 1 inside
            l, r = px[i, y], px[w - bw + i, y]
            po[i, y] = tuple(int(l[k] * (0.5 + 0.5 * a) + r[k] * (0.5 - 0.5 * a)) for k in range(3))
            po[w - 1 - i, y] = tuple(int(px[w - 1 - i, y][k] * (0.5 + 0.5 * a)
                                         + px[bw - 1 - i, y][k] * (0.5 - 0.5 * a)) for k in range(3))
    px = out.copy().load()
    for x in range(w):
        for i in range(bh):
            a = i / bh
            t, b = px[x, i], px[x, h - bh + i]
            po[x, i] = tuple(int(t[k] * (0.5 + 0.5 * a) + b[k] * (0.5 - 0.5 * a)) for k in range(3))
            po[x, h - 1 - i] = tuple(int(px[x, h - 1 - i][k] * (0.5 + 0.5 * a)
                                         + px[x, bh - 1 - i][k] * (0.5 - 0.5 * a)) for k in range(3))
    return out


def save(n, img):
    img = img.convert("RGBA")
    blob = b"RTEX" + struct.pack("<II", img.width, img.height) + img.tobytes()
    for tree in TREES:
        d = os.path.join(tree, "USA", "Data", "raidtex")
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, "t%d.bin" % n), "wb").write(blob)
    img.convert("RGB").save(os.path.join(CACHE, "t%d.png" % n))
    print("t%d: %dx%d" % (n, img.width, img.height))


def main():
    args = sys.argv[1:]
    pack = args[args.index("--pack") + 1] if "--pack" in args else None
    cams = cameras()
    V0, V1 = view(cams[0]), view(cams[1])
    p0 = os.path.join(pack, "hires", "bgd", PACK_CAM0) if pack else None
    cam0 = Image.open(p0 if p0 and os.path.exists(p0) else os.path.join(CACHE, "rc4070_x4.png")).convert("RGB")
    cam1 = Image.open(os.path.join(CACHE, "rc4071_x4.png")).convert("RGB")
    S0, S1 = cam0.width / 320.0, cam1.width / 320.0

    # t1 - wall boards: the near wall, head-on from camera 1, above the rail
    # and clear of the shelves and the cistern.
    wood = rectify(cam1, world_quad(V1, S1, (4300, -3300, 10818), (1600, 0, 0), (0, 1500, 0)), 512, 480)
    save(1, seamless(wood))

    # t2 - floor tiles: camera 0 looks down on the floor; a patch clear of
    # the rug and the towel rack's feet.
    floor = rectify(cam0, world_quad(V0, S0, (5300, 0, 6450), (1600, 0, 0), (0, 0, 1600)), 512, 512)
    save(2, seamless(floor))

    # t3 - enamel: the lit floor of the bath, seen from above by camera 0 -
    # pale green with rust, the colour the bath, basin and pan all share.
    # (The bath's FRONT, from camera 1, is in its own shadow: black.)
    # A clean stretch of it on the 1280 picture, clear of the towel's shadow
    # and the spout: (700..830, 745..800).
    enamel = cam0.crop((int(175 * S0), int(186.25 * S0), int(207.5 * S0), int(200 * S0))).resize((256, 108), Image.BICUBIC)
    save(3, seamless(enamel, 0.3))

    # t4 - the dark wood rail: the near wall's wainscot band from camera 1.
    # Between the bath's end (x 5200) and the toilet (6660): the bath hides
    # the rail anywhere further left.
    rail = rectify(cam1, world_quad(V1, S1, (5300, -1700, 10818), (1300, 0, 0), (0, 500, 0)), 512, 196)
    save(4, seamless(rail))

    # t6 - the towel: camera 0's view of the one on the rack.
    towel = rectify(cam0, world_quad(V0, S0, (6100, -950, 5480), (1150, 0, 0), (0, 0, 260)), 256, 64)
    save(6, seamless(towel, 0.25))

    # ---- The rest, MEASURED: every rectangle below was found by casting rays
    # through camera 0's picture (points picked on the 1280 x 960 repaint)
    # onto the floor and wall planes, and is the same rectangle raid1.lvl
    # builds - so each picture lands on its own surface. Each is extracted
    # along the world axes the arena maps a face by (u with +X, or +Z on an
    # X wall; v down, or with +Z on a floor), from the face's low corner.
    P = lambda x, y: (x * S0 / 4.0, y * S0 / 4.0)

    # t5 again - the door: the leaf is 1180 x 3195 (x 2407..3586), not the
    # 1450 x 2392 guessed before, which is what skewed the old picture.
    door = rectify(cam0, world_quad(V0, S0, (2407, -3195, 5500), (1180, 0, 0), (0, 3195, 0)), 320, 866)
    save(5, door)

    # t10 - the bath mat, by its own corners (it lies a few degrees askew).
    # Its box's low X / low Z corner is the picture's top right.
    mat = rectify(cam0, [P(940, 465), P(955, 590), P(680, 615), P(665, 475)], 384, 188)
    save(10, mat)

    # t15 - the mat before the pan: only its far strip is not under the lid.
    tmat = rectify(cam0, world_quad(V0, S0, (6800, -5, 8540), (950, 0, 0), (0, 0, 360)), 256, 96)
    save(15, seamless(tmat, 0.25))

    # t16 - the stained green plaster under the rail; t17 - the skirting.
    # Both off the far wall between the door and the towel rack.
    plaster = rectify(cam0, world_quad(V0, S0, (3750, -1140, 5454), (2200, 0, 0), (0, 640, 0)), 512, 150)
    save(16, seamless(plaster, 0.12))
    skirt = rectify(cam0, world_quad(V0, S0, (3750, -470, 5454), (2200, 0, 0), (0, 470, 0)), 512, 110)
    save(17, seamless(skirt, 0.12))

    # t18 - the bath's outside, which is dark: camera 1 sees its long side.
    outside = rectify(cam1, world_quad(V1, S1, (2700, -1000, 9040), (900, 0, 0), (0, 800, 0)), 256, 228)
    save(18, seamless(outside, 0.25))

    # t19 - the cistern box's top: grey painted wood.
    cist = rectify(cam0, world_quad(V0, S0, (6680, -1800, 10160), (1200, 0, 0), (0, 0, 570)), 384, 182)
    save(19, seamless(cist, 0.2))

    # t20 - the rusted lid of the pan; t21 - the towel over the bath.
    lid = cam0.crop(tuple(int(c) for c in P(300, 630) + P(366, 716))).resize((128, 128), Image.BICUBIC)
    save(20, seamless(lid, 0.3))
    btowel = cam0.crop(tuple(int(c) for c in P(700, 650) + P(836, 690))).resize((256, 76), Image.BICUBIC)
    save(21, seamless(btowel, 0.2))


if __name__ == "__main__":
    main()
