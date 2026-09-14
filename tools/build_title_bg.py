#!/usr/bin/env python3
"""
CUSTOM: the title screen's background and its wordmark, baked as two files.

The stock title.pix is one picture: the RESIDENT EVIL wordmark burnt into an
eye.  Burnt in, the two cannot be separated - change the picture and the name
goes with it.  So they are split here.

  data\\titlebg.pix    the new backdrop, 320x240 ABGR1555, the format
                       display_image() already reads.
  data\\titlelogo.bin  the wordmark cut out of the ORIGINAL title.pix as
                       straight RGBA, drawn over the backdrop as its own
                       sprite (see title_draw_logo in TitleScreen.cpp).

The wordmark is the game's own art, moved - not redrawn.  Nothing here invents
a logo; the matte below only separates the pixels that are already in the
player's title.pix from the eye behind them.

The backdrop is RC1121.PIX, the mansion's four-poster bedroom, graded for the
job: a lit room is a busy room, and a wordmark needs somewhere quiet to sit.
"""

import os, struct
import numpy as np
from PIL import Image, ImageFilter

W, H = 320, 240

# ---------------------------------------------------------------- pix codec

def load_pix(path):
    d = np.fromfile(path, dtype='<u2')[:W * H].reshape(H, W)
    r = (d & 0x1F); g = ((d >> 5) & 0x1F); b = ((d >> 10) & 0x1F)
    return (np.stack([r, g, b], -1).astype(np.float32) * (255.0 / 31.0))

def save_pix(rgb, path):
    # 5 bits per channel is eight steps of 8, and a dark graded picture is
    # mostly gradient - quantised straight it bands into visible plates.  A
    # half-step of ordered noise before rounding trades that for grain, which
    # on this screen is the right currency.
    v = rgb * (31.0 / 255.0)
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6],
                      [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 - 0.5
    v = v + np.tile(bayer, (v.shape[0] // 4 + 1, v.shape[1] // 4 + 1))[
        :v.shape[0], :v.shape[1]][..., None]
    q = np.clip(np.rint(v), 0, 31).astype(np.uint16)
    d = q[..., 0] | (q[..., 1] << 5) | (q[..., 2] << 10) | 0x8000
    d.astype('<u2').tofile(path)

# ------------------------------------------------------------------ helpers

def blur(a, r):
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r))).astype(np.float32)

def gray(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114

# ------------------------------------------------------------------ backdrop

def build_backdrop(src):
    a = load_pix(src)

    # The room is lit warm-green by the renderer's own palette.  Pull most of
    # the saturation out and put the rest back as a cold cast: the wordmark is
    # the only red allowed on this screen, so nothing under it may compete.
    lum = gray(a)[..., None]
    a = lum * 0.80 + a * 0.20
    a = a * np.array([0.82, 0.90, 1.18], np.float32)

    # Down, hard.  display_image writes the framebuffer the sprites composite
    # over, so every stop left in the backdrop is a stop the lettering has to
    # beat.
    a *= 0.60

    # Lift the deep end off pure black so the room still reads as a room
    # rather than a silhouette, and crush the highlights that survive.
    a = 9.0 + a * 0.92
    a = 255.0 * np.power(np.clip(a / 255.0, 0, 1), 1.10)

    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    nx = (xx - W * 0.5) / (W * 0.5)
    ny = (yy - H * 0.5) / (H * 0.5)

    # Vignette: an oval, soft, biting hardest in the corners.
    rr = np.sqrt(nx * nx * 0.86 + ny * ny)
    a *= np.clip(1.06 - 0.62 * np.clip(rr - 0.30, 0, None) ** 1.5, 0.20, 1.0)[..., None]

    # The floor takes up the bottom third and its pattern is the busiest thing
    # in the frame.  A scrim rising from the bottom edge puts it away without
    # cropping it out - the room keeps its depth, the pattern stops shouting.
    floor = np.clip((yy - H * 0.52) / (H * 0.48), 0, 1) ** 1.4
    a *= (1.0 - 0.42 * floor)[..., None]

    # And a band across the middle, where the wordmark lands (y 76..168).
    band = np.exp(-((yy - 122.0) / 54.0) ** 2)
    a *= (1.0 - 0.26 * band)[..., None]

    # A breath of bloom off what is still bright - the canopy and the doorway -
    # so the darkening does not read as a flat curve pulled over a photograph.
    a = a + blur(a, 7.0) * 0.16

    return np.clip(a, 0, 255)

# ------------------------------------------------------------------ wordmark

LOGO_BOX = (14, 76, 316, 168)          # measured off title.pix

def build_logo(src):
    from scipy.ndimage import distance_transform_edt

    full = load_pix(src)
    x0, y0, x1, y1 = LOGO_BOX
    a = full[y0:y1, x0:x1]
    r, g, b = a[..., 0], a[..., 1], a[..., 2]

    # The lettering is the only strongly red thing in the picture; the eye
    # behind it is blue-grey.  Redness alone separates them, and the soft
    # ramp keeps the original's antialiased edges instead of stair-stepping
    # them.
    red = r - np.maximum(g, b)
    alpha = np.clip((red - 14.0) / 46.0, 0, 1)

    # Edge pixels are the letter blended with the eye, so their colour carries
    # the eye in it.  Take the colour from the nearest fully-inked pixel
    # instead and let alpha do the blending against whatever is behind it now.
    core = red > 50.0
    idx = distance_transform_edt(~core, return_distances=False,
                                 return_indices=True)
    col = a[idx[0], idx[1]]

    rgba = np.zeros(a.shape[:2] + (4,), np.float32)
    rgba[..., :3] = col
    rgba[..., 3] = alpha * 255.0
    return np.clip(rgba, 0, 255).astype(np.uint8)

# ---------------------------------------------------------------------- main

TREES = ["assets/USA", "bin/Debug/USA", "bin/Release/USA"]

def main():
    root = os.environ.get("RE_ROOT", ".")
    data = os.path.join(root, "assets", "USA", "Data")

    bg = build_backdrop(os.path.join(data, "RC1121.PIX"))
    logo = build_logo(os.path.join(data, "title.pix"))
    h, w = logo.shape[:2]
    blob = b"RLG1" + struct.pack("<II", w, h) + logo.tobytes()

    for t in TREES:
        d = os.path.join(root, t, "Data")
        if not os.path.isdir(d):
            continue
        save_pix(bg, os.path.join(d, "titlebg.pix"))
        with open(os.path.join(d, "titlelogo.bin"), "wb") as f:
            f.write(blob)
        print("wrote", d)

    print("logo %dx%d  bg mean %.1f" % (w, h, bg.mean()))

if __name__ == "__main__":
    main()
