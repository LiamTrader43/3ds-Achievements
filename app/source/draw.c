#include "draw.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "bench.h"
#include "font_inter.h"
#include "hero.h"

// The 3DS's ARM11 has no divide instruction and slow float functions, and every frame has
// to be drawn in 16.7 ms (twice over on the top screen with 3D on), so the slow work here
// is done once and kept: the light's pixels per frame shape, scaled icons, the shadow, the
// dimmed background. Framebuffers are stored rotated: column x is 240 pixels, bottom row
// first, so drawing goes down a column with the pointer moving back 3 bytes a pixel.

#define SLIDER_STEP     (1.0f / 32)     // how far the 3D slider must move before the top screen is redrawn
#define COLUMN          (SCREEN_HEIGHT * 3)     // bytes in a framebuffer column

static u8 *s_fb[2];     // back buffers of GFX_TOP and GFX_BOTTOM for this frame (GFX_TOP: the current eye's)

// Stereoscopic 3D: the top screen has an image per eye. The left one is drawn exactly as in
// 2D; in the right one, everything is moved sideways by its depth (Draw_Depth), so it
// appears in front of the screen or behind it.
static bool  s_stereo;  // 3D is on (the slider is up)
static float s_slider;  // the slider's position the top screen is drawn for, 0-1
static u8   *s_top[2];  // the top screen's left and right images this frame
static int   s_eye;     // which of them is being drawn
static int   s_shift;   // how far what's drawn now on the top screen is moved to the right

// The columns of each buffer drawn this frame (top left, top right, bottom), which Draw_End
// flushes from the CPU's cache so the screen sees them: x0 to x1, exclusive.
static int s_dirty0[3], s_dirty1[3];

static u8 s_cover16[256];   // a font alpha (0-255) as a coverage (0-16)

static void InitCorners(void);
static void InitSolid(void);
static void InitLight(void);

void Draw_Init(void)
{
    gfxInitDefault();   // both screens BGR8, double buffered
    for (int a = 0; a < 256; a++)
        s_cover16[a] = (u8)((a * 16 + 127) / 255);
    InitCorners();
    InitSolid();
    InitLight();
}

void Draw_Exit(void)
{
    gfxExit();
}

int Draw_Width(gfxScreen_t screen)
{
    return screen == GFX_TOP ? TOP_WIDTH : BOTTOM_WIDTH;
}

bool Draw_Stereo(float slider)
{
    bool on = slider > 0.0f;

    // A slider at rest can wobble a little: only a real move redraws.
    if (on == s_stereo && (!on || fabsf(slider - s_slider) < SLIDER_STEP))
        return false;
    if (on != s_stereo)
        gfxSet3D(on);       // takes effect at the next swap, with both images drawn by then
    s_stereo = on;
    s_slider = on ? slider : 0.0f;
    return true;
}

int Draw_Eyes(void)
{
    return s_stereo ? 2 : 1;
}

void Draw_Eye(int eye)
{
    s_eye = eye > 0 && s_stereo ? 1 : 0;
    s_fb[GFX_TOP] = s_top[s_eye];
    s_shift = 0;
}

void Draw_Depth(int depth)
{
    // In front of the screen, the right eye sees a thing further left than the left eye does.
    s_shift = s_eye == 1 ? -(int)lroundf(depth * s_slider) : 0;
}

// Marks columns x0 to x1 of a buffer (0, 1: top left, right; 2: bottom) as drawn.
static void Dirty(int buffer, int x0, int x1)
{
    int width = buffer == 2 ? BOTTOM_WIDTH : TOP_WIDTH;
    x0 = x0 < 0 ? 0 : x0;
    x1 = x1 > width ? width : x1;
    if (x1 <= x0)
        return;
    if (s_dirty1[buffer] <= s_dirty0[buffer])
        s_dirty0[buffer] = x0, s_dirty1[buffer] = x1;
    else
    {
        s_dirty0[buffer] = x0 < s_dirty0[buffer] ? x0 : s_dirty0[buffer];
        s_dirty1[buffer] = x1 > s_dirty1[buffer] ? x1 : s_dirty1[buffer];
    }
}

void Draw_Begin(int top, int bottom)
{
    // Without 3D, libctru returns the left image for GFX_RIGHT too, so it's only asked for with 3D on.
    s_top[0] = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    s_top[1] = s_stereo ? gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) : s_top[0];
    s_fb[GFX_BOTTOM] = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    for (int i = 0; i < 3; i++)
        s_dirty0[i] = s_dirty1[i] = 0;
    if (top != DRAW_KEEP)
        for (int eye = 0; eye < Draw_Eyes(); eye++)
        {
            Dirty(eye, 0, TOP_WIDTH);
            if (top == DRAW_CLEAR)
                memset(s_top[eye], 0, TOP_WIDTH * COLUMN);
        }
    if (bottom != DRAW_KEEP)
    {
        Dirty(2, 0, BOTTOM_WIDTH);
        if (bottom == DRAW_CLEAR)
            memset(s_fb[GFX_BOTTOM], 0, BOTTOM_WIDTH * COLUMN);
    }
    Draw_Eye(0);
}

void Draw_End(void)
{
    // Only what was drawn is flushed (a frame that just moves the light is a few columns).
    for (int i = 0; i < 3; i++)
    {
        if (s_dirty1[i] <= s_dirty0[i] || (i == 1 && !s_stereo))
            continue;
        u8 *buffer = i == 2 ? s_fb[GFX_BOTTOM] : s_top[i];
        GSPGPU_FlushDataCache(buffer + s_dirty0[i] * COLUMN, (u32)(s_dirty1[i] - s_dirty0[i]) * COLUMN);
    }
    gfxSwapBuffers();
    gspWaitForVBlank();
}

// Pixel (x, y) in the back buffer, no checks: x already moved by the depth.
static inline u8 *At(gfxScreen_t screen, int x, int y)
{
    return s_fb[screen] + x * COLUMN + (SCREEN_HEIGHT - 1 - y) * 3;
}

// Pixel (x, y) in the back buffer (on the top screen, moved by the current depth), or NULL
// off the screen.
static inline u8 *Pixel(gfxScreen_t screen, int x, int y)
{
    if (screen == GFX_TOP)
        x += s_shift;
    if (x < 0 || y < 0 || x >= Draw_Width(screen) || y >= SCREEN_HEIGHT)
        return NULL;
    return At(screen, x, y);
}

