#ifndef OVERLAY_BLEND_H
#define OVERLAY_BLEND_H

// The banner's background, blended over the game one screen column at a time. A banner
// repair has to finish before the game swaps screens, so the common formats get their
// own loops. No 3DS code here, so it can be tested on a PC.

#include <stdint.h>

#define BLEND_BGR8      1       // GSP_BGR8_OES: bytes B, G, R
#define BLEND_RGBA8     0       // GSP_RGBA8_OES: bytes A, B, G, R

typedef struct { uint8_t r, g, b; } BlendColor;

// Blends `rows` pixels starting at p and going to lower addresses (rows further down
// the screen), bpp bytes apart. alpha is out of 256. Returns false for a format this
// has no fast loop for; the caller then does those itself.
static inline int Blend_Column(uint8_t *p, uint32_t fmt, uint32_t rows, BlendColor color, uint32_t alpha)
{
    const uint32_t keep = 256 - alpha;
    const uint32_t r = color.r * alpha, g = color.g * alpha, b = color.b * alpha;

    if (fmt == BLEND_BGR8)
    {
        for (uint32_t i = 0; i < rows; i++, p -= 3)
        {
            p[0] = (uint8_t)((b + p[0] * keep) >> 8);
            p[1] = (uint8_t)((g + p[1] * keep) >> 8);
            p[2] = (uint8_t)((r + p[2] * keep) >> 8);
        }
        return 1;
    }
    if (fmt == BLEND_RGBA8)
    {
        for (uint32_t i = 0; i < rows; i++, p -= 4)
        {
            p[0] = 0xFF;
            p[1] = (uint8_t)((b + p[1] * keep) >> 8);
            p[2] = (uint8_t)((g + p[2] * keep) >> 8);
            p[3] = (uint8_t)((r + p[3] * keep) >> 8);
        }
        return 1;
    }
    return 0;
}

#endif
