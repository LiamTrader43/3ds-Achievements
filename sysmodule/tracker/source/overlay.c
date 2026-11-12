#include "overlay.h"

#include <stdio.h>
#include <string.h>

#include "overlay_blend.h"
#include "../../../app/source/font_inter.h"     // the app's font; only its 13 px set is used here

#pragma GCC optimize("O2")     // a banner repair races the game's next screen swap

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

// Luma maps physical memory below 0x30000000 uncached at PA | bit 31 (its
// kernel extension's main.c; Flashback reads DSP memory this way), so the
// banner can be looked for in a framebuffer without cache maintenance. Slow
// per access, so only used for the per-millisecond checks.
#ifndef PA_PTR
#define PA_PTR(pa)          ((u8 *)((pa) | (1u << 31)))
#endif

#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       240
#define BANNER_HEIGHT       30
#define BACKGROUND_ALPHA    166     // out of 256: background over the game (~65%)
#define BANNER_TITLE_Y      3       // tops of the banner's two lines of text
#define BANNER_TEXT_Y       15
#define BANNER_GAP          6       // between the second line and its right-aligned text

_Static_assert(OVERLAY_LINE_H == 13, "overlay.h line height = the font13 set");
_Static_assert(BANNER_TEXT_Y + FONT13_BOTTOM < BANNER_HEIGHT - 1, "text stays above the gold bottom edge");
_Static_assert(SCREEN_WIDTH - 16 == OVERLAY_BANNER_W, "overlay.h banner width");

static const Rgb BACKGROUND = { 16, 16, 24 };
static const Rgb GOLD = { 255, 196, 40 };
static const Rgb TEXT = { 255, 255, 255 };

#define MAX_BUFFERS         6       // bottom buffers a game rotates through (SM3DL: 3)
#define BUFFER_MEMORY_MS    500     // a buffer not shown for this long is forgotten
#define BANNER_PRIORITY     0x20    // while a banner is up (the tracker's own is 58)