static inline void Put(u8 *p, Color c)
{
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

static inline void Plot(gfxScreen_t screen, int x, int y, Color c)
{
    u8 *p = Pixel(screen, x, y);
    if (p != NULL)
        Put(p, c);
}

// Draws color over pixel p; coverage is 0 (nothing) to 16 (solid).
static inline void Mixed(u8 *p, Color c, int coverage)
{
    if (coverage >= 16)
    {
        Put(p, c);
        return;
    }
    if (coverage <= 0)
        return;
    p[0] = (u8)((p[0] * (16 - coverage) + c.b * coverage) / 16);
    p[1] = (u8)((p[1] * (16 - coverage) + c.g * coverage) / 16);
    p[2] = (u8)((p[2] * (16 - coverage) + c.r * coverage) / 16);
}

static inline void Blend(gfxScreen_t screen, int x, int y, Color c, int coverage)
{
    u8 *p = coverage > 0 ? Pixel(screen, x, y) : NULL;
    if (p != NULL)
        Mixed(p, c, coverage);
}

// Darkens pixel p to (256 - amount) / 256 of its brightness.
static inline void Darker(u8 *p, int amount)
{
    if (amount <= 0)
        return;
    int keep = 256 - (amount > 256 ? 256 : amount);
    p[0] = (u8)(p[0] * keep >> 8);
    p[1] = (u8)(p[1] * keep >> 8);
    p[2] = (u8)(p[2] * keep >> 8);
}

// The same for bytes bytes from p (whole pixels or not): each times keep (0-255) / 256,
// two at a time in 16-bit lanes of a word.
static void DarkerBytes(u8 *p, int bytes, int keep)
{
    for (; bytes > 0 && ((uintptr_t)p & 3) != 0; bytes--, p++)
        *p = (u8)(*p * keep >> 8);
    u32 *w = (u32 *)p, k = (u32)keep;
    for (; bytes >= 4; bytes -= 4, w++)
    {
        u32 v = *w;
        *w = (((v & 0x00FF00FF) * k >> 8) & 0x00FF00FF) | (((v >> 8) & 0x00FF00FF) * k & 0xFF00FF00);
    }
    for (p = (u8 *)w; bytes > 0; bytes--, p++)
        *p = (u8)(*p * keep >> 8);
}

// How much of pixel (x, y) of a w x h box with rounded corners is inside it (0-16), from
// 16 points in the pixel. Coverage() below looks it up instead.
static int SlowCoverage(int x, int y, int w, int h, int radius)
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

// The top-left corner of every radius up to CORNER_MAX, worked out once: [radius][y][x].
// The other corners are its mirror images (the 16 points are symmetric in the pixel).
#define CORNER_MAX  32
static u8 s_corner[CORNER_MAX + 1][CORNER_MAX][CORNER_MAX];

static void InitCorners(void)
{
    for (int r = 1; r <= CORNER_MAX; r++)
        for (int y = 0; y < r; y++)
            for (int x = 0; x < r; x++)
                s_corner[r][y][x] = (u8)SlowCoverage(x, y, 2 * r, 2 * r, r);
}

static inline int Coverage(int x, int y, int w, int h, int radius)
{
    int cx = x < radius ? x : x >= w - radius ? w - 1 - x : -1;
    if (cx < 0)
        return 16;
    int cy = y < radius ? y : y >= h - radius ? h - 1 - y : -1;
    if (cy < 0)
        return 16;
    return radius <= CORNER_MAX ? s_corner[radius][cy][cx] : SlowCoverage(x, y, w, h, radius);
}

// The part of a w x h box at (x, y) that's on the screen, as offsets into the box:
// columns *dx0 to *dx1, rows *dy0 to *dy1 (exclusive). x is already moved by the depth.
// False if none of it is.
static bool Clip(gfxScreen_t screen, int x, int y, int w, int h, int *dx0, int *dx1, int *dy0, int *dy1)
{
    *dx0 = x < 0 ? -x : 0;
    *dy0 = y < 0 ? -y : 0;
    *dx1 = x + w > Draw_Width(screen) ? Draw_Width(screen) - x : w;
    *dy1 = y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT - y : h;
    return *dx0 < *dx1 && *dy0 < *dy1;
}

static inline int Shifted(gfxScreen_t screen, int x)
{
    return screen == GFX_TOP ? x + s_shift : x;
}

void Draw_RoundRect(gfxScreen_t screen, int x, int y, int w, int h, int radius, Color color)
{
    int dx0, dx1, dy0, dy1;
    x = Shifted(screen, x);
    if (!Clip(screen, x, y, w, h, &dx0, &dx1, &dy0, &dy1))
        return;
    for (int dx = dx0; dx < dx1; dx++)
    {
        // Only the corners have edges to smooth.
        bool cornerColumn = dx < radius || dx >= w - radius;
        u8  *p = At(screen, x + dx, y + dy0);
        for (int dy = dy0; dy < dy1; dy++, p -= 3)
        {
            if (cornerColumn && (dy < radius || dy >= h - radius))
                Mixed(p, color, Coverage(dx, dy, w, h, radius));
            else
                Put(p, color);
        }
    }
}

void Draw_Rect(gfxScreen_t screen, int x, int y, int w, int h, Color color)
{
    int dx0, dx1, dy0, dy1;
    x = Shifted(screen, x);
    if (!Clip(screen, x, y, w, h, &dx0, &dx1, &dy0, &dy1))
        return;
    for (int dx = dx0; dx < dx1; dx++)
    {
        u8 *p = At(screen, x + dx, y + dy0);
        for (int dy = dy0; dy < dy1; dy++, p -= 3)
            Put(p, color);
    }
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
        int gx = Shifted(screen, Pixels(pen64) + g->x), gy = y + g->y, dx0, dx1, dy0, dy1;
        if (Clip(screen, gx, gy, g->w, g->h, &dx0, &dx1, &dy0, &dy1))
            for (int col = dx0; col < dx1; col++)
            {
                u8 *p = At(screen, gx + col, gy + dy0);
                const unsigned char *a = alpha + dy0 * g->w + col;
                for (int row = dy0; row < dy1; row++, p -= 3, a += g->w)
                    if (*a != 0)
                        Mixed(p, color, s_cover16[*a]);
            }
        pen64 += g->adv64;
    }
    return Pixels(pen64) - x;
}

void Draw_DimHero(const u8 *pixels, u8 *dimmed)
{
    for (int x = 0; x < HERO_WIDTH; x++)
        for (int row = 0; row < HERO_HEIGHT; row++)     // row 0 is the bottom of the screen
        {
            int level = 40 + 110 * row / (HERO_HEIGHT - 1);                     // out of 256
            for (int c = 0; c < 3; c++, pixels++, dimmed++)
                *dimmed = (u8)(*pixels * level >> 8);
        }
}

