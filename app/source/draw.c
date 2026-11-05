#include "draw.h"

#include <string.h>

#include "font.h"   // the tracker's copy of Rosalina's font (sysmodule/tracker/source)

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

void Draw_Begin(void)
{
    s_fb[GFX_TOP] = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    s_fb[GFX_BOTTOM] = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
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

void Draw_Rect(gfxScreen_t screen, int x, int y, int w, int h, Color color)
{
    for (int dx = 0; dx < w; dx++)
        for (int dy = 0; dy < h; dy++)
            Plot(screen, x + dx, y + dy, color);
}

int Draw_Text(gfxScreen_t screen, int x, int y, int scale, Color color, const char *text, int maxWidth)
{
    int limit = maxWidth > 0 ? x + maxWidth : Draw_Width(screen);
    int start = x;
    int step = FONT_WIDTH * scale;

    for (const char *s = text; *s != '\0' && x + step <= limit; s++, x += step)
    {
        const unsigned char *glyph = &font[(unsigned char)*s * FONT_HEIGHT];
        for (int row = 0; row < FONT_HEIGHT; row++)
            for (int col = 0; col < FONT_WIDTH; col++)
                if ((glyph[row] >> (6 - col)) & 1)
                    Draw_Rect(screen, x + col * scale, y + row * scale, scale, scale, color);
    }
    return x - start;
}

void Draw_Icon(gfxScreen_t screen, int x, int y, const u16 *icon)
{
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++)
    {
        int tile = i / 64, in = i % 64;
        int px = (tile % 6) * 8 + ((in & 1) | ((in >> 1) & 2) | ((in >> 2) & 4));
        int py = (tile / 6) * 8 + (((in >> 1) & 1) | ((in >> 2) & 2) | ((in >> 3) & 4));
        u16 v = icon[i];
        Color c = { (u8)(((v >> 11) & 0x1F) * 255 / 31), (u8)(((v >> 5) & 0x3F) * 255 / 63), (u8)((v & 0x1F) * 255 / 31) };
        Plot(screen, x + px, y + py, c);
    }
}
