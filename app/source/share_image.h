#ifndef SHARE_IMAGE_H
#define SHARE_IMAGE_H

// The share image: one square picture of a game's progress (title, the game's icon over
// its background, its name, trophy + progress bar + count), and its PNG file. Plain C
// with no 3DS calls (zlib for the PNG), so it can be tested on a PC.

#include <stdbool.h>
#include <stdint.h>

#define SHARE_SIZE      720     // the image is SHARE_SIZE x SHARE_SIZE pixels
#define SHARE_PREVIEW   240     // a third of that, for the top screen
#define SHARE_BYTES     (SHARE_SIZE * SHARE_SIZE * 3)

typedef struct
{
    const char     *name;           // the game's name
    const uint16_t *icon;           // its 48x48 SMDH icon (RGB565 in 8x8 tiles), or NULL
    const uint16_t *trophy;         // its unlocked-achievement icon, same format
    const uint8_t  *hero;           // its background (hero.bin in hero.h's wide format), or NULL
    int             unlocked, total;
} ShareInfo;

// Draws the image into rgb (SHARE_BYTES: RGB, rows top to bottom).
void     ShareImage_Draw(const ShareInfo *info, uint8_t *rgb);

// A third of the size (SHARE_PREVIEW x SHARE_PREVIEW, RGB): each pixel the average of 3x3.
void     ShareImage_Shrink(const uint8_t *rgb, uint8_t *preview);

// The image as a PNG file, in a buffer from malloc (NULL if there's no memory).
uint8_t *ShareImage_Png(const uint8_t *rgb, int width, int height, uint32_t *size);

#endif
