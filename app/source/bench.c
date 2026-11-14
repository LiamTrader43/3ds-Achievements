#include "bench.h"

#ifdef BENCH

#include <stdio.h>

#include "draw.h"

#define TICKS_PER_MS    268111.856      // SYSCLOCK_ARM11 / 1000
#define LATE_MS         20.0            // a frame this long missed a screen refresh
#define END_FRAME       1020
#define LOG_PATH        "/achievements/bench.log"

// The script, in loop iterations (one a frame at 60 fps): the first game's list and
// background load during the first 1.5 s, then each phase is timed.
typedef struct
{
    const char *name;
    int         from, to;
    int         frames, drawn, late;
    u64         draw, drawMax, end, endMax, period, periodMax;
    u64         parts[BENCH_PARTS];
} Phase;

static Phase s_phases[] =
{
    { "2D, light only",          90, 210 },
    { "2D, changing game",      210, 450 },     // Right, Left, ... every 45 frames
    { "3D, light only",         480, 600 },     // slider all the way up from 450
    { "3D, changing game",      600, 870 },
    { "2D, scrolling the list", 900, END_FRAME },   // Down held from 880
};
#define PHASES  ((int)(sizeof(s_phases) / sizeof(s_phases[0])))

static int s_frame = -1;
static u64 s_lastFrame, s_drawStart, s_drawDone;

// The current iteration's drawing, for the list of late frames.
static u64  s_parts[BENCH_PARTS], s_draw, s_end;
static bool s_drawn;
#define LATE_LINES  120
static char s_late[LATE_LINES][224];
static int  s_lateCount;

static double Ms(u64 ticks, int count);

static Phase *PhaseOf(int frame)
{
    for (int i = 0; i < PHASES; i++)
        if (frame >= s_phases[i].from && frame < s_phases[i].to)
            return &s_phases[i];
    return NULL;
}

bool Bench_Input(u32 *down, u32 *held, u32 *up)
{
    u64 now = svcGetSystemTick();

    // How long the last iteration took, start to start.
    Phase *last = s_frame >= 0 ? PhaseOf(s_frame) : NULL;
    if (last != NULL)
    {
        u64 period = now - s_lastFrame;
        last->frames++;
        last->period += period;
        if (period > last->periodMax)
            last->periodMax = period;
        if (period > LATE_MS * TICKS_PER_MS)
        {
            last->late++;
            if (s_lateCount < LATE_LINES)
            {
                static const char *names[BENCH_PARTS] = { "hero", "shadow", "map", "tiles", "scale", "light",
                                                          "shape", "hints", "bottom", "load", "decode", "fade" };
                char *line = s_late[s_lateCount++];
                int   n = snprintf(line, 224, "%d %.1f ms: ", s_frame, Ms(period, 1));
                if (!s_drawn)
                    n += snprintf(line + n, 224 - n, "not drawn");
                else
                    n += snprintf(line + n, 224 - n, "draw %.1f end %.1f", Ms(s_draw, 1), Ms(s_end, 1));
                for (int i = 0; i < BENCH_PARTS && n < 210; i++)
                    if (s_parts[i] > TICKS_PER_MS / 10)
                        n += snprintf(line + n, 224 - n, ", %s %.1f", names[i], Ms(s_parts[i], 1));
            }
        }
    }
    s_lastFrame = now;
    s_frame++;
    for (int i = 0; i < BENCH_PARTS; i++)
        s_parts[i] = 0;
    s_drawn = false;

    int f = s_frame;
    u32 d = 0, h = 0;
    if ((f >= 210 && f < 450) || (f >= 600 && f < 870))
    {
        int step = f - (f < 450 ? 210 : 600);
        if (step % 45 == 0)
            d = (step / 45) % 2 == 0 ? KEY_RIGHT : KEY_LEFT;
    }
    if (f >= 880 && f < END_FRAME)
    {
        h = KEY_DOWN;
        if (f == 880)
            d = KEY_DOWN;
    }
    *down = d;
    *held = h | d;
    *up = 0;
    return f < END_FRAME;
}

float Bench_Slider(void)
{
    return s_frame >= 450 && s_frame < 870 ? 1.0f : 0.0f;
}

