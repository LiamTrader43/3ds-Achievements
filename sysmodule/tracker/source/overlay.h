#ifndef OVERLAY_H
#define OVERLAY_H

#include <3ds.h>

typedef struct
{
    u8 r, g, b;
} Rgb;

// One thing to draw on a full screen: text (a line LINE_H tall from y), a filled
// rectangle when text is NULL, or a 48x48 RGB565 icon in SMDH tile order when icon
// isn't NULL. A rectangle with outline > 0 is only its outline, that many pixels wide,
// with corners rounded at radius (edges smoothed). Items are drawn in order, later ones
// over earlier ones.
typedef struct
{
    u16         x;
    s16         y;              // may be above the screen; what's off it is clipped
    u16         width, height;  // rectangles only
    Rgb         color;
    const char *text;
    const u16  *icon;
    u8          outline, radius;    // rectangles only
} OverlayItem;

#define OVERLAY_MAX_ITEMS   36
#define OVERLAY_ICON_SIZE   48
#define OVERLAY_LINE_H      13      // a line of text: Inter, as in the app (app/source/font_inter.h)
#define OVERLAY_BANNER_W    304     // room for a line of text in the banner

// Pixel width of text.
int     Overlay_TextWidth(const char *text);

// How many of text's first characters fit in width pixels.
int     Overlay_TextFit(const char *text, int width);

// Copies text cut to maxWidth pixels, ending in "..." if it was longer.
void    Overlay_Ellipsize(char *out, u32 capacity, const char *text, int maxWidth);

// Shows a banner across the top of the bottom screen for about `milliseconds`:
// `title` on the first line, `text` on the second with `rightText` right-
// aligned (text is cut with "..." to fit). Drawn straight into whichever
// framebuffer is on screen (Luma's kernel backdoor), and redrawn whenever the
// game swaps buffers. Blocks while shown. The game's next frames replace it afterwards.
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