// Screen columns x0 to x1 of the current eye's image: the background's columns from + x,
// faded.
static void HeroColumns(const u8 *dimmed, int fade, int from, int x0, int x1)
{
    for (int x = x0; x < x1; x++)
    {
        // Moved by its depth: past the margins, the edge column repeats.
        int column = x + from;
        column = column < 0 ? 0 : column >= HERO_WIDTH ? HERO_WIDTH - 1 : column;
        const u32 *in = (const u32 *)(dimmed + column * COLUMN);
        u32 *out = (u32 *)(s_fb[GFX_TOP] + x * COLUMN);
        if (fade >= 256)
        {
            memcpy(out, in, COLUMN);
            continue;
        }
        // Each byte times fade / 256, two bytes at a time in 16-bit lanes (255 * 255 fits).
        const u32 f = (u32)(fade < 0 ? 0 : fade);
        for (int i = 0; i < COLUMN / 4; i++)
        {
            u32 v = in[i];
            out[i] = (((v & 0x00FF00FF) * f >> 8) & 0x00FF00FF) | (((v >> 8) & 0x00FF00FF) * f & 0xFF00FF00);
        }
    }
}

void Draw_Hero(const u8 *dimmed, int fade, int depth)
{
    for (int eye = 0; eye < Draw_Eyes(); eye++)
    {
        Draw_Eye(eye);
        Draw_Depth(depth);
        int from = HERO_MARGIN - s_shift;   // the background's column at the screen's left edge

        // Fully faded in: the columns as they are, in one copy.
        if (fade >= 256 && from >= 0 && from + TOP_WIDTH <= HERO_WIDTH)
            memcpy(s_fb[GFX_TOP], dimmed + from * COLUMN, TOP_WIDTH * COLUMN);
        else if (eye == 1)
        {
            // Fading: the right image is the left one's columns, moved (the left image
            // shows the background from column HERO_MARGIN, unmoved). Only the edge is faded here.
            int x0 = s_shift > 0 ? s_shift : 0, x1 = TOP_WIDTH + (s_shift < 0 ? s_shift : 0);
            if (x1 > x0 && s_shift >= -HERO_MARGIN && s_shift <= HERO_MARGIN)
            {
                memcpy(s_fb[GFX_TOP] + x0 * COLUMN, s_top[0] + (x0 - s_shift) * COLUMN, (x1 - x0) * COLUMN);
                HeroColumns(dimmed, fade, from, 0, x0);
                HeroColumns(dimmed, fade, from, x1, TOP_WIDTH);
            }
            else
                HeroColumns(dimmed, fade, from, 0, TOP_WIDTH);
        }
        else
            HeroColumns(dimmed, fade, from, 0, TOP_WIDTH);
    }
    Draw_Eye(0);
}

// ---- Icons (48x48 RGB565 in 8x8 tiles, Z-order inside each tile)

// Icon pixel i: where it goes in the 48x48 image and its colour.
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

// A few of the icon's pixels: with its address, tells a changed icon from the one cached.
static u32 IconSample(const u16 *icon)
{
    u32 hash = 2166136261u;
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i += 37)
        hash = (hash ^ icon[i]) * 16777619u;
    return hash;
}

// Icons decoded into rows of colours, the last few used.
#define DECODED_ICONS   6
typedef struct
{
    const u16  *icon;
    u32         sample, used;
    Color       image[ICON_SIZE][ICON_SIZE];    // [y][x]
    bool        black[ICON_SIZE][ICON_SIZE];    // the pixel is 0x0000
} DecodedIcon;
static DecodedIcon s_decoded[DECODED_ICONS];
static u32         s_clock;

static const DecodedIcon *Decode(const u16 *icon)
{
    u32 sample = IconSample(icon);
    DecodedIcon *slot = &s_decoded[0];
    for (int i = 0; i < DECODED_ICONS; i++)
    {
        DecodedIcon *d = &s_decoded[i];
        if (d->icon == icon && d->sample == sample)
        {
            d->used = ++s_clock;
            return d;
        }
        if (d->used < slot->used)
            slot = d;
    }
    slot->icon = icon;
    slot->sample = sample;
    slot->used = ++s_clock;
#ifdef BENCH
    u64 bench = svcGetSystemTick();
#endif
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++)
    {
        int   px, py;
        Color c;
        IconPixel(icon, i, &px, &py, &c);
        slot->image[py][px] = c;
        slot->black[py][px] = icon[i] == 0;
    }
#ifdef BENCH
    Bench_Add(BENCH_DECODE, svcGetSystemTick() - bench);
#endif
    return slot;
}

void Draw_Icon(gfxScreen_t screen, int x, int y, const u16 *icon)
{
    const DecodedIcon *d = Decode(icon);
    for (int py = 0; py < ICON_SIZE; py++)
        for (int px = 0; px < ICON_SIZE; px++)
            Plot(screen, x + px, y + py, d->image[py][px]);
}

void Draw_TrophyIcon(gfxScreen_t screen, int x, int y, const u16 *icon)
{
    const DecodedIcon *d = Decode(icon);
    int dx0, dx1, dy0, dy1;
    x = Shifted(screen, x);
    if (!Clip(screen, x, y, ICON_SIZE, ICON_SIZE, &dx0, &dx1, &dy0, &dy1))
        return;
    for (int dx = dx0; dx < dx1; dx++)
    {
        u8 *p = At(screen, x + dx, y + dy0);
        for (int dy = dy0; dy < dy1; dy++, p -= 3)
            if (!d->black[dy][dx])          // the black around the trophy's rounded square
                Put(p, d->image[dy][dx]);
    }
}

// Scaled tiles, the last few drawn: the static ones and both eyes' copies of a moving one
// are drawn from here. The rounded corners, dimming and frame are added as each is drawn.
#define TILE_MAX        128
#define SCALED_TILES    8               // exact ones
#define QUICK_TILES     2               // and quick ones (two tiles can be changing size at once)
typedef struct
{
    const u16  *icon;
    u32         sample, used;
    int         size, from;                         // from: the size it was quickly made from (0: exact)
    u8          rgb[TILE_MAX * TILE_MAX * 3];       // column by column, bottom row first: B, G, R
} ScaledTile;
static ScaledTile s_scaled[SCALED_TILES + QUICK_TILES];

// For each corner radius and column into the corner, the first row that's fully inside
// (the corner's height if none is).
static u8 s_firstSolid[CORNER_MAX + 1][CORNER_MAX];

static void InitSolid(void)
{
    for (int r = 1; r <= CORNER_MAX; r++)
        for (int x = 0; x < r; x++)
        {
            int y = 0;
            while (y < r && s_corner[r][y][x] != 16)
                y++;
            s_firstSolid[r][x] = (u8)y;
        }
}

// Rows from the top (and as many from the bottom) of column dx of a size x size box with
// corner radius that aren't fully inside it.
static int SolidFrom(int dx, int size, int radius)
{
    int cx = dx < radius ? dx : dx >= size - radius ? size - 1 - dx : -1;
    if (cx < 0)
        return 0;
    if (radius <= CORNER_MAX)
        return s_firstSolid[radius][cx];
    int y = 0;
    while (y < radius && SlowCoverage(dx, y, size, size, radius) != 16)
        y++;
    return y;
}

