#!/usr/bin/env python3
"""build_raid_eye.py - the EXTRA (RAID) screen's backdrop.

The eye, whole, from the game's own opening.

title.pix cannot give it: the RESIDENT EVIL logo is baked across the middle of
that file (x 24..307, y 85..155, measured off it) with a shadow under every
stroke, and the strokes are wide enough that there is no reconstructing what is
behind them. What the title screen shows is the eye with the logo ON it.

ou.avi is the same shot before the logo arrives. Its frame at 6.9 s is the eye
open, clean, lashes and catchlights and all - measured by counting red pixels
across the sequence, which go 0 until 5.5 s and climb from there.

The frame is cropped with the PUPIL ABOVE CENTRE, dimmed, vignetted and given
its scanlines here rather than at runtime: the eyeball does not move, and a
static effect belongs in the static layer.

It comes out as TWO layers, because the eye has to look around: the eyeball
holds still and the iris moves on it, which is what an eye does - sliding the
whole picture instead just looks like the camera drifting. So the iris is cut
out as its own sprite and the hole it leaves is filled by extending the sclera
inwards along the radius; only a few pixels of that fill are ever uncovered, and
only on the side the iris moves away from.

Output: assets/USA/Data/raideye.bin  =  'REY2', base w/h, iris w/h, the iris's
home position in the base, then both layers as RGBA rows - which is what
MarniCreateTexture takes unchanged (bpp 32 is memcpy'd into an R8G8B8A8 texture,
so the file's byte order IS the texture's).

Run: python3 tools/build_raid_eye.py
"""

import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageFilter

OUT_W, OUT_H = 320, 240       # the screen itself: the eyeball does not move
FRAME_AT = 6.9                # seconds into ou.avi - the last clean frame
PUPIL_Y = 0.38                # where the pupil sits, as a fraction of height
ZOOM = 1.10                   # how much of the frame the window covers
DIM = 0.72
IRIS_TRAVEL = 14.0            # must cover the largest offset in s_titleEyeLook
SCANLINE = 0.80


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def grab_frame(avi, at):
    if not os.path.isfile(avi):
        raise SystemExit("not found: %s" % avi)
    tmp = os.path.join(tempfile.gettempdir(), "raideye_src.png")
    subprocess.check_call(["ffmpeg", "-y", "-v", "error", "-ss", str(at),
                           "-i", avi, "-frames:v", "1", tmp])
    im = Image.open(tmp).convert("RGB")
    im.load()
    os.unlink(tmp)
    return im


def find_pupil(im):
    """The pupil is the big dark disc inside the bright sclera.

    Found rather than hardcoded so that changing FRAME_AT cannot silently put
    the framing somewhere else: threshold dark, keep what is surrounded by
    bright, take the centroid of the largest such region near the middle.
    """
    a = np.asarray(im).astype(float).mean(axis=2)
    dark = a < np.percentile(a, 35)
    bright = a > np.percentile(a, 88)

    # the sclera's bounding box tells us where the eye is at all
    ys, xs = np.nonzero(bright)
    if len(xs) < 50:
        return im.size[0] // 2, im.size[1] // 2
    x0, x1 = np.percentile(xs, 5), np.percentile(xs, 95)
    y0, y1 = np.percentile(ys, 5), np.percentile(ys, 95)

    m = np.zeros_like(dark)
    m[int(y0):int(y1), int(x0):int(x1)] = dark[int(y0):int(y1), int(x0):int(x1)]
    ys, xs = np.nonzero(m)
    if len(xs) < 50:
        return int((x0 + x1) / 2), int((y0 + y1) / 2)
    return int(xs.mean()), int(ys.mean())


