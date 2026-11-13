#include "draw.h"

#include <string.h>

#include "font_inter.h"

static u8 *s_fb[2];     // back buffers of GFX_TOP and GFX_BOTTOM for this frame

void Draw_Init(void)
{
    gfxInitDefault();   // both screens BGR8, double buffered
}

void Draw_Exit(void)
{
    gfxExit();
}

int Draw_Width(gfxScreen_t screen)
{
    return screen == GFX_TOP ? TOP_WIDTH : BOTTOM_WIDTH;
}

void Draw_Begin(bool clearTop)
{
    s_fb[GFX_TOP] = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    s_fb[GFX_BOTTOM] = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (clearTop)
        memset(s_fb[GFX_TOP], 0, TOP_WIDTH * SCREEN_HEIGHT * 3);
    memset(s_fb[GFX_BOTTOM], 0, BOTTOM_WIDTH * SCREEN_HEIGHT * 3);
}

void Draw_End(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

// Framebuffers are stored rotated: column x is 240 pixels, bottom row first.
static inline void Plot(gfxScreen_t screen, int x, int y, Color c)
{
    if (x < 0 || y < 0 || x >= Draw_Width(screen) || y >= SCREEN_HEIGHT)
        return;
    u8 *p = s_fb[screen] + (x * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y)) * 3;
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

// Draws color over what's there; coverage is 0 (nothing) to 16 (solid).
static inline void Blend(gfxScreen_t screen, int x, int y, Color c, int coverage)
{
    if (coverage >= 16)
    {
        Plot(screen, x, y, c);
        return;
    }
    if (coverage <= 0 || x < 0 || y < 0 || x >= Draw_Width(screen) || y >= SCREEN_HEIGHT)
        return;
    u8 *p = s_fb[screen] + (x * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y)) * 3;
    p[0] = (u8)((p[0] * (16 - coverage) + c.b * coverage) / 16);
    p[1] = (u8)((p[1] * (16 - coverage) + c.g * coverage) / 16);
    p[2] = (u8)((p[2] * (16 - coverage) + c.r * coverage) / 16);
}

// How much of pixel (x, y) of a w x h box with rounded corners is inside it (0-16).
static int Coverage(int x, int y, int w, int h, int radius)
{
    int cx = x < radius ? radius : x >= w - radius ? w - radius : -1;
    int cy = y < radius ? radius : y >= h - radius ? h - radius : -1;
    int limit = radius * 8 * radius * 8, inside = 0;

    if (cx < 0 || cy < 0)
        return 16;
    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++)
        {
            int dx = x * 8 + 2 * sx + 1 - cx * 8;
            int dy = y * 8 + 2 * sy + 1 - cy * 8;
            if (dx * dx + dy * dy <= limit)
                inside++;
        }
    return inside;
}

void Draw_RoundRect(gfxScreen_t screen, int x, int y, int w, int h, int radius, Color color)
{
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            Blend(screen, x + dx, y + dy, color, Coverage(dx, dy, w, h, radius));
}

void Draw_Rect(gfxScreen_t screen, int x, int y, int w, int h, Color color)
{
    for (int dx = 0; dx < w; dx++)
        for (int dy = 0; dy < h; dy++)
            Plot(screen, x + dx, y + dy, color);
}

static const FontGlyph *Glyph(int scale, unsigned char c)
{
    if (c < 32 || c > 126)
        c = '?';
    return scale >= 2 ? &font26_glyphs[c - 32] : &font13_glyphs[c - 32];
}

// The pen moves in 1/64 pixels; a position is rounded to the nearest pixel.
static inline int Pixels(int pen64)
{
    return (pen64 + 32) >> 6;
}

int Draw_TextWidth(const char *text, int scale)
{
    int pen64 = 0;
    for (const char *s = text; *s != '\0'; s++)
        pen64 += Glyph(scale, (unsigned char)*s)->adv64;
    return Pixels(pen64);
}

int Draw_TextFit(const char *text, int scale, int width)
{
    int pen64 = 0, n = 0;
    for (; text[n] != '\0'; n++)
    {
        pen64 += Glyph(scale, (unsigned char)text[n])->adv64;
        if (Pixels(pen64) > width)
            break;
    }
    return n;
}

