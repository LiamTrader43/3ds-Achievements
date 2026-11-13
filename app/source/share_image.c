#include "share_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "share_font.h"

// Layout, in pixels of the 720x720 image (the mock was drawn on a 240 grid, times 3).
#define S               SHARE_SIZE
#define TITLE_BASELINE  102
#define ICON_PX         288                     // the game's icon
#define ICON_X          ((S - ICON_PX) / 2)
#define ICON_Y          156
#define ICON_RADIUS     (ICON_PX / 8)           // like the app's tiles
#define FRAME           6                       // yellow frame around the icon (2 px in the 240 px preview, like the app's selected tile)
#define NAME_BASELINE   528
#define NAME_MAX_W      (S - 120)
#define ROW_Y           612                     // middle of the trophy / bar / count row
#define ROW_LEFT        90
#define ROW_RIGHT       (S - 90)
#define ROW_GAP         24
#define TROPHY_PX       66
#define BAR_H           15

typedef struct { uint8_t r, g, b; } Rgb;

static const Rgb ACCENT = { 255, 209, 102 };    // the app's yellow
static const Rgb WHITE = { 255, 255, 255 };
static const Rgb GREEN = { 50, 205, 50 };
static const Rgb GOLD = { 255, 196, 40 };
static const Rgb DARK = { 50, 50, 58 };

static uint8_t *Pixel(uint8_t *rgb, int x, int y)
{
    return rgb + ((size_t)y * S + x) * 3;
}

// Mixes c over the pixel by alpha (0-255).
static void Blend(uint8_t *rgb, int x, int y, Rgb c, int alpha)
{
    if (alpha <= 0 || x < 0 || y < 0 || x >= S || y >= S)
        return;
    uint8_t *p = Pixel(rgb, x, y);
    if (alpha >= 255)
    {
        p[0] = c.r, p[1] = c.g, p[2] = c.b;
        return;
    }
    p[0] = (uint8_t)((p[0] * (255 - alpha) + c.r * alpha + 127) / 255);
    p[1] = (uint8_t)((p[1] * (255 - alpha) + c.g * alpha + 127) / 255);
    p[2] = (uint8_t)((p[2] * (255 - alpha) + c.b * alpha + 127) / 255);
}

// How much of pixel (px, py) a rounded rectangle covers, 0-255, its edges smoothed.
// The rectangle runs from (x0, y0) to (x1, y1), exclusive.
static int RoundCover(int px, int py, float x0, float y0, float x1, float y1, float radius)
{
    float cx = px + 0.5f, cy = py + 0.5f;
    float ix = cx < x0 + radius ? x0 + radius : cx > x1 - radius ? x1 - radius : cx;
    float iy = cy < y0 + radius ? y0 + radius : cy > y1 - radius ? y1 - radius : cy;
    float dx = cx - ix, dy = cy - iy, d;
    if (dx == 0.0f)
        d = dy < 0.0f ? -dy : dy;           // along a straight edge (or inside): no square root
    else if (dy == 0.0f)
        d = dx < 0.0f ? -dx : dx;
    else
        d = sqrtf(dx * dx + dy * dy);       // in a corner
    float cover = radius - d + 0.5f;
    return cover >= 1.0f ? 255 : cover <= 0.0f ? 0 : (int)(cover * 255.0f + 0.5f);
}

static void FillRound(uint8_t *rgb, int x, int y, int w, int h, int radius, Rgb c, int alpha)
{
    if (radius * 2 > w)
        radius = w / 2;
    if (radius * 2 > h)
        radius = h / 2;
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            Blend(rgb, px, py, c, RoundCover(px, py, x, y, x + w, y + h, radius) * alpha / 255);
}

