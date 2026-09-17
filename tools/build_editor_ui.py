#!/usr/bin/env python3
"""build_editor_ui.py - bake the RE1 EDITOR's own interface art into one atlas.

The editor is part of the game process and draws its panels with the renderer
that draws the game (src/game/editor/ui/). That renderer can do exactly one
thing: a textured, tinted quad. Everything the interface is made of therefore
has to exist as pixels in one texture - the fonts, the rounded plates the
panels are built from, and the icons - which is what this script bakes.

Why baked rather than rasterised at runtime: the game has no font stack. The
engine's only text primitive is an 8x8 bitmap blitter locked to a 40-column
line, which is a debug readout and not an interface. Baking Saira Condensed at
twice its design size and drawing it through the linear sampler gives real
typography at any window size, with no dependency added to a decompilation
project.

Inputs (from the Space GUI pack in assets/SpaceGUI/, OFL/CC-BY as shipped):
  sources/fonts/SairaCondensed-{Medium,Bold}.ttf
  sources/png/Icons/128/*.png       white silhouettes, tinted at draw time

Outputs:
  assets/USA/Data/edui.bin          'EUI1' + w + h + RGBA8 rows
  src/game/editor/ui/EditorUIData.h generated rects, glyph metrics, icon ids
  (the .bin also into bin/Debug and bin/Release, where those trees exist)

The packer, the font baker and both writers are tools/atlas_lib.py, shared with
build_achievement_ui.py; what is here is the editor's own art.

Run from the repo root:  python3 tools/build_editor_ui.py
"""

import os

from PIL import Image, ImageDraw, ImageFilter

from atlas_lib import (FIRST_CHAR, LAST_CHAR, ROOT, Packer, bake_font,
                       glyph_table, gui, header_start, write_blob,
                       write_header)

# --- atlas geometry ---------------------------------------------------------
ATLAS_W = 1024
ATLAS_H = 576

# Everything is baked at twice the size it is drawn at and sampled linearly.
# At 1080p the interface draws at exactly half the baked size, which is the
# sharpest a filtered blit gets; at 1440p and 4K the same sheet scales up
# instead of turning into a grid of fat pixels.
BAKE = 2

# design px (what the C++ side lays out in, before the DPI scale)
FONTS = [
    ("Ui",    "SairaCondensed-Medium.ttf",   17),
    ("Bold",  "SairaCondensed-Bold.ttf",     17),
    ("Small", "SairaCondensed-Medium.ttf",   14),
    ("Title", "SairaCondensed-SemiBold.ttf", 22),
]

ICON_SRC = 64           # baked icon size, atlas px (drawn at 32 design px)

# 9-slice tiles: (name, design radius, outline width or 0 for a filled tile)
TILES = [
    ("Round3", 3, 0),
    ("Round5", 5, 0),
    ("Round8", 8, 0),
    ("Line3",  3, 1),
    ("Line5",  5, 1),
]

# ---------------------------------------------------------------------------
# Plates: the rounded rectangles every panel, button and field is built from.
#
# Drawn 4x and downsampled, because a rounded corner is the one place in a flat
# interface where aliasing is obvious.
# ---------------------------------------------------------------------------
SS = 4


