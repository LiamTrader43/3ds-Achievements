#include "overlay.h"

#include <string.h>

#include "font.h"

_Static_assert(OVERLAY_FONT_WIDTH == FONT_WIDTH && OVERLAY_FONT_HEIGHT == FONT_HEIGHT, "overlay.h font size");

// Luma3DS kernel extension (csvc.s).
Result svcCustomBackdoor(void *func, ...);
void   svcFlushDataCacheRange(void *address, u32 length);

// LCD registers through Luma's global physical mapping (PA | bit 31), as
// Rosalina's draw.h does. Only touched in kernel mode here. (A host test can
// define REG32 itself to render into a fake framebuffer.)
#ifndef REG32
#define REG32(pa)           (*(vu32 *)((pa) | (1u << 31)))
#endif
#define FB_BOTTOM_ADDR_1    REG32(0x10400568)
#define FB_BOTTOM_ADDR_2    REG32(0x1040056C)
#define FB_BOTTOM_FMT       REG32(0x10400570)
#define FB_BOTTOM_SEL       REG32(0x10400578)
#define FB_BOTTOM_STRIDE    REG32(0x10400590)

#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       240
#define BANNER_HEIGHT       30
#define BACKGROUND_ALPHA    166     // out of 256: background over the game (~65%)
#define RECHECK_MS          33      // also catch games that render in place

static const Rgb BACKGROUND = { 16, 16, 24 };
static const Rgb GOLD = { 255, 196, 40 };
static const Rgb TEXT = { 255, 255, 255 };

typedef struct
{
    const char *title;
    const char *text;
    const char *rightText;  // right-aligned on the text line (e.g. "1/3")
    u32         kernPaToVa;
    u32         pa;         // probe: out; draw: in (buffer that was flushed)
    u32         size;       // probe: out, bytes the buffer spans
    bool        drew;       // draw: out
} DrawArgs;

static const u8 BYTES_PER_PIXEL[8] = { 4, 3, 2, 2, 2, 0, 0, 0 };

static void Encode(u32 fmt, Rgb c, u8 *out)
{
    switch (fmt)
    {
        case GSP_RGBA8_OES:     // bytes: A, B, G, R
            out[0] = 0xFF; out[1] = c.b; out[2] = c.g; out[3] = c.r;
            break;
        case GSP_BGR8_OES:      // bytes: B, G, R
            out[0] = c.b; out[1] = c.g; out[2] = c.r;
            break;
        case GSP_RGB565_OES:
            *(u16 *)out = (u16)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
            break;
        case GSP_RGB5_A1_OES:
            *(u16 *)out = (u16)(((c.r >> 3) << 11) | ((c.g >> 3) << 6) | ((c.b >> 3) << 1) | 1);
            break;
        case GSP_RGBA4_OES:
            *(u16 *)out = (u16)(((c.r >> 4) << 12) | ((c.g >> 4) << 8) | ((c.b >> 4) << 4) | 0xF);
            break;
    }
}

static Rgb Decode(u32 fmt, const u8 *p)
{
    u32 v;
    switch (fmt)
    {
        case GSP_RGBA8_OES:
            return (Rgb){ p[3], p[2], p[1] };
        case GSP_BGR8_OES:
            return (Rgb){ p[2], p[1], p[0] };
        case GSP_RGB565_OES:
            v = *(const u16 *)p;
            return (Rgb){ (u8)(((v >> 11) & 31) << 3), (u8)(((v >> 5) & 63) << 2), (u8)((v & 31) << 3) };
        case GSP_RGB5_A1_OES:
            v = *(const u16 *)p;
            return (Rgb){ (u8)(((v >> 11) & 31) << 3), (u8)(((v >> 6) & 31) << 3), (u8)(((v >> 1) & 31) << 3) };
        default:    // GSP_RGBA4_OES
            v = *(const u16 *)p;
            return (Rgb){ (u8)(((v >> 12) & 15) << 4), (u8)(((v >> 8) & 15) << 4), (u8)(((v >> 4) & 15) << 4) };
    }
}

// Framebuffers are stored rotated: screen column x is 240 pixels at
// pa + x * stride, and offset y * bpp is screen row 239 - y.
static inline u8 *Pixel(u8 *fb, u32 stride, u32 bpp, u32 x, u32 y)
{
    return fb + x * stride + (SCREEN_HEIGHT - 1 - y) * bpp;
}

