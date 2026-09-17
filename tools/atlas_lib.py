"""atlas_lib.py - what the port's two UI atlas bakers share.

tools/build_achievement_ui.py (achvui.bin: the toast, the status-screen skin,
the title-menu words) and tools/build_editor_ui.py (edui.bin: the RE1 EDITOR)
bake different art into the same kind of sheet - one RGBA texture behind a
12-byte header, loaded by src/game/PortAtlas.cpp, and a generated C header that
names the rects and carries the glyph tables src/game/PortText.cpp reads. The
machinery for that used to live in both scripts, copied once and then drifting.
It is here once; each script keeps only what is its own: which art it bakes and
which rects it names.

Not a script - run the two bakers, which import this.
"""

import os
import struct

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The glyph range both fonts bake. PortText answers '?' outside it.
FIRST_CHAR = 32
LAST_CHAR = 126


def gui(*parts):
    """A file from the Space GUI pack (assets/SpaceGUI/, OFL/CC-BY as shipped)."""
    return os.path.join(ROOT, "assets", "SpaceGUI", "sources", *parts)


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------
def bleed(atlas, x, y, w, h):
    """Duplicate the right column and bottom row one pixel further out.

    Everything in these sheets is drawn with a LINEAR sampler (the UI is scaled
    to the window, not blitted texel for texel), so a sample taken exactly on a
    sub-rect's right or bottom edge blends with whatever sits in the gap beyond
    it. Without this a plate's border fades out along those two edges.
    """
    px = atlas.load()
    for j in range(h):
        px[x + w, y + j] = px[x + w - 1, y + j]
    for i in range(w + 1):
        px[x + i, y + h] = px[x + i, y + h - 1]


class Packer(object):
    """Shelf packer. The content is small and known, so rows suffice."""

    def __init__(self, atlas, y=0):
        self.atlas = atlas
        self.x = 0
        self.y = y
        self.row_h = 0

    def add(self, img, edge_bleed=False):
        atlas_w, atlas_h = self.atlas.size
        w, h = img.size
        if w == 0 or h == 0:
            return (0, 0)
        # +2: one pixel for the bleed column/row, one to keep a real gap
        if self.x + w + 2 > atlas_w:
            self.x = 0
            self.y += self.row_h + 2
            self.row_h = 0
        if self.y + h + 2 > atlas_h:
            raise SystemExit("atlas overflow at %dx%d (row y=%d)" % (w, h, self.y))
        pos = (self.x, self.y)
        self.atlas.alpha_composite(img, pos)
        if edge_bleed:
            bleed(self.atlas, pos[0], pos[1], w, h)
        self.x += w + 2
        self.row_h = max(self.row_h, h)
        return pos


# ---------------------------------------------------------------------------
# Fonts
# ---------------------------------------------------------------------------
def bake_font(path, px, packer, bg=(0, 0, 0, 0)):
    """Bake FIRST_CHAR..LAST_CHAR of one font at `px` pixels into the sheet.

    Returns (glyphs, ascent, descent, line). Each glyph is the PortGlyph tuple
    (x, y, w, h, bx, by, adv): bx/by are the offset from the pen position, with
    by measured DOWN from the line's top (the PIL bbox origin), not from the
    baseline.

    `bg` is the colour under a glyph's transparent pixels. The toast has always
    baked on black and the editor on white; Pillow 12 puts the same bytes in the
    sheet either way, but achvui.bin was last baked on another machine's Pillow
    and nobody has shown that one agrees, so each caller keeps its own.
    """
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
        img = Image.new("RGBA", (gw, gh), bg)
        ImageDraw.Draw(img).text((-box[0], -box[1]), ch, font=font,
                                 fill=(255, 255, 255, 255))
        # The sheet is sampled linearly, so a glyph packed hard against its
        # neighbour bleeds into it when the quad lands off a texel centre. The
        # packer's 2px gutter is what keeps that from happening; the glyph
        # itself is stored tight so the metrics stay honest.
        x, y = packer.add(img)
        glyphs.append((x, y, gw, gh, box[0], box[1], adv))
    return glyphs, ascent, descent, ascent + descent


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def write_blob(name, tag, atlas):
    """Write <tag> + w + h + RGBA8 rows to every data tree that exists.

    assets/ is the source of truth, but config.ini's [Assets] Path is empty by
    default, which means the game reads its data tree from NEXT TO THE
    EXECUTABLE - bin/Debug/USA and bin/Release/USA, each its own copy. Baking
    only into assets/ leaves those two behind, and the symptom is silent: the
    header the build compiles in has the new rects while the atlas the game
    loads still has the old pixels there, so the new art simply does not
    appear. (build_editor_ui.py used to write assets/ only.)
    """
    atlas_w, atlas_h = atlas.size
    # bpp 32 is memcpy'd straight into an R8G8B8A8 texture, so the byte order
    # written here IS the texture's: R, G, B, A per pixel.
    blob = tag + struct.pack("<II", atlas_w, atlas_h) + atlas.tobytes("raw", "RGBA")

    targets = [os.path.join(ROOT, "assets", "USA", "Data", name)]
    for cfg in ("Debug", "Release"):
        d = os.path.join(ROOT, "bin", cfg, "USA", "Data")
        if os.path.isdir(d):
            targets.append(os.path.join(d, name))

    for out_bin in targets:
        os.makedirs(os.path.dirname(out_bin), exist_ok=True)
        with open(out_bin, "wb") as fp:
            fp.write(blob)
        print("wrote %s (%d bytes)" % (out_bin, os.path.getsize(out_bin)))


def header_start(filename, script, about, port_text):
    """The opening every generated header shares.

    `about` is the rest of the do-not-edit comment, already wrapped; `port_text`
    is the include path to PortText.h from where the header lives.
    """
    lines = ["// %s - GENERATED by tools/%s" % (filename, script)]
    lines.extend(about)
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "%s"' % port_text)
    lines.append("")
    return lines


def glyph_table(symbol, count_macro, glyphs, annotate=False):
    """A `static const PortGlyph` table, as lines.

    `annotate` column-aligns the numbers and names each row's character, which
    the editor's header does and the toast's never has.
    """
    lines = ["static const PortGlyph %s[%s] = {" % (symbol, count_macro)]
    for code, g in zip(range(FIRST_CHAR, LAST_CHAR + 1), glyphs):
        if annotate:
            ch = chr(code)
            shown = "space" if ch == " " else ("'%s'" % ch if ch != "'" else "quote")
            lines.append("    { %4d, %4d, %3d, %3d, %4d, %4d, %3d },  // %s"
                         % (g[0], g[1], g[2], g[3], g[4], g[5], g[6], shown))
        else:
            lines.append("    { %d, %d, %d, %d, %d, %d, %d }," % g)
    lines.append("};")
    return lines


def write_header(relpath, lines):
    """Write a generated header under the repo root, LF line endings everywhere."""
    out_h = os.path.join(ROOT, relpath)
    os.makedirs(os.path.dirname(out_h), exist_ok=True)
    with open(out_h, "w", newline="\n") as fp:
        fp.write("\n".join(lines))
    print("wrote %s" % out_h)
