#!/usr/bin/env python3
"""
CUSTOM: the RAID bathroom's PAINTED materials - the ones no background has a
clean patch of (brass, mirror, towel cloth, the rug, porcelain, the lamp's
glass, iron, soap, nickel, a bottle, amber glass). Drawn here from noise and a seed, so they are this
project's own and reproducible, and tracked:

    portdata/USA/Data/raidtex/t<n>.bin   'RTEX' + u32 w + u32 h + RGBA

t1..t6, t10 and t15..t21 are cut out of the game's own pictures by
tools/build_raid_textures.py and are not tracked.

    python tools/build_raid_materials.py && python tools/deploy_portdata.py
"""

import os, random, struct
from PIL import Image, ImageDraw, ImageFilter, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "portdata", "USA", "Data", "raidtex")


def noise(w, h, sigma, blur=0.0, seed=0, stretch=None):
    """Grey noise around 128. `stretch` (sx, sy) draws it small and pulls it
    out - streaks along whichever axis is stretched."""
    random.seed(seed)
    sw, sh = (w, h) if stretch is None else (max(1, w // stretch[0]), max(1, h // stretch[1]))
    im = Image.new("L", (sw, sh))
    im.putdata([max(0, min(255, int(random.gauss(128, sigma)))) for _ in range(sw * sh)])
    if stretch is not None:
        im = im.resize((w, h), Image.BICUBIC)
    if blur > 0:
        im = im.filter(ImageFilter.GaussianBlur(blur))
    return im


def tint(grey, lo, hi):
    """Map a grey image onto the line from colour lo (0) to hi (255)."""
    lut = []
    for c in range(3):
        lut += [int(lo[c] + (hi[c] - lo[c]) * i / 255) for i in range(256)]
    return Image.merge("RGB", (grey, grey, grey)).point(lut)


def wrap_blur(im, r):
    """A blur that wraps round the edges, so the result still tiles."""
    w, h = im.size
    big = Image.new(im.mode, (w * 3, h * 3))
    for i in range(3):
        for j in range(3):
            big.paste(im, (i * w, j * h))
    return big.filter(ImageFilter.GaussianBlur(r)).crop((w, h, 2 * w, 2 * h))


def save(n, img):
    img = img.convert("RGBA")
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "t%d.bin" % n), "wb") as fh:
        fh.write(b"RTEX" + struct.pack("<II", img.width, img.height) + img.tobytes())
    print("t%d: %dx%d" % (n, img.width, img.height))


def brass():
    g = wrap_blur(noise(128, 128, 40, seed=7, stretch=(1, 16)), 0.6)
    spots = wrap_blur(noise(128, 128, 60, seed=8), 3)
    spots = spots.point(lambda v: 255 if v < 112 else 0)
    im = tint(g, (110, 78, 34), (214, 176, 98))
    dark = tint(g, (60, 52, 34), (96, 86, 58))                # tarnish
    return Image.composite(dark, im, wrap_blur(spots, 2))


def terry():
    g = wrap_blur(noise(128, 128, 50, seed=21), 0.7)
    im = tint(g, (176, 170, 156), (238, 234, 222))
    d = ImageDraw.Draw(im, "RGBA")
    for y in (18, 26):                                        # a woven band
        d.rectangle((0, y, 127, y + 4), fill=(92, 108, 140, 150))
        d.rectangle((0, y + 64, 127, y + 68), fill=(92, 108, 140, 150))
    return im


def nickel():
    """The bath's and basin's taps: worn nickel, pale grey, as camera 0 sees
    the long spout."""
    g = wrap_blur(noise(128, 128, 30, seed=81, stretch=(1, 12)), 0.6)
    spots = wrap_blur(noise(128, 128, 60, seed=82), 3).point(lambda v: 255 if v < 108 else 0)
    im = tint(g, (96, 98, 92), (196, 198, 188))
    return Image.composite(tint(g, (70, 60, 44), (110, 96, 70)), im, wrap_blur(spots, 2))


def bottle():
    """A white glass bottle with a paper label band; its v runs down it."""
    g = wrap_blur(noise(64, 128, 10, seed=91), 2)
    im = tint(g, (206, 204, 194), (238, 236, 228))
    d = ImageDraw.Draw(im, "RGBA")
    d.rectangle((0, 58, 63, 92), fill=(196, 182, 150, 255))         # the label
    d.line((0, 66, 63, 66), fill=(70, 60, 50, 200), width=2)
    d.line((0, 80, 63, 80), fill=(70, 60, 50, 160), width=1)
    return im


def amber():
    """Brown glass, with the rust of the lid's ring a little darker."""
    g = wrap_blur(noise(64, 64, 16, seed=101), 3)
    return tint(g, (58, 30, 10), (132, 74, 30))


def porcelain():
    g = wrap_blur(noise(128, 128, 14, seed=41), 2)
    im = tint(g, (196, 196, 186), (240, 238, 228))
    d = ImageDraw.Draw(im, "RGBA")
    random.seed(42)
    for _ in range(14):                                       # crazing
        x, y = random.randrange(128), random.randrange(128)
        for _ in range(4):
            nx, ny = x + random.randint(-14, 14), y + random.randint(-14, 14)
            d.line((x, y, nx, ny), fill=(120, 116, 100, 70))
            x, y = nx, ny
    return im


def iron():
    g = wrap_blur(noise(128, 128, 40, seed=61), 1)
    rust = wrap_blur(noise(128, 128, 60, seed=62), 4).point(lambda v: 255 if v > 150 else 0)
    im = tint(g, (34, 34, 36), (84, 82, 80))
    return Image.composite(tint(g, (70, 38, 22), (120, 66, 36)), im, wrap_blur(rust, 2))


def soap():
    g = wrap_blur(noise(64, 64, 12, seed=71), 2)
    return tint(g, (176, 168, 112), (226, 216, 160))


if __name__ == "__main__":
    save(7, brass())
    save(9, terry())
    save(11, porcelain())
    save(13, iron())
    save(14, soap())
    save(23, nickel())
    save(24, bottle())
    save(28, amber())