// The icon at size: exact (bilinear from the 48x48 icon), or with from > 0 quickly, the
// nearest pixels of the exact from-size one (while a tile changes size).
static const ScaledTile *Scale(const u16 *icon, int size, int from)
{
    u32 sample = IconSample(icon);
    ScaledTile *slot = NULL;
    for (int i = 0; i < SCALED_TILES + QUICK_TILES; i++)
    {
        ScaledTile *t = &s_scaled[i];
        if (t->icon == icon && t->sample == sample && t->size == size && t->from == from)
        {
            t->used = ++s_clock;
            return t;
        }
    }
    if (from > 0)
    {
        const ScaledTile *big = Scale(icon, from, 0);
        for (int i = SCALED_TILES; i < SCALED_TILES + QUICK_TILES; i++)
            if (slot == NULL || s_scaled[i].used < slot->used)
                slot = &s_scaled[i];
        u8 map[TILE_MAX], kmap[TILE_MAX];
        u32 step = ((u32)from << 16) / (u32)size, pos = step / 2;
        for (int i = 0; i < size; i++, pos += step)
            map[i] = (u8)(pos >> 16);
        for (int k = 0; k < size; k++)
            kmap[k] = (u8)(from - 1 - map[size - 1 - k]);
        for (int dx = 0; dx < size; dx++)
        {
            const u8 *in = &big->rgb[map[dx] * from * 3];
            u8 *out = &slot->rgb[dx * size * 3];
            for (int k = 0; k < size; k++, out += 3)
            {
                const u8 *p = in + kmap[k] * 3;
                out[0] = p[0], out[1] = p[1], out[2] = p[2];
            }
        }
        slot->icon = icon, slot->sample = sample, slot->used = ++s_clock, slot->size = size, slot->from = from;
        return slot;
    }
    for (int i = 0; i < SCALED_TILES; i++)
        if (slot == NULL || s_scaled[i].used < slot->used)
            slot = &s_scaled[i];

#ifdef BENCH
    u64 bench = svcGetSystemTick();
#endif
    const DecodedIcon *d = Decode(icon);
    const int span = (ICON_SIZE - 1) * 256;     // source positions in 1/256ths of a pixel
    static u8 xlo[TILE_MAX], xhi[TILE_MAX], xweight[TILE_MAX];
    static u8 lo[TILE_MAX], hi[TILE_MAX], weight[TILE_MAX];   // the same per row as stored (bottom first)
    for (int i = 0; i < size; i++)
    {
        int f = i * span / (size - 1);
        xlo[i] = (u8)(f >> 8);
        xhi[i] = (u8)(xlo[i] + 1 < ICON_SIZE ? xlo[i] + 1 : xlo[i]);
        xweight[i] = (u8)(f & 255);
    }
    for (int k = 0; k < size; k++)
        lo[k] = xlo[size - 1 - k], hi[k] = xhi[size - 1 - k], weight[k] = xweight[size - 1 - k];

    slot->icon = icon, slot->sample = sample, slot->used = ++s_clock, slot->size = size, slot->from = 0;
    for (int dx = 0; dx < size; dx++)
    {
        // Bilinear in two steps: this column across every source row (kept at full
        // precision, so it's the same sum as one bilinear mix), then down the column.
        int x0 = xlo[dx], x1 = xhi[dx], wx = xweight[dx];
        u16 across[ICON_SIZE][3];
        for (int sy = 0; sy < ICON_SIZE; sy++)
        {
            Color a = d->image[sy][x0], b = d->image[sy][x1];
            across[sy][0] = (u16)(a.b * (256 - wx) + b.b * wx);
            across[sy][1] = (u16)(a.g * (256 - wx) + b.g * wx);
            across[sy][2] = (u16)(a.r * (256 - wx) + b.r * wx);
        }
        u8 *p = &slot->rgb[dx * size * 3];
        for (int k = 0; k < size; k++, p += 3)
        {
            const u16 *top = across[lo[k]], *bottom = across[hi[k]];
            int wy = weight[k];
            p[0] = (u8)((top[0] * (256 - wy) + bottom[0] * wy) >> 16);
            p[1] = (u8)((top[1] * (256 - wy) + bottom[1] * wy) >> 16);
            p[2] = (u8)((top[2] * (256 - wy) + bottom[2] * wy) >> 16);
        }
    }
#ifdef BENCH
    Bench_Add(BENCH_TILE_SCALE, svcGetSystemTick() - bench);
#endif
    return slot;
}