typedef struct
{
    const char *title;
    const char *text;
    const char *rightText;  // right-aligned on the text line (e.g. "1/3")
    int         rightX;     // where rightText starts
    u32         kernPaToVa;
    u32         pa;         // probe: out, the on-screen buffer; draw: in (buffer that was flushed)
    u32         shown;      // probe: out, the on-screen buffer (kept while pa is reused)
    u32         fmt, stride;    // probe: out
    u32         size;       // probe: out, bytes a buffer spans
    u32         registers[2];   // probe: out, the two address registers
    u32         buffers[MAX_BUFFERS];   // probe: in, buffers the game has shown lately
    u32         count;
    bool        missing[MAX_BUFFERS];   // probe: out, the banner is (partly) gone from that buffer
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

static inline const FontGlyph *Glyph(char ch)
{
    unsigned char c = (unsigned char)ch;
    if (c < 32 || c > 126)
        c = '?';
    return &font13_glyphs[c - 32];
}

// The pen moves in 1/64 pixels; a position is rounded to the nearest pixel.
static inline int Pixels(int pen64)
{
    return (pen64 + 32) >> 6;
}

int Overlay_TextWidth(const char *text)
{
    int pen64 = 0;
    for (; *text != '\0'; text++)
        pen64 += Glyph(*text)->adv64;
    return Pixels(pen64);
}

int Overlay_TextFit(const char *text, int width)
{
    int pen64 = 0, n = 0;
    for (; text[n] != '\0'; n++)
    {
        pen64 += Glyph(text[n])->adv64;
        if (Pixels(pen64) > width)
            break;
    }
    return n;
}

void Overlay_Ellipsize(char *out, u32 capacity, const char *text, int maxWidth)
{
    u32 length = strlen(text);
    if (length < capacity && Overlay_TextWidth(text) <= maxWidth)
    {
        memcpy(out, text, length + 1);
        return;
    }
    int keep = Overlay_TextFit(text, maxWidth - Overlay_TextWidth("..."));
    if (keep > (int)capacity - 4)
        keep = (int)capacity - 4;
    while (keep > 0 && text[keep - 1] == ' ')
        keep--;                     // no gap before the dots
    memcpy(out, text, keep);
    memcpy(out + keep, "...", 4);
}

// c over `under` by a glyph's coverage (0-255), in 16 steps like the app's text.
static inline Rgb Mix(Rgb under, Rgb c, u32 alpha)
{
    u32 cover = (alpha * 16 + 127) / 255;
    if (cover >= 16)
        return c;
    return (Rgb){ (u8)((under.r * (16 - cover) + c.r * cover) / 16), (u8)((under.g * (16 - cover) + c.g * cover) / 16),
                  (u8)((under.b * (16 - cover) + c.b * cover) / 16) };
}

// Draws s from x, blended over what's there, stopping before any character would
// pass `limit`, only in the screen columns marked in `columns`.
static void DrawText(u8 *fb, u32 stride, u32 bpp, u32 fmt, int x, int y, int limit, const char *s, Rgb c,
                     const bool *columns)
{
    int pen64 = x << 6;

    for (; *s != '\0'; s++)
    {
        const FontGlyph *g = Glyph(*s);
        if (Pixels(pen64 + g->adv64) > limit)
            break;
        int left = Pixels(pen64) + g->x;
        for (int col = 0; col < g->w; col++)
        {
            int px = left + col;
            if (px < 0 || px >= SCREEN_WIDTH || !columns[px])
                continue;
            const u8 *alpha = font13_alpha + g->off + col;
            for (int row = 0; row < g->h; row++, alpha += g->w)
                if (*alpha != 0)
                {
                    u8 *p = Pixel(fb, stride, bpp, px, y + g->y + row);
                    u8 raw[4];
                    Encode(fmt, Mix(Decode(fmt, p), c, *alpha), raw);
                    memcpy(p, raw, bpp);
                }
        }
        pen64 += g->adv64;
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

// Physical memory a framebuffer can be in (VRAM or FCRAM). The address
// registers can hold anything before a game sets them.
static bool IsFramebuffer(u32 pa, u32 size)
{
    return (pa >= 0x18000000 && pa + size <= 0x18600000) || (pa >= 0x20000000 && pa + size <= 0x30000000);
}

// Kernel mode: which buffer is on screen, the address registers, and whether
// the banner's gold bottom edge is intact in each buffer the game has shown
// lately (read uncached, so a frame the GPU just wrote is seen without
// flushing).
static void ProbeKernel(DrawArgs *a)
{
    u32 fmt, bpp, stride;
    a->pa = a->shown = CurrentBuffer(&fmt, &bpp, &stride);
    a->size = SCREEN_WIDTH * stride;
    a->fmt = fmt;
    a->stride = stride;
    a->registers[0] = FB_BOTTOM_ADDR_1;
    a->registers[1] = FB_BOTTOM_ADDR_2;

    u8 gold[4];
    Encode(fmt, GOLD, gold);
    for (u32 i = 0; i < a->count; i++)
    {
        u32 pa = a->buffers[i];
        // Every 4th column and the last: a new frame replaces them all, and
        // DrawKernel then checks every column.
        a->missing[i] = false;
        if (a->shown == 0 || !IsFramebuffer(pa, a->size))
            continue;
        for (u32 x = 0; x < SCREEN_WIDTH && !a->missing[i]; x = x + 4 < SCREEN_WIDTH ? x + 4 : (x == SCREEN_WIDTH - 1 ? SCREEN_WIDTH : SCREEN_WIDTH - 1))
            a->missing[i] = memcmp(Pixel(PA_PTR(pa), stride, bpp, x, BANNER_HEIGHT - 1), gold, bpp) != 0;
    }
}

// Kernel mode: draws the banner into buffer a->pa (if the screen's format
// hasn't changed since the probe). Column by column: a screen column is stored contiguously,
// and the game's next frame can land on only some of them, so each column
// whose gold pixel (on the banner's bottom edge) is gone is redrawn and the
// others are left alone (blending twice would darken them).
static void DrawKernel(DrawArgs *a)
{
    static bool redraw[SCREEN_WIDTH];
    u32         fmt, bpp, stride;

    a->drew = false;
    if (CurrentBuffer(&fmt, &bpp, &stride) == 0 || fmt != a->fmt || stride != a->stride
        || !IsFramebuffer(a->pa, SCREEN_WIDTH * stride))
        return;

    u8 *fb = (u8 *)(a->pa + a->kernPaToVa);

    u8 gold[4];
    Encode(fmt, GOLD, gold);
    bool any = false;
    for (u32 x = 0; x < SCREEN_WIDTH; x++)
    {
        redraw[x] = memcmp(Pixel(fb, stride, bpp, x, BANNER_HEIGHT - 1), gold, bpp) != 0;
        any |= redraw[x];
    }
    if (!any)
        return;

    for (u32 x = 0; x < SCREEN_WIDTH; x++)
    {
        if (!redraw[x])
            continue;
        if (Blend_Column(Pixel(fb, stride, bpp, x, 0), fmt, BANNER_HEIGHT - 1,
                         (BlendColor){ BACKGROUND.r, BACKGROUND.g, BACKGROUND.b }, BACKGROUND_ALPHA))
        {
            memcpy(Pixel(fb, stride, bpp, x, BANNER_HEIGHT - 1), gold, bpp);
            continue;
        }

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

    // Each text pixel is blended over the background just drawn in its column.
    DrawText(fb, stride, bpp, fmt, 8, BANNER_TITLE_Y, SCREEN_WIDTH - 8, a->title, GOLD, redraw);
    DrawText(fb, stride, bpp, fmt, 8, BANNER_TEXT_Y, a->rightX - BANNER_GAP, a->text, TEXT, redraw);
    DrawText(fb, stride, bpp, fmt, a->rightX, BANNER_TEXT_Y, SCREEN_WIDTH - 8, a->rightText, TEXT, redraw);
    a->drew = true;
}

// Kernel virtual address of physical memory (as Flashback's KERNPA2VA_OFFSET).
static u32 KernPaToVa(void)
{
    return GET_VERSION_MINOR(osGetKernelVersion()) < 44 ? 0xD0000000 : 0xC0000000;
}

// The game draws each new frame into a buffer that isn't on screen, then
// shows it. The banner has to be back in that buffer before then, or the
// frame is seen without it. Games rotate through more buffers than the two
// address registers hold (SM3DL uses three: the next frame goes into the one
// in neither register), so every buffer shown in the last BUFFER_MEMORY_MS
// is watched. Logged on hardware before this: SM3DL swapped 134 times in 5 s
// and the banner was restored in time only twice.
static struct
{
    u32 pa;
    u64 lastSeen;
} g_known[MAX_BUFFERS];
static u32 g_knownCount, g_knownFmt, g_knownStride;

// What the last banner saw, for the log (Overlay_Stats).
static struct
{
    u32 ticks, maxGapUs, maxTickUs;
    u32 swaps;              // on-screen buffer changes
    u32 fixedShown;         // banner restored in the on-screen buffer (late: seen without it)
    u32 fixedRegister;      // ... in the other register's buffer
    u32 fixedOther;         // ... in a buffer in neither register (the next frame)
    u32 seen[4], seenCount; // distinct buffer addresses in the registers
    u32 fmt, stride;
    s32 priorityResult;
    u32 fixes;              // repairs timed below
    u32 flushBeforeUs, flushBeforeMaxUs, drawUs, drawMaxUs, flushAfterUs, flushAfterMaxUs;
    u32 swappedDuringFix;   // the buffer being repaired went on screen before the repair ended
} g_stats;

// Adds one timed step to a running total and maximum.
static void AddTiming(u64 from, u64 to, u32 *total, u32 *maximum)
{
    u32 us = (u32)((to - from) / CPU_TICKS_PER_USEC);
    *total += us;
    if (us > *maximum)
        *maximum = us;
}

// Kernel mode: just the on-screen buffer.
static void ShownKernel(DrawArgs *a)
{
    u32 fmt, bpp, stride;
    a->shown = CurrentBuffer(&fmt, &bpp, &stride);
}

static void NoteSeen(u32 pa)
{
    for (u32 i = 0; i < g_stats.seenCount && i < 4; i++)
        if (g_stats.seen[i] == pa)
            return;
    if (g_stats.seenCount < 4)
        g_stats.seen[g_stats.seenCount] = pa;
    g_stats.seenCount++;
}

// Adds the buffers in the address registers to g_known (or refreshes them)
// and forgets buffers not shown lately.
static void RememberBuffers(const DrawArgs *a)
{
    u64 now = osGetTime();
    if (a->fmt != g_knownFmt || a->stride != g_knownStride)
        g_knownCount = 0;
    g_knownFmt = a->fmt;
    g_knownStride = a->stride;

    for (u32 r = 0; r < 2 && a->shown != 0; r++)
    {
        u32 pa = a->registers[r];
        if (!IsFramebuffer(pa, a->size))
            continue;
        NoteSeen(pa);
        u32 i = 0;
        while (i < g_knownCount && g_known[i].pa != pa)
            i++;
        if (i == g_knownCount)
        {
            if (g_knownCount < MAX_BUFFERS)
                g_knownCount++;
            else                                // replace the one shown longest ago
            {
                i = 0;
                for (u32 j = 1; j < MAX_BUFFERS; j++)
                    if (g_known[j].lastSeen < g_known[i].lastSeen)
                        i = j;
            }
            g_known[i].pa = pa;
        }
        g_known[i].lastSeen = now;
    }

    for (u32 i = 0; i < g_knownCount; )
        if (now - g_known[i].lastSeen > BUFFER_MEMORY_MS)
            g_known[i] = g_known[--g_knownCount];
        else
            i++;
}

static void OverlayTick(DrawArgs *args)
{
    args->count = g_knownCount;
    for (u32 i = 0; i < g_knownCount; i++)
        args->buffers[i] = g_known[i].pa;

    svcCustomBackdoor(ProbeKernel, args);
    u32 shown = args->shown;
    for (u32 i = 0; i < args->count; i++)
    {
        if (!args->missing[i])
            continue;
        // Drop stale cache lines first: the GPU writes frames behind the
        // cache, and the banner blends with what's really there.
        args->pa = args->buffers[i];
        u64 t0 = svcGetSystemTick();
        svcFlushDataCacheRange((void *)(args->pa + args->kernPaToVa), args->size);
        u64 t1 = svcGetSystemTick();
        svcCustomBackdoor(DrawKernel, args);
        u64 t2 = svcGetSystemTick();
        if (!args->drew)
            continue;
        svcFlushDataCacheRange((void *)(args->pa + args->kernPaToVa), args->size);  // let the LCD see it
        u64 t3 = svcGetSystemTick();

        g_stats.fixes++;
        AddTiming(t0, t1, &g_stats.flushBeforeUs, &g_stats.flushBeforeMaxUs);
        AddTiming(t1, t2, &g_stats.drawUs, &g_stats.drawMaxUs);
        AddTiming(t2, t3, &g_stats.flushAfterUs, &g_stats.flushAfterMaxUs);
        svcCustomBackdoor(ShownKernel, args);
        if (args->pa != shown && args->shown == args->pa)
            g_stats.swappedDuringFix++;
        args->shown = shown;

        if (args->pa == shown)
            g_stats.fixedShown++;
        else if (args->pa == args->registers[0] || args->pa == args->registers[1])
            g_stats.fixedRegister++;
        else
            g_stats.fixedOther++;
    }
    RememberBuffers(args);      // a buffer new to the registers is checked next tick
}

void Overlay_Show(const char *title, const char *text, const char *rightText, u32 milliseconds)
{
    static char fittedTitle[64], fittedText[128];
    int rightX = SCREEN_WIDTH - 8 - Overlay_TextWidth(rightText);

    // Measured here, before the kernel-mode drawing.
    Overlay_Ellipsize(fittedTitle, sizeof(fittedTitle), title, OVERLAY_BANNER_W);
    Overlay_Ellipsize(fittedText, sizeof(fittedText), text, (rightText[0] != '\0' ? rightX - BANNER_GAP : SCREEN_WIDTH - 8) - 8);

    DrawArgs args = { .title = fittedTitle, .text = fittedText, .rightText = rightText, .rightX = rightX,
                      .kernPaToVa = KernPaToVa() };
    u32      lastShown = 0;
    u64      last = 0;
    s32      priority = 0;

    memset(&g_stats, 0, sizeof(g_stats));
    g_knownCount = 0;

    // At its own low priority the tracker was kept waiting up to 65 ms on
    // hardware, missing frames entirely. The loop mostly sleeps, so a higher
    // priority is cheap.
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    g_stats.priorityResult = svcSetThreadPriority(CUR_THREAD_HANDLE, BANNER_PRIORITY);

    for (u64 end = osGetTime() + milliseconds; osGetTime() < end; )
    {
        u64 start = svcGetSystemTick();
        if (last != 0 && (u32)((start - last) / CPU_TICKS_PER_USEC) > g_stats.maxGapUs)
            g_stats.maxGapUs = (u32)((start - last) / CPU_TICKS_PER_USEC);
        last = start;

        OverlayTick(&args);

        u32 tickUs = (u32)((svcGetSystemTick() - start) / CPU_TICKS_PER_USEC);
        if (tickUs > g_stats.maxTickUs)
            g_stats.maxTickUs = tickUs;
        if (lastShown != 0 && args.shown != lastShown)
            g_stats.swaps++;
        lastShown = args.shown;
        g_stats.fmt = args.fmt;
        g_stats.stride = args.stride;
        g_stats.ticks++;

        svcSleepThread(250 * 1000LL);
    }

    if (R_SUCCEEDED(g_stats.priorityResult))
        svcSetThreadPriority(CUR_THREAD_HANDLE, priority);
}

void Overlay_Stats(char *out, u32 size)
{
    snprintf(out, size, "ticks %lu, max gap %lu us, max tick %lu us, swaps %lu, fixed shown %lu, "
             "fixed other register %lu, fixed unlisted %lu, buffers %lu (%08lX %08lX %08lX %08lX), "
             "fmt %lu, stride %lu, priority %08lX, fixes %lu, us avg/max flush %lu/%lu draw %lu/%lu flush %lu/%lu, swapped during fix %lu",
             g_stats.ticks, g_stats.maxGapUs, g_stats.maxTickUs, g_stats.swaps, g_stats.fixedShown,
             g_stats.fixedRegister, g_stats.fixedOther, g_stats.seenCount, g_stats.seen[0], g_stats.seen[1],
             g_stats.seen[2], g_stats.seen[3], g_stats.fmt, g_stats.stride, (u32)g_stats.priorityResult,
             g_stats.fixes, g_stats.flushBeforeUs / (g_stats.fixes ? g_stats.fixes : 1), g_stats.flushBeforeMaxUs,
             g_stats.drawUs / (g_stats.fixes ? g_stats.fixes : 1), g_stats.drawMaxUs,
             g_stats.flushAfterUs / (g_stats.fixes ? g_stats.fixes : 1), g_stats.flushAfterMaxUs, g_stats.swappedDuringFix);
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

static u8  s_lengths[OVERLAY_MAX_ITEMS];    // text lengths, measured before entering the kernel
static u8  s_next[OVERLAY_MAX_ITEMS];       // per text: the first glyph that can still reach this column
static int s_pen64[OVERLAY_MAX_ITEMS];      // and where that glyph's pen position is, in 1/64 pixels

// An icon's pixel (x, y): 8x8 tiles, Z-order inside each (the SMDH layout).
static inline Rgb IconPixel(const u16 *icon, u32 x, u32 y)
{
    u32 tile = (y / 8) * 6 + x / 8;
    u32 inner = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    u16 v = icon[tile * 64 + inner];
    return (Rgb){ (u8)(((v >> 11) & 31) << 3), (u8)(((v >> 5) & 63) << 2), (u8)((v & 31) << 3) };
}

// Blends text item i's pixels in screen column x into column. Columns are drawn left
// to right, and s_next/s_pen64 move along the text with them.
static void TextColumn(const OverlayItem *it, u32 i, int x, Rgb *column)
{
    while (s_next[i] < s_lengths[i] && Pixels(s_pen64[i]) + FONT13_MAX_RIGHT <= x)
        s_pen64[i] += Glyph(it->text[s_next[i]++])->adv64;     // ends left of x: done

    int pen64 = s_pen64[i];
    for (u32 k = s_next[i]; k < s_lengths[i] && Pixels(pen64) + FONT13_MIN_X <= x; k++)
    {
        const FontGlyph *g = Glyph(it->text[k]);
        int col = x - (Pixels(pen64) + g->x);
        if (col >= 0 && col < g->w)
        {
            const u8 *alpha = font13_alpha + g->off + col;
            for (int row = 0; row < g->h; row++, alpha += g->w)
            {
                int y = it->y + g->y + row;
                if (*alpha != 0 && y >= 0 && y < SCREEN_HEIGHT)
                    column[y] = Mix(column[y], it->color, *alpha);
            }
        }
        pen64 += g->adv64;
    }
}

// Kernel mode: builds each screen column in colour (black, then the items in order)
// and writes it out, so every pixel gets its final colour once.
static void DrawScreenKernel(ScreenArgs *a)
{
    static Rgb  column[SCREEN_HEIGHT];
    u32         fmt, bpp, stride;

    a->pa = CurrentBuffer(&fmt, &bpp, &stride);
    a->size = SCREEN_WIDTH * stride;
    if (a->pa == 0)
        return;

    u8 *fb = (u8 *)(a->pa + a->kernPaToVa);
    for (u32 i = 0; i < a->count; i++)
    {
        s_next[i] = 0;
        s_pen64[i] = a->items[i].x << 6;
    }

    for (u32 x = 0; x < SCREEN_WIDTH; x++)
    {
        memset(column, 0, sizeof(column));

        for (u32 i = 0; i < a->count; i++)
        {
            const OverlayItem *it = &a->items[i];
            if (it->icon == NULL && it->text != NULL)
            {
                TextColumn(it, i, (int)x, column);      // a glyph can start left of it->x
                continue;
            }
            if (x < it->x)
                continue;
            u32 dx = x - it->x;

            if (it->icon != NULL)
            {
                if (dx < OVERLAY_ICON_SIZE)
                    for (u32 row = 0; row < OVERLAY_ICON_SIZE; row++)
                    {
                        int y = it->y + (int)row;
                        if (y >= 0 && y < SCREEN_HEIGHT)
                            column[y] = IconPixel(it->icon, dx, row);
                    }
            }
            else if (dx < it->width)
                for (int y = it->y < 0 ? 0 : it->y; y < it->y + (int)it->height && y < SCREEN_HEIGHT; y++)
                    column[y] = it->color;
        }

        u8 *p = Pixel(fb, stride, bpp, x, 0);
        if (fmt == GSP_BGR8_OES)
            for (u32 y = 0; y < SCREEN_HEIGHT; y++, p -= 3)
            {
                p[0] = column[y].b;
                p[1] = column[y].g;
                p[2] = column[y].r;
            }
        else
            for (u32 y = 0; y < SCREEN_HEIGHT; y++, p -= bpp)
            {
                u8 raw[4];
                Encode(fmt, column[y], raw);
                memcpy(p, raw, bpp);
            }
    }
}

void Overlay_DrawScreen(const OverlayItem *items, u32 count)
{
    ScreenArgs args = { items, count < OVERLAY_MAX_ITEMS ? count : OVERLAY_MAX_ITEMS, KernPaToVa(), 0, 0 };

    for (u32 i = 0; i < args.count; i++)
    {
        size_t length = items[i].text ? strlen(items[i].text) : 0;
        s_lengths[i] = (u8)(length < 255 ? length : 255);
    }

    svcCustomBackdoor(DrawScreenKernel, &args);
    if (args.pa != 0)
        svcFlushDataCacheRange((void *)(args.pa + args.kernPaToVa), args.size);     // let the LCD see it
}
