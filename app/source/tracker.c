#include "tracker.h"

#include <stdio.h>
#include <string.h>

#include "hotkey.h"

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

u32 Tracker_Hotkey(void)
{
    char  text[64];
    u32   keys = 0;
    FILE *file = fopen("sdmc:" HOTKEY_PATH, "rb");
    if (file != NULL)
    {
        size_t n = fread(text, 1, sizeof(text) - 1, file);
        fclose(file);
        text[n] = '\0';
        keys = Hotkey_Parse(text);
    }
    return keys != 0 ? keys : HOTKEY_DEFAULT;
}

bool Tracker_SetHotkey(u32 keys)
{
    if (!Hotkey_Valid(keys))
        return false;
    if (keys == HOTKEY_DEFAULT)
    {
        remove("sdmc:" HOTKEY_PATH);
        return Tracker_Hotkey() == HOTKEY_DEFAULT;
    }
    char  text[64];
    Hotkey_FileText(keys, text, sizeof(text));
    FILE *file = fopen("sdmc:" HOTKEY_PATH, "wb");
    if (file == NULL)
        return false;
    bool ok = fwrite(text, 1, strlen(text), file) == strlen(text);
    ok = fclose(file) == 0 && ok;
    return ok && Tracker_Hotkey() == keys;      // read back: what the tracker will see
}

int Tracker_RosalinaCombos(u32 combos[2])
{
    s64 out = 0;
    int count = 0;
    if (R_FAILED(svcGetSystemInfo(&out, 0x10000, 0x101)))
        return 0;                               // not Luma: no Rosalina
    combos[count++] = out == 0 ? HOTKEY_ROSALINA_DEFAULT : (u32)out;   // as Rosalina reads it

    static char text[16 * 1024];
    FILE *file = fopen("sdmc:" HOTKEY_LUMA_CONFIG, "rb");
    if (file != NULL)
    {
        size_t n = fread(text, 1, sizeof(text) - 1, file);
        fclose(file);
        text[n] = '\0';
        u32 saved = Hotkey_LumaConfigCombo(text);
        if (saved != 0 && saved != combos[0])
            combos[count++] = saved;
    }
    return count;
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

Result Tracker_Restart(void)
{
    if (!Tracker_Running())
        return 0;
    Result res = Stop();
    if (Tracker_Running())
        return R_FAILED(res) ? res : MAKERESULT(RL_TEMPORARY, RS_INVALIDSTATE, RM_APPLICATION, RD_TIMEOUT);
    return Tracker_Start();
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
