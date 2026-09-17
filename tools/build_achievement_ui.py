#!/usr/bin/env python3
"""build_achievement_ui.py - bake the achievement toast's art into one atlas.

The achievement pop-up (src/game/Achievements.cpp) does not go through the PSX
VRAM-page emulation at all: it is a port-only overlay that uploads ONE RGBA
texture through MarniCreateTexture and draws it with MarniDrawSpriteEx. This
script builds that texture, plus the generated metrics header the C++ side
indexes it with.

Inputs (all from the Space GUI pack in assets/SpaceGUI/):
  sources/fonts/SairaCondensed-{Bold,SemiBold}.ttf
  sources/png/Misc/CornerBigTL.png      corner brackets (mirrored for the other three)
  sources/png/Panels/Frame.png          9-slice frame, used for the icon slot
  sources/png/Icons/128/*.png           the icon set (white silhouettes)

Outputs:
  assets/USA/Data/achvui.bin            'AUI1' + w + h + RGBA8 rows
  src/game/AchievementAtlasData.h       panel/icon/glyph rects, generated

Everything in the pack is white/greyscale art meant to be tinted at draw time,
so the atlas keeps it white and the runtime multiplies in the colour.

Run from the repo root:  python3 tools/build_achievement_ui.py
"""

import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

# --- atlas geometry ---------------------------------------------------------
ATLAS_W = 512
# 640, not 512: the two fonts fill the sheet to row 508 and the grain tile has
# to live somewhere. Nothing hardcodes the size - the blob carries it in its
# header and both loaders read ACHV_ATLAS_W/H from the generated header - so
# the sheet grows by a row band rather than by a power of two.
ATLAS_H = 640

# The panel is baked at 2x its design size; the runtime draws the atlas at
# (backbufferHeight / 480) * 0.5 pixels per atlas pixel, so 500x92 here is a
# 250x46 toast in 640x480 game terms.
PANEL_W = 500
PANEL_H = 92
PANEL_CUT = 18          # diagonal corner cut, top-left and bottom-right

# The panel is drawn as three horizontal slices - left cap, stretched middle,
# right cap - so the toast can open and close its width without distorting its
# corners (Achievements.cpp's expand/collapse animation). Both caps have to be
# wide enough to contain their cut corner and both of their brackets, and their
# sum is the width of the collapsed "icon only" capsule.
PANEL_CAP_L = 56
PANEL_CAP_R = 56

ICON_SIZE = 64
ICONS = [
    "Medal", "Skull", "Scroll", "Cup", "Star", "Crown", "Gun", "Key",
    "Heart", "Clock", "Target", "Flag1", "Check", "Stats", "Flame", "Cold",
    "Pencil", "Warning",
]

FONT_TITLE_PX = 24
FONT_BODY_PX = 17
FIRST_CHAR = 32
LAST_CHAR = 126

# --- colours (the pack's palette, read off the kit's own screens) -----------
FILL_TOP = (12, 42, 46)
FILL_BOTTOM = (5, 20, 24)
FILL_ALPHA = 236
STROKE = (46, 196, 182, 200)
BRACKET = (120, 240, 226, 255)


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def gui(*parts):
    return os.path.join(repo_root(), "assets", "SpaceGUI", "sources", *parts)