def rounded_tile(radius_design, outline):
    r = radius_design * BAKE
    side = r * 2 + 2                     # 2px of straight edge to stretch from
    img = Image.new("RGBA", (side * SS, side * SS), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    box = (0, 0, side * SS - 1, side * SS - 1)
    if outline:
        d.rounded_rectangle(box, radius=r * SS, outline=(255, 255, 255, 255),
                            width=max(1, outline * BAKE * SS))
    else:
        d.rounded_rectangle(box, radius=r * SS, fill=(255, 255, 255, 255))
    return img.resize((side, side), Image.LANCZOS), r


def shadow_tile():
    """A soft drop shadow for menus and popups, as a 9-slice."""
    r = 10 * BAKE
    side = r * 2 + 2
    img = Image.new("RGBA", (side, side), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((r // 2, r // 2, side - 1 - r // 2, side - 1 - r // 2),
                        radius=r // 3, fill=(255, 255, 255, 255))
    img = img.filter(ImageFilter.GaussianBlur(r / 3.2))
    return img, r


# ---------------------------------------------------------------------------
# Icons
# ---------------------------------------------------------------------------
# name -> pack file, or None when the shape is drawn below. The pack has no
# editor vocabulary (no cube, no light, no camera), and those are exactly the
# icons the outliner needs, so they are drawn here in the same weight.
PACK_ICONS = {
    "ROTATE": "Repeat", "PLAY": "Play", "PAUSE": "Pause",
    "ENEMY": "Skull", "SPAWN": "User", "GRID": "Tiles", "SNAP": "Magnet",
    "EYE": "Eye", "COG": "Cog", "PLUS": "Plus", "MINUS": "Minus",
    "TRASH": "Bin", "SEARCH": "Search", "CHECK": "Check", "CLOSE": "X",
    "LOCK": "Lock1", "WARNING": "Warning", "INFO": "Info", "HELP": "Help",
    "HOME": "Home", "STAR": "Star", "CLOCK": "Clock", "PIN": "Location",
    "MENU": "Menu", "STATS": "Stats", "KEY": "Key", "GUN": "Gun",
    "AMMO": "GunAmmo", "AID": "FirstAidKit", "FLAME": "Flame",
    "DOTS": "Dots", "SHARE": "Share", "COMPASS": "Compass",
    "TARGET": "Target", "FLASH": "Flash", "CHAT": "Chat",
    "PENCIL": "Pencil", "BAN": "Ban", "ITEM": "Gem",
}

DRAWN_ICONS = [
    "SELECT", "MOVE", "SCALE", "SAVE", "STOP", "FOLDER", "BOX", "LIGHT", "CAMERA",
    "ZONE", "CHEVRON_R", "CHEVRON_D", "ARROW_L", "ARROW_R", "WIRE", "LIT",
    "OPEN", "UNDO", "REDO", "SPEED", "AXIS",
]

ICON_ORDER = [
    "SELECT", "MOVE", "ROTATE", "SCALE",
    "SAVE", "OPEN", "UNDO", "REDO",
    "PLAY", "STOP", "PAUSE",
    "BOX", "ITEM", "ENEMY", "SPAWN", "LIGHT", "CAMERA", "ZONE",
    "FOLDER", "GRID", "SNAP", "EYE", "COG", "SPEED", "AXIS",
    "PLUS", "MINUS", "TRASH", "SEARCH", "CHECK", "CLOSE", "LOCK",
    "WARNING", "INFO", "HELP", "HOME", "STAR", "CLOCK", "PIN", "MENU",
    "STATS", "KEY", "GUN", "AMMO", "AID", "FLAME", "DOTS", "SHARE",
    "COMPASS", "TARGET", "FLASH", "CHAT", "PENCIL", "BAN",
    "CHEVRON_R", "CHEVRON_D", "ARROW_L", "ARROW_R", "WIRE", "LIT",
]

W = (255, 255, 255, 255)


def new_icon():
    """A supersampled canvas; draw in 0..S coordinates."""
    s = ICON_SRC * SS
    return Image.new("RGBA", (s, s), (255, 255, 255, 0)), s


def finish(img):
    return img.resize((ICON_SRC, ICON_SRC), Image.LANCZOS)


def draw_icon(name):
    img, S = new_icon()
    d = ImageDraw.Draw(img)
    u = S / 64.0                 # one design pixel
    lw = int(round(5 * u))       # the pack's stroke weight at 64px

    def L(pts, width=None, joint="curve"):
        d.line([(x * u, y * u) for x, y in pts], fill=W,
               width=width if width is not None else lw, joint=joint)

    def P(pts):
        d.polygon([(x * u, y * u) for x, y in pts], fill=W)

    def R(x0, y0, x1, y1, r=0, outline=False):
        box = (x0 * u, y0 * u, x1 * u, y1 * u)
        if outline:
            d.rounded_rectangle(box, radius=r * u, outline=W, width=lw)
        else:
            d.rounded_rectangle(box, radius=r * u, fill=W)

    if name == "SELECT":
        # the editor's own cursor: a filled arrow with a tail
        P([(16, 8), (16, 48), (26, 38), (33, 54), (41, 50), (34, 35), (47, 33)])
    elif name == "SCALE":
        R(10, 34, 30, 54, 3, outline=True)
        R(38, 10, 54, 26, 3, outline=True)
        L([(28, 36), (44, 20)])
    elif name == "SAVE":
        # a floppy, which is still what "save" looks like everywhere
        R(10, 10, 54, 54, 4, outline=True)
        R(20, 12, 44, 26, 2)
        R(18, 34, 46, 54, 2, outline=True)
    elif name == "OPEN":
        L([(10, 20), (26, 20), (31, 26), (54, 26)], joint="curve")
        L([(10, 20), (10, 50), (54, 50), (54, 26)])
    elif name == "MOVE":
        # the four-way arrow every editor uses for translate
        L([(32, 12), (32, 52)])
        L([(12, 32), (52, 32)])
        P([(32, 6), (24, 18), (40, 18)])
        P([(32, 58), (24, 46), (40, 46)])
        P([(6, 32), (18, 24), (18, 40)])
        P([(58, 32), (46, 24), (46, 40)])
    elif name in ("UNDO", "REDO"):
        # a loop with a head at the end of it, not a bare arc
        d.arc((14 * u, 20 * u, 50 * u, 56 * u), 180, 350, fill=W, width=lw)
        P([(14, 52), (6, 34), (22, 34)])
        if name == "REDO":
            img = img.transpose(Image.FLIP_LEFT_RIGHT)
    elif name == "STOP":
        R(14, 14, 50, 50, 4)
    elif name == "FOLDER":
        L([(9, 18), (26, 18), (31, 25), (55, 25)])
        R(9, 18, 55, 50, 4, outline=True)
    elif name == "BOX":
        # an isometric cube: the outliner's word for a wall
        L([(32, 8), (54, 20), (54, 44), (32, 56), (10, 44), (10, 20), (32, 8)])
        L([(32, 8), (32, 32)])
        L([(32, 32), (54, 20)])
        L([(32, 32), (10, 20)])
    elif name == "LIGHT":
        d.ellipse((18 * u, 10 * u, 46 * u, 38 * u), outline=W, width=lw)
        L([(26, 38), (26, 46)])
        L([(38, 38), (38, 46)])
        L([(25, 50), (39, 50)])
        L([(28, 56), (36, 56)])
    elif name == "CAMERA":
        R(8, 20, 40, 46, 3, outline=True)
        P([(44, 26), (56, 18), (56, 48), (44, 40)])
    elif name == "ZONE":
        for x in range(10, 54, 10):
            L([(x, 12), (min(x + 6, 54), 12)])
            L([(x, 52), (min(x + 6, 54), 52)])
        for y in range(12, 52, 10):
            L([(12, y), (12, min(y + 6, 52))])
            L([(52, y), (52, min(y + 6, 52))])
    elif name in ("CHEVRON_R", "CHEVRON_D", "ARROW_L", "ARROW_R"):
        if name == "CHEVRON_R":
            L([(26, 16), (42, 32), (26, 48)], width=int(6 * u))
        elif name == "CHEVRON_D":
            L([(16, 26), (32, 42), (48, 26)], width=int(6 * u))
        elif name == "ARROW_L":
            L([(46, 32), (18, 32)])
            P([(10, 32), (26, 22), (26, 42)])
        else:
            L([(18, 32), (46, 32)])
            P([(54, 32), (38, 22), (38, 42)])
    elif name == "WIRE":
        d.ellipse((10 * u, 10 * u, 54 * u, 54 * u), outline=W, width=lw)
        d.ellipse((10 * u, 22 * u, 54 * u, 42 * u), outline=W, width=int(3 * u))
        d.ellipse((22 * u, 10 * u, 42 * u, 54 * u), outline=W, width=int(3 * u))
    elif name == "LIT":
        d.ellipse((10 * u, 10 * u, 54 * u, 54 * u), fill=(255, 255, 255, 110))
        d.pieslice((10 * u, 10 * u, 54 * u, 54 * u), 200, 20, fill=W)
    elif name == "SPEED":
        d.arc((10 * u, 16 * u, 54 * u, 60 * u), 180, 360, fill=W, width=lw)
        L([(32, 38), (44, 24)])
    elif name == "AXIS":
        L([(14, 50), (14, 14)])
        L([(14, 50), (50, 50)])
        L([(14, 50), (38, 26)], width=int(3 * u))
    else:
        raise SystemExit("no drawing for icon " + name)
    return finish(img)


def load_icon(pack_name):
    src = Image.open(gui("png", "Icons", "128", pack_name + ".png")).convert("RGBA")
    return src.resize((ICON_SRC, ICON_SRC), Image.LANCZOS)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
def main():
    atlas = Image.new("RGBA", (ATLAS_W, ATLAS_H), (255, 255, 255, 0))
    packer = Packer(atlas)

    # A solid block first: every flat fill in the interface is this, tinted.
    # 4x4 rather than 1x1 so the linear sampler always lands on solid white.
    white = Image.new("RGBA", (4, 4), W)
    white_pos = packer.add(white)

    tiles = []
    for name, radius, outline in TILES:
        img, cut = rounded_tile(radius, outline)
        pos = packer.add(img)
        tiles.append((name, pos, img.size, cut))
    simg, scut = shadow_tile()
    spos = packer.add(simg)
    tiles.append(("Shadow", spos, simg.size, scut))

    icons = []
    for name in ICON_ORDER:
        img = draw_icon(name) if name in DRAWN_ICONS else load_icon(PACK_ICONS[name])
        pos = packer.add(img)
        icons.append((name, pos))

    fonts = []
    for cname, ttf, design in FONTS:
        # White under the glyphs, not black - see bake_font.
        glyphs, asc, desc, line = bake_font(gui("fonts", ttf), design * BAKE,
                                            packer, bg=(255, 255, 255, 0))
        fonts.append((cname, design, glyphs, asc, desc, line))

    used = packer.y + packer.row_h + 2
    print("atlas rows used: %d of %d" % (used, ATLAS_H))

    # --- the blob ----------------------------------------------------------
    write_blob("edui.bin", b"EUI1", atlas)

    # --- the header --------------------------------------------------------
    L = header_start("EditorUIData.h", "build_editor_ui.py", [
        "// Do not edit by hand: re-run the generator instead. It bakes the",
        "// editor's fonts, plates and icons into assets/USA/Data/edui.bin and",
        "// emits the metrics the UI toolkit indexes that texture with.",
    ], "../../PortText.h")
    L.append("#define EDUI_ATLAS_W    %d" % ATLAS_W)
    L.append("#define EDUI_ATLAS_H    %d" % ATLAS_H)
    L.append("#define EDUI_BAKE       %d   // baked px per design px" % BAKE)
    L.append("")
    L.append("// A rectangle in atlas pixels.")
    L.append("struct EdUiRect { short x, y, w, h; };")
    L.append("")
    L.append("// The font tables below are PortGlyph (PortText.h), in BAKED pixels -")
    L.append("// the toolkit multiplies by its own scale.")
    L.append("")
    L.append("static const EdUiRect g_eduiWhite = { %d, %d, 4, 4 };"
             % (white_pos[0], white_pos[1]))
    L.append("")
    L.append("// 9-slice plates. `cut` is the corner size in atlas pixels: the")
    L.append("// toolkit draws four corners at that size and stretches the rest.")
    for i, (name, pos, size, cut) in enumerate(tiles):
        L.append("#define EDUI_TILE_%-8s %d" % (name.upper(), i))
    L.append("#define EDUI_TILE_COUNT    %d" % len(tiles))
    L.append("static const EdUiRect g_eduiTiles[EDUI_TILE_COUNT] = {")
    for name, pos, size, cut in tiles:
        L.append("    { %d, %d, %d, %d },  // %s" % (pos[0], pos[1], size[0], size[1], name))
    L.append("};")
    L.append("static const short g_eduiTileCut[EDUI_TILE_COUNT] = { %s };"
             % ", ".join(str(t[3]) for t in tiles))
    L.append("")
    L.append("// Icons, all baked at one size and tinted at draw time.")
    L.append("#define EDUI_ICON_SRC   %d" % ICON_SRC)
    for i, (name, pos) in enumerate(icons):
        L.append("#define EDUI_ICON_%-10s %d" % (name, i))
    L.append("#define EDUI_ICON_COUNT   %d" % len(icons))
    L.append("static const EdUiRect g_eduiIcons[EDUI_ICON_COUNT] = {")
    for name, pos in icons:
        L.append("    { %d, %d, %d, %d },  // %s" % (pos[0], pos[1], ICON_SRC, ICON_SRC, name))
    L.append("};")
    L.append("")
    L.append("#define EDUI_FONT_FIRST   %d" % FIRST_CHAR)
    L.append("#define EDUI_FONT_LAST    %d" % LAST_CHAR)
    L.append("#define EDUI_FONT_CHARS   %d" % (LAST_CHAR - FIRST_CHAR + 1))
    for i, (cname, design, glyphs, asc, desc, line) in enumerate(fonts):
        L.append("#define EDUI_FONT_%-8s %d" % (cname.upper(), i))
    L.append("#define EDUI_FONT_COUNT    %d" % len(fonts))
    L.append("")
    for cname, design, glyphs, asc, desc, line in fonts:
        L.append("// %s: Saira Condensed at %d design px, baked at %d."
                 % (cname, design, design * BAKE))
        L.extend(glyph_table("g_eduiFont%s" % cname, "EDUI_FONT_CHARS", glyphs,
                             annotate=True))
        L.append("")
    L.append("static const PortGlyph* const g_eduiFonts[EDUI_FONT_COUNT] = {")
    for cname, design, glyphs, asc, desc, line in fonts:
        L.append("    g_eduiFont%s," % cname)
    L.append("};")
    L.append("")
    L.append("// Design size, and the baked ascent/line height in BAKED pixels.")
    L.append("static const short g_eduiFontSize[EDUI_FONT_COUNT]   = { %s };"
             % ", ".join(str(f[1]) for f in fonts))
    L.append("static const short g_eduiFontAscent[EDUI_FONT_COUNT] = { %s };"
             % ", ".join(str(f[3]) for f in fonts))
    L.append("static const short g_eduiFontLine[EDUI_FONT_COUNT]   = { %s };"
             % ", ".join(str(f[5]) for f in fonts))
    L.append("")

    write_header(os.path.join("src", "game", "editor", "ui", "EditorUIData.h"), L)

    # A look at what was baked, for the eye rather than for the build.
    prev = os.path.join(ROOT, "tools", "editor_ui_atlas_preview.png")
    flat = Image.new("RGBA", (ATLAS_W, used), (24, 26, 30, 255))
    flat.alpha_composite(atlas.crop((0, 0, ATLAS_W, used)))
    flat.save(prev)
    print("wrote %s" % prev)


if __name__ == "__main__":
    main()
