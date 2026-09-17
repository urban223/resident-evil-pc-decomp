// EditorUIDraw.cpp - the toolkit's floor: one texture, clipped quads, text.
//
// CUSTOM (port-only).
//
// Every pixel of the interface comes out of assets/USA/Data/edui.bin through
// this file, and this is the only file that knows about backbuffer pixels. Two
// things earn their own explanation:
//
// CLIPPING is done here in software rather than with the hardware scissor. A
// scissor is one rectangle for the whole pipeline, and the viewport already
// owns it - the room has to be cut to the viewport rect while the panels are
// being drawn over the rest of the window. Clipping an axis-aligned textured
// quad by hand is exact anyway: shrink the rectangle, move the texture
// coordinates by the same fraction, and the result is the same pixels the
// scissor would have kept.
//
// The SAMPLER is linear, unlike the game's own 2D. The atlas is baked at twice
// the design size (EDUI_BAKE) precisely so it can be filtered down: point
// sampling a 34px glyph into a 17px box is what the engine's 8x8 font already
// looks like, and getting away from that is the whole reason this file exists.
#include "EditorUIInternal.h"
#include "../../PortAtlas.h"
#include "../../../marni/MarniSystem.h"
#include "../../../system/AssetPath.h"

static PortAtlas s_atlas = PORT_ATLAS_INIT;

int EdUiDraw_Ready(void)
{
    return PortAtlas_Load(&s_atlas, GAME_DATA_ROOT "Data\\edui.bin", "EUI1",
                          EDUI_ATLAS_W, EDUI_ATLAS_H);
}

void EdUiDraw_Reset(void)
{
    // The device went away (a resolution change destroys every texture). Let
    // the next Ready() upload the sheet again rather than drawing from a
    // handle that no longer names anything.
    PortAtlas_Reset(&s_atlas);
}

EdRect EdUiDraw_Clip(void)
{
    if (g_edui.clipTop <= 0) return EdR(0.0f, 0.0f, g_edui.w, g_edui.h);
    return g_edui.clip[g_edui.clipTop - 1];
}

// ---------------------------------------------------------------------------
// The one draw call.
//
// `src` is in atlas pixels, `dst` in design pixels. The quad is cut against
// the clip rectangle first, with the texture coordinates carried along by the
// same fractions, and only then scaled into the backbuffer.
// ---------------------------------------------------------------------------
void EdUiDraw_Quad(const EdUiRect* src, EdRect dst, unsigned int argb)
{
    if (s_atlas.tex == MARNI_NULL_HANDLE) return;
    if (dst.w <= 0.0f || dst.h <= 0.0f) return;
    if ((argb & 0xFF000000u) == 0u) return;

    const EdRect c = EdUiDraw_Clip();
    float x0 = dst.x, y0 = dst.y, x1 = dst.x + dst.w, y1 = dst.y + dst.h;
    const float cx1 = c.x + c.w, cy1 = c.y + c.h;
    if (x1 <= c.x || x0 >= cx1 || y1 <= c.y || y0 >= cy1) return;

    float u0 = (float)src->x, v0 = (float)src->y;
    float u1 = (float)(src->x + src->w), v1 = (float)(src->y + src->h);
    const float du = (u1 - u0) / dst.w;
    const float dv = (v1 - v0) / dst.h;

    if (x0 < c.x)  { u0 += (c.x - x0) * du; x0 = c.x; }
    if (y0 < c.y)  { v0 += (c.y - y0) * dv; y0 = c.y; }
    if (x1 > cx1)  { u1 -= (x1 - cx1) * du; x1 = cx1; }
    if (y1 > cy1)  { v1 -= (y1 - cy1) * dv; y1 = cy1; }
    if (x1 - x0 <= 0.0f || y1 - y0 <= 0.0f) return;

    const float k = g_edui.scale;
    const float iw = 1.0f / (float)EDUI_ATLAS_W;
    const float ih = 1.0f / (float)EDUI_ATLAS_H;

    MarniDrawSpriteEx(x0 * k, y0 * k, (x1 - x0) * k, (y1 - y0) * k,
                      u0 * iw, v0 * ih, u1 * iw, v1 * ih,
                      argb, s_atlas.tex, MARNI_SAMPLER_LINEAR, MARNI_BLEND_ALPHA);
}

