// Starts the background achievement tracker (title 0004013000AC4102), which
// Luma3DS loads from /luma/sysmodules/0004013000AC4102.cxi. Run once per boot
// from the Homebrew Launcher. If the tracker is already running, it shows the
// tracker's status (read from its memory) and the end of its log instead.

#include <3ds.h>

#include <stdio.h>
#include <string.h>

#define TRACKER_TITLE_ID    0x0004013000AC4102ULL
#define PROCESS_TITLE_ID    0x10001     // Luma svcGetProcessInfo extension
#define MAP_ADDRESS         0x0E000000  // a tracker page is mapped here briefly

Result svcMapProcessMemoryEx(Handle dstProcess, u32 dstAddress, Handle srcProcess, u32 srcAddress, u32 size, u32 flags);
Result svcUnmapProcessMemoryEx(Handle process, u32 address, u32 size);

// Must match TrackerStatus in tracker/source/main.c.
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

static const char *STAGES[] = { "not started", "heap setup entered", "heap set up", "srv started", "fs started",
                                "main entered", "first log written", "polling", "game found" };

// The tracker's process ID, or 0 if it isn't running.
static u32 TrackerPid(void)
{
    u32 pids[64];
    s32 count;

    if (R_FAILED(svcGetProcessList(&count, pids, 64)))
        return 0;

    for (s32 i = 0; i < count; i++)
    {
        Handle  process;
        s64     tid;

        if (R_FAILED(svcOpenProcess(&process, pids[i])))
            continue;
        Result res = svcGetProcessInfo(&tid, process, PROCESS_TITLE_ID);
        svcCloseHandle(process);
        if (R_SUCCEEDED(res) && (u64)tid == TRACKER_TITLE_ID)
            return pids[i];
    }
    return 0;
}

// Finds the tracker's status block by its magic and prints it.
static void ShowStatus(u32 pid)
{
    Handle process;
    if (R_FAILED(svcOpenProcess(&process, pid)))
    {
        printf("Can't open the tracker process.\n");
        return;
    }

    bool found = false;
    for (u32 page = 0x00100000; page < 0x00180000 && !found; page += 0x1000)
    {
        if (R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, process, page, 0x1000, 0)))
            continue;
        const u32 *words = (const u32 *)MAP_ADDRESS;
        for (u32 i = 0; i + sizeof(TrackerStatus) / 4 <= 0x1000 / 4; i++)
        {
            if (words[i] != 0x54484341 || words[i + 1] != 0x54415453)
                continue;
            TrackerStatus s;
            memcpy(&s, &words[i], sizeof(s));
            printf("Tracker status (from its memory):\n");
            printf("  stage: %lu (%s)\n", s.stage, s.stage < 9 ? STAGES[s.stage] : "?");
            printf("  free mem: %lu KB  heap: %lu KB (%08lX)\n", s.freeMemory / 1024, s.heapSize / 1024, s.heapResult);
            printf("  srv: %08lX  fs: %08lX\n", s.srvResult, s.fsResult);
            printf("  last SD open: %08lX\n", s.lastFileResult);
            printf("  polls: %lu  game: %08lX\n\n", s.polls, s.gameTitleLow);
            found = true;
            break;
        }
        svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, 0x1000);
    }
    svcCloseHandle(process);

    if (!found)
        printf("Tracker status block not found.\n\n");
}

// Lists the tracker's threads with their priority and ideal core.
static void ShowThreads(u32 pid)
{
    Handle  process;
    u32     tids[16];
    s32     count = 0;

    if (R_FAILED(svcOpenProcess(&process, pid)))
        return;
    Result res = svcGetThreadList(&count, tids, 16, process);
    printf("Tracker threads: %ld (%08lX)\n", count, res);
    for (s32 i = 0; i < count; i++)
    {
        Handle  thread;
        s32     priority = -1, core = -1;
        if (R_SUCCEEDED(svcOpenThread(&thread, process, tids[i])))
        {
            svcGetThreadPriority(&priority, thread);
            svcGetThreadIdealProcessor(&core, thread);
            svcCloseHandle(thread);
        }
        printf("  thread %lu: priority %ld, core %ld\n", tids[i], priority, core);
    }
    printf("\n");
    svcCloseHandle(process);
}

