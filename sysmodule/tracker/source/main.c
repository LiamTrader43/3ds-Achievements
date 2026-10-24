// Background achievement tracker: a sysmodule loaded by Luma3DS from
// /luma/sysmodules/0004013000AC4102.cxi and started by the starter app.
//
// Every 100 ms it finds the running game, loads that game's definitions when
// the game changes, and checks them by mapping the game's memory with Luma's
// svcMapProcessMemoryEx. Unlocks are saved to the progress file, with a
// banner on the bottom screen (overlay.c). A few seconds after a game starts,
// a banner shows its progress (or that it has no achievements).
// R + Down + Select pauses the game and shows the achievement list (menu.c).

#include <3ds.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "achievements.h"
#include "menu.h"
#include "overlay.h"

#define POLL_NS             (100 * 1000 * 1000LL)
#define MAP_ADDRESS         0x0E000000      // free in this process; game pages are mapped here briefly
#define LOG_PATH            "/achievements/tracker.log"
#define NOTIF_TERMINATE     0x100           // PM asks sysmodules to exit (shutdown)
#define PROCESS_NAME        0x10000         // Luma svcGetProcessInfo extensions
#define PROCESS_TITLE_ID    0x10001
#define BANNER_MS           6000
#define START_DELAY_MS      5000            // after a game launches, before its start banner
#define START_BANNER_MS     5000
#define BANNER_CHARS        49              // a banner text line with nothing right-aligned
#define SMDH_SIZE           0x36C0

Result svcMapProcessMemoryEx(Handle dstProcess, u32 dstAddress, Handle srcProcess, u32 srcAddress, u32 size, u32 flags);
Result svcUnmapProcessMemoryEx(Handle process, u32 address, u32 size);

#define HEAP_SIZE           0x10000

extern char *fake_heap_start, *fake_heap_end;
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;

static Handle   g_game;
static char     g_gameTitle[65];

// Diagnostics the starter reads straight from this process's memory, so they
// work even when nothing can be written to the SD card. Find it by the magic.
typedef struct
{
    u32 magic[2];           // "ACHTSTAT"
    u32 stage;              // last start-up step reached (see STAGE_*)
    s32 srvResult;
    s32 fsResult;
    s32 lastFileResult;     // last FSUSER_OpenFileDirectly result
    u32 polls;
    u32 gameTitleLow;       // low half of the last running game's title ID
    s32 heapResult;         // svcControlMemory result for the heap
    u32 heapSize;           // heap actually allocated (0 = none)
    u32 freeMemory;         // memory left in this process's resource limit before the heap
} TrackerStatus;

enum { STAGE_HEAPS_ENTERED = 1, STAGE_HEAPS, STAGE_SRV, STAGE_FS, STAGE_MAIN, STAGE_LOGGED, STAGE_POLLING, STAGE_GAME };

