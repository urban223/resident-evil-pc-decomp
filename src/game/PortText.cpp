// PortText.cpp - CUSTOM (port-only): see PortText.h.
#include "PortText.h"

#include <stddef.h>
#include <string.h>

const PortGlyph* PortText_Glyph(const PortFont* font, unsigned char c)
{
    if (c < font->first || c > font->last) c = '?';
    return &font->glyphs[c - font->first];
}

float PortText_Width(const PortFont* font, const char* s, float k)
{
    float w = 0.0f;
    if (s == NULL) return 0.0f;
    for (; *s; s++) w += (float)PortText_Glyph(font, (unsigned char)*s)->adv * k;
    return w;
}

void PortText_Draw(const PortFont* font, const char* s, float x, float y,
                   float k, PortTextSink sink, void* user)
{
    if (s == NULL) return;
    for (; *s; s++) {
        const PortGlyph* g = PortText_Glyph(font, (unsigned char)*s);
        if (g->w > 0 && g->h > 0) {
            sink(user, g, x + (float)g->bx * k, y + (float)g->by * k,
                 (float)g->w * k, (float)g->h * k);
        }
        x += (float)g->adv * k;
    }
}

float PortText_Ellipsise(const PortFont* font, const char* s, float k,
                         float maxW, char* out, int cap)
{
    const float dots = PortText_Width(font, "...", k);
    float acc = 0.0f;
    int n = 0;
    while (s[n] != '\0' && n < cap - 4) {
        const float adv = (float)PortText_Glyph(font, (unsigned char)s[n])->adv * k;
        if (acc + adv + dots > maxW) break;
        acc += adv;
        n++;
    }
    memcpy(out, s, (size_t)n);
    out[n] = '.'; out[n + 1] = '.'; out[n + 2] = '.'; out[n + 3] = '\0';
    return acc + dots;
}
