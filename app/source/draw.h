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

// What a frame does with a screen (Draw_Begin): KEEP = not redrawn, its back buffer keeps
// what it showed two frames ago (both its buffers must be up to date); CLEAR = redrawn on
// black; COVER = redrawn by drawing that sets every pixel (the background, the list), so
// it isn't cleared first. true and false work as CLEAR and KEEP.
enum { DRAW_KEEP = 0, DRAW_CLEAR = 1, DRAW_COVER = 2 };

// Starts a frame.
void    Draw_Begin(int top, int bottom);

// Stereoscopic 3D on the top screen. Call once a frame, before Draw_Begin, with the 3D
// slider's position (0-1; 0 turns 3D off). True when the top screen must be redrawn: 3D
// turned on or off, or the slider moved.
bool    Draw_Stereo(float slider);

// How many images the top screen has this frame: 2 with 3D on (left and right eye), else 1.
// Draw the top screen once for each, after Draw_Eye.
int     Draw_Eyes(void);

// Top-screen drawing goes to the left (0) or right (1) eye's image from now on, at depth 0.
// Draw_Begin starts with the left.
void    Draw_Eye(int eye);

// How far in front of the screen (positive) or behind it (negative) what's drawn next on the
// top screen appears: the gap between the two eyes' images with the slider all the way up, in
// pixels. The left image is the same at any depth (it's what 2D shows); the right one moves.
void    Draw_Depth(int depth);

// Shows the frame (flushing only the columns drawn) and waits for the next vblank.
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

// A trophy icon (the same format) whose pure black pixels, around its rounded square, are left
// out, so it sits on any background.
void    Draw_TrophyIcon(gfxScreen_t screen, int x, int y, const u16 *icon);

// The same icon smoothly enlarged to size x size pixels (2-128), with rounded corners, and
// darkened by dim (0-256, like Draw_Darken over it). frame: a colour for a frame DRAW_FRAME
// pixels wide around it (its corners DRAW_FRAME more round), or NULL. The last few sizes
// are kept, so a tile that doesn't change size (or the same tile in the other eye's image)
// is only copied. quickFrom: while a tile is changing size, the size of a kept image to take
// the nearest pixels from instead (much quicker; 0 = the exact image).
#define DRAW_FRAME  2
void    Draw_IconScaled(gfxScreen_t screen, int x, int y, int size, int radius, const u16 *icon, int dim,
                        const Color *frame, int quickFrom);

// Makes a game's background (hero.h: BGR8 in framebuffer order, HERO_MARGIN columns wider
// than the screen on each side) into the dim picture Draw_Hero shows, darker toward the
// bottom. dimmed: HERO_BYTES, 4-byte aligned.
void    Draw_DimHero(const u8 *pixels, u8 *dimmed);

// That picture as the top screen's background in every eye's image, at depth. fade: 0
// (invisible) to 256 (fully faded in). Draw it first: it sets every pixel.
void    Draw_Hero(const u8 *dimmed, int fade, int depth);

// A w x h RGB image (rows top to bottom) at (x, y).
void    Draw_Image(gfxScreen_t screen, int x, int y, int w, int h, const u8 *rgb);

// A light running around a rounded frame that's already drawn (a w x h box with corner
// radius, the frame thickness pixels wide inside its edge, in colour base): a lighter
// stretch (light) moving clockwise, one lap every 4.8 s. Remembers the box without the light,
// so Draw_FrameShineAgain can move the light in a frame where nothing else is redrawn.
void    Draw_FrameShine(gfxScreen_t screen, int x, int y, int w, int h, int radius, int thickness,
                        Color base, Color light);

// Puts the frame's box back and draws the light at its new place (in each eye's image, with
// 3D on). False if there's no frame.
bool    Draw_FrameShineAgain(void);

// Forgets the frame (nothing is shining).
void    Draw_FrameShineOff(void);

// Draw_Text with a 1 px dark shadow below and to the right, for text over pictures.
int     Draw_TextShadow(gfxScreen_t screen, int x, int y, int scale, Color color, const char *text, int maxWidth);

// A soft shadow under a rounded box (x, y, w, h, radius), offsetY pixels lower, fading out
// over blur pixels: darkens what's there by up to strength (1-255, of 256). Draw it before
// the box. coveredAt: the depth the box will be drawn at, solid, over it (what's under it
// isn't darkened), or DRAW_UNCOVERED.
#define DRAW_UNCOVERED  0x7FFF
void    Draw_Shadow(gfxScreen_t screen, int x, int y, int w, int h, int radius, int blur, int offsetY, int strength,
                    int coveredAt);

// Darkens a rounded box's pixels by amount (0-256), edges smoothed.
void    Draw_Darken(gfxScreen_t screen, int x, int y, int w, int h, int radius, int amount);

// Only the outline of a rounded box, thickness pixels wide inside its edge, at alpha (0-16).
void    Draw_RoundRing(gfxScreen_t screen, int x, int y, int w, int h, int radius, int thickness, Color color, int alpha);

// A rounded box filled with a colour and things drawn on it (a button hint), kept so it can be
// drawn again without redrawing what's on it: inside its edge it doesn't depend on what's
// under it, only its smoothed corners do.
#define DRAW_STAMP_W    400
#define DRAW_STAMP_H    32
typedef struct
{
    bool        kept;
    gfxScreen_t screen;
    int         x, y, w, h, radius;
    Color       color;
    u8          pixels[DRAW_STAMP_W * DRAW_STAMP_H * 3];    // column by column, bottom row first
} DrawStamp;

// Keeps the box (x, y, w, h, radius, filled with color) as just drawn, at the current depth.
void    Draw_StampKeep(DrawStamp *s, gfxScreen_t screen, int x, int y, int w, int h, int radius, Color color);

// Draws it again at the current depth. False if it wasn't kept (or doesn't fit there).
bool    Draw_StampAgain(const DrawStamp *s);

// Fills rows y0 to y1 (exclusive) with a vertical fade from top to bottom.
void    Draw_Gradient(gfxScreen_t screen, int y0, int y1, Color top, Color bottom);

// Mixes rows y0 to y1 toward that fade, keeping keep/256 of what's there (for fading in).
// from: a whole screen drawn with Draw_Into to take the pixels from instead of what's
// there (then give every row: every pixel of the screen is set), or NULL.
void    Draw_FadeToGradient(gfxScreen_t screen, int y0, int y1, Color top, Color bottom, int keep, const u8 *from);

// Draws on screen into buffer (Draw_Width(screen) * SCREEN_HEIGHT * 3 bytes, 4-byte aligned)
// instead of its back buffer, until called with NULL. Not for the top screen with 3D on.
void    Draw_Into(gfxScreen_t screen, u8 *buffer);

// Pixel width of a screen.
int     Draw_Width(gfxScreen_t screen);

#endif
