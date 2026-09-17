// PortAtlas.cpp - CUSTOM (port-only): see PortAtlas.h.
#include "PortAtlas.h"
#include "FileLoader.h"
#include "../marni/MarniSystem.h"

#include <stdlib.h>
#include <string.h>

#define PORT_ATLAS_MAX_TRIES 3

int PortAtlas_Load(PortAtlas* atlas, const char* path, const char* tag,
                   int width, int height)
{
    if (atlas->tex != MARNI_NULL_HANDLE) return 1;
    if (atlas->tries >= PORT_ATLAS_MAX_TRIES) return 0;
    if (!IsGraphicsSystemReadyForOperation()) return 0;   // device not up yet
    atlas->tries++;

    const size_t pixels = (size_t)width * (size_t)height * 4;
    const size_t expect = pixels + 12;                    // tag + w + h
    unsigned char* buf = (unsigned char*)malloc(expect);
    if (buf == NULL) return 0;

    size_t read = LoadFile(path, buf, 0);
    if (read == expect && memcmp(buf, tag, 4) == 0) {
        const unsigned int w = *(unsigned int*)(buf + 4);
        const unsigned int h = *(unsigned int*)(buf + 8);
        if (w == (unsigned int)width && h == (unsigned int)height) {
            // bpp 32 is memcpy'd straight into an R8G8B8A8 texture
            // (MarniDX::CreateTexture), so the file's byte order IS the
            // texture's: R, G, B, A per pixel, which is what the bakers write
            // with Image.tobytes().
            MarniCreateTexture(width, height, 32, buf + 12, &atlas->tex);
        }
    }
    free(buf);
    return atlas->tex != MARNI_NULL_HANDLE;
}

void PortAtlas_Reset(PortAtlas* atlas)
{
    atlas->tex = MARNI_NULL_HANDLE;
    atlas->tries = 0;
}
