// UiAtlas.cpp - CUSTOM (port-only): loader and draw helpers for the shared
// Space GUI atlas. See UiAtlas.h for why there are two draw paths.
#include "UiAtlas.h"
#include "PortAtlas.h"
#include "../marni/MarniSystem.h"
#include "../system/AssetPath.h"

// Rendering.cpp - queue a textured quad in the pending-sprite list. Port-only;
// the game's own 2D goes through display_texture instead.
extern int PendingSprite_Push(float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1,
                              DWORD color, MarniHandle tex, unsigned int depth);
extern int PendingSprite_PushEx(float x, float y, float w, float h,
                                float u0, float v0, float u1, float v1,
                                DWORD color, MarniHandle tex, unsigned int depth,
                                int pointSample);

static PortAtlas s_atlas = PORT_ATLAS_INIT;

int UiAtlas_Ready(void)
{
    return PortAtlas_Load(&s_atlas, GAME_DATA_ROOT "Data\\achvui.bin", "AUI1",
                          ACHV_ATLAS_W, ACHV_ATLAS_H);
}

float UiAtlas_Scale(void)
{
    DWORD bw = 0, bh = 0;
    MarniGetBackBufferSize(&bw, &bh);
    if (bh < 240) bh = 240;
    float k = ((float)bh / 480.0f) * 0.5f;
    if (k < 0.45f) k = 0.45f;
    if (k > 2.20f) k = 2.20f;
    return k;
}

// ---------------------------------------------------------------------------
// One place that turns an atlas rect into UVs, so the two draw paths cannot
// drift apart.
// ---------------------------------------------------------------------------
static void uv_of(const AchvRect* r, float* u0, float* v0, float* u1, float* v1)
{
    *u0 = (float)r->x / (float)ACHV_ATLAS_W;
    *v0 = (float)r->y / (float)ACHV_ATLAS_H;
    *u1 = (float)(r->x + r->w) / (float)ACHV_ATLAS_W;
    *v1 = (float)(r->y + r->h) / (float)ACHV_ATLAS_H;
}

void UiAtlas_Blit(const AchvRect* r, float x, float y, float w, float h,
                  unsigned int color)
{
    if (s_atlas.tex == MARNI_NULL_HANDLE) return;
    float u0, v0, u1, v1;
    uv_of(r, &u0, &v0, &u1, &v1);
    MarniDrawSpriteEx(x, y, w, h, u0, v0, u1, v1, color, s_atlas.tex,
                      MARNI_SAMPLER_LINEAR, MARNI_BLEND_ALPHA);
}

void UiAtlas_Push(const AchvRect* r, float x, float y, float w, float h,
                  unsigned int color, unsigned int depth)
{
    if (s_atlas.tex == MARNI_NULL_HANDLE) return;
    float u0, v0, u1, v1;
    uv_of(r, &u0, &v0, &u1, &v1);
    PendingSprite_Push(x, y, w, h, u0, v0, u1, v1, color, s_atlas.tex, depth);
}

void UiAtlas_PushPixel(const AchvRect* r, float x, float y, float w, float h,
                       unsigned int color, unsigned int depth)
{
    if (s_atlas.tex == MARNI_NULL_HANDLE) return;
    float u0, v0, u1, v1;
    uv_of(r, &u0, &v0, &u1, &v1);
    PendingSprite_PushEx(x, y, w, h, u0, v0, u1, v1, color, s_atlas.tex, depth, 1);
}

void UiAtlas_Fill(float x, float y, float w, float h, unsigned int color)
{
    UiAtlas_Blit(&g_achvWhite, x, y, w, h, color);
}

void UiAtlas_FillPushed(float x, float y, float w, float h,
                        unsigned int color, unsigned int depth)
{
    UiAtlas_Push(&g_achvWhite, x, y, w, h, color, depth);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static const AchvGlyph* glyph_of(int font, unsigned char c)
{
    const AchvGlyph* table = (font == UI_FONT_TITLE) ? g_achvFontTitle
                                                     : g_achvFontBody;
    if (c < ACHV_FONT_FIRST || c > ACHV_FONT_LAST) c = '?';
    return &table[c - ACHV_FONT_FIRST];
}

float UiAtlas_TextWidth(int font, const char* s, float k)
{
    float w = 0.0f;
    if (s == NULL) return 0.0f;
    for (; *s; s++) w += (float)glyph_of(font, (unsigned char)*s)->adv * k;
    return w;
}

// The two text calls differ only in which draw path they hand each glyph to.
static void text_common(int font, const char* s, float x, float y, float k,
                        unsigned int color, int pushed, unsigned int depth)
{
    if (s == NULL) return;
    for (; *s; s++) {
        const AchvGlyph* g = glyph_of(font, (unsigned char)*s);
        if (g->w > 0 && g->h > 0) {
            AchvRect r;
            r.x = g->x; r.y = g->y; r.w = g->w; r.h = g->h;
            const float gx = x + (float)g->bx * k;
            const float gy = y + (float)g->by * k;
            const float gw = (float)g->w * k;
            const float gh = (float)g->h * k;
            if (pushed) UiAtlas_Push(&r, gx, gy, gw, gh, color, depth);
            else        UiAtlas_Blit(&r, gx, gy, gw, gh, color);
        }
        x += (float)g->adv * k;
    }
}

void UiAtlas_Text(int font, const char* s, float x, float y, float k,
                  unsigned int color)
{
    text_common(font, s, x, y, k, color, 0, 0);
}

void UiAtlas_TextPushed(int font, const char* s, float x, float y, float k,
                        unsigned int color, unsigned int depth)
{
    text_common(font, s, x, y, k, color, 1, depth);
}
