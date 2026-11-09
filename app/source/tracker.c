#include "tracker.h"

#include <stdio.h>
#include <string.h>

#define TRACKER_TITLE_ID    0x0004013000AC4102ULL
#define PROCESS_TITLE_ID    0x10001     // Luma svcGetProcessInfo extension
#define MAP_ADDRESS         0x0E000000  // a tracker page is mapped here briefly
#define PAGE_SIZE           0x1000
#define LOG_PATH            "sdmc:/achievements/tracker.log"
#define DUMP_PATH           "sdmc:/achievements/tracker_dump.bin"

Result svcMapProcessMemoryEx(Handle dstProcess, u32 dstAddress, Handle srcProcess, u32 srcAddress, u32 size, u32 flags);
Result svcUnmapProcessMemoryEx(Handle process, u32 address, u32 size);

u32 Tracker_Pid(void)
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

Result Tracker_Start(void)
{
    if (Tracker_Pid() != 0)
        return 0;

    FS_ProgramInfo program = { TRACKER_TITLE_ID, MEDIATYPE_NAND };
    Result res = pmAppInit();
    if (R_SUCCEEDED(res))
    {
        res = PMAPP_LaunchTitle(&program, 0);
        pmAppExit();
    }
    return res;
}

// Finds the tracker's status block by its magic.
static void ReadStatus(Handle process, TrackerInfo *info)
{
    for (u32 page = 0x00100000; page < 0x00180000 && !info->hasStatus; page += PAGE_SIZE)
    {
        if (R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, process, page, PAGE_SIZE, 0)))
            continue;
        const u32 *words = (const u32 *)MAP_ADDRESS;
        for (u32 i = 0; i + sizeof(TrackerStatus) / 4 <= PAGE_SIZE / 4; i++)
        {
            if (words[i] != 0x54484341 || words[i + 1] != 0x54415453)
                continue;
            memcpy(&info->status, &words[i], sizeof(info->status));
            info->hasStatus = true;
            break;
        }
        svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, PAGE_SIZE);
    }
}

static void ReadThreads(Handle process, TrackerInfo *info)
{
    u32 ids[TRACKER_MAX_THREADS];
    s32 count = 0;

    if (R_FAILED(svcGetThreadList(&count, ids, TRACKER_MAX_THREADS, process)))
        return;
    for (s32 i = 0; i < count && i < TRACKER_MAX_THREADS; i++)
    {
        Handle thread;
        s32    priority = -1, core = -1;
        if (R_SUCCEEDED(svcOpenThread(&thread, process, ids[i])))
        {
            svcGetThreadPriority(&priority, thread);
            svcGetThreadIdealProcessor(&core, thread);
            svcCloseHandle(thread);
        }
        info->threads[i].id = ids[i];
        info->threads[i].priority = priority;
        info->threads[i].core = core;
        info->threadCount = i + 1;
    }
}

static void ReadLog(TrackerInfo *info)
{
    FILE *file = fopen(LOG_PATH, "rb");
    if (file == NULL)
        return;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    long start = size > TRACKER_LOG_SIZE - 1 ? size - (TRACKER_LOG_SIZE - 1) : 0;
    fseek(file, start, SEEK_SET);
    size_t read = fread(info->log, 1, TRACKER_LOG_SIZE - 1, file);
    fclose(file);
    info->log[read] = '\0';
}

void Tracker_Read(TrackerInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->pid = Tracker_Pid();
    ReadLog(info);
    if (info->pid == 0)
        return;

    Handle process;
    if (R_FAILED(svcOpenProcess(&process, info->pid)))
        return;
    ReadStatus(process, info);
    ReadThreads(process, info);
    svcCloseHandle(process);
}

u32 Tracker_Dump(u32 pid)
{
    static const u32 ranges[][2] = { { 0x00100000, 0x00130000 }, { 0x0FFF0000, 0x10000000 } };
    Handle process;
    u32    pages = 0;

    if (R_FAILED(svcOpenProcess(&process, pid)))
        return 0;
    FILE *file = fopen(DUMP_PATH, "wb");
    if (file == NULL)
    {
        svcCloseHandle(process);
        return 0;
    }

    for (u32 r = 0; r < 2; r++)
        for (u32 page = ranges[r][0]; page < ranges[r][1]; page += PAGE_SIZE)
        {
            if (R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, process, page, PAGE_SIZE, 0)))
                continue;
            u32 header[2] = { page, PAGE_SIZE };
            fwrite(header, sizeof(header), 1, file);
            fwrite((const void *)MAP_ADDRESS, PAGE_SIZE, 1, file);
            svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, PAGE_SIZE);
            pages++;
        }

    fclose(file);
    svcCloseHandle(process);
    return pages;
}

const char *Tracker_StageName(u32 stage)
{
    static const char *names[] = { "not started", "heap setup entered", "heap set up", "srv started", "fs started",
                                   "main entered", "first log written", "polling", "game found" };
    return stage < sizeof(names) / sizeof(names[0]) ? names[stage] : "?";
}