int Draw_Text(gfxScreen_t screen, int x, int y, int scale, Color color, const char *text, int maxWidth)
{
    int limit = maxWidth > 0 ? x + maxWidth : Draw_Width(screen);
    int pen64 = x << 6;

    for (const char *s = text; *s != '\0'; s++)
    {
        const FontGlyph *g = Glyph(scale, (unsigned char)*s);
        if (Pixels(pen64 + g->adv64) > limit)
            break;
        const unsigned char *alpha = (scale >= 2 ? font26_alpha : font13_alpha) + g->off;
        int gx = Pixels(pen64) + g->x;
        for (int row = 0; row < g->h; row++)
            for (int col = 0; col < g->w; col++, alpha++)
                if (*alpha != 0)
                    Blend(screen, gx + col, y + g->y + row, color, (*alpha * 16 + 127) / 255);
        pen64 += g->adv64;
    }
    return Pixels(pen64) - x;
}

void Draw_Hero(const u8 *pixels, int fade)
{
    u8 *out = s_fb[GFX_TOP];

    for (int x = 0; x < TOP_WIDTH; x++)
        for (int row = 0; row < SCREEN_HEIGHT; row++)    // row 0 is the bottom of the screen
        {
            int level = 40 + 110 * row / (SCREEN_HEIGHT - 1);                   // out of 256
            level = level * fade >> 8;
            for (int c = 0; c < 3; c++)
                out[c] = (u8)(pixels[c] * level >> 8);
            out += 3;
            pixels += 3;
        }
}

// Icon pixel i: where it goes in the 48x48 image (8x8 tiles, Z-order inside each) and its colour.
static inline void IconPixel(const u16 *icon, int i, int *px, int *py, Color *c)
{
    int tile = i / 64, in = i % 64;
    u16 v = icon[i];

    *px = (tile % 6) * 8 + ((in & 1) | ((in >> 1) & 2) | ((in >> 2) & 4));
    *py = (tile / 6) * 8 + (((in >> 1) & 1) | ((in >> 2) & 2) | ((in >> 3) & 4));
    c->r = (u8)(((v >> 11) & 0x1F) * 255 / 31);
    c->g = (u8)(((v >> 5) & 0x3F) * 255 / 63);
    c->b = (u8)((v & 0x1F) * 255 / 31);
}

void Draw_Icon(gfxScreen_t screen, int x, int y, const u16 *icon)
{
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++)
    {
        int   px, py;
        Color c;
        IconPixel(icon, i, &px, &py, &c);
        Plot(screen, x + px, y + py, c);
    }
}

static inline u8 Mix(u8 a, u8 b, u8 c, u8 d, int wx, int wy)
{
    int top = a * (256 - wx) + b * wx;
    int bottom = c * (256 - wx) + d * wx;
    return (u8)((top * (256 - wy) + bottom * wy) >> 16);
}

void Draw_IconScaled(gfxScreen_t screen, int x, int y, int size, int radius, const u16 *icon)
{
    static Color image[ICON_SIZE][ICON_SIZE];
    const int span = (ICON_SIZE - 1) * 256;     // source positions in 1/256ths of a pixel

    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++)
    {
        int   px, py;
        Color c;
        IconPixel(icon, i, &px, &py, &c);
        image[py][px] = c;
    }

    for (int dy = 0; dy < size; dy++)
    {
        int fy = dy * span / (size - 1), y0 = fy >> 8, wy = fy & 255;
        int y1 = y0 + 1 < ICON_SIZE ? y0 + 1 : y0;

        for (int dx = 0; dx < size; dx++)
        {
            int fx = dx * span / (size - 1), x0 = fx >> 8, wx = fx & 255;
            int x1 = x0 + 1 < ICON_SIZE ? x0 + 1 : x0;
            Color a = image[y0][x0], b = image[y0][x1], c = image[y1][x0], d = image[y1][x1];
            Color out = { Mix(a.r, b.r, c.r, d.r, wx, wy), Mix(a.g, b.g, c.g, d.g, wx, wy), Mix(a.b, b.b, c.b, d.b, wx, wy) };
            Blend(screen, x + dx, y + dy, out, Coverage(dx, dy, size, size, radius));
        }
    }
}

void Draw_Image(gfxScreen_t screen, int x, int y, int w, int h, const u8 *rgb)
{
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++, rgb += 3)
        {
            Color c = { rgb[0], rgb[1], rgb[2] };
            Plot(screen, x + col, y + row, c);
        }
}
