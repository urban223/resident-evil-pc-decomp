// UiAtlas.h - CUSTOM (port-only): the shared Space GUI atlas.
//
// One RGBA texture (assets/USA/Data/achvui.bin, baked by
// tools/build_achievement_ui.py) holds every port-added UI element: the
// achievement toast's plate and icons, the two fonts, and the small marks the
// status-screen skin draws with. Both features load it through here so there
// is one texture and one load path rather than two.
//
// Two ways to draw from it, and the difference matters:
//
//   UiAtlas_Blit  draws NOW, straight through the Marni layer. Whatever is
//                 drawn this way lands on top of everything already drawn
//                 this frame - which is what a notification wants.
//   UiAtlas_Push  queues the quad in the renderer's pending-sprite list at a
//                 given depth, so it sorts against the game's own sprites.
//                 The status-screen skin uses this: its plates must end up
//                 BEHIND the item icons the engine draws, not over them.
#pragma once

#include "../platform/types.h"
#include "AchievementAtlasData.h"

#ifdef __cplusplus
extern "C" {
#endif

// Load on first use; answers 0 until the graphics device is up. Safe to call
// every frame - the load is attempted a few times and then left alone.
int  UiAtlas_Ready(void);

// Atlas pixels -> backbuffer pixels for UI that scales with the window.
// (backbufferHeight / 480) * 0.5, clamped, the same factor the toast uses.
float UiAtlas_Scale(void);

// Immediate draw (see the note above). Colour is 0xAARRGGBB.
void UiAtlas_Blit(const AchvRect* r, float x, float y, float w, float h,
                  unsigned int color);

// Queued draw at `depth`. Lower depth = nearer the viewer. The game's item
// icons land around 644-676, so skin plates want ~700 and a full-screen scrim
// ~1000; anything below 644 draws over the icons.
void UiAtlas_Push(const AchvRect* r, float x, float y, float w, float h,
                  unsigned int color, unsigned int depth);

// Same, but nearest-neighbour. For art that IS pixels - the small hand-plotted
// marks - where filtering only blurs what was drawn deliberately.
void UiAtlas_PushPixel(const AchvRect* r, float x, float y, float w, float h,
                       unsigned int color, unsigned int depth);

// A solid rectangle, from the atlas's white block.
void UiAtlas_Fill(float x, float y, float w, float h, unsigned int color);
void UiAtlas_FillPushed(float x, float y, float w, float h,
                        unsigned int color, unsigned int depth);

// Text from one of the two baked fonts. `font` selects it; scale is in
// backbuffer pixels per atlas pixel (pass UiAtlas_Scale() * k).
#define UI_FONT_TITLE 0
#define UI_FONT_BODY  1

float UiAtlas_TextWidth(int font, const char* s, float k);
void  UiAtlas_Text(int font, const char* s, float x, float y, float k,
                   unsigned int color);
void  UiAtlas_TextPushed(int font, const char* s, float x, float y, float k,
                         unsigned int color, unsigned int depth);

#ifdef __cplusplus
}
#endif