void Draw_IconScaled(gfxScreen_t screen, int x, int y, int size, int radius, const u16 *icon, int dim,
                     const Color *frame, int quickFrom)
{
    if (size > TILE_MAX || size < 2)
        return;

    // Exact, or (while it changes size) quickly from the quickFrom-size image; kept either way.
    bool quick = quickFrom >= 2 && quickFrom <= TILE_MAX && quickFrom != size;
    const ScaledTile *t = Scale(icon, size, quick ? quickFrom : 0);

    // The box drawn: the tile, or the tile with the frame around it (DRAW_FRAME pixels wide,
    // corners that much more round). The frame is only drawn where the tile doesn't cover it.
    const int F = frame != NULL ? DRAW_FRAME : 0;
    const int bs = size + 2 * F, br = radius + F, keep = 256 - (dim > 256 ? 256 : dim < 0 ? 0 : dim);
    int bx = Shifted(screen, x - F), by = y - F, fx0, fx1, fy0, fy1;
    if (!Clip(screen, bx, by, bs, bs, &fx0, &fx1, &fy0, &fy1))
        return;
    for (int fx = fx0; fx < fx1; fx++)
    {
        int dx = fx - F;                                    // the tile's column
        u8 *column = At(screen, bx + fx, by + bs - 1);      // the box's bottom row; row fy is at (bs - 1 - fy) * 3
        bool tileColumn = dx >= 0 && dx < size;
        const u8 *source = tileColumn ? &t->rgb[dx * size * 3] : NULL;

        // The tile's solid rows in this column, as box rows (top first), on the screen.
        int run0 = fy1, run1 = fy1;
        if (tileColumn)
        {
            int from = SolidFrom(dx, size, radius);
            run0 = F + from, run1 = F + size - from;
            run0 = run0 < fy0 ? fy0 : run0;
            run1 = run1 > fy1 ? fy1 : run1;
            if (run1 <= run0)
                run0 = run1 = fy1;
            else
            {
                // Rows run0 to run1, the lowest one first in memory; as stored, k = size - 1 - dy.
                int k0 = size - 1 - (run1 - 1 - F), count = run1 - run0;
                u8 *to = column + (bs - run1) * 3;
                memcpy(to, source + k0 * 3, count * 3);
                if (keep < 256)
                    DarkerBytes(column + (bs - run1) * 3, count * 3, keep);
            }
        }

        // The rest: the frame, then the tile's smoothed corners over it (dimmed by how much is tile).
        for (int fy = fy0; fy < fy1; fy++)
        {
            if (fy == run0)
            {
                fy = run1 - 1;
                continue;
            }
            u8 *p = column + (bs - 1 - fy) * 3;
            if (frame != NULL)
                Mixed(p, *frame, Coverage(fx, fy, bs, bs, br));
            int dy = fy - F;
            if (!tileColumn || dy < 0 || dy >= size)
                continue;
            int c = Coverage(dx, dy, size, size, radius);
            if (c == 0)
                continue;
            int k = size - 1 - dy;
            const u8 *rgb = source + k * 3;
            Color color = { rgb[2], rgb[1], rgb[0] };
            Mixed(p, color, c);
            Darker(p, dim * c / 16);
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

// ---- The light running around the selected game's frame: a lighter stretch of the frame's
// colour moving clockwise, one lap every SHINE_LAP_MS. Which pixels it can touch, how much
// of each is frame or glow, and where each is along the frame are worked out once per
// frame shape; each frame then only goes through those.
#define SHINE_LAP_MS    4800
#define SHINE_HALF      190             // half the light's length, in 1024ths of a lap
#define SHINE_GLOW      5               // the light's glow reaches this far outside the frame
#define SHINE_GLOW_PEAK 180             // and is this strong right at its edge (of 256)
#define SHINE_POINTS    12288           // most pixels a frame's light can touch

typedef struct
{
    s16     dx, dy;         // from the frame's top-left
    u16     along;          // where it is along the frame, in 1024ths of a lap
    u16     frame;          // how much of it is frame, 0-256
    u8      outer;          // how much of it is inside the frame's outer edge, 0-16
    bool    glow;           // the glow outside the frame reaches it
    float   fade;           // how strong the glow is there, 0-1
} LightPoint;

static struct
{
    bool        saved[2];   // per eye (index 1 only with 3D): its points' pixels without the light are kept
    gfxScreen_t screen;
    int         x[2], y, w, h, radius, thickness;   // where the frame is in each eye's image
    Color       base, light;
    int         count, shapeW, shapeH, shapeR, shapeT;  // the points, and the shape they're for
    LightPoint  points[SHINE_POINTS];
    u8          pixels[2][SHINE_POINTS * 3];
} s_shine;
static float s_lightCurve[SHINE_HALF];  // how strongly the light shows at each distance from its middle

static void InitLight(void)
{
    for (int d = 0; d < SHINE_HALF; d++)
        s_lightCurve[d] = 0.5f + 0.5f * cosf(3.14159265f * d / SHINE_HALF);
    s_shine.shapeW = -1;
}

// The eye whose image the light is being drawn in.
static inline int ShineEye(void)
{
    return s_shine.screen == GFX_TOP ? s_eye : 0;
}

// sqrtf(dx * dx + dy * dy), without the square root along a straight edge (the same value).
static inline float Distance(float dx, float dy)
{
    if (dx == 0.0f)
        return fabsf(dy);
    if (dy == 0.0f)
        return fabsf(dx);
    return sqrtf(dx * dx + dy * dy);
}

// atan2f to within about 0.00001 radians (a polynomial; newlib's is slow on the ARM11).
static float Atan2(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y);
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = 3.14159274f - r;
    return y < 0.0f ? -r : r;
}

// Where pixel centre (cx, cy) is along the middle of the frame (a w x h rounded rectangle
// with corner radius r, from its top-left), clockwise from the top edge's left end, in 1024ths.
static int AlongFrame(float cx, float cy, float w, float h, float r)
{
    const float halfPi = 1.57079633f;
    float top = w - 2 * r, side = h - 2 * r, arc = halfPi * r;
    float s;

    if (cx < r && cy < r)
        s = 2 * top + 2 * side + 3 * arc + arc * (Atan2(cy - r, cx - r) + 2 * halfPi) / halfPi;
    else if (cx > w - r && cy < r)
        s = top + arc * (Atan2(cy - r, cx - (w - r)) + halfPi) / halfPi;
    else if (cx > w - r && cy > h - r)
        s = top + arc + side + arc * Atan2(cy - (h - r), cx - (w - r)) / halfPi;
    else if (cx < r && cy > h - r)
        s = 2 * top + 2 * arc + side + arc * (Atan2(cy - (h - r), cx - r) - halfPi) / halfPi;
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

// Works out the points for the frame's shape: on the frame, the part of each pixel that's
// frame colour gets lighter (smooth edges, nothing inside touched); just outside it, a soft
// glow fades out over SHINE_GLOW pixels.
static void ShapeLight(void)
{
    const int t = s_shine.thickness, w = s_shine.w, h = s_shine.h, r = s_shine.radius, g = SHINE_GLOW;
    const int innerR = r > t ? r - t : 0, innerW = w - 2 * t, innerH = h - 2 * t;
    const float mid = t / 2.0f;
    int n = 0;

    for (int dx = -g; dx < w + g && n < SHINE_POINTS; dx++)
    {
        // Down the sides every row can have frame or glow; elsewhere only the rows down to
        // the corner (or the frame's thickness, between the corners) and up from the bottom.
        bool side = dx < t || dx >= w - t;
        int  band = dx < r || dx >= w - r ? (r > t ? r : t) : t;
        for (int dy = -g; dy < h + g && n < SHINE_POINTS; dy++)
        {
            if (!side && dy == band && h - band > band)
                dy = h - band;
            bool inBox = dx >= 0 && dy >= 0 && dx < w && dy < h;
            bool inInner = dx >= t && dy >= t && dx < w - t && dy < h - t;
            // Inside the frame, away from its inner corners, there's neither frame nor glow.
            if (inInner && !((dx - t < innerR || dx - t >= innerW - innerR) && (dy - t < innerR || dy - t >= innerH - innerR)))
                continue;
            int outer = inBox ? Coverage(dx, dy, w, h, r) : 0;
            int inner = inInner ? Coverage(dx - t, dy - t, innerW, innerH, innerR) : 0;
            int frame = outer * (16 - inner);       // how much of the pixel is frame, 0-256

            // Distance outside the frame's outer edge (pixel centre), for the glow.
            float cx = dx + 0.5f, cy = dy + 0.5f;
            float ex = cx < r ? r : cx > w - r ? w - r : cx, ey = cy < r ? r : cy > h - r ? h - r : cy;
            float outside = Distance(cx - ex, cy - ey) - r;
            bool glow = outer < 16 && outside < g;
            if (frame <= 0 && !glow)
                continue;

            LightPoint *p = &s_shine.points[n++];
            p->dx = (s16)dx, p->dy = (s16)dy;
            p->along = (u16)AlongFrame(cx - mid, cy - mid, w - t, h - t, r - mid);
            p->frame = (u16)frame;
            p->outer = (u8)outer;
            p->glow = glow;
            p->fade = 1.0f - (outside > 0.0f ? outside : 0.0f) / g;
        }
    }
    s_shine.count = n;
    s_shine.shapeW = w, s_shine.shapeH = h, s_shine.shapeR = r, s_shine.shapeT = t;
}

// Draws the light where it is now, in the current eye's image. keep: first keep each
// point's pixel (the frame was just drawn); else put the kept pixels back first.
static void DrawLight(bool keep)
{
    const int eye = ShineEye(), bx = s_shine.x[eye], by = s_shine.y, width = Draw_Width(s_shine.screen);
    const int head = (int)(osGetTime() % SHINE_LAP_MS * 1024 / SHINE_LAP_MS);
    const int add[3] = { s_shine.light.b - s_shine.base.b, s_shine.light.g - s_shine.base.g,
                         s_shine.light.r - s_shine.base.r };
    const u8 light[3] = { s_shine.light.b, s_shine.light.g, s_shine.light.r };
    u8 *fb = s_fb[s_shine.screen], *kept = s_shine.pixels[eye];

    for (int i = 0; i < s_shine.count; i++, kept += 3)
    {
        const LightPoint *pt = &s_shine.points[i];
        int x = bx + pt->dx, y = by + pt->dy;
        if (x < 0 || y < 0 || x >= width || y >= SCREEN_HEIGHT)
            continue;
        u8 *p = fb + x * COLUMN + (SCREEN_HEIGHT - 1 - y) * 3;
        if (keep)
            kept[0] = p[0], kept[1] = p[1], kept[2] = p[2];
        else
            p[0] = kept[0], p[1] = kept[1], p[2] = kept[2];

        int d = (pt->along - head) & 1023;
        if (d > 512)
            d = 1024 - d;
        if (d >= SHINE_HALF)
            continue;
        float k = s_lightCurve[d];
        if (k <= 0.0f)
            continue;

        if (pt->frame > 0)
        {
            int amount = (int)(k * pt->frame);      // 0-256
            for (int c = 0; c < 3; c++)
            {
                int v = p[c] + (add[c] * amount >> 8);
                p[c] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
        }
        if (pt->glow)
        {
            float fade = pt->fade;
            int amount = (int)(k * fade * fade * SHINE_GLOW_PEAK * (16 - pt->outer) / 16);     // 0-256
            for (int c = 0; c < 3; c++)
                p[c] = (u8)(p[c] + ((light[c] - p[c]) * amount >> 8));
        }
    }
}

void Draw_FrameShine(gfxScreen_t screen, int x, int y, int w, int h, int radius, int thickness,
                     Color base, Color light)
{
    s_shine.screen = screen;
    int eye = ShineEye();
    s_shine.x[eye] = Shifted(screen, x), s_shine.y = y, s_shine.w = w, s_shine.h = h;
    s_shine.radius = radius, s_shine.thickness = thickness;
    s_shine.base = base, s_shine.light = light;
    if (w != s_shine.shapeW || h != s_shine.shapeH || radius != s_shine.shapeR || thickness != s_shine.shapeT)
        BENCH_TIME(BENCH_LIGHT_SHAPE, ShapeLight());
    s_shine.saved[eye] = true;
    if (eye == 0)
        s_shine.saved[1] = false;   // the right eye's comes after the left's, with 3D on
    DrawLight(true);
}

bool Draw_FrameShineAgain(void)
{
    int eyes = s_shine.screen == GFX_TOP ? Draw_Eyes() : 1;

    for (int eye = 0; eye < eyes; eye++)
        if (!s_shine.saved[eye])
            return false;
    for (int eye = 0; eye < eyes; eye++)
    {
        Draw_Eye(eye);
        DrawLight(false);
        int x = s_shine.x[ShineEye()];
        Dirty(s_shine.screen == GFX_TOP ? ShineEye() : 2, x - SHINE_GLOW, x + s_shine.w + SHINE_GLOW);
    }
    Draw_Eye(0);
    return true;
}

void Draw_FrameShineOff(void)
{
    s_shine.saved[0] = s_shine.saved[1] = false;
}

int Draw_TextShadow(gfxScreen_t screen, int x, int y, int scale, Color color, const char *text, int maxWidth)
{
    static const Color SHADOW = { 0, 0, 0 };
    Draw_Text(screen, x + 1, y + 1, scale, SHADOW, text, maxWidth > 0 ? maxWidth + 1 : 0);
    return Draw_Text(screen, x, y, scale, color, text, maxWidth);
}

// The selected tile's shadow, worked out for its size and kept: how much each pixel darkens.
#define SHADOW_MAX      176
static struct
{
    int w, h, radius, blur, strength;   // the shape it's for (w = 0: none)
    u8  amount[SHADOW_MAX * SHADOW_MAX];    // column by column, top row first
    u8  full0[SHADOW_MAX], full1[SHADOW_MAX];   // each column's rows darkened the most (under the box)
} s_shadow;

static void ShapeShadow(int w, int h, int radius, int blur, int strength)
{
    // The shadow is symmetric left to right and top to bottom: a quarter is worked out and
    // mirrored (the same values: the distances are half-pixels, exact either way).
    int bw = w + 2 * blur, bh = h + 2 * blur, straight = -1;
    for (int column = 0; column < (bw + 1) / 2; column++)
    {
        int dx = column - blur;
        u8 *out = &s_shadow.amount[column * bh];
        float cx = dx + 0.5f;
        float ix = cx < radius ? radius : cx > w - radius ? w - radius : cx;

        // Between the corners every column is the same.
        if (ix == cx && straight >= 0)
        {
            memcpy(out, &s_shadow.amount[straight * bh], bh);
            s_shadow.full0[column] = s_shadow.full0[straight], s_shadow.full1[column] = s_shadow.full1[straight];
            continue;
        }
        if (ix == cx)
            straight = column;

        for (int row = 0; row < (bh + 1) / 2; row++)
        {
            // Distance outside the rounded box from the pixel's centre (0 inside it).
            float cy = row - blur + 0.5f;
            float iy = cy < radius ? radius : cy > h - radius ? h - radius : cy;
            float d = Distance(cx - ix, cy - iy) - radius;
            int amount = 0;
            if (d < blur)
            {
                float fade = d <= 0.0f ? 1.0f : 1.0f - d / blur;
                amount = (int)(strength * fade * fade);
            }
            amount = amount > 255 ? 255 : amount;
            out[row] = out[bh - 1 - row] = (u8)amount;
        }

        // The rows darkened the most: one run through the middle.
        int full0 = 0, full1 = 0;
        while (full0 < bh && out[full0] != strength)
            full0++;
        for (full1 = full0; full1 < bh && out[full1] == strength; full1++)
            ;
        s_shadow.full0[column] = (u8)(full0 < bh ? full0 : 0);
        s_shadow.full1[column] = (u8)(full0 < bh ? full1 : 0);
    }
    for (int column = (bw + 1) / 2; column < bw; column++)
    {
        int from = bw - 1 - column;
        memcpy(&s_shadow.amount[column * bh], &s_shadow.amount[from * bh], bh);
        s_shadow.full0[column] = s_shadow.full0[from], s_shadow.full1[column] = s_shadow.full1[from];
    }
    s_shadow.w = w, s_shadow.h = h, s_shadow.radius = radius, s_shadow.blur = blur, s_shadow.strength = strength;
}

// Darkens column x of the shadow (screen x, rows from by) for its rows from to to.
static void ShadowRows(gfxScreen_t screen, int x, int by, const u8 *amount, int from, int to, int full0, int full1)
{
    int a = from > full0 ? from : full0, b = to < full1 ? to : full1;
    if (b <= a)
        a = b = to;
    for (int dy = from; dy < a; dy++)
        Darker(At(screen, x, by + dy), amount[dy]);
    if (b > a)      // rows a to b, bottom one first in memory
        DarkerBytes(At(screen, x, by + b - 1), (b - a) * 3, 256 - s_shadow.strength);
    for (int dy = b; dy < to; dy++)
        Darker(At(screen, x, by + dy), amount[dy]);
}

void Draw_Shadow(gfxScreen_t screen, int x, int y, int w, int h, int radius, int blur, int offsetY, int strength,
                 int coveredAt)
{
    int bw = w + 2 * blur, bh = h + 2 * blur;
    if (bw > SHADOW_MAX || bh > SHADOW_MAX || strength <= 0 || strength > 255)
        return;
    if (s_shadow.w != w || s_shadow.h != h || s_shadow.radius != radius || s_shadow.blur != blur || s_shadow.strength != strength)
        BENCH_TIME(BENCH_SHADOW_MAP, ShapeShadow(w, h, radius, blur, strength));

    int bx = Shifted(screen, x - blur), by = y + offsetY - blur, dx0, dx1, dy0, dy1;
    if (!Clip(screen, bx, by, bw, bh, &dx0, &dx1, &dy0, &dy1))
        return;

    // Where the box itself will be drawn over it (between its corners, at its own depth):
    // nothing there needs darkening. Its rows in the shadow's rows: hole0 to hole1.
    int holeX0 = 0, holeX1 = 0, hole0 = y - by, hole1 = y + h - by;
    if (coveredAt != DRAW_UNCOVERED && screen == GFX_TOP)
    {
        int shift = s_eye == 1 ? -(int)lroundf(coveredAt * s_slider) : 0;
        holeX0 = x + shift + radius, holeX1 = x + shift + w - radius;
    }
    for (int dx = dx0; dx < dx1; dx++)
    {
        int sx = bx + dx, full0 = s_shadow.full0[dx], full1 = s_shadow.full1[dx];
        const u8 *amount = &s_shadow.amount[dx * bh];
        if (sx >= holeX0 && sx < holeX1)
        {
            ShadowRows(screen, sx, by, amount, dy0, hole0 > dy0 ? (hole0 < dy1 ? hole0 : dy1) : dy0, full0, full1);
            ShadowRows(screen, sx, by, amount, hole1 > dy0 ? (hole1 < dy1 ? hole1 : dy1) : dy0, dy1, full0, full1);
        }
        else
            ShadowRows(screen, sx, by, amount, dy0, dy1, full0, full1);
    }
}

void Draw_Darken(gfxScreen_t screen, int x, int y, int w, int h, int radius, int amount)
{
    int dx0, dx1, dy0, dy1;
    if (amount <= 0)
        return;
    x = Shifted(screen, x);
    if (!Clip(screen, x, y, w, h, &dx0, &dx1, &dy0, &dy1))
        return;
    for (int dx = dx0; dx < dx1; dx++)
    {
        bool cornerColumn = dx < radius || dx >= w - radius;
        u8  *p = At(screen, x + dx, y + dy0);
        for (int dy = dy0; dy < dy1; dy++, p -= 3)
        {
            int cover = cornerColumn && (dy < radius || dy >= h - radius) ? Coverage(dx, dy, w, h, radius) : 16;
            Darker(p, amount * cover / 16);
        }
    }
}

// Rings' pixels (how much of each is ring), worked out once per shape: the empty slots all
// share one.
#define RING_SHAPES     6
#define RING_POINTS     4096
typedef struct
{
    s16 dx, dy;
    u8  cover;      // 1-16
} RingPoint;
static struct
{
    int         w, h, radius, thickness, count;     // count < 0: too many points to keep
    u32         used;
    RingPoint   points[RING_POINTS];
} s_rings[RING_SHAPES];

// Calls point(dx, dy, cover) for each pixel of the ring with some ring in it.
#define FOR_EACH_RING_PIXEL(w, h, radius, thickness, ...)                                               \
    do {                                                                                                \
        int inner_ = (radius) > (thickness) ? (radius) - (thickness) : 0;                               \
        for (int dx = 0; dx < (w); dx++)                                                                \
        {                                                                                               \
            /* Down the sides, the whole column is ring; elsewhere only its top and bottom rows */     \
            /* can be (as far down as the corner, or the thickness between the corners). */            \
            bool side_ = dx < (thickness) || dx >= (w) - (thickness);                                   \
            int  band_ = dx < (radius) || dx >= (w) - (radius) ? ((radius) > (thickness) ? (radius) : (thickness)) : (thickness); \
            for (int dy = 0; dy < (h); dy++)                                                            \
            {                                                                                           \
                if (!side_ && dy == band_ && (h) - band_ > band_)                                       \
                    dy = (h) - band_;                                                                   \
                int cover = Coverage(dx, dy, (w), (h), (radius));                                       \
                if (dx >= (thickness) && dy >= (thickness) && dx < (w) - (thickness) && dy < (h) - (thickness)) \
                    cover -= Coverage(dx - (thickness), dy - (thickness), (w) - 2 * (thickness), (h) - 2 * (thickness), inner_); \
                if (cover > 0)                                                                          \
                    __VA_ARGS__;                                                                        \
            }                                                                                           \
        }                                                                                               \
    } while (0)

void Draw_RoundRing(gfxScreen_t screen, int x, int y, int w, int h, int radius, int thickness, Color color, int alpha)
{
    int found = -1, oldest = 0;
    for (int i = 0; i < RING_SHAPES; i++)
    {
        if (s_rings[i].used != 0 && s_rings[i].w == w && s_rings[i].h == h && s_rings[i].radius == radius &&
            s_rings[i].thickness == thickness)
            found = i;
        if (s_rings[i].used < s_rings[oldest].used)
            oldest = i;
    }
    if (found < 0)
    {
        found = oldest;
        int n = 0;
        RingPoint *points = s_rings[found].points;
        FOR_EACH_RING_PIXEL(w, h, radius, thickness,
            { if (n < RING_POINTS) points[n] = (RingPoint){ (s16)dx, (s16)dy, (u8)cover }; n++; });
        s_rings[found].w = w, s_rings[found].h = h, s_rings[found].radius = radius, s_rings[found].thickness = thickness;
        s_rings[found].count = n <= RING_POINTS ? n : -1;
    }
    s_rings[found].used = ++s_clock;

    if (s_rings[found].count < 0)
    {
        FOR_EACH_RING_PIXEL(w, h, radius, thickness, Blend(screen, x + dx, y + dy, color, cover * alpha / 16));
        return;
    }
    const RingPoint *p = s_rings[found].points;
    for (int i = 0; i < s_rings[found].count; i++, p++)
        Blend(screen, x + p->dx, y + p->dy, color, p->cover * alpha / 16);
}

void Draw_StampKeep(DrawStamp *s, gfxScreen_t screen, int x, int y, int w, int h, int radius, Color color)
{
    int sx = Shifted(screen, x);
    s->kept = false;
    if (w > DRAW_STAMP_W || h > DRAW_STAMP_H || sx < 0 || y < 0 || sx + w > Draw_Width(screen) || y + h > SCREEN_HEIGHT)
        return;
    for (int dx = 0; dx < w; dx++)
        memcpy(&s->pixels[dx * h * 3], At(screen, sx + dx, y + h - 1), h * 3);
    s->screen = screen, s->x = x, s->y = y, s->w = w, s->h = h, s->radius = radius, s->color = color;
    s->kept = true;
}

bool Draw_StampAgain(const DrawStamp *s)
{
    int sx = Shifted(s->screen, s->x);
    if (!s->kept || sx < 0 || sx + s->w > Draw_Width(s->screen))
        return false;
    for (int dx = 0; dx < s->w; dx++)
    {
        u8 *column = At(s->screen, sx + dx, s->y + s->h - 1);     // its bottom row
        const u8 *kept = &s->pixels[dx * s->h * 3];
        if (dx >= s->radius && dx < s->w - s->radius)
        {
            memcpy(column, kept, s->h * 3);
            continue;
        }
        // A corner column: what's fully inside as kept, the smoothed edge over what's there now.
        for (int k = 0; k < s->h; k++)
        {
            int cover = Coverage(dx, s->h - 1 - k, s->w, s->h, s->radius);
            u8 *p = column + k * 3;
            if (cover >= 16)
                p[0] = kept[k * 3], p[1] = kept[k * 3 + 1], p[2] = kept[k * 3 + 2];
            else
                Mixed(p, s->color, cover);
        }
    }
    return true;
}

// The colour of a vertical fade from top (row y0) to bottom (row y1) at row y.
static inline Color GradientAt(int y, int y0, int y1, Color top, Color bottom)
{
    int span = y1 - y0 > 1 ? y1 - y0 - 1 : 1, t = y - y0;
    return (Color){ (u8)(top.r + (bottom.r - top.r) * t / span), (u8)(top.g + (bottom.g - top.g) * t / span),
                    (u8)(top.b + (bottom.b - top.b) * t / span) };
}

void Draw_Gradient(gfxScreen_t screen, int y0, int y1, Color top, Color bottom)
{
    u8 column[COLUMN];

    if (y0 < 0)
        y0 = 0;
    if (y1 > SCREEN_HEIGHT)
        y1 = SCREEN_HEIGHT;
    if (y1 <= y0)
        return;
    // Columns are stored bottom row first, so one column is built and copied across.
    for (int y = y0; y < y1; y++)
    {
        Color c = GradientAt(y, y0, y1, top, bottom);
        u8 *p = column + (SCREEN_HEIGHT - 1 - y) * 3;
        p[0] = c.b, p[1] = c.g, p[2] = c.r;
    }
    int from = (SCREEN_HEIGHT - y1) * 3, length = (y1 - y0) * 3;
    for (int x = 0; x < Draw_Width(screen); x++)
        memcpy(s_fb[screen] + x * COLUMN + from, column + from, length);
}

void Draw_Into(gfxScreen_t screen, u8 *buffer)
{
    static u8 *back[2];
    if (buffer != NULL)
    {
        if (back[screen] == NULL)
            back[screen] = s_fb[screen];
        s_fb[screen] = buffer;
    }
    else if (back[screen] != NULL)
    {
        s_fb[screen] = back[screen];
        back[screen] = NULL;
    }
}

void Draw_FadeToGradient(gfxScreen_t screen, int y0, int y1, Color top, Color bottom, int keep, const u8 *from)
{
    if (keep < 0)
        keep = 0;
    if (keep >= 256)
    {
        if (from != NULL)
            memcpy(s_fb[screen], from, Draw_Width(screen) * COLUMN);
        return;
    }
    int start = y0 < 0 ? 0 : y0, end = y1 < SCREEN_HEIGHT ? y1 : SCREEN_HEIGHT;
    if (end <= start)
        return;
    if (keep == 0)
    {
        Draw_Gradient(screen, y0, y1, top, bottom);     // the same as mixing all the way
        return;
    }

    // What each byte of a column moves toward (B, G, R from the bottom row up), worked out
    // once and used down every column.
    u16 add[COLUMN];
    for (int y = start; y < end; y++)
    {
        Color c = GradientAt(y, y0, y1, top, bottom);
        u16 *a = &add[(SCREEN_HEIGHT - 1 - y) * 3];
        a[0] = (u16)(c.b * (256 - keep)), a[1] = (u16)(c.g * (256 - keep)), a[2] = (u16)(c.r * (256 - keep));
    }

    // Whole columns: four bytes at a time, two in each pair of 16-bit lanes (each byte times
    // keep plus its share of the fade stays under 65536).
    if (start == 0 && end == SCREEN_HEIGHT)
    {
        u32 even[COLUMN / 4], odd[COLUMN / 4];
        for (int i = 0; i < COLUMN / 4; i++)
        {
            even[i] = add[i * 4] | (u32)add[i * 4 + 2] << 16;
            odd[i] = add[i * 4 + 1] | (u32)add[i * 4 + 3] << 16;
        }
        const u32 k = (u32)keep;
        for (int x = 0; x < Draw_Width(screen); x++)
        {
            u32 *out = (u32 *)(s_fb[screen] + x * COLUMN);
            const u32 *in = from != NULL ? (const u32 *)(from + x * COLUMN) : out;
            for (int i = 0; i < COLUMN / 4; i++)
            {
                u32 v = in[i];
                out[i] = ((((v & 0x00FF00FF) * k + even[i]) >> 8) & 0x00FF00FF) |
                         ((((v >> 8) & 0x00FF00FF) * k + odd[i]) & 0xFF00FF00);
            }
        }
        return;
    }

    for (int x = 0; x < Draw_Width(screen); x++)
    {
        u8 *p = s_fb[screen] + x * COLUMN;
        const u8 *in = from != NULL ? from + x * COLUMN : p;
        for (int i = (SCREEN_HEIGHT - end) * 3; i < (SCREEN_HEIGHT - start) * 3; i++)
            p[i] = (u8)((in[i] * keep + add[i]) >> 8);
    }
}
