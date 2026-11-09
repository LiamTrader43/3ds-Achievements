#ifndef TRACKER_H
#define TRACKER_H

// The background tracker sysmodule (title 0004013000AC4102), which Luma3DS loads
// from /luma/sysmodules/0004013000AC4102.cxi: starting it and reading its state.

#include <3ds.h>

#define TRACKER_MAX_THREADS 8
#define TRACKER_LOG_SIZE    2048

// Must match TrackerStatus in sysmodule/tracker/source/main.c.
typedef struct
{
    u32 magic[2];
    u32 stage;
    s32 srvResult;
    s32 fsResult;
    s32 lastFileResult;
    u32 polls;
    u32 gameTitleLow;
    s32 heapResult;
    u32 heapSize;
    u32 freeMemory;
} TrackerStatus;

typedef struct
{
    u32 id;
    s32 priority, core;
} TrackerThread;

typedef struct
{
    u32             pid;            // 0 = not running
    bool            hasStatus;      // status was found in its memory
    TrackerStatus   status;
    int             threadCount;
    TrackerThread   threads[TRACKER_MAX_THREADS];
    char            log[TRACKER_LOG_SIZE];  // the end of /achievements/tracker.log
} TrackerInfo;

// The tracker's process ID, or 0 if it isn't running.
u32         Tracker_Pid(void);

// Starts the tracker unless it's already running.
Result      Tracker_Start(void);

void        Tracker_Read(TrackerInfo *info);

// Saves the tracker's code/data pages and the top of its stack to
// /achievements/tracker_dump.bin, as records of {u32 address, u32 size, data}.
// Returns the number of pages saved.
u32         Tracker_Dump(u32 pid);

const char *Tracker_StageName(u32 stage);

#endif
