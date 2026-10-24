#ifndef OVERLAY_H
#define OVERLAY_H

#include <3ds.h>

typedef struct
{
    u8 r, g, b;
} Rgb;

// One thing to draw on a full screen: text, or a filled rectangle when text
// is NULL.
typedef struct
{
    u16         x, y;
    u16         width, height;  // rectangles only
    Rgb         color;
    const char *text;
} OverlayItem;

#define OVERLAY_MAX_ITEMS   16
#define OVERLAY_FONT_WIDTH  6
#define OVERLAY_FONT_HEIGHT 10

// Shows a banner across the top of the bottom screen for about `milliseconds`:
// `title` on the first line, `text` on the second with `rightText` right-
// aligned. Drawn straight into whichever framebuffer is on screen (Luma's
// kernel backdoor), and redrawn whenever the game swaps buffers. Blocks while
// shown. The game's next frames replace it afterwards.
void    Overlay_Show(const char *title, const char *text, const char *rightText, u32 milliseconds);

// A one-line summary of what the last Overlay_Show saw (timing, buffer swaps,
// whether the banner was restored before or after a frame was on screen).
void    Overlay_Stats(char *out, u32 size);

// The bottom screen's on-screen framebuffer (physical address), 0 if unknown.
u32     Overlay_Buffer(void);

// Replaces the whole bottom screen with black and the items (at most
// OVERLAY_MAX_ITEMS), opaque. Every pixel is written once with its final
// colour, so redrawing the same items shows no flicker. Meant for a paused
// game: a running one draws over it.
void    Overlay_DrawScreen(const OverlayItem *items, u32 count);

#endif
