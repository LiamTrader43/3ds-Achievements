#ifndef DRAW_H
#define DRAW_H

// Software drawing into the two screens' framebuffers (BGR8, stored rotated). Text
// is Inter, pre-rendered smooth (font_inter.h, made by tools/app_font.py).

#include <3ds.h>

#define TOP_WIDTH       400
#define BOTTOM_WIDTH    320
#define SCREEN_HEIGHT   240
#define LINE_H          13      // height of a line of text at scale 1 (scale 2: twice that)
#define ICON_SIZE       48

typedef struct { u8 r, g, b; } Color;

void    Draw_Init(void);
void    Draw_Exit(void);

// Starts a frame, clearing the back buffer of each screen that will be redrawn. A screen
// that isn't keeps what it showed two frames ago (both its buffers must be up to date).
void    Draw_Begin(bool clearTop, bool clearBottom);
// Shows the frame and waits for the next vblank.
void    Draw_End(void);

void    Draw_Rect(gfxScreen_t screen, int x, int y, int w, int h, Color color);

// A rectangle with corners of the given radius, their edges smoothed.
void    Draw_RoundRect(gfxScreen_t screen, int x, int y, int w, int h, int radius, Color color);

// Draws text at scale 1 or 2 in a line whose top is y, cut at maxWidth pixels
// (0 = screen edge). Returns the width drawn.
int     Draw_Text(gfxScreen_t screen, int x, int y, int scale, Color color, const char *text, int maxWidth);

// Pixel width of text, as Draw_Text would draw it.
int     Draw_TextWidth(const char *text, int scale);

// How many of text's first characters fit in width pixels.
int     Draw_TextFit(const char *text, int scale, int width);

// A 48x48 SMDH icon (RGB565 in 8x8 tiles, Z-order inside each tile).
void    Draw_Icon(gfxScreen_t screen, int x, int y, const u16 *icon);

// The same icon smoothly enlarged to size x size pixels (size > 48), with rounded corners.
void    Draw_IconScaled(gfxScreen_t screen, int x, int y, int size, int radius, const u16 *icon);

// A full top-screen image (400x240 BGR8 in framebuffer order) as a dim background
// that's darker toward the bottom. fade: 0 (invisible) to 256 (fully faded in).
void    Draw_Hero(const u8 *pixels, int fade);

// A w x h RGB image (rows top to bottom) at (x, y).
void    Draw_Image(gfxScreen_t screen, int x, int y, int w, int h, const u8 *rgb);

// A light running around a rounded frame that's already drawn (a w x h box with corner
// radius, the frame thickness pixels wide inside its edge, in colour base): a lighter
// stretch (light) moving clockwise, one lap every 4.8 s. Remembers the box without the light,
// so Draw_FrameShineAgain can move the light in a frame where nothing else is redrawn.
void    Draw_FrameShine(gfxScreen_t screen, int x, int y, int w, int h, int radius, int thickness,
                        Color base, Color light);

// Puts the frame's box back and draws the light at its new place. False if there's no frame.
bool    Draw_FrameShineAgain(void);

// Forgets the frame (nothing is shining).
void    Draw_FrameShineOff(void);

// Pixel width of a screen.
int     Draw_Width(gfxScreen_t screen);

#endif
