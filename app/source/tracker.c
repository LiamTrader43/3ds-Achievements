#include "tracker.h"

#include <stdio.h>

#define TRACKER_TITLE_ID    0x0004013000AC4102ULL
#define PROCESS_TITLE_ID    0x10001     // Luma svcGetProcessInfo extension
#define OFF_PATH            "sdmc:/achievements/tracker_off"    // exists while the user has it turned off
#define ALLOW_PATCHED_PATH  "sdmc:/achievements/allow_patched"  // exists while patched games can earn achievements
#define STOP_TIMEOUT_NS     2000000000LL

bool Tracker_Running(void)
{
    u32 pids[64];
    s32 count;

    if (R_FAILED(svcGetProcessList(&count, pids, 64)))
        return false;

    for (s32 i = 0; i < count; i++)
    {
        Handle  process;
        s64     tid;

        if (R_FAILED(svcOpenProcess(&process, pids[i])))
            continue;
        Result res = svcGetProcessInfo(&tid, process, PROCESS_TITLE_ID);
        svcCloseHandle(process);
        if (R_SUCCEEDED(res) && (u64)tid == TRACKER_TITLE_ID)
            return true;
    }
    return false;
}

Result Tracker_Start(void)
{
    if (Tracker_Running())
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

static bool FlagSet(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    fclose(file);
    return true;
}

bool Tracker_TurnedOff(void)
{
    return FlagSet(OFF_PATH);
}

bool Tracker_PatchedAllowed(void)
{
    return FlagSet(ALLOW_PATCHED_PATH);
}

void Tracker_SetPatchedAllowed(bool allowed)
{
    if (!allowed)
    {
        remove(ALLOW_PATCHED_PATH);
        return;
    }

    FILE *file = fopen(ALLOW_PATCHED_PATH, "wb");
    if (file != NULL)
        fclose(file);
}

// Asks the tracker to exit, then waits until it's gone.
static Result Stop(void)
{
    Result res = pmAppInit();
    if (R_SUCCEEDED(res))
    {
        res = PMAPP_TerminateTitle(TRACKER_TITLE_ID, STOP_TIMEOUT_NS);
        pmAppExit();
    }
    for (int i = 0; i < 30 && Tracker_Running(); i++)
        svcSleepThread(100 * 1000 * 1000LL);
    return res;
}

Result Tracker_SetEnabled(bool enabled)
{
    if (enabled)
    {
        remove(OFF_PATH);
        return Tracker_Start();
    }

    FILE *file = fopen(OFF_PATH, "wb");
    if (file != NULL)
        fclose(file);
    return Stop();
}
