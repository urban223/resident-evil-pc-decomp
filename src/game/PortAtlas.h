// PortAtlas.h - CUSTOM (port-only): the one loader for the baked RGBA atlases.
//
// The port's own UI comes out of two sheets - achvui.bin (the Space GUI atlas,
// UiAtlas.cpp) and edui.bin (the editor, editor/ui/EditorUIDraw.cpp). Both are
// written by the same kind of baker and share one file layout:
//
//   4 bytes  tag ('AUI1', 'EUI1', ...)
//   u32      width
//   u32      height
//   w*h*4    pixels, R G B A per pixel, top row first
//
// The loader used to exist twice with only the tag and the size changed; this
// is that function once. Each caller still owns its own PortAtlas, so the two
// sheets load, fail and reset independently.
#pragma once

#include "../marni/MarniDX.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortAtlas {
    MarniHandle tex;     // MARNI_NULL_HANDLE until the upload succeeds
    int         tries;   // load attempts made while the device was up
} PortAtlas;

#define PORT_ATLAS_INIT { MARNI_NULL_HANDLE, 0 }

// Load a baked RGBA atlas: <tag> + w + h + rows. Answers 0 until the graphics
// device is up; gives up after a few attempts so a missing file is not
// reopened every frame. `tag` is the four tag bytes, no terminator needed;
// `width`/`height` are the compiled-in size the file must match.
int  PortAtlas_Load(PortAtlas* atlas, const char* path, const char* tag,
                    int width, int height);

// Forget the texture and the attempts, so the next Load uploads again. For
// device loss: a resolution change destroys every texture, and drawing from
// the old handle would name nothing.
void PortAtlas_Reset(PortAtlas* atlas);

#ifdef __cplusplus
}
#endif
