#include "gamemem.h"

#include <3ds.h>
#include <stdint.h>
#include <string.h>

// Luma kernel extensions (csvc.s).
Result svcMapProcessMemoryEx(Handle dstProcess, u32 dstAddress, Handle srcProcess, u32 srcAddress, u32 size, u32 flags);
Result svcUnmapProcessMemoryEx(Handle process, u32 address, u32 size);

#define PAGE            0x1000u
#define NEVER_MAPPED    0x00100000u     // below: unmapped in every 3DS process (null pointer + offset)
#define PROBE_ADDRESS   0x00100000u     // the game's code: always there (the check when there's no plan)

#define MAP_ADDRESS     0x0E000000u     // where the one mapping goes (free in this process)
#define MAX_RUN         (128 * PAGE)    // largest range mapped in one call
#define MERGE_GAP       (64 * PAGE)     // planned pages this close share a range
#define MAX_FAILURES    8               // ranges that wouldn't map: then this game copies page by page

#define COPY_PAGES      16              // pages copied per poll (OoT needs ~9)
#define PLAN_PAGES      32
#define UNREADABLE      8               // pages found unreadable this poll (not tried again)

static Handle       s_game;
static u32          s_pid;
static bool         s_polling;          // between a successful BeginPoll and EndPoll
static bool         s_stuck;            // an unmap failed: retried before anything else
static u32          s_stuckSize;
static int          s_failures;         // ranges of this game that wouldn't map

static u8           s_copy[COPY_PAGES][PAGE] __attribute__((aligned(8)));
static u8           s_spare[PAGE] __attribute__((aligned(8)));  // a page read when the copies are full
static u32          s_copyPage[COPY_PAGES];
static int          s_copyCount;

static u32          s_plan[PLAN_PAGES]; // pages read last poll, ascending
static int          s_planCount;
static u32          s_seen[PLAN_PAGES]; // pages read this poll, ascending
static int          s_seenCount;

static u32          s_unreadable[UNREADABLE];
static int          s_unreadableCount;

static u32          s_lastPage = 1;     // the page of the last read (1: none) and its copy
static const u8    *s_lastCopy;
static GameMemStats s_stats;

static bool Map(u32 gameAddress, u32 size)
{
    s_stats.maps++;
    Result res = svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, s_game, gameAddress, size, 0);
    if (R_FAILED(res))
    {
        s_stats.mapFailures++;
        s_stats.lastError = (u32)res;
        s_stats.lastErrorAddress = gameAddress;
        return false;
    }
    return true;
}

static bool Unmap(u32 size)
{
    Result res = svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, size);
    if (R_FAILED(res))
    {
        s_stuck = true;                 // nothing else is mapped until this one goes
        s_stuckSize = size;
        s_stats.unmapFailures++;
        s_stats.lastError = (u32)res;
        s_stats.lastErrorAddress = MAP_ADDRESS;
        return false;
    }
    return true;
}

// Adds a page read this poll to the next poll's plan.
static void Remember(u32 page)
{
    int i = 0;
    while (i < s_seenCount && s_seen[i] < page)
        i++;
    if ((i < s_seenCount && s_seen[i] == page) || s_seenCount == PLAN_PAGES)
        return;
    memmove(&s_seen[i + 1], &s_seen[i], sizeof(u32) * (size_t)(s_seenCount - i));
    s_seen[i] = page;
    s_seenCount++;
}

static const u8 *FindCopy(u32 page)
{
    for (int i = 0; i < s_copyCount; i++)
        if (s_copyPage[i] == page)
            return s_copy[i];
    return NULL;
}

// Copies the game's page from the mapping (mapped at MAP_ADDRESS + offset) into a
// free copy, or the spare page when they're all used.
static const u8 *Keep(u32 page, u32 offset)
{
    u8 *to = s_spare;
    if (s_copyCount < COPY_PAGES)
    {
        to = s_copy[s_copyCount];
        s_copyPage[s_copyCount++] = page;
    }
    memcpy(to, (const u8 *)(uintptr_t)(MAP_ADDRESS + offset), PAGE);
    return to;
}

// Maps one page, copies it, unmaps it. NULL if it can't be read.
static const u8 *CopyPage(u32 page)
{
    if (s_stuck || !Map(page, PAGE))
        return NULL;
    const u8 *copy = Keep(page, 0);
    if (!Unmap(PAGE))
        return NULL;                    // don't trust anything more this poll
    return copy;
}

