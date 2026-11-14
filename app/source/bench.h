#ifndef BENCH_H
#define BENCH_H

// A drawing benchmark, built with `make BENCH=1` (achievements-bench.3dsx): the app runs a
// fixed script of key presses and 3D slider positions instead of reading them, times every
// frame, then shows the results and writes /achievements/bench.log. START exits.

#include <3ds.h>

// Replaces this frame's keys with the script's. False once the script is over.
bool    Bench_Input(u32 *down, u32 *held, u32 *up);

// The script's 3D slider position.
float   Bench_Slider(void);

// Around the drawing of a frame: before Draw_Begin, before Draw_End, after Draw_End.
void    Bench_DrawStart(void);
void    Bench_DrawDone(void);
void    Bench_EndDone(void);

// Shows the results and writes the log; returns when START is pressed.
void    Bench_Report(void);

// Time spent in parts of the drawing, added up per phase. Some parts are inside others
// (marked "in ..." in the report).
enum
{
    BENCH_HERO, BENCH_SHADOW, BENCH_SHADOW_MAP, BENCH_TILES, BENCH_TILE_SCALE, BENCH_LIGHT,
    BENCH_LIGHT_SHAPE, BENCH_HINTS, BENCH_BOTTOM, BENCH_LOAD, BENCH_DECODE, BENCH_FADE, BENCH_PARTS
};
void    Bench_Add(int part, u64 ticks);

#ifdef BENCH
#define BENCH_TIME(part, ...)   do { u64 bench_ = svcGetSystemTick(); __VA_ARGS__; Bench_Add(part, svcGetSystemTick() - bench_); } while (0)
#else
#define BENCH_TIME(part, ...)   do { __VA_ARGS__; } while (0)
#endif

#endif