# ---------------------------------------------------------------------------
# panel
# ---------------------------------------------------------------------------
def build_panel():
    """The toast background: cut-corner plate, stroke, and four brackets."""
    w, h, cut = PANEL_W, PANEL_H, PANEL_CUT
    panel = Image.new("RGBA", (w, h), (0, 0, 0, 0))

    # The plate outline - top-left and bottom-right corners cut off, which is
    # the shape every dialog in the pack uses.
    poly = [
        (cut, 0), (w - 1, 0), (w - 1, h - 1 - cut),
        (w - 1 - cut, h - 1), (0, h - 1), (0, cut),
    ]

    # Vertical gradient, masked to the plate.
    grad = Image.new("RGBA", (w, h))
    gp = grad.load()
    for y in range(h):
        t = y / float(h - 1)
        r = int(FILL_TOP[0] + (FILL_BOTTOM[0] - FILL_TOP[0]) * t)
        g = int(FILL_TOP[1] + (FILL_BOTTOM[1] - FILL_TOP[1]) * t)
        b = int(FILL_TOP[2] + (FILL_BOTTOM[2] - FILL_TOP[2]) * t)
        for x in range(w):
            gp[x, y] = (r, g, b, FILL_ALPHA)

    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).polygon(poly, fill=255)
    panel.paste(grad, (0, 0), mask)

    # A soft cyan wash down the left edge, so that side reads as lit. It has to
    # stay inside the left cap: the middle slice is stretched to whatever width
    # the toast currently has, so anything that varies along x out there would
    # smear across the whole panel.
    wash = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    wd = wash.load()
    for y in range(h):
        for x in range(PANEL_CAP_L):
            t = 1.0 - (x / float(PANEL_CAP_L))
            a = int(30 * t * t)
            if a:
                wd[x, y] = (46, 196, 182, a)
    panel.alpha_composite(Image.composite(wash, Image.new("RGBA", (w, h)), mask))

    d = ImageDraw.Draw(panel)
    d.line(poly + [poly[0]], fill=STROKE, width=2)

    # Corner brackets: one asset, mirrored into the other three corners. The
    # two cut corners take their bracket further in so it sits on the straight
    # part of the edge instead of crossing the diagonal.
    src = Image.open(gui("png", "Misc", "CornerBigTL.png")).convert("RGBA")
    br = tint(src.resize((26, 26), Image.LANCZOS), BRACKET)
    inset = 6
    panel.alpha_composite(br, (inset + cut, inset))                            # TL (cut)
    panel.alpha_composite(br.transpose(Image.FLIP_LEFT_RIGHT),
                          (w - 26 - inset, inset))                             # TR
    panel.alpha_composite(br.transpose(Image.FLIP_TOP_BOTTOM),
                          (inset, h - 26 - inset))                             # BL
    panel.alpha_composite(br.transpose(Image.ROTATE_180),
                          (w - 26 - inset - cut, h - 26 - inset))              # BR (cut)
    return panel


def build_slot():
    """The icon slot: Frame.png 9-sliced out to 64x64."""
    src = Image.open(gui("png", "Panels", "Frame.png")).convert("RGBA")
    s = 8  # Frame.png is 24x24 - an 8px border with an 8px stretchable middle
    size = ICON_SIZE
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    boxes = {
        "tl": (0, 0, s, s), "tm": (s, 0, src.width - s, s), "tr": (src.width - s, 0, src.width, s),
        "ml": (0, s, s, src.height - s), "mm": (s, s, src.width - s, src.height - s),
        "mr": (src.width - s, s, src.width, src.height - s),
        "bl": (0, src.height - s, s, src.height), "bm": (s, src.height - s, src.width - s, src.height),
        "br": (src.width - s, src.height - s, src.width, src.height),
    }
    mid = size - 2 * s
    out.paste(src.crop(boxes["tl"]), (0, 0))
    out.paste(src.crop(boxes["tr"]), (size - s, 0))
    out.paste(src.crop(boxes["bl"]), (0, size - s))
    out.paste(src.crop(boxes["br"]), (size - s, size - s))
    out.paste(src.crop(boxes["tm"]).resize((mid, s)), (s, 0))
    out.paste(src.crop(boxes["bm"]).resize((mid, s)), (s, size - s))
    out.paste(src.crop(boxes["ml"]).resize((s, mid)), (0, s))
    out.paste(src.crop(boxes["mr"]).resize((s, mid)), (size - s, s))
    out.paste(src.crop(boxes["mm"]).resize((mid, mid)), (s, s))
    return out