// Draws s from x, stopping before any character would pass `limit`.
static void DrawText(u8 *fb, u32 stride, u32 bpp, u32 fmt, u32 x, u32 y, u32 limit, const char *s, Rgb c)
{
    u8 raw[4];
    Encode(fmt, c, raw);

    for (; *s != '\0' && x + FONT_WIDTH <= limit; s++, x += FONT_WIDTH)
    {
        const unsigned char *glyph = &font[(unsigned char)*s * FONT_HEIGHT];
        for (u32 row = 0; row < FONT_HEIGHT; row++)
            for (u32 col = 0; col < FONT_WIDTH; col++)
                if ((glyph[row] >> (6 - col)) & 1)
                    memcpy(Pixel(fb, stride, bpp, x + col, y + row), raw, bpp);
    }
}

// The bottom screen's on-screen buffer, or 0. Kernel mode.
static u32 CurrentBuffer(u32 *fmt, u32 *bpp, u32 *stride)
{
    *fmt = FB_BOTTOM_FMT & 7;
    *bpp = BYTES_PER_PIXEL[*fmt];
    *stride = FB_BOTTOM_STRIDE;
    u32 pa = (FB_BOTTOM_SEL & 1) ? FB_BOTTOM_ADDR_2 : FB_BOTTOM_ADDR_1;
    return (*bpp == 0 || *stride < SCREEN_HEIGHT * *bpp) ? 0 : pa;
}

// Kernel mode: which buffer is on screen and how big it is.
static void ProbeKernel(DrawArgs *a)
{
    u32 fmt, bpp, stride;
    a->pa = CurrentBuffer(&fmt, &bpp, &stride);
    a->size = SCREEN_WIDTH * stride;
}

// Kernel mode: draws the banner into a->pa, unless the game has swapped since
// the probe or the banner is still there (blending twice would darken it).
static void DrawKernel(DrawArgs *a)
{
    u32 fmt, bpp, stride;
    a->drew = false;
    if (CurrentBuffer(&fmt, &bpp, &stride) != a->pa || a->pa == 0)
        return;

    u8 *fb = (u8 *)(a->pa + a->kernPaToVa);

    // The gold line along the banner's bottom edge marks a banner already drawn.
    u8 gold[4];
    Encode(fmt, GOLD, gold);
    static const u32 marks[] = { 0, SCREEN_WIDTH / 2, SCREEN_WIDTH - 1 };
    bool present = true;
    for (u32 i = 0; i < 3; i++)
        if (memcmp(Pixel(fb, stride, bpp, marks[i], BANNER_HEIGHT - 1), gold, bpp) != 0)
            present = false;
    if (present)
        return;

    for (u32 x = 0; x < SCREEN_WIDTH; x++)
    {
        for (u32 y = 0; y < BANNER_HEIGHT - 1; y++)
        {
            u8 *p = Pixel(fb, stride, bpp, x, y);
            Rgb under = Decode(fmt, p);
            Rgb mixed = {
                (u8)((BACKGROUND.r * BACKGROUND_ALPHA + under.r * (256 - BACKGROUND_ALPHA)) >> 8),
                (u8)((BACKGROUND.g * BACKGROUND_ALPHA + under.g * (256 - BACKGROUND_ALPHA)) >> 8),
                (u8)((BACKGROUND.b * BACKGROUND_ALPHA + under.b * (256 - BACKGROUND_ALPHA)) >> 8),
            };
            u8 raw[4];
            Encode(fmt, mixed, raw);
            memcpy(p, raw, bpp);
        }
        memcpy(Pixel(fb, stride, bpp, x, BANNER_HEIGHT - 1), gold, bpp);
    }

    // The name stops a character short of the right-aligned count.
    u32 rightX = SCREEN_WIDTH - 8 - strlen(a->rightText) * FONT_WIDTH;
    DrawText(fb, stride, bpp, fmt, 8, 3, SCREEN_WIDTH - 8, a->title, GOLD);
    DrawText(fb, stride, bpp, fmt, 8, 15, rightX - FONT_WIDTH, a->text, TEXT);
    DrawText(fb, stride, bpp, fmt, rightX, 15, SCREEN_WIDTH - 8, a->rightText, TEXT);
    a->drew = true;
}

// Kernel virtual address of physical memory (as Flashback's KERNPA2VA_OFFSET).
static u32 KernPaToVa(void)
{
    return GET_VERSION_MINOR(osGetKernelVersion()) < 44 ? 0xD0000000 : 0xC0000000;
}