volatile TrackerStatus g_status = { { 0x54484341, 0x54415453 }, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Memory still available under this process's resource limit.
static u32 FreeMemory(void)
{
    Handle              limit;
    ResourceLimitType   type = RESLIMIT_COMMIT;
    s64                 max = 0, used = 0;

    if (R_FAILED(svcGetResourceLimit(&limit, CUR_PROCESS_HANDLE)))
        return 0;
    svcGetResourceLimitLimitValues(&max, limit, &type, 1);
    svcGetResourceLimitCurrentValues(&used, limit, &type, 1);
    svcCloseHandle(limit);
    return (u32)(max - used);
}

// Replaces libctru's default, which gives an app all the free memory in its
// region (a linear heap size of 0 means "everything left"). This module shares
// the Base region with the system, so taking it all crashed the console.
// Best effort: a small normal heap if there's room, otherwise none (the
// tracker never calls malloc itself). Never a linear heap.
void __system_allocateHeaps(void)
{
    u32 address = 0x08000000, size = 0;

    g_status.stage = STAGE_HEAPS_ENTERED;
    g_status.freeMemory = FreeMemory();

    for (u32 want = HEAP_SIZE; want >= 0x1000 && size == 0; want /= 2)
    {
        g_status.heapResult = svcControlMemory(&address, 0x08000000, 0, want, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);
        if (R_SUCCEEDED(g_status.heapResult))
            size = want;
    }
    g_status.heapSize = size;

    __ctru_heap = 0x08000000;
    __ctru_heap_size = size;
    __ctru_linear_heap = 0;
    __ctru_linear_heap_size = 0;
    fake_heap_start = (char *)0x08000000;
    fake_heap_end = fake_heap_start + size;

    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    g_status.stage = STAGE_HEAPS;
}

// A background module gets only what it needs: no applet or input services.
// (srvGetServiceHandle blocks forever on a service that isn't registered yet,
// so don't open optional services here.)
void __appInit(void)
{
    g_status.srvResult = srvInit();
    g_status.stage = STAGE_SRV;
    g_status.fsResult = fsInit();
    g_status.stage = STAGE_FS;
}

void __appExit(void)
{
    fsExit();
    srvExit();
}

//------------------------------------------------------------------------------
// Files
//------------------------------------------------------------------------------

static Result OpenSdFile(Handle *file, const char *path, u32 flags)
{
    Result res = FSUSER_OpenFileDirectly(file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                         fsMakePath(PATH_ASCII, path), flags, 0);
    g_status.lastFileResult = res;
    return res;
}

bool Platform_ReadFile(const char *path, char *buffer, uint32_t capacity, uint32_t *length)
{
    Handle  file;
    u64     size;
    u32     read = 0;

    if (R_FAILED(OpenSdFile(&file, path, FS_OPEN_READ)))
        return false;

    bool ok = R_SUCCEEDED(FSFILE_GetSize(file, &size))
              && R_SUCCEEDED(FSFILE_Read(file, &read, 0, buffer, size < capacity ? (u32)size : capacity));
    FSFILE_Close(file);

    buffer[read] = '\0';
    *length = read;
    return ok;
}

bool Platform_AppendFile(const char *path, const char *text)
{
    Handle  file;
    u64     size;
    u32     written;

    if (R_FAILED(OpenSdFile(&file, path, FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE)))
        return false;

    bool ok = R_SUCCEEDED(FSFILE_GetSize(file, &size))
              && R_SUCCEEDED(FSFILE_Write(file, &written, size, text, strlen(text), FS_WRITE_FLUSH));
    FSFILE_Close(file);
    return ok;
}

static void Log(const char *format, ...)
{
    char    line[320];
    va_list args;

    int n = snprintf(line, sizeof(line), "[%llu] ", (unsigned long long)(osGetTime() / 1000));
    va_start(args, format);
    vsnprintf(line + n, sizeof(line) - n - 1, format, args);
    va_end(args);
    strcat(line, "\n");
    Platform_AppendFile(LOG_PATH, line);
}

// For diagnosing banner flicker on hardware (see Overlay_Stats).
static void LogBannerStats(void)
{
    char stats[256];
    Overlay_Stats(stats, sizeof(stats));
    Log("banner: %s", stats);
}

//------------------------------------------------------------------------------
// Game memory and notifications
//------------------------------------------------------------------------------

bool Platform_ReadMemory(uint32_t address, uint8_t size, uint32_t *value)
{
    u32 page = address & ~0xFFFu;
    u32 span = ((address + size - 1) & ~0xFFFu) - page + 0x1000;

    if (R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, g_game, page, span, 0)))
        return false;

    const u8 *p = (const u8 *)(MAP_ADDRESS + (address - page));
    u32 v = 0;
    for (u8 i = 0; i < size; i++)
        v |= (u32)p[i] << (8 * i);

    svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, MAP_ADDRESS, span);
    *value = v;
    return true;
}

void Platform_Unlocked(const char *name, const char *description)
{
    (void)description;
    int  unlocked, total;
    char progress[16];

    Achievements_Progress(&unlocked, &total);
    snprintf(progress, sizeof(progress), "%d/%d", unlocked, total);

    Log("unlocked: %s (%s)", name, progress);
    Overlay_Show("Achievement unlocked!", name, progress, BANNER_MS);
    LogBannerStats();
}