// One pixel of a 48x48 SMDH-tiled RGB565 icon.
static Rgb IconAt(const uint16_t *icon, int x, int y)
{
    int tile = (y / 8) * 6 + x / 8;
    int ix = x & 7, iy = y & 7;
    int inner = (ix & 1) | ((iy & 1) << 1) | ((ix & 2) << 1) | ((iy & 2) << 2) | ((ix & 4) << 2) | ((iy & 4) << 3);
    uint16_t v = icon[tile * 64 + inner];
    Rgb c = { (uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
    return c;
}

static uint8_t Lerp(int a, int b, int c, int d, int wx, int wy)
{
    int top = a * (256 - wx) + b * wx;
    int bottom = c * (256 - wx) + d * wx;
    return (uint8_t)((top * (256 - wy) + bottom * wy + (1 << 15)) >> 16);
}

// A 48x48 icon smoothly enlarged to size x size at (x, y), cut to a rounded rectangle
// that's inset pixels in from its edges (in 48ths of size) with the given radius.
static void DrawIcon(uint8_t *rgb, const uint16_t *icon, int x, int y, int size, float inset, float radius)
{
    static Rgb image[48][48];
    const int span = 47 * 256;

    for (int iy = 0; iy < 48; iy++)
        for (int ix = 0; ix < 48; ix++)
            image[iy][ix] = IconAt(icon, ix, iy);

    float edge = inset * size / 48.0f;
    for (int dy = 0; dy < size; dy++)
    {
        int fy = dy * span / (size - 1), y0 = fy >> 8, wy = fy & 255, y1 = y0 < 47 ? y0 + 1 : y0;
        for (int dx = 0; dx < size; dx++)
        {
            int cover = RoundCover(dx, dy, edge, edge, size - edge, size - edge, radius);
            if (cover == 0)
                continue;
            int fx = dx * span / (size - 1), x0 = fx >> 8, wx = fx & 255, x1 = x0 < 47 ? x0 + 1 : x0;
            Rgb a = image[y0][x0], b = image[y0][x1], c = image[y1][x0], d = image[y1][x1];
            Rgb out = { Lerp(a.r, b.r, c.r, d.r, wx, wy), Lerp(a.g, b.g, c.g, d.g, wx, wy), Lerp(a.b, b.b, c.b, d.b, wx, wy) };
            Blend(rgb, x + dx, y + dy, out, cover);
        }
    }
}

// The game's background: the middle 240x240 of hero.bin, three times the size, dimmed
// (55% at the top to 90% at the bottom) so the text stands out. Without one, a dark fade.
// Each source row is widened once and kept for the three output rows it touches.
#define CROP        240                 // the square taken from the middle of hero.bin
#define CROP_LEFT   80

// Source position (in 1/256ths of a pixel, pixel centres lined up) of output pixel i.
static int SourceAt(int i)
{
    int f = ((2 * i + 1) * 128) / 3 - 128;
    return f < 0 ? 0 : f > (CROP - 1) * 256 ? (CROP - 1) * 256 : f;
}

// Source row sy widened to S pixels, each channel times 256.
static void WidenRow(const uint8_t *crop, int sy, const uint16_t *left, const uint8_t *weight, uint16_t *out)
{
    const uint8_t *row = crop + sy * CROP * 3;
    for (int x = 0; x < S; x++, out += 3)
    {
        const uint8_t *a = row + left[x], *b = a + (left[x] < (CROP - 1) * 3 ? 3 : 0);
        int w = weight[x];
        out[0] = (uint16_t)(a[0] * (256 - w) + b[0] * w);
        out[1] = (uint16_t)(a[1] * (256 - w) + b[1] * w);
        out[2] = (uint16_t)(a[2] * (256 - w) + b[2] * w);
    }
}

static void DrawBackground(uint8_t *rgb, const uint8_t *hero)
{
    static uint8_t  crop[CROP * CROP * 3];
    static uint16_t left[S];
    static uint8_t  weight[S];
    static uint16_t rows[2][S * 3];

    if (hero == NULL)
    {
        for (int y = 0; y < S; y++)
        {
            uint8_t v = (uint8_t)(40 - 28 * y / (S - 1)), *p = Pixel(rgb, 0, y);
            for (int x = 0; x < S; x++, p += 3)
                p[0] = v, p[1] = v, p[2] = (uint8_t)(v + 6);
        }
        return;
    }

    // hero.bin is stored in columns, bottom row first, each B, G, R: take the square as rows of R, G, B.
    for (int sx = 0; sx < CROP; sx++)
    {
        const uint8_t *column = hero + (CROP_LEFT + sx) * 240 * 3;
        for (int sy = 0; sy < CROP; sy++)
        {
            const uint8_t *p = column + (239 - sy) * 3;
            uint8_t *q = crop + (sy * CROP + sx) * 3;
            q[0] = p[2], q[1] = p[1], q[2] = p[0];
        }
    }
    for (int x = 0; x < S; x++)
    {
        int f = SourceAt(x);
        left[x] = (uint16_t)((f >> 8) * 3);
        weight[x] = (uint8_t)(f & 255);
    }

    uint16_t *a = rows[0], *b = rows[1];   // widened source rows y0 and y1
    int haveA = -1, haveB = -1;             // which rows they hold
    for (int y = 0; y < S; y++)
    {
        int f = SourceAt(y), y0 = f >> 8, wy = f & 255, y1 = y0 < CROP - 1 ? y0 + 1 : y0;
        int keep = (1152 * (S - 1) - 896 * y) / (10 * (S - 1));     // 256 x (0.45 - 0.35 y / 719)

        if (haveA != y0)
        {
            if (haveB == y0)                // moving down a row: the lower row becomes the upper
            {
                uint16_t *t = a;
                a = b, b = t;
                haveB = haveA;
                haveA = y0;
            }
            else
            {
                WidenRow(crop, y0, left, weight, a);
                haveA = y0;
            }
        }
        if (haveB != y1)
        {
            WidenRow(crop, y1, left, weight, b);
            haveB = y1;
        }

        uint8_t *p = Pixel(rgb, 0, y);
        for (int i = 0; i < S * 3; i++)
        {
            uint32_t v = ((uint32_t)a[i] * (256 - wy) + (uint32_t)b[i] * wy + (1 << 15)) >> 16;   // 0-255
            p[i] = (uint8_t)((v * keep + 128) >> 8);
        }
    }
}

static const ShareGlyph *NameGlyph(char ch)
{
    unsigned code = (unsigned char)ch;
    if (code < SHARE_NAME_FIRST || code > SHARE_NAME_LAST)
        code = '?';
    return &share_name_glyphs[code - SHARE_NAME_FIRST];
}

static const ShareGlyph *CountGlyph(char ch)
{
    unsigned code = (unsigned char)ch;
    if (code < SHARE_COUNT_FIRST || code > SHARE_COUNT_LAST)
        return NULL;
    return &share_count_glyphs[code - SHARE_COUNT_FIRST];
}

typedef const ShareGlyph *(*GlyphFor)(char ch);

static int TextWidth(const char *text, GlyphFor glyph)
{
    int pen64 = 0;
    for (; *text != '\0'; text++)
        if (glyph(*text) != NULL)
            pen64 += glyph(*text)->adv64;
    return (pen64 + 32) >> 6;
}

static void DrawText(uint8_t *rgb, int x, int baseline, const char *text, GlyphFor glyph,
                     const unsigned char *alpha, Rgb c)
{
    int pen64 = 0;
    for (; *text != '\0'; text++)
    {
        const ShareGlyph *g = glyph(*text);
        if (g == NULL)
            continue;
        int gx = x + ((pen64 + 32) >> 6) + g->x, gy = baseline + g->y;
        const unsigned char *a = alpha + g->off;
        for (int row = 0; row < g->h; row++)
            for (int col = 0; col < g->w; col++, a++)
                Blend(rgb, gx + col, gy + row, c, *a);
        pen64 += g->adv64;
    }
}

// The game's name, cut with "..." to fit width pixels.
static void FitName(char *out, size_t capacity, const char *name, int width)
{
    size_t length = strlen(name);
    if (length < capacity && TextWidth(name, NameGlyph) <= width)
    {
        memcpy(out, name, length + 1);
        return;
    }
    int room = width - TextWidth("...", NameGlyph), pen64 = 0;
    size_t keep = 0;
    while (keep < length && keep + 4 < capacity && ((pen64 + NameGlyph(name[keep])->adv64 + 32) >> 6) <= room)
        pen64 += NameGlyph(name[keep++])->adv64;
    while (keep > 0 && name[keep - 1] == ' ')
        keep--;                     // no gap before the dots
    memcpy(out, name, keep);
    memcpy(out + keep, "...", 4);
}

void ShareImage_Draw(const ShareInfo *info, uint8_t *rgb)
{
    char text[96];
    bool complete = info->total > 0 && info->unlocked >= info->total;

    DrawBackground(rgb, info->hero);

    // Title.
    {
        int x = (S - SHARE_TITLE_W) / 2, y = TITLE_BASELINE - SHARE_TITLE_ASCENT;
        const unsigned char *a = share_title_alpha;
        for (int row = 0; row < SHARE_TITLE_H; row++)
            for (int col = 0; col < SHARE_TITLE_W; col++, a++)
                Blend(rgb, x + col, y + row, ACCENT, *a);
    }

    // The game's icon in a yellow frame, rounded, like the selected game in the app.
    FillRound(rgb, ICON_X - FRAME, ICON_Y - FRAME, ICON_PX + 2 * FRAME, ICON_PX + 2 * FRAME,
              ICON_RADIUS + FRAME, ACCENT, 255);
    if (info->icon != NULL)
        DrawIcon(rgb, info->icon, ICON_X, ICON_Y, ICON_PX, 0.0f, ICON_RADIUS);
    else
        FillRound(rgb, ICON_X, ICON_Y, ICON_PX, ICON_PX, ICON_RADIUS, DARK, 255);

    // Name.
    FitName(text, sizeof(text), info->name, NAME_MAX_W);
    DrawText(rgb, (S - TextWidth(text, NameGlyph)) / 2, NAME_BASELINE, text, NameGlyph, share_name_alpha, WHITE);

    // Trophy, progress bar, count. The trophy art has a 1 px dark edge and rounded corners
    // (radius 7 of 48) drawn on black: cut to that shape so no black corners show.
    DrawIcon(rgb, info->trophy, ROW_LEFT, ROW_Y - TROPHY_PX / 2, TROPHY_PX, 1.0f, 7.0f * TROPHY_PX / 48);

    snprintf(text, sizeof(text), "%d/%d", info->unlocked, info->total);
    int countW = TextWidth(text, CountGlyph);
    DrawText(rgb, ROW_RIGHT - countW, ROW_Y + SHARE_COUNT_CAP / 2, text, CountGlyph, share_count_alpha,
             complete ? GOLD : WHITE);

    int barX = ROW_LEFT + TROPHY_PX + ROW_GAP, barW = ROW_RIGHT - countW - ROW_GAP - barX + 1;
    int barY = ROW_Y - BAR_H / 2;
    FillRound(rgb, barX, barY, barW, BAR_H, BAR_H / 2, DARK, 255);
    if (info->total > 0 && info->unlocked > 0)
    {
        int unlocked = info->unlocked < info->total ? info->unlocked : info->total;
        int fillW = (barW - 1) * unlocked / info->total + 1;
        FillRound(rgb, barX, barY, fillW, BAR_H, BAR_H / 2, complete ? GOLD : GREEN, 255);
    }
}

void ShareImage_Shrink(const uint8_t *rgb, uint8_t *preview)
{
    for (int y = 0; y < SHARE_PREVIEW; y++)
        for (int x = 0; x < SHARE_PREVIEW; x++)
            for (int ch = 0; ch < 3; ch++)
            {
                int sum = 0;
                for (int dy = 0; dy < 3; dy++)
                    for (int dx = 0; dx < 3; dx++)
                        sum += rgb[((size_t)(3 * y + dy) * S + 3 * x + dx) * 3 + ch];
                preview[(y * SHARE_PREVIEW + x) * 3 + ch] = (uint8_t)((sum + 4) / 9);
            }
}

static void Put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

// Finishes the chunk whose length field is at p (type and data follow): length and CRC.
static uint8_t *EndChunk(uint8_t *p, uint32_t length)
{
    Put32(p, length);
    Put32(p + 8 + length, (uint32_t)crc32(crc32(0, Z_NULL, 0), p + 4, 4 + length));
    return p + 12 + length;
}

static uint8_t Paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

uint8_t *ShareImage_Png(const uint8_t *rgb, int width, int height, uint32_t *size)
{
    const uint32_t stride = (uint32_t)width * 3;
    const uLong raw = (uLong)(stride + 1) * height;
    const uLong bound = compressBound(raw);
    uint8_t *png = malloc(8 + 25 + 12 + bound + 12);
    uint8_t *line = malloc(stride + 1);
    z_stream z;

    if (png == NULL || line == NULL)
    {
        free(png);
        free(line);
        return NULL;
    }

    static const uint8_t SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t *p = png;
    memcpy(p, SIGNATURE, 8);
    p += 8;

    memcpy(p + 4, "IHDR", 4);
    Put32(p + 8, (uint32_t)width);
    Put32(p + 12, (uint32_t)height);
    p[16] = 8;          // bits per channel
    p[17] = 2;          // RGB
    p[18] = p[19] = p[20] = 0;
    p = EndChunk(p, 13);

    // One IDAT: every row with the Paeth filter (good for photos), through deflate in its
    // run-length mode, which on this image is about 4x faster than the default and
    // compresses as well (measured on the PC: 329 KB vs 330 KB at level 6).
    uint8_t *idat = p;
    memcpy(idat + 4, "IDAT", 4);
    memset(&z, 0, sizeof(z));
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_RLE) != Z_OK)
    {
        free(png);
        free(line);
        return NULL;
    }
    z.next_out = idat + 8;
    z.avail_out = (uInt)bound;
    int result = Z_OK;
    for (int y = 0; y < height && result == Z_OK; y++)
    {
        const uint8_t *row = rgb + (size_t)y * stride;
        line[0] = 4;
        if (y == 0)
        {
            // Nothing above: Paeth picks the pixel to the left.
            line[1] = row[0], line[2] = row[1], line[3] = row[2];
            for (uint32_t i = 3; i < stride; i++)
                line[1 + i] = (uint8_t)(row[i] - row[i - 3]);
        }
        else
        {
            const uint8_t *up = row - stride;
            line[1] = (uint8_t)(row[0] - up[0]), line[2] = (uint8_t)(row[1] - up[1]), line[3] = (uint8_t)(row[2] - up[2]);
            for (uint32_t i = 3; i < stride; i++)
                line[1 + i] = (uint8_t)(row[i] - Paeth(row[i - 3], up[i], up[i - 3]));
        }
        z.next_in = line;
        z.avail_in = stride + 1;
        result = deflate(&z, y == height - 1 ? Z_FINISH : Z_NO_FLUSH);
        if (y < height - 1 && result == Z_OK && z.avail_in != 0)
            result = Z_BUF_ERROR;
    }
    uint32_t compressed = (uint32_t)z.total_out;
    deflateEnd(&z);
    free(line);
    if (result != Z_STREAM_END)
    {
        free(png);
        return NULL;
    }
    p = EndChunk(idat, compressed);

    memcpy(p + 4, "IEND", 4);
    p = EndChunk(p, 0);

    *size = (uint32_t)(p - png);
    return png;
}
