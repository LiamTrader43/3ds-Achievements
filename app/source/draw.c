#include "draw.h"

#include <math.h>
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

void Draw_Begin(bool clearTop, bool clearBottom)
{
    s_fb[GFX_TOP] = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    s_fb[GFX_BOTTOM] = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (clearTop)
        memset(s_fb[GFX_TOP], 0, TOP_WIDTH * SCREEN_HEIGHT * 3);
    if (clearBottom)
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

// The light running around the selected game's frame: a lighter stretch of the frame's
// colour moving clockwise, one lap every SHINE_LAP_MS.
#define SHINE_LAP_MS    4800
#define SHINE_HALF      190             // half the light's length, in 1024ths of a lap
#define SHINE_GLOW      5               // the light's glow reaches this far outside the frame
#define SHINE_GLOW_PEAK 180             // and is this strong right at its edge (of 256)
#define SHINE_MAX_BOX   128             // largest frame whose pixels are kept for redraws

static struct
{
    bool        saved;
    gfxScreen_t screen;
    int         x, y, w, h, radius, thickness;
    Color       base, light;
    u8          pixels[(SHINE_MAX_BOX + 2 * SHINE_GLOW) * (SHINE_MAX_BOX + 2 * SHINE_GLOW) * 3];  // without the light
} s_shine;

// Where pixel centre (cx, cy) is along the middle of the frame (a w x h rounded rectangle
// with corner radius r, from its top-left), clockwise from the top edge's left end, in 1024ths.
static int AlongFrame(float cx, float cy, float w, float h, float r)
{
    const float halfPi = 1.57079633f;
    float top = w - 2 * r, side = h - 2 * r, arc = halfPi * r;
    float s;

    if (cx < r && cy < r)
        s = 2 * top + 2 * side + 3 * arc + arc * (atan2f(cy - r, cx - r) + 2 * halfPi) / halfPi;
    else if (cx > w - r && cy < r)
        s = top + arc * (atan2f(cy - r, cx - (w - r)) + halfPi) / halfPi;
    else if (cx > w - r && cy > h - r)
        s = top + arc + side + arc * atan2f(cy - (h - r), cx - (w - r)) / halfPi;
    else if (cx < r && cy > h - r)
        s = 2 * top + 2 * arc + side + arc * (atan2f(cy - (h - r), cx - r) - halfPi) / halfPi;
    else if (cy <= r)
        s = cx - r;
    else if (cy >= h - r)
        s = top + 2 * arc + side + (w - r - cx);
    else if (cx <= r)
        s = 2 * top + 3 * arc + side + (h - r - cy);
    else
        s = top + arc + (cy - r);
    int along = (int)(s * 1024.0f / (2 * top + 2 * side + 4 * arc));
    return along & 1023;
}

// How strongly the light shows at a point along the frame (0-1): 1 at its middle, fading
// to 0 at its ends.
static float LightAt(int along, int head)
{
    int d = (along - head) & 1023;
    if (d > 512)
        d = 1024 - d;
    if (d >= SHINE_HALF)
        return 0.0f;
    return 0.5f + 0.5f * cosf(3.14159265f * d / SHINE_HALF);
}

// Draws the light where it is now. On the frame, only the part of each pixel that's frame
// colour gets lighter (smooth edges, nothing inside touched); just outside it, a soft glow
// fades out over SHINE_GLOW pixels.
static void DrawLight(void)
{
    const int t = s_shine.thickness, w = s_shine.w, h = s_shine.h, r = s_shine.radius, g = SHINE_GLOW;
    const int head = (int)(osGetTime() % SHINE_LAP_MS * 1024 / SHINE_LAP_MS);
    const float mid = t / 2.0f;
    const int add[3] = { s_shine.light.b - s_shine.base.b, s_shine.light.g - s_shine.base.g,
                         s_shine.light.r - s_shine.base.r };
    const u8 light[3] = { s_shine.light.b, s_shine.light.g, s_shine.light.r };

    for (int dy = -g; dy < h + g; dy++)
    {
        // Rows between the corners only have frame and glow at their two ends.
        bool band = dy >= r && dy < h - r;
        for (int dx = -g; dx < w + g; dx++)
        {
            if (band && dx == t + 1)
                dx = w - t - 1;
            int x = s_shine.x + dx, y = s_shine.y + dy;
            if (x < 0 || y < 0 || x >= Draw_Width(s_shine.screen) || y >= SCREEN_HEIGHT)
                continue;

            bool inBox = dx >= 0 && dy >= 0 && dx < w && dy < h;
            int outer = inBox ? Coverage(dx, dy, w, h, r) : 0;
            int inner = 0;
            if (dx >= t && dy >= t && dx < w - t && dy < h - t)
                inner = Coverage(dx - t, dy - t, w - 2 * t, h - 2 * t, r > t ? r - t : 0);
            int frame = outer * (16 - inner);       // how much of the pixel is frame, 0-256

            // Distance outside the frame's outer edge (pixel centre), for the glow.
            float cx = dx + 0.5f, cy = dy + 0.5f;
            float ix = cx < r ? r : cx > w - r ? w - r : cx, iy = cy < r ? r : cy > h - r ? h - r : cy;
            float outside = sqrtf((cx - ix) * (cx - ix) + (cy - iy) * (cy - iy)) - r;
            if (frame <= 0 && outside >= g)
                continue;

            float k = LightAt(AlongFrame(cx - mid, cy - mid, w - t, h - t, r - mid), head);
            if (k <= 0.0f)
                continue;

            u8 *p = s_fb[s_shine.screen] + (x * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y)) * 3;
            if (frame > 0)
            {
                int amount = (int)(k * frame);      // 0-256
                for (int c = 0; c < 3; c++)
                {
                    int v = p[c] + (add[c] * amount >> 8);
                    p[c] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
            }
            if (outer < 16 && outside < g)
            {
                float fade = 1.0f - (outside > 0.0f ? outside : 0.0f) / g;
                int amount = (int)(k * fade * fade * SHINE_GLOW_PEAK * (16 - outer) / 16);   // 0-256
                for (int c = 0; c < 3; c++)
                    p[c] = (u8)(p[c] + ((light[c] - p[c]) * amount >> 8));
            }
        }
    }
}

// Copies the frame's box (with the glow around it) between the back buffer and the saved pixels.
static void CopyBox(bool save)
{
    const int g = SHINE_GLOW, size = SHINE_MAX_BOX + 2 * SHINE_GLOW;

    for (int dx = -g; dx < s_shine.w + g; dx++)
    {
        int x = s_shine.x + dx;
        if (x < 0 || x >= Draw_Width(s_shine.screen))
            continue;
        for (int dy = -g; dy < s_shine.h + g; dy++)
        {
            int y = s_shine.y + dy;
            if (y < 0 || y >= SCREEN_HEIGHT)
                continue;
            u8 *p = s_fb[s_shine.screen] + (x * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y)) * 3;
            u8 *q = s_shine.pixels + ((dy + g) * size + dx + g) * 3;
            if (save)
                memcpy(q, p, 3);
            else
                memcpy(p, q, 3);
        }
    }
}

void Draw_FrameShine(gfxScreen_t screen, int x, int y, int w, int h, int radius, int thickness,
                     Color base, Color light)
{
    s_shine.screen = screen;
    s_shine.x = x, s_shine.y = y, s_shine.w = w, s_shine.h = h;
    s_shine.radius = radius, s_shine.thickness = thickness;
    s_shine.base = base, s_shine.light = light;
    s_shine.saved = w <= SHINE_MAX_BOX && h <= SHINE_MAX_BOX;
    if (s_shine.saved)
        CopyBox(true);
    DrawLight();
}

bool Draw_FrameShineAgain(void)
{
    if (!s_shine.saved)
        return false;
    CopyBox(false);
    DrawLight();
    return true;
}

void Draw_FrameShineOff(void)
{
    s_shine.saved = false;
}