void EdUI_Quad(const EdUiRect* src, EdRect dst, unsigned int argb)
{
    EdUiDraw_Quad(src, dst, argb);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
void EdUI_Fill(EdRect r, unsigned int argb)
{
    EdUiDraw_Quad(&g_eduiWhite, r, argb);
}

void EdUI_Line(float x0, float y0, float x1, float y1, float w,
               unsigned int argb)
{
    // Axis-aligned only: that is all the interface draws, and a general
    // segment would need a rotated quad the clipper above cannot cut.
    if (y0 == y1) {
        EdUI_Fill(EdR(x0 < x1 ? x0 : x1, y0 - w * 0.5f,
                      (x1 > x0 ? x1 - x0 : x0 - x1), w), argb);
    } else if (x0 == x1) {
        EdUI_Fill(EdR(x0 - w * 0.5f, y0 < y1 ? y0 : y1,
                      w, (y1 > y0 ? y1 - y0 : y0 - y1)), argb);
    }
}

void EdUI_Outline(EdRect r, unsigned int argb)
{
    const float t = 1.0f;
    EdUI_Fill(EdR(r.x, r.y, r.w, t), argb);
    EdUI_Fill(EdR(r.x, r.y + r.h - t, r.w, t), argb);
    EdUI_Fill(EdR(r.x, r.y + t, t, r.h - t * 2.0f), argb);
    EdUI_Fill(EdR(r.x + r.w - t, r.y + t, t, r.h - t * 2.0f), argb);
}

// A 9-slice: four corners at their baked size, four edges stretched along one
// axis, one middle stretched along both. Collapses to a plain stretch when the
// rectangle is smaller than two corners, which is what a 4px-tall scrollbar
// thumb needs.
void EdUI_Plate(EdRect r, int tile, unsigned int argb)
{
    if (tile < 0 || tile >= EDUI_TILE_COUNT) return;
    const EdUiRect* t = &g_eduiTiles[tile];
    const float cutAtlas = (float)g_eduiTileCut[tile];
    const float cut = cutAtlas / (float)EDUI_BAKE;      // design px

    if (r.w <= cut * 2.0f || r.h <= cut * 2.0f) {
        EdUiDraw_Quad(t, r, argb);
        return;
    }

    const float ca = cutAtlas;
    const float mw = (float)t->w - ca * 2.0f;           // atlas middle
    const float mh = (float)t->h - ca * 2.0f;
    const float ix = r.w - cut * 2.0f;                  // design middle
    const float iy = r.h - cut * 2.0f;

    EdUiRect s;
    // top row
    s.x = t->x;                    s.y = t->y; s.w = (short)ca; s.h = (short)ca;
    EdUiDraw_Quad(&s, EdR(r.x, r.y, cut, cut), argb);
    s.x = (short)(t->x + ca);      s.w = (short)mw;
    EdUiDraw_Quad(&s, EdR(r.x + cut, r.y, ix, cut), argb);
    s.x = (short)(t->x + t->w - ca); s.w = (short)ca;
    EdUiDraw_Quad(&s, EdR(r.x + r.w - cut, r.y, cut, cut), argb);
    // middle row
    s.y = (short)(t->y + ca);      s.h = (short)mh;
    s.x = t->x;                    s.w = (short)ca;
    EdUiDraw_Quad(&s, EdR(r.x, r.y + cut, cut, iy), argb);
    s.x = (short)(t->x + ca);      s.w = (short)mw;
    EdUiDraw_Quad(&s, EdR(r.x + cut, r.y + cut, ix, iy), argb);
    s.x = (short)(t->x + t->w - ca); s.w = (short)ca;
    EdUiDraw_Quad(&s, EdR(r.x + r.w - cut, r.y + cut, cut, iy), argb);
    // bottom row
    s.y = (short)(t->y + t->h - ca); s.h = (short)ca;
    s.x = t->x;                    s.w = (short)ca;
    EdUiDraw_Quad(&s, EdR(r.x, r.y + r.h - cut, cut, cut), argb);
    s.x = (short)(t->x + ca);      s.w = (short)mw;
    EdUiDraw_Quad(&s, EdR(r.x + cut, r.y + r.h - cut, ix, cut), argb);
    s.x = (short)(t->x + t->w - ca); s.w = (short)ca;
    EdUiDraw_Quad(&s, EdR(r.x + r.w - cut, r.y + r.h - cut, cut, cut), argb);
}

void EdUI_Icon(EdRect r, int icon, unsigned int argb)
{
    if (icon < 0 || icon >= EDUI_ICON_COUNT) return;
    // Square and centred: the icons are baked square and stretching one would
    // be visible immediately next to one that was not.
    float s = (r.w < r.h) ? r.w : r.h;
    EdUiDraw_Quad(&g_eduiIcons[icon],
                  EdR(r.x + (r.w - s) * 0.5f, r.y + (r.h - s) * 0.5f, s, s),
                  argb);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static PortFont font_of(int font)
{
    if (font < 0 || font >= EDUI_FONT_COUNT) font = EDUI_FONT_UI;
    const PortFont f = { g_eduiFonts[font], EDUI_FONT_FIRST, EDUI_FONT_LAST };
    return f;
}

// Baked pixels -> design pixels.
static float font_k(int font)
{
    (void)font;
    return 1.0f / (float)EDUI_BAKE;
}

float EdUI_TextW(int font, const char* s)
{
    const PortFont f = font_of(font);
    return PortText_Width(&f, s, font_k(font));
}

float EdUI_FontH(int font)
{
    if (font < 0 || font >= EDUI_FONT_COUNT) font = EDUI_FONT_UI;
    return (float)g_eduiFontLine[font] * font_k(font);
}

static void glyph_quad(void* user, const PortGlyph* g,
                       float x, float y, float w, float h)
{
    EdUiRect src;
    src.x = g->x; src.y = g->y; src.w = g->w; src.h = g->h;
    EdUiDraw_Quad(&src, EdR(x, y, w, h), *(const unsigned int*)user);
}

// y is the TOP of the line box, not the baseline: laying out rows against a
// baseline means every call site has to know the font's ascent.
void EdUI_Text(int font, const char* s, float x, float y, unsigned int argb)
{
    if (s == NULL || *s == '\0') return;
    const PortFont f = font_of(font);
    PortText_Draw(&f, s, x, y, font_k(font), glyph_quad, &argb);
}

void EdUI_TextIn(int font, const char* s, EdRect box, int align,
                 unsigned int argb)
{
    if (s == NULL || *s == '\0' || box.w <= 0.0f) return;

    char cut[128];
    const float full = EdUI_TextW(font, s);
    const char* draw = s;
    float w = full;

    if (full > box.w) {
        // Ellipsise. A name too long for its column is common enough in the
        // outliner that cutting it mid-glyph would look like a bug.
        const PortFont f = font_of(font);
        w = PortText_Ellipsise(&f, s, font_k(font), box.w, cut, (int)sizeof(cut));
        draw = cut;
    }

    float x = box.x;
    if (align == ED_ALIGN_CENTRE)     x = box.x + (box.w - w) * 0.5f;
    else if (align == ED_ALIGN_RIGHT) x = box.x + box.w - w;

    // Optical centring: the line box includes the descender, and rows read low
    // when it is counted. Half of it back up is what looks centred.
    const float lh = EdUI_FontH(font);
    const float y = box.y + (box.h - lh) * 0.5f;
    EdUI_Text(font, draw, x, y, argb);
}