def skin_marks():
    """Small shapes the status-screen skin needs beyond the toast's art.

    The skin is mostly lines, rectangles and text, and those are drawn with the
    atlas's white block and the baked fonts - no art required. Only these four
    shapes cannot be built out of axis-aligned quads, so they get baked:
    a filled diamond and a ring for the pip rows, a triangle for the selected
    cell's side markers, a one-dimensional falloff used as the glint's tail and
    as the soft edge of a glow, and the empty-slot cross.
    """
    out = []

    d_sz = 14
    dia = Image.new("RGBA", (d_sz, d_sz), (0, 0, 0, 0))
    ImageDraw.Draw(dia).polygon(
        [(d_sz // 2, 0), (d_sz - 1, d_sz // 2), (d_sz // 2, d_sz - 1), (0, d_sz // 2)],
        fill=(255, 255, 255, 255))
    out.append(("diamond", dia))

    ring = Image.new("RGBA", (d_sz, d_sz), (0, 0, 0, 0))
    ImageDraw.Draw(ring).ellipse([0, 0, d_sz - 1, d_sz - 1],
                                 outline=(255, 255, 255, 255), width=2)
    out.append(("ring", ring))

    t_w, t_h = 12, 10
    tri = Image.new("RGBA", (t_w, t_h), (0, 0, 0, 0))
    ImageDraw.Draw(tri).polygon([(0, 0), (t_w - 1, 0), (t_w // 2, t_h - 1)],
                                fill=(255, 255, 255, 255))
    out.append(("triangle", tri))

    # 64x4 horizontal falloff, opaque at the left edge, gone at the right
    fall = Image.new("RGBA", (64, 4), (0, 0, 0, 0))
    fp = fall.load()
    for x in range(64):
        a = int(255 * (1.0 - x / 63.0) ** 2)
        for y in range(4):
            fp[x, y] = (255, 255, 255, a)
    out.append(("falloff", fall))

    # the empty-slot X. Stepping a diagonal out of quads costs dozens of
    # sprites per cell and there are ten empty cells on Jill's screen, so the
    # cross is baked once and drawn as one quad.
    c_sz = 16
    cross = Image.new("RGBA", (c_sz, c_sz), (0, 0, 0, 0))
    cd = ImageDraw.Draw(cross)
    cd.line([(1, 1), (c_sz - 2, c_sz - 2)], fill=(255, 255, 255, 255), width=2)
    cd.line([(c_sz - 2, 1), (1, c_sz - 2)], fill=(255, 255, 255, 255), width=2)
    out.append(("cross", cross))

    # The item action menu's row icons, 10x10. RE2 Remake puts a small glyph
    # left of each option and it is most of what makes that menu read at a
    # glance. At this size PIL's primitives blur into mush, so the four are
    # hand-plotted pixel by pixel - the same way the game's own 8x14 font is.
    def plot(rows):
        im = Image.new("RGBA", (10, 10), (0, 0, 0, 0))
        px = im.load()
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch == "#":
                    px[x, y] = (255, 255, 255, 255)
        return im

    # USE: an arrow coming down onto a surface - apply, consume.
    out.append(("iconuse", plot([
        "....##....",
        "....##....",
        "....##....",
        "....##....",
        ".########.",
        "..######..",
        "...####...",
        "....##....",
        "..........",
        "##########",
    ])))

    # EQUIP: a crosshair\.
    out.append(("iconequip", plot([
        "....##....",
        "..######..",
        ".##....##.",
        ".#......#.",
        "##..##..##",
        "##..##..##",
        ".#......#.",
        ".##....##.",
        "..######..",
        "....##....",
    ])))

    # CHECK: a magnifier\.
    out.append(("iconcheck", plot([
        ".####.....",
        "##..##....",
        "#....#....",
        "#....#....",
        "##..##....",
        ".####.....",
        "...###....",
        "....###...",
        ".....###..",
        "......##..",
    ])))

    # COMBINE: two boxes overlapping - the two items becoming one\. A cog, the
    # remake's glyph for this, has no readable form at ten pixels\.
    out.append(("iconcombine", plot([
        "#####.....",
        "#...#.....",
        "#...#.....",
        "#...#.....",
        "#########.",
        "....#...#.",
        "....#...#.",
        "....#...#.",
        "....#####.",
        "..........",
    ])))
    return out


# A plain bold sans for the two title-menu words the game has no art for. The
# original's own lettering is an ordinary grotesque; at an 11-pixel cap height
# the difference between one and another is a pixel here or there.
GUI_FALLBACK_SANS = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"


def _decode_tim4(path):
    """Minimal 4bpp CLUT TIM reader - enough for the title text sheets."""
    d = open(path, "rb").read()
    off = 8                                    # magic + flags
    csz, cx, cy, cw, ch = struct.unpack("<IHHHH", d[off:off + 12])
    pal_raw = d[off + 12:off + 12 + cw * ch * 2]
    off += csz
    isz, ix, iy, iw, ih = struct.unpack("<IHHHH", d[off:off + 12])
    off += 12
    px = d[off:off + isz - 12]
    pal = []
    for i in range(cw * ch):
        v = struct.unpack("<H", pal_raw[i * 2:i * 2 + 2])[0]
        pal.append((((v & 31) << 3), (((v >> 5) & 31) << 3), (((v >> 10) & 31) << 3),
                    0 if v == 0 else 255))
    w, h = iw * 4, ih
    im = Image.new("RGBA", (w, h))
    o = im.load()
    for y in range(h):
        base = y * iw * 2
        for x in range(0, w, 4):
            unit = px[base + (x >> 1)] | (px[base + (x >> 1) + 1] << 8)
            for k in range(4):
                o[x + k, y] = pal[(unit >> (4 * k)) & 0xF]
    return im


def native_title_words(root):
    """NEW GAME and LOAD GAME, lifted pixel for pixel out of the game's own art.

    The sheet bakes both words into one 256-wide block per highlight state,
    together with the copyright lines, so there is no way to place them
    individually from the game's texture page. Cropping them into this atlas
    instead gives the real pixels AND control over where each one goes - and it
    puts all four menu words through a single draw path, which is what keeps
    them looking like one menu. The two sheets (pad / no pad) differ only in
    their top line, so either is a valid source for these.
    """
    sheet = os.path.join(root, "assets", "USA", "Data", "t_start.tim")
    if not os.path.isfile(sheet):
        return []
    im = _decode_tim4(sheet)

    def normalise(crop):
        # The sheet inks these words in exactly two greys - 112 solid and 56
        # for the antialiased edge - so the shape converts losslessly to white
        # with the grey carried as alpha. That matters because the draw applies
        # a colour MULTIPLY: left at 112 the words could only ever be dimmed,
        # never lit, and the two baked ones beside them would not match.
        out = Image.new("RGBA", crop.size, (0, 0, 0, 0))
        src, dst = crop.load(), out.load()
        for y in range(crop.size[1]):
            for x in range(crop.size[0]):
                r, g, b, a = src[x, y]
                if a == 0:
                    continue
                dst[x, y] = (255, 255, 255, min(255, r * 255 // 112))
        return out

    return [
        ("wordnew",  normalise(im.crop((77, 84, 182, 96)))),     # NEW GAME
        ("wordload", normalise(im.crop((72, 194, 185, 206)))),   # LOAD GAME
        # The copyright lines are baked into the same block as the menu words.
        # Replacing that block with our own list would drop them, so they are
        # lifted out too and drawn on their own, centred at the bottom.
        ("wordcopy", normalise(im.crop((0, 131, 256, 152)))),
    ]


def title_words():
    """The two title-menu options RE1 has no art for.

    t_start.tim / t_press.tim carry only NEW GAME and LOAD GAME, and each is
    baked into a 256-wide block together with the copyright lines - there is no
    EXTRA and no QUIT anywhere in the game's data. These two are lettered to sit
    beside the originals: Liberation Sans Bold at 15px gives the same 11-pixel
    cap height, and the tracking is matched to the native word's own (105 px
    across "NEW GAME"). They are drawn point-sampled at 1:1, like the sheet
    they stand next to, so the two sources read as one typeface.
    """
    font = ImageFont.truetype(GUI_FALLBACK_SANS, 15)

    # Everything is measured from the top of a flat capital. The native crops
    # put the cap line on row 0, so matching that is what lines the four words
    # up on one baseline even though their boxes differ in height.
    cap_top = font.getbbox("X")[1]

    out = []
    for name, text, track in (("wordextra", "EXTRA", 4), ("wordquit", "QUIT", 4)):
        boxes = [font.getbbox(c) for c in text]
        w = sum(b[2] - b[0] + track for b in boxes) - track
        # Tall enough for the lowest ink in the word: Q's tail drops below the
        # baseline, and a box sized to the capitals simply cut it off - which is
        # exactly how the letter came out looking wrong.
        h = max(b[3] for b in boxes) - cap_top + 1
        im = Image.new("RGBA", (w + 4, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        x = 2
        for c, b in zip(text, boxes):
            # One shared offset for every glyph, so each keeps its true vertical
            # place: Q's overshoot above the cap line and its tail below stay as
            # the typeface drew them.
            d.text((x - b[0], -cap_top), c, font=font, fill=(255, 255, 255, 255))
            x += b[2] - b[0] + track
        # Trim horizontally to the ink. The menu draws all four words from one
        # left x, and the native crops start their ink in column 0 - so any
        # transparent margin here is a pure indent, which is exactly how EXTRA
        # and QUIT ended up sitting a couple of pixels right of the other two.
        # The rows are NOT trimmed: row 0 is the cap line, which is what keeps
        # the baseline shared.
        bx = im.getbbox()
        im = im.crop((bx[0], 0, bx[2], h))
        out.append((name, im))
    return out


def extra_words():
    """RAID - the heading of the EXTRA screen.

    Cut to look like the screen it sits on: the RESIDENT EVIL logo in title.pix
    is a heavy CONDENSED GROTESQUE, not a serif - tall, tightly packed, flat
    terminals, with a dark shadow offset down and right. Saira Condensed Black
    (the pack's own family, already in this atlas as the toast font) is the
    closest of anything to hand, tracked out to match the logo's own spacing.

    The letters are baked white and the shadow BLACK, because the blit applies
    a colour MULTIPLY: white takes whatever red the screen asks for and black
    stays black, so one bake gives both the lettering and its shadow without a
    second draw or a second rect.
    """
    font = ImageFont.truetype(
        gui("fonts", "SairaCondensed-Black.ttf"), 62)
    text, track, drop = "RAID", 3, 2

    boxes = [font.getbbox(c) for c in text]
    top = min(b[1] for b in boxes)
    w = sum(b[2] - b[0] + track for b in boxes) - track
    h = max(b[3] for b in boxes) - top + 1

    def letters(fill):
        im = Image.new("RGBA", (w + 8, h + 8), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        x = 4
        for c, b in zip(text, boxes):
            d.text((x - b[0], 4 - top), c, font=font, fill=fill)
            x += b[2] - b[0] + track
        return im

    out = Image.new("RGBA", (w + 8, h + 8), (0, 0, 0, 0))
    shadow = letters((0, 0, 0, 192))
    out.alpha_composite(shadow, (drop, drop))
    out.alpha_composite(letters((255, 255, 255, 255)))
    # Trimmed to the ink on every side: the screen centres it on its own width,
    # so a margin would put it off centre by half of itself.
    return [("wordraid", out.crop(out.getbbox()))]

def extra_marks():
    """The flare that comes up behind RAID once it has settled.

    Two pieces, both white with the shape carried in ALPHA so the blit can
    colour and fade them freely: a round falloff for the light itself and a
    horizontal streak for the anamorphic smear across it. Squared falloff, so
    both reach zero at the edge - a soft light with a visible rectangular
    border is worse than no light at all.
    """
    import math

    R = 48
    glow = Image.new("RGBA", (R * 2, R * 2), (255, 255, 255, 0))
    gp = glow.load()
    for y in range(R * 2):
        for x in range(R * 2):
            d = math.hypot(x - R + 0.5, y - R + 0.5) / R
            if d >= 1.0:
                continue
            k = (1.0 - d) ** 2
            gp[x, y] = (255, 255, 255, int(255 * k))

    W, H = 128, 7
    streak = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    sp = streak.load()
    for y in range(H):
        # across the bar: a hard centre line with a quick falloff
        v = 1.0 - abs(y - (H - 1) / 2.0) / ((H - 1) / 2.0 + 0.5)
        v = v * v
        for x in range(W):
            u = 1.0 - abs(x - (W - 1) / 2.0) / ((W - 1) / 2.0)
            sp[x, y] = (255, 255, 255, int(255 * v * u * u))

    # Grain. One tile, and the screen draws a 160x120 WINDOW of it at a
    # different offset every frame - so one quad's worth of atlas gives endless
    # variation for a single draw call, instead of a stack of tiles or a
    # thousand one-pixel quads. Drawn point-sampled and stretched 2x, which is
    # what gives it the chunk of film grain rather than the fizz of noise.
    grain = Image.new("RGBA", (168, 128), (255, 255, 255, 0))
    np_ = grain.load()
    seed = 0x5EED
    for y in range(128):
        for x in range(168):
            # xorshift, so the tile is identical on every machine that bakes it
            seed ^= (seed << 13) & 0xFFFFFFFF
            seed ^= seed >> 17
            seed ^= (seed << 5) & 0xFFFFFFFF
            v = seed & 0xFF
            # biased dark: grain should mostly sit near zero and spike
            a = 0 if v < 150 else (v - 150) * 255 // 105
            np_[x, y] = (255, 255, 255, a)

    # The ghost arc: the ring a lens throws opposite a bright source. Named
    # flarering because skin_marks() already bakes a "ring" - two marks of the
    # same name would emit the same C symbol twice and the header would not
    # compile. Baked as a
    # ring rather than a disc, with the brightness falling away round the
    # circumference so that what shows is an ARC and not a hoop - a complete
    # circle reads as a drawn shape, a partial one as an artefact of a lens.
    RR = 48
    ring = Image.new("RGBA", (RR * 2, RR * 2), (255, 255, 255, 0))
    rp = ring.load()
    for y in range(RR * 2):
        for x in range(RR * 2):
            dx, dy = x - RR + 0.5, y - RR + 0.5
            d = math.hypot(dx, dy) / RR
            if d > 1.0:
                continue
            # a soft band at 0.84 of the radius
            band = math.exp(-((d - 0.84) / 0.085) ** 2)
            # and round the rim: brightest towards the lower left
            ang = math.atan2(dy, dx)
            lobe = 0.30 + 0.70 * max(0.0, math.cos(ang - 2.5)) ** 1.6
            v = band * lobe
            if v > 0.004:
                rp[x, y] = (255, 255, 255, int(255 * min(1.0, v)))

    return [("glow", glow), ("streak", streak), ("grain", grain), ("flarering", ring)]


def tint(img, rgba):
    """Multiply white art by a colour, keeping its alpha."""
    r, g, b, a = img.split()
    solid = Image.new("RGBA", img.size, rgba[:3] + (255,))
    solid.putalpha(a.point(lambda v: v * rgba[3] // 255))
    return solid


# ---------------------------------------------------------------------------
# atlas assembly
# ---------------------------------------------------------------------------
def bleed(atlas, x, y, w, h):
    """Duplicate the right column and bottom row one pixel further out.

    Everything here is drawn with a LINEAR sampler (the toast is scaled to the
    window, not blitted texel for texel), so a sample taken exactly on a
    sub-rect's right or bottom edge blends with whatever sits in the gap
    beyond it. Without this the panel's border and the icon slot's frame fade
    out along those two edges.
    """
    px = atlas.load()
    for j in range(h):
        px[x + w, y + j] = px[x + w - 1, y + j]
    for i in range(w + 1):
        px[x + i, y + h] = px[x + i, y + h - 1]


class Packer(object):
    """Trivial shelf packer - the content is small and known, so rows suffice."""

    def __init__(self, atlas, y):
        self.atlas = atlas
        self.x = 0
        self.y = y
        self.row_h = 0

    def add(self, img, edge_bleed=False):
        w, h = img.size
        # +2: one pixel for the bleed column/row, one to keep a real gap
        if self.x + w + 2 > ATLAS_W:
            self.x = 0
            self.y += self.row_h + 2
            self.row_h = 0
        if self.y + h + 2 > ATLAS_H:
            raise SystemExit("atlas overflow: %dx%d does not fit" % (w, h))
        pos = (self.x, self.y)
        self.atlas.alpha_composite(img, pos)
        if edge_bleed:
            bleed(self.atlas, pos[0], pos[1], w, h)
        self.x += w + 2
        self.row_h = max(self.row_h, h)
        return pos


def bake_font(path, px, packer):
    font = ImageFont.truetype(path, px)
    ascent, descent = font.getmetrics()
    glyphs = []
    for code in range(FIRST_CHAR, LAST_CHAR + 1):
        ch = chr(code)
        adv = int(round(font.getlength(ch)))
        box = font.getbbox(ch)          # (x0, y0, x1, y1) relative to the origin
        gw = max(0, box[2] - box[0])
        gh = max(0, box[3] - box[1])
        if gw == 0 or gh == 0:          # space and friends
            glyphs.append((0, 0, 0, 0, 0, 0, adv))
            continue
        img = Image.new("RGBA", (gw, gh), (0, 0, 0, 0))
        ImageDraw.Draw(img).text((-box[0], -box[1]), ch, font=font,
                                 fill=(255, 255, 255, 255))
        x, y = packer.add(img)
        # bx/by are the offset from the pen position (baseline at by=0 means the
        # glyph box's top is box[1] below the line's top, not the baseline).
        glyphs.append((x, y, gw, gh, box[0], box[1], adv))
    return glyphs, ascent, descent, ascent + descent


def main():
    root = repo_root()
    atlas = Image.new("RGBA", (ATLAS_W, ATLAS_H), (0, 0, 0, 0))

    panel = build_panel()
    atlas.alpha_composite(panel, (0, 0))
    bleed(atlas, 0, 0, PANEL_W, PANEL_H)
    panel_rect = (0, 0, PANEL_W, PANEL_H)

    # A 4x4 opaque white block for solid fills (the progress bar, dividers).
    white_x, white_y = PANEL_W + 2, 0
    atlas.alpha_composite(Image.new("RGBA", (4, 4), (255, 255, 255, 255)),
                          (white_x, white_y))
    white_rect = (white_x + 1, white_y + 1, 2, 2)   # inset: no edge bleed

    slot = build_slot()
    packer = Packer(atlas, PANEL_H + 3)
    slot_rect = packer.add(slot, edge_bleed=True) + slot.size

    icon_rects = []
    for name in ICONS:
        src = Image.open(gui("png", "Icons", "128", name + ".png")).convert("RGBA")
        img = src.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
        pos = packer.add(img)
        icon_rects.append((name, pos + (ICON_SIZE, ICON_SIZE)))

    mark_rects = []
    for name, im in skin_marks():
        pos = packer.add(im, edge_bleed=True)
        mark_rects.append((name, pos + im.size))
    for name, im in (title_words() + native_title_words(root)
                     + extra_words() + extra_marks()):
        pos = packer.add(im, edge_bleed=True)
        mark_rects.append((name, pos + im.size))

    packer.x = 0
    packer.y += packer.row_h + 3
    packer.row_h = 0
    title, t_asc, t_desc, t_line = bake_font(
        gui("fonts", "SairaCondensed-Bold.ttf"), FONT_TITLE_PX, packer)
    body, b_asc, b_desc, b_line = bake_font(
        gui("fonts", "SairaCondensed-SemiBold.ttf"), FONT_BODY_PX, packer)

    # --- write the texture ---
    blob = b"AUI1" + struct.pack("<II", ATLAS_W, ATLAS_H) + atlas.tobytes("raw", "RGBA")

    # assets/ is the source of truth, but config.ini's [Assets] Path is empty
    # by default, which means the game reads its data tree from NEXT TO THE
    # EXECUTABLE - bin/Debug/USA and bin/Release/USA, each its own copy. Baking
    # only into assets/ leaves those two behind, and the symptom is silent:
    # the header the build compiles in has the new rects while the atlas the
    # game loads still has transparent pixels there, so the new art simply
    # does not appear. Write every tree that exists.
    targets = [os.path.join(root, "assets", "USA", "Data", "achvui.bin")]
    for cfg in ("Debug", "Release"):
        d = os.path.join(root, "bin", cfg, "USA", "Data")
        if os.path.isdir(d):
            targets.append(os.path.join(d, "achvui.bin"))

    for out_bin in targets:
        os.makedirs(os.path.dirname(out_bin), exist_ok=True)
        with open(out_bin, "wb") as fp:
            fp.write(blob)
        print("wrote %s (%d bytes)" % (out_bin, os.path.getsize(out_bin)))

    # --- write the generated header ---
    def rect(r):
        return "{ %d, %d, %d, %d }" % r

    def glyph_table(name, glyphs, asc, line):
        out = ["static const PortGlyph %s[ACHV_FONT_CHARS] = {" % name]
        for g in glyphs:
            out.append("    { %d, %d, %d, %d, %d, %d, %d }," % g)
        out.append("};")
        return "\n".join(out)

    lines = []
    lines.append("// AchievementAtlasData.h - GENERATED by tools/build_achievement_ui.py")
    lines.append("// Do not edit by hand: re-run the generator instead. It bakes the Space GUI")
    lines.append("// pack (assets/SpaceGUI/) into assets/USA/Data/achvui.bin and emits the rects")
    lines.append("// below, which Achievements.cpp indexes that texture with.")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "PortText.h"')
    lines.append("")
    lines.append("#define ACHV_ATLAS_W      %d" % ATLAS_W)
    lines.append("#define ACHV_ATLAS_H      %d" % ATLAS_H)
    lines.append("#define ACHV_FONT_FIRST   %d" % FIRST_CHAR)
    lines.append("#define ACHV_FONT_LAST    %d" % LAST_CHAR)
    lines.append("#define ACHV_FONT_CHARS   %d" % (LAST_CHAR - FIRST_CHAR + 1))
    lines.append("")
    lines.append("// Atlas rectangle, in atlas pixels.")
    lines.append("struct AchvRect { short x, y, w, h; };")
    lines.append("")
    lines.append("// The font tables at the bottom are PortGlyph (PortText.h), in atlas pixels.")
    lines.append("")
    lines.append("static const AchvRect g_achvPanel = %s;" % rect(panel_rect))
    lines.append("static const AchvRect g_achvWhite = %s;" % rect(white_rect))
    lines.append("static const AchvRect g_achvIconSlot = %s;" % rect(slot_rect))
    lines.append("")
    lines.append("#define ACHV_PANEL_CUT    %d" % PANEL_CUT)
    lines.append("// Horizontal 3-slice: left cap | stretched middle | right cap.")
    lines.append("#define ACHV_PANEL_CAP_L  %d" % PANEL_CAP_L)
    lines.append("#define ACHV_PANEL_CAP_R  %d" % PANEL_CAP_R)
    lines.append("")
    for i, (name, _) in enumerate(icon_rects):
        lines.append("#define ACHV_ICON_%-10s %d" % (name.upper(), i))
    lines.append("#define ACHV_ICON_COUNT   %d" % len(icon_rects))
    lines.append("")
    lines.append("static const AchvRect g_achvIcons[ACHV_ICON_COUNT] = {")
    for name, r in icon_rects:
        lines.append("    %s,  // %s" % (rect(r), name))
    lines.append("};")
    lines.append("")
    lines.append("// Shapes for the status-screen skin (src/game/UiSkin.cpp).")
    for name, r in mark_rects:
        lines.append("static const AchvRect g_achvMark%s = %s;"
                     % (name.capitalize(), rect(r)))
    lines.append("")
    lines.append("#define ACHV_TITLE_ASCENT %d" % t_asc)
    lines.append("#define ACHV_TITLE_LINE   %d" % t_line)
    lines.append("#define ACHV_BODY_ASCENT  %d" % b_asc)
    lines.append("#define ACHV_BODY_LINE    %d" % b_line)
    lines.append("")
    lines.append(glyph_table("g_achvFontTitle", title, t_asc, t_line))
    lines.append("")
    lines.append(glyph_table("g_achvFontBody", body, b_asc, b_line))
    lines.append("")

    out_h = os.path.join(root, "src", "game", "AchievementAtlasData.h")
    with open(out_h, "w") as fp:
        fp.write("\n".join(lines))
    print("wrote %s" % out_h)

    if "--preview" in sys.argv:
        atlas.save(os.path.join(root, "achvui_preview.png"))
        print("wrote achvui_preview.png")


if __name__ == "__main__":
    main()
