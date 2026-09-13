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
ATLAS_H = 512

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

    packer.x = 0
    packer.y += packer.row_h + 3
    packer.row_h = 0
    title, t_asc, t_desc, t_line = bake_font(
        gui("fonts", "SairaCondensed-Bold.ttf"), FONT_TITLE_PX, packer)
    body, b_asc, b_desc, b_line = bake_font(
        gui("fonts", "SairaCondensed-SemiBold.ttf"), FONT_BODY_PX, packer)

    # --- write the texture ---
    out_bin = os.path.join(root, "assets", "USA", "Data", "achvui.bin")
    os.makedirs(os.path.dirname(out_bin), exist_ok=True)
    with open(out_bin, "wb") as fp:
        fp.write(b"AUI1")
        fp.write(struct.pack("<II", ATLAS_W, ATLAS_H))
        fp.write(atlas.tobytes("raw", "RGBA"))
    print("wrote %s (%d bytes)" % (out_bin, os.path.getsize(out_bin)))

    # --- write the generated header ---
    def rect(r):
        return "{ %d, %d, %d, %d }" % r

    def glyph_table(name, glyphs, asc, line):
        out = ["static const AchvGlyph %s[ACHV_FONT_CHARS] = {" % name]
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
    lines.append("#define ACHV_ATLAS_W      %d" % ATLAS_W)
    lines.append("#define ACHV_ATLAS_H      %d" % ATLAS_H)
    lines.append("#define ACHV_FONT_FIRST   %d" % FIRST_CHAR)
    lines.append("#define ACHV_FONT_LAST    %d" % LAST_CHAR)
    lines.append("#define ACHV_FONT_CHARS   %d" % (LAST_CHAR - FIRST_CHAR + 1))
    lines.append("")
    lines.append("// Atlas rectangle, in atlas pixels.")
    lines.append("struct AchvRect { short x, y, w, h; };")
    lines.append("")
    lines.append("// One baked glyph. bx/by are the glyph box's offset from the pen position")
    lines.append("// (by is measured DOWN from the line's top, i.e. the PIL bbox origin);")
    lines.append("// adv is the horizontal advance.")
    lines.append("struct AchvGlyph { short x, y, w, h; short bx, by; short adv; };")
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