def main():
    root = repo_root()
    src = grab_frame(os.path.join(root, "assets", "USA", "Movie", "ou.avi"),
                     FRAME_AT)
    px, py = find_pupil(src)
    print("pupil at (%d, %d) of %dx%d" % (px, py, src.size[0], src.size[1]))

    win_w = src.size[0] / ZOOM
    win_h = win_w * OUT_H / OUT_W
    x0 = px - win_w / 2.0
    y0 = py - win_h * PUPIL_Y          # pupil above centre, by construction
    x0 = max(0.0, min(x0, src.size[0] - win_w))
    y0 = max(0.0, min(y0, src.size[1] - win_h))

    im = src.crop((int(x0), int(y0), int(x0 + win_w), int(y0 + win_h)))
    im = im.resize((OUT_W, OUT_H), Image.LANCZOS).filter(
        ImageFilter.GaussianBlur(0.6))

    a = np.asarray(im).astype(float)

    # --- find the iris in the FRAMED image ---------------------------------
    # After the crop, not before: one transform instead of two, and nothing to
    # keep in step when the framing changes.
    lum = a.mean(axis=2)
    dark = lum < np.percentile(lum, 40)
    yy0, xx0 = np.mgrid[0:OUT_H, 0:OUT_W]
    inner = ((xx0 > OUT_W * 0.15) & (xx0 < OUT_W * 0.85) &
             (yy0 > OUT_H * 0.15) & (yy0 < OUT_H * 0.85))
    ys, xs = np.nonzero(dark & inner)
    icx, icy = float(xs.mean()), float(ys.mean())
    # radius from the AREA, which is robust where a percentile of the radius is
    # not: lashes and the lid shadow are dark too, and they sit off to one side.
    irad = float(np.sqrt((dark & inner).sum() / np.pi))
    print("iris at (%.0f, %.0f) r %.0f" % (icx, icy, irad))

    rr = np.sqrt((xx0 - icx) ** 2 + (yy0 - icy) ** 2)

    # --- the hole: extend the sclera inwards along the radius --------------
    #
    # Filled WIDER than the iris by the distance it can ever travel, not merely
    # to its own edge. Filling to the edge leaves the original iris rim sitting
    # in the base just outside it, and the moment the sprite slides off that
    # rim, an arc of the old circle stands there on the sclera - visible up and
    # to the right, which is exactly where the first look in the table goes.
    base = a.copy()
    edge = irad + 2.0 + IRIS_TRAVEL
    inside = rr < edge
    ys, xs = np.nonzero(inside)
    for y, x in zip(ys, xs):
        d = max(rr[y, x], 1e-3)
        sx = int(round(icx + (x - icx) * (edge + 3.0) / d))
        sy = int(round(icy + (y - icy) * (edge + 3.0) / d))
        sx = min(max(sx, 0), OUT_W - 1)
        sy = min(max(sy, 0), OUT_H - 1)
        base[y, x] = a[sy, sx]

    # Blur the fill, and only the fill.
    #
    # Extending along the radius works where there is sclera to extend FROM. At
    # the bottom there is not: the iris meets the lower lid, so the ray that
    # should pick up white picks up lashes and drags them inward as streaks -
    # which is what made the bottom of the eye look smeared. Blurring turns
    # those streaks into the soft gradient the sclera is anyway, and the iris
    # covers all of it bar a crescent.
    filled = np.zeros((OUT_H, OUT_W), dtype=float)
    filled[inside] = 1.0
    soft = np.asarray(
        Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).filter(
            ImageFilter.GaussianBlur(5.0))).astype(float)
    # feather the swap so the blurred patch does not show its own edge
    m = np.clip((edge - rr) / 6.0, 0.0, 1.0)[..., None]
    base = base * (1.0 - m) + soft * m

    # --- the iris sprite ---------------------------------------------------
    pad = 3
    half = int(np.ceil(irad)) + pad
    ix0, iy0 = int(round(icx)) - half, int(round(icy)) - half
    iw = ih = half * 2
    sub = np.zeros((ih, iw, 4), dtype=float)
    for y in range(ih):
        for x in range(iw):
            sy, sx = iy0 + y, ix0 + x
            if 0 <= sy < OUT_H and 0 <= sx < OUT_W:
                sub[y, x, :3] = a[sy, sx]
    dy2 = (np.arange(ih) - (ih - 1) / 2.0) ** 2
    dx2 = (np.arange(iw) - (iw - 1) / 2.0) ** 2
    rad = np.sqrt(dy2[:, None] + dx2[None, :])
    # feathered edge: a hard circle shows its own outline when it moves
    sub[..., 3] = np.clip((irad + 1.0 - rad) / 2.5, 0.0, 1.0) * 255.0

    # --- shading, the same for both layers ---------------------------------
    # The vignette is sampled at each layer's own HOME position, and the
    # scanlines are baked into both. The iris only ever moves an even number of
    # pixels (see TitleScreen.cpp), so its scanlines stay in phase with the
    # eyeball's - shift it an odd number and the two combs beat against each
    # other.
    def shade(img, ox, oy):
        out = img.copy()
        h, w = out.shape[:2]
        gy, gx = np.mgrid[0:h, 0:w]
        r = np.sqrt(((gx + ox - OUT_W / 2) / (OUT_W / 2)) ** 2 +
                    ((gy + oy - OUT_H / 2) / (OUT_H / 2)) ** 2)
        k = (np.clip(1.18 - 0.62 * r * r, 0.0, 1.0) * DIM)
        sl = np.ones(h)
        sl[(1 - oy % 2)::2] = SCANLINE
        out[..., :3] *= (k * sl[:, None])[..., None]
        return out

    base4 = np.concatenate([base, np.full((OUT_H, OUT_W, 1), 255.0)], axis=2)
    base4 = shade(base4, 0, 0)
    sub = shade(sub, ix0, iy0)

    def pack(x):
        return np.clip(x, 0, 255).astype(np.uint8).tobytes()

    blob = (b"REY2" + struct.pack("<IIIIii", OUT_W, OUT_H, iw, ih, ix0, iy0)
            + pack(base4) + pack(sub))
    rgba = np.clip(base4, 0, 255).astype(np.uint8)

    targets = [os.path.join(root, "assets", "USA", "Data", "raideye.bin")]
    for cfg in ("Debug", "Release"):
        d = os.path.join(root, "bin", cfg, "USA", "Data")
        if os.path.isdir(d):
            targets.append(os.path.join(d, "raideye.bin"))
    for path in targets:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fp:
            fp.write(blob)
        print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))

    Image.fromarray(rgba, "RGBA").convert("RGB").save(
        os.path.join(root, "raideye_preview.png"))


if __name__ == "__main__":
    main()