void Bench_DrawStart(void)
{
    s_drawStart = svcGetSystemTick();
}

void Bench_DrawDone(void)
{
    s_drawDone = svcGetSystemTick();
}

void Bench_EndDone(void)
{
    Phase *p = PhaseOf(s_frame);
    if (p == NULL)
        return;
    u64 draw = s_drawDone - s_drawStart, end = svcGetSystemTick() - s_drawDone;
    s_draw = draw, s_end = end, s_drawn = true;
    p->drawn++;
    p->draw += draw;
    p->end += end;
    if (draw > p->drawMax)
        p->drawMax = draw;
    if (end > p->endMax)
        p->endMax = end;
}

static double Ms(u64 ticks, int count)
{
    return count > 0 ? ticks / TICKS_PER_MS / count : 0.0;
}

void Bench_Add(int part, u64 ticks)
{
    Phase *p = PhaseOf(s_frame);
    if (p != NULL && part >= 0 && part < BENCH_PARTS)
        p->parts[part] += ticks;
    if (part >= 0 && part < BENCH_PARTS)
        s_parts[part] += ticks;
}

void Bench_Report(void)
{
    static const Color WHITE = { 255, 255, 255 }, GREY = { 160, 160, 170 }, GOLD = { 255, 209, 102 };
    char lines[PHASES * 4 + 2][96];
    int  count = 0;

    for (int i = 0; i < PHASES; i++)
    {
        Phase *p = &s_phases[i];
        snprintf(lines[count++], sizeof(lines[0]), "%s: %d/%d late, frame %.1f/%.1f ms",
                 p->name, p->late, p->frames, Ms(p->period, p->frames), Ms(p->periodMax, 1));
        snprintf(lines[count++], sizeof(lines[0]), "  draw %.2f/%.2f, end %.2f/%.2f ms (%d drawn)",
                 Ms(p->draw, p->drawn), Ms(p->drawMax, 1), Ms(p->end, p->drawn), Ms(p->endMax, 1), p->drawn);
        // Per drawn frame.
        const u64 *t = p->parts;
        snprintf(lines[count++], sizeof(lines[0]), "  hero %.2f, shadow %.2f (map %.2f), tiles %.2f (scale %.2f)",
                 Ms(t[BENCH_HERO], p->drawn), Ms(t[BENCH_SHADOW], p->drawn), Ms(t[BENCH_SHADOW_MAP], p->drawn),
                 Ms(t[BENCH_TILES], p->drawn), Ms(t[BENCH_TILE_SCALE], p->drawn));
        snprintf(lines[count++], sizeof(lines[0]), "  light %.2f (shape %.2f), hints %.2f, bottom %.2f",
                 Ms(t[BENCH_LIGHT], p->drawn), Ms(t[BENCH_LIGHT_SHAPE], p->drawn), Ms(t[BENCH_HINTS], p->drawn),
                 Ms(t[BENCH_BOTTOM], p->drawn));
    }

    FILE *f = fopen(LOG_PATH, "w");
    if (f != NULL)
    {
        for (int i = 0; i < count; i++)
            fprintf(f, "%s\n", lines[i]);
        fprintf(f, "Late frames (iteration, length, what took the time):\n");
        for (int i = 0; i < s_lateCount; i++)
            fprintf(f, "  %s\n", s_late[i]);
        fclose(f);
    }

    Draw_Stereo(0.0f);
    for (int frame = 0; aptMainLoop(); frame++)
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        if (frame < 2)
        {
            Draw_Begin(true, true);
            Draw_Text(GFX_TOP, 8, 6, 1, GOLD, "Drawing benchmark (saved to " LOG_PATH ")", 0);
            for (int i = 0, row = 0; i < count; i++)
                if (i % 4 < 2)      // the parts are only in the log
                    Draw_Text(GFX_TOP, 8, 26 + row++ * 16, 1, i % 4 == 0 ? WHITE : GREY, lines[i], 0);
            Draw_Text(GFX_BOTTOM, 8, 6, 1, GOLD, "START: Exit", 0);
            Draw_Text(GFX_BOTTOM, 8, 26, 1, GREY, "Times per part are in the log.", 0);
            Draw_End();
        }
        else
            gspWaitForVBlank();
    }
}

#endif