void Overlay_Show(const char *title, const char *text, const char *rightText, u32 milliseconds)
{
    u32 offset = KernPaToVa();
    DrawArgs args = { title, text, rightText, offset, 0, 0, false };

    u64 end = osGetTime() + milliseconds;
    u64 lastCheck = 0;
    u32 lastPa = 0;

    while (osGetTime() < end)
    {
        svcCustomBackdoor(ProbeKernel, &args);
        if (args.pa != 0 && (args.pa != lastPa || osGetTime() - lastCheck >= RECHECK_MS))
        {
            // Drop stale cache lines first: the GPU writes frames behind the
            // cache, and the banner blends with what's really there.
            svcFlushDataCacheRange((void *)(args.pa + offset), args.size);
            svcCustomBackdoor(DrawKernel, &args);
            if (args.drew)
                svcFlushDataCacheRange((void *)(args.pa + offset), args.size);  // let the LCD see it
            lastPa = args.pa;
            lastCheck = osGetTime();
        }
        svcSleepThread(1000 * 1000LL);
    }
}

u32 Overlay_Buffer(void)
{
    DrawArgs args = { 0 };
    svcCustomBackdoor(ProbeKernel, &args);
    return args.pa;
}

typedef struct
{
    const OverlayItem  *items;
    u32                 count;
    u32                 kernPaToVa;
    u32                 pa;         // out: the buffer drawn into, 0 if none
    u32                 size;       // out
} ScreenArgs;

static u8 s_lengths[OVERLAY_MAX_ITEMS];     // text lengths, measured before entering the kernel

// Kernel mode: builds each screen column (240 colour indices: 0 = black,
// i + 1 = item i) and writes it out, so every pixel gets its final colour once.
static void DrawScreenKernel(ScreenArgs *a)
{
    static const Rgb    BLACK = { 0, 0, 0 };
    static u32          palette[OVERLAY_MAX_ITEMS + 1];
    static u8           column[SCREEN_HEIGHT];
    u32                 fmt, bpp, stride;

    a->pa = CurrentBuffer(&fmt, &bpp, &stride);
    a->size = SCREEN_WIDTH * stride;
    if (a->pa == 0)
        return;

    u8 *fb = (u8 *)(a->pa + a->kernPaToVa);
    Encode(fmt, BLACK, (u8 *)&palette[0]);
    for (u32 i = 0; i < a->count; i++)
        Encode(fmt, a->items[i].color, (u8 *)&palette[i + 1]);

    for (u32 x = 0; x < SCREEN_WIDTH; x++)
    {
        memset(column, 0, sizeof(column));

        for (u32 i = 0; i < a->count; i++)
        {
            const OverlayItem *it = &a->items[i];
            if (x < it->x)
                continue;
            u32 dx = x - it->x;

            if (it->text == NULL)
            {
                if (dx < it->width)
                    for (u32 y = it->y; y < it->y + it->height && y < SCREEN_HEIGHT; y++)
                        column[y] = (u8)(i + 1);
                continue;
            }

            if (dx / FONT_WIDTH >= s_lengths[i])
                continue;
            const unsigned char *glyph = &font[(unsigned char)it->text[dx / FONT_WIDTH] * FONT_HEIGHT];
            u32 bit = 6 - dx % FONT_WIDTH;
            for (u32 row = 0; row < FONT_HEIGHT && it->y + row < SCREEN_HEIGHT; row++)
                if ((glyph[row] >> bit) & 1)
                    column[it->y + row] = (u8)(i + 1);
        }

        u8 *p = Pixel(fb, stride, bpp, x, 0);
        for (u32 y = 0; y < SCREEN_HEIGHT; y++, p -= bpp)
        {
            const u8 *c = (const u8 *)&palette[column[y]];
            p[0] = c[0];
            p[1] = c[1];
            if (bpp >= 3)
                p[2] = c[2];
            if (bpp == 4)
                p[3] = c[3];
        }
    }
}

void Overlay_DrawScreen(const OverlayItem *items, u32 count)
{
    ScreenArgs args = { items, count < OVERLAY_MAX_ITEMS ? count : OVERLAY_MAX_ITEMS, KernPaToVa(), 0, 0 };

    for (u32 i = 0; i < args.count; i++)
    {
        size_t length = items[i].text ? strlen(items[i].text) : 0;
        s_lengths[i] = (u8)(length < SCREEN_WIDTH / FONT_WIDTH ? length : SCREEN_WIDTH / FONT_WIDTH);
    }

    svcCustomBackdoor(DrawScreenKernel, &args);
    if (args.pa != 0)
        svcFlushDataCacheRange((void *)(args.pa + args.kernPaToVa), args.size);     // let the LCD see it
}
