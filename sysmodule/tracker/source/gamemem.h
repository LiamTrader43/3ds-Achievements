#ifndef GAMEMEM_H
#define GAMEMEM_H

// Reading the running game's memory, one poll at a time.
//
// Pages of the game are mapped into this process with Luma's svcMapProcessMemoryEx
// and unmapped with svcUnmapProcessMemoryEx. Both flush the ENTIRE data cache and
// invalidate the instruction cache (even when a map fails), which made games
// stutter while every read mapped and unmapped. Learned on hardware (2026-11-10):
//
//   - Only ever ONE mapping at a time, unmapped before the next is made. Builds that
//     kept several pages mapped side by side could not unmap them, so every later map
//     failed, reads came back unreadable (= 0 to the conditions) and gave false
//     unlocks; mappings kept between polls also crashed the console when the game
//     closed. The original map-read-unmap per read never had either problem.
//   - So each poll copies what it needs: the pages the last poll read are mapped a
//     range at a time (nearby pages merged: one call and one flush per range), copied
//     into this process, and unmapped. Pages outside that plan are copied the same way
//     the first time they're read. Every value comes from a copy made in that poll.
//   - If memory that was readable can't be read (or an unmap failed), the poll is
//     skipped instead of checked with zeros.

#include <3ds/types.h>
#include <stdbool.h>

typedef struct
{
    u32 reads, failedReads;         // failed: unreadable memory (null pointers etc.)
    u32 maps, mapFailures, unmapFailures;
    u32 skippedPolls;
    u32 lastError, lastErrorAddress; // the last failed map/unmap: result and address
} GameMemStats;

// Starts a poll of the game process (handle open for the whole poll, pid to tell a
// new or restarted game apart) and copies the pages the last poll read. False: the
// game's memory can't be read right now; skip checking this poll (still call
// GameMem_EndPoll).
bool    GameMem_BeginPoll(Handle game, u32 pid);

// Reads size (1, 2 or 4) bytes, little-endian. False if the memory can't be read.
bool    GameMem_Read(u32 address, u8 size, u32 *value);

// Ends the poll (nothing is left mapped either way).
void    GameMem_EndPoll(void);

// Counts since the last call.
void    GameMem_TakeStats(GameMemStats *stats);

// The readable pages the last poll read (ascending, at most capacity). For the log.
int     GameMem_LastPages(u32 *pages, int capacity);

#endif