// Reads the game's SMDH ("icon" in its ExeFS) through the SaveDataAndContent
// archive, trying the media type FS launched it from, then SD, game card and
// NAND. Logs each step's result.
static bool ReadSmdh(u32 pid, u64 titleId, u8 *smdh)
{
    static const u32    iconPath[5] = { 0, 0, 2, 0x6E6F6369, 0 };   // ExeFS file "icon"
    FS_ProgramInfo      info = { 0 };
    u32                 media[4] = { MEDIATYPE_SD, MEDIATYPE_SD, MEDIATYPE_GAME_CARD, MEDIATYPE_NAND };

    Result res = FSUSER_GetProgramLaunchInfo(&info, pid);
    Log("title: launch info %08lX (program %016llX, media %d)", res, (unsigned long long)info.programId,
        (int)info.mediaType);
    if (R_SUCCEEDED(res))
    {
        media[0] = info.mediaType;
        titleId = info.programId;
    }

    for (u32 i = 0; i < 4; i++)
    {
        if (i > 0 && media[i] == media[0])
            continue;

        u32     archivePath[4] = { (u32)titleId, (u32)(titleId >> 32), media[i], 0 };
        FS_Path archive = { PATH_BINARY, sizeof(archivePath), archivePath };
        FS_Path path = { PATH_BINARY, sizeof(iconPath), iconPath };
        Handle  file;
        u64     size = 0;
        u32     read = 0;

        res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, archive, path, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(res))
        {
            // ExeFS files are read whole, in one call.
            res = FSFILE_GetSize(file, &size);
            if (R_SUCCEEDED(res))
                res = FSFILE_Read(file, &read, 0, smdh, size < SMDH_SIZE ? (u32)size : SMDH_SIZE);
            FSFILE_Close(file);
        }
        Log("title: media %lu: %08lX (size %llu, read %lu)", media[i], res, (unsigned long long)size, read);

        if (R_SUCCEEDED(res) && read >= 0x408 && memcmp(smdh, "SMDH", 4) == 0)
            return true;
    }
    return false;
}

// The game's short title from its SMDH: English, else Japanese, else the
// process name. Characters outside ASCII become '?'. Returns false when it
// fell back to the process name.
static bool ReadGameTitle(u32 pid, u64 titleId, char *out, size_t capacity)
{
    static u8 smdh[SMDH_SIZE];

    out[0] = '\0';
    if (ReadSmdh(pid, titleId, smdh))
    {
        for (int language = 1; language >= 0 && out[0] == '\0'; language--)
        {
            const u8 *title = smdh + 8 + language * 0x200;
            size_t    n = 0;
            for (size_t i = 0; i < 0x40 && n + 1 < capacity; i++)
            {
                u16 c = (u16)(title[2 * i] | title[2 * i + 1] << 8);
                if (c == 0)
                    break;
                out[n++] = c == '\n' ? ' ' : c < 0x80 ? (char)c : '?';
            }
            out[n] = '\0';
        }
    }
    if (out[0] != '\0')
        return true;

    Handle process;
    if (R_SUCCEEDED(svcOpenProcess(&process, pid)))
    {
        s64 name[2] = { 0, 0 };
        svcGetProcessInfo(&name[0], process, PROCESS_NAME);
        svcCloseHandle(process);
        snprintf(out, capacity, "%.8s", (const char *)name);
    }
    return false;
}

