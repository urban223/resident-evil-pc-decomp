// PortText.h - CUSTOM (port-only): measuring and drawing the baked fonts.
//
// Both baked atlases carry fonts - achvui.bin (the toast and the status-screen
// skin) and edui.bin (the editor) - in the same glyph layout, and the loops that
// measure and draw a string with them used to exist three times over:
// UiAtlas.cpp, Achievements.cpp and editor/ui/EditorUIDraw.cpp. They are here
// once. The one genuine difference between the callers is where a glyph's quad
// goes - drawn now, queued at a depth, or cut against the editor's clip rect -
// so each caller hands that over as a sink.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// One baked glyph, as the generators (tools/build_achievement_ui.py,
// tools/build_editor_ui.py) emit it. x/y/w/h is its box in the atlas. bx/by
// offset that box from the pen position (by is measured DOWN from the line's
// top, i.e. the PIL bbox origin); adv is the horizontal advance. The unit is
// whatever the atlas was baked at - atlas pixels for the toast, 2x design
// pixels for the editor - and the `k` every call takes converts it.
typedef struct PortGlyph { short x, y, w, h; short bx, by; short adv; } PortGlyph;

// A glyph table and the character range it covers. Characters outside the
// range draw as '?'.
typedef struct PortFont {
    const PortGlyph* glyphs;
    int              first;
    int              last;
} PortFont;

// Receives one glyph that has pixels (spaces are skipped, but still advance):
// its atlas box in `g`, and the destination rectangle, already scaled.
typedef void (*PortTextSink)(void* user, const PortGlyph* g,
                             float x, float y, float w, float h);

const PortGlyph* PortText_Glyph(const PortFont* font, unsigned char c);

// Advance width of `s`, times k. NULL measures 0.
float PortText_Width(const PortFont* font, const char* s, float k);

// Pen starts at x; y is the top of the line box, not the baseline.
void  PortText_Draw(const PortFont* font, const char* s, float x, float y,
                    float k, PortTextSink sink, void* user);

// The longest prefix of `s` that still fits `maxW` with "..." after it, written
// to `out` (cap >= 4). Returns the width of what was written. Meant for when the
// whole string has already been measured and does not fit.
float PortText_Ellipsise(const PortFont* font, const char* s, float k,
                         float maxW, char* out, int cap);

#ifdef __cplusplus
}
#endif
