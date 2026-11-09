#ifndef DRAW_H
#define DRAW_H

// Software drawing into the two screens' framebuffers (BGR8, stored rotated),
// with the same 6x10 font as the tracker's in-game menu.

#include <3ds.h>

#define TOP_WIDTH       400
#define BOTTOM_WIDTH    320
#define SCREEN_HEIGHT   240
#define CHAR_W          6
#define CHAR_H          10
#define ICON_SIZE       48

typedef struct { u8 r, g, b; } Color;

void    Draw_Init(void);
void    Draw_Exit(void);

// Starts a frame: both back buffers cleared to black.
void    Draw_Begin(void);
// Shows the frame and waits for the next vblank.
void    Draw_End(void);

void    Draw_Rect(gfxScreen_t screen, int x, int y, int w, int h, Color color);

// A rectangle with corners of the given radius, their edges smoothed.
void    Draw_RoundRect(gfxScreen_t screen, int x, int y, int w, int h, int radius, Color color);

// Draws text at scale 1 or 2, cut at maxWidth pixels (0 = screen edge).
// Returns the width drawn.
int     Draw_Text(gfxScreen_t screen, int x, int y, int scale, Color color, const char *text, int maxWidth);

// A 48x48 SMDH icon (RGB565 in 8x8 tiles, Z-order inside each tile).
void    Draw_Icon(gfxScreen_t screen, int x, int y, const u16 *icon);

// The same icon smoothly enlarged to size x size pixels (size > 48), with rounded corners.
void    Draw_IconScaled(gfxScreen_t screen, int x, int y, int size, int radius, const u16 *icon);

// A full top-screen image (400x240 BGR8 in framebuffer order) as a dim background
// that's darkest behind the title and toward the bottom. fade: 0 (invisible) to 256 (fully faded in).
void    Draw_Hero(const u8 *pixels, int fade);

// Pixel width of a screen.
int     Draw_Width(gfxScreen_t screen);

#endif