// "<game>, <unlocked>/<total> achievements unlocked" (or "achievements not
// supported" without definitions), the game's name cut with "..." to fit.
static void ShowStartBanner(bool supported)
{
    char    suffix[48], message[128];   // filled to BANNER_CHARS at most
    int     unlocked, total;

    if (supported)
    {
        Achievements_Progress(&unlocked, &total);
        snprintf(suffix, sizeof(suffix), ", %d/%d achievements unlocked", unlocked, total);
    }
    else
        snprintf(suffix, sizeof(suffix), ", achievements not supported");

    int room = BANNER_CHARS - (int)strlen(suffix);
    int length = (int)strlen(g_gameTitle);
    if (length <= room)
        snprintf(message, sizeof(message), "%s%s", g_gameTitle, suffix);
    else
        snprintf(message, sizeof(message), "%.*s...%s", room > 3 ? room - 3 : 0, g_gameTitle, suffix);

    Log("start banner: %s", message);
    Overlay_Show("3DS Achievements", message, "", START_BANNER_MS);
    LogBannerStats();
}

// The running application (title ID 00040000xxxxxxxx), if any.
static bool FindApplication(u32 *pid, u64 *titleId)
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

        if (R_SUCCEEDED(res) && ((u64)tid >> 32) == 0x00040000)
        {
            *pid = pids[i];
            *titleId = (u64)tid;
            return true;
        }
    }
    return false;
}

int main(void)
{
    Handle  notification = 0;
    u64     loadedTitle = 0;
    bool    active = false;
    bool    haveTitle = false;      // from the SMDH, not the process name
    bool    startShown = false;     // this game's start banner
    u64     startAt = 0;

    g_status.stage = STAGE_MAIN;
    Log("tracker started");
    g_status.stage = STAGE_LOGGED;
    Result res = srvEnableNotification(&notification);
    if (R_SUCCEEDED(res))
        res = srvSubscribe(NOTIF_TERMINATE);
    bool canStop = R_SUCCEEDED(res);
    Log("shutdown notification: %08lX", res);

    while (true)
    {
        // Sleep one poll, waking early for a shutdown request.
        if (!canStop)
            svcSleepThread(POLL_NS);
        else if (svcWaitSynchronization(notification, POLL_NS) == 0)
        {
            u32 id = 0;
            srvReceiveNotification(&id);
            if (id == NOTIF_TERMINATE)
                break;
        }
        g_status.polls++;
        if (g_status.stage < STAGE_POLLING)
            g_status.stage = STAGE_POLLING;

        u32 pid;
        u64 titleId;
        if (!FindApplication(&pid, &titleId))
        {
            loadedTitle = 0;
            active = false;
            continue;
        }

        g_status.stage = STAGE_GAME;
        g_status.gameTitleLow = (u32)titleId;

        if (titleId != loadedTitle)
        {
            loadedTitle = titleId;
            active = Achievements_Load(titleId);
            haveTitle = ReadGameTitle(pid, titleId, g_gameTitle, sizeof(g_gameTitle));
            Log("game %016llX (%s): %d achievement line(s)", (unsigned long long)titleId, g_gameTitle,
                Achievements_Count());
            startShown = false;
            startAt = osGetTime() + START_DELAY_MS;
        }

        // Once per launch, a few seconds in (the game is drawing by then and
        // FS can read its title), unless it's suspended behind HOME.
        u32 applet;
        if (!startShown && osGetTime() >= startAt && Menu_GameInFront(&applet))
        {
            if (!haveTitle)
                haveTitle = ReadGameTitle(pid, titleId, g_gameTitle, sizeof(g_gameTitle));
            ShowStartBanner(active);
            startShown = true;
        }

        bool menu = Menu_HotkeyHeld();
        if ((!active && !menu) || R_FAILED(svcOpenProcess(&g_game, pid)))
            continue;

        if (menu)
        {
            if (!haveTitle)     // FS may not have been ready right at launch
                haveTitle = ReadGameTitle(pid, titleId, g_gameTitle, sizeof(g_gameTitle));
            if (!Menu_GameInFront(&applet))
                Log("menu: game not in front (active applet %lX)", applet);
            else if (!Menu_Open(g_game, g_gameTitle))
                Log("menu: couldn't pause the game (active applet %lX)", applet);
        }
        else
            Achievements_Update();
        svcCloseHandle(g_game);
    }

    Log("tracker stopping");
    svcCloseHandle(notification);
    return 0;
}