// Saves the tracker's code/data pages and the top of its stack to
// /achievements/tracker_dump.bin, as records of {u32 address, u32 size, data}.
// The stack shows where its main thread is (all zero = it never ran).
static void DumpTracker(u32 pid)
{
    static const u32 ranges[][2] = { { 0x00100000, 0x00130000 }, { 0x0FFF0000, 0x10000000 } };
    Handle process;
    u32    pages = 0;

    if (R_FAILED(svcOpenProcess(&process, pid)))
        return;
    FILE *file = fopen("sdmc:/achievements/tracker_dump.bin", "wb");
    if (file == NULL)
    {
        svcCloseHandle(process);
        printf("Couldn't write tracker_dump.bin.\n");
        return;
    }

    for (u32 r = 0; r < 2; r++)
        for (u32 page = ranges[r][0]; page < ranges[r][1]; page += 0x1000)
        {
            if (R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, process, page, 0x1000, 0)))
                continue;
            u32 header[2] = { page, 0x1000 };
            fwrite(header, sizeof(header), 1, file);
            fwrite((const void *)MAP_ADDRESS, 0x1000, 1, file);
            svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, 0x1000);
            pages++;
        }

    fclose(file);
    svcCloseHandle(process);
    printf("Saved %lu tracker pages to\n/achievements/tracker_dump.bin\n\n", pages);
}

// Prints the end of the tracker's log.
static void ShowLog(void)
{
    static char text[1024];
    FILE *file = fopen("sdmc:/achievements/tracker.log", "rb");
    if (file == NULL)
    {
        printf("No /achievements/tracker.log yet.\n");
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    long start = size > (long)sizeof(text) - 1 ? size - (long)(sizeof(text) - 1) : 0;
    fseek(file, start, SEEK_SET);
    size_t read = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[read] = '\0';

    // Show only the last 8 lines.
    char *p = text + read;
    for (int lines = 0; p > text && lines <= 8; p--)
        if (p[-1] == '\n')
            lines++;
    printf("tracker.log (end):\n%s\n", p);
}

// Returns true for A, false for START.
static bool WaitForA(void)
{
    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_A)
            return true;
        if (down & KEY_START)
            return false;
        gspWaitForVBlank();
    }
    return false;
}

static int Finish(void)
{
    printf("Press START to exit.\n");
    WaitForA();
    gfxExit();
    return 0;
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("3DS Achievements\n\n");

    u32 pid = TrackerPid();
    if (pid != 0)
    {
        printf("The tracker is already running.\n\n");
        ShowStatus(pid);
        ShowThreads(pid);
        DumpTracker(pid);
        ShowLog();
        return Finish();
    }

    // Pause here, so a crash after this point is clearly the tracker's.
    printf("Not running.\n\nPress A to start the tracker,\nSTART to exit.\n");
    if (!WaitForA())
    {
        gfxExit();
        return 0;
    }

    FS_ProgramInfo program = { TRACKER_TITLE_ID, MEDIATYPE_NAND };
    printf("\nLaunching...\n");
    Result res = pmAppInit();
    if (R_SUCCEEDED(res))
    {
        res = PMAPP_LaunchTitle(&program, 0);
        pmAppExit();
    }
    printf("Launch result: 0x%08lX\n", res);

    pid = TrackerPid();
    if (R_FAILED(res) || pid == 0)
    {
        printf("Could not start the tracker.\n\nCheck that:\n");
        printf("- /luma/sysmodules/0004013000AC4102.cxi\n  is on the SD card\n");
        printf("- Luma config has \"Enable loading\n  external FIRMs and modules\" on\n\n");
        return Finish();
    }

    // Give it a moment, then show how far it got and what it logged.
    svcSleepThread(2000 * 1000 * 1000LL);
    printf("Still running after 2 s: %s\n\n", TrackerPid() ? "yes" : "NO");
    ShowStatus(pid);
    ShowThreads(pid);
    DumpTracker(pid);
    ShowLog();
    return Finish();
}