// Copies the pages the last poll read, nearby ones a range at a time. False if a
// page that was readable then can't be read now.
static bool CopyPlan(void)
{
    for (int i = 0; i < s_planCount; )
    {
        u32 first = s_plan[i], last = first;
        int j = i + 1;
        while (j < s_planCount && s_plan[j] - last <= MERGE_GAP && s_plan[j] + PAGE - first <= MAX_RUN)
            last = s_plan[j++];

        u32 size = last + PAGE - first;
        if (j - i > 1 && s_failures < MAX_FAILURES)
        {
            if (Map(first, size))
            {
                for (int k = i; k < j; k++)
                    if (s_copyCount < COPY_PAGES)
                        Keep(s_plan[k], s_plan[k] - first);
                if (!Unmap(size))
                    return false;
                i = j;
                continue;
            }
            s_failures++;               // e.g. a hole between the pages: one at a time
        }
        for (int k = i; k < j; k++)
            if (FindCopy(s_plan[k]) == NULL && s_copyCount < COPY_PAGES && CopyPage(s_plan[k]) == NULL)
                return false;
        i = j;
    }
    return true;
}

bool GameMem_BeginPoll(Handle game, u32 pid)
{
    if (pid != s_pid)                   // another game, or the same one restarted
    {
        s_pid = pid;
        s_planCount = 0;
        s_failures = 0;
    }
    s_game = game;
    s_copyCount = 0;
    s_seenCount = 0;
    s_unreadableCount = 0;
    s_lastPage = 1;
    s_polling = false;

    if (s_stuck)
    {
        if (R_FAILED(svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, s_stuckSize)))
        {
            s_stats.skippedPolls++;
            return false;
        }
        s_stuck = false;
    }

    bool ok;
    if (s_planCount > 0)
        ok = CopyPlan();
    else                                // nothing planned: check that mapping works at all
        ok = Map(PROBE_ADDRESS, PAGE) && Unmap(PAGE);
    if (!ok)
    {
        s_stats.skippedPolls++;
        return false;
    }
    s_polling = true;
    return true;
}

void GameMem_EndPoll(void)
{
    s_polling = false;
    s_lastPage = 1;
    memcpy(s_plan, s_seen, sizeof(u32) * (size_t)s_seenCount);
    s_planCount = s_seenCount;
}

// The game's page as copied this poll (copying it now if it's new), or NULL.
static const u8 *GamePage(u32 page)
{
    if (page == s_lastPage)
        return s_lastCopy;
    if (page < NEVER_MAPPED)
        return NULL;                    // nothing to map (and a failed map flushes too)

    for (int i = 0; i < s_unreadableCount; i++)
        if (s_unreadable[i] == page)
            return NULL;                // already failed this poll (every try flushes)

    const u8 *copy = FindCopy(page);
    if (copy == NULL)
        copy = CopyPage(page);
    if (copy == NULL)
    {
        if (s_unreadableCount < UNREADABLE)
            s_unreadable[s_unreadableCount++] = page;
        return NULL;
    }
    Remember(page);
    s_lastPage = page;
    s_lastCopy = copy;
    return copy;
}

bool GameMem_Read(u32 address, u8 size, u32 *value)
{
    u32       v = 0;
    u32       page = 1;
    const u8 *copy = NULL;

    s_stats.reads++;
    for (u8 i = 0; i < size; i++)
    {
        u32 a = address + i;            // a value can straddle two pages
        if ((a & ~(PAGE - 1)) != page)
        {
            page = a & ~(PAGE - 1);
            copy = s_polling ? GamePage(page) : NULL;
            if (copy == NULL)
            {
                s_stats.failedReads++;
                return false;
            }
        }
        v |= (u32)copy[a & (PAGE - 1)] << (8 * i);
    }
    *value = v;
    return true;
}

void GameMem_TakeStats(GameMemStats *stats)
{
    *stats = s_stats;
    memset(&s_stats, 0, sizeof(s_stats));
}

int GameMem_LastPages(u32 *pages, int capacity)
{
    int n = s_planCount < capacity ? s_planCount : capacity;
    memcpy(pages, s_plan, sizeof(u32) * (size_t)n);
    return n;
}
