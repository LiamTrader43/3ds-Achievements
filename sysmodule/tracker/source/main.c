// Background achievement tracker: a sysmodule loaded by Luma3DS from
// /luma/sysmodules/0004013000AC4102.cxi, started by the app, or with the console when
// the app's "Start with the console" is on (autostart_files.h).
//
// Every 100 ms it finds the running game, loads that game's definitions when
// the game changes, and checks them by mapping the game's memory with Luma's
// svcMapProcessMemoryEx. Unlocks are saved to the progress file, with a
// banner on the bottom screen (overlay.c). A few seconds after a game starts,
// a banner shows its progress (or that it has no achievements).
// R + A + Left pauses the game and shows the achievement list (menu.c).

#include <3ds.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "autostart_files.h"
#include "gamemem.h"
#include "heap.h"
#include "menu.h"
#include "overlay.h"

#define POLL_NS             (100 * 1000 * 1000LL)
#define LOG_PATH            "/achievements/tracker.log"
#define NOTIF_TERMINATE     0x100           // PM asks sysmodules to exit (shutdown)
Result svcControlService(u32 op, Handle *out, const char *name);
#define PROCESS_NAME        0x10000         // Luma svcGetProcessInfo extensions
#define PROCESS_TITLE_ID    0x10001
#define BANNER_MS           6000
#define APP_TITLE_ID        0x000400000AC41000ULL   // the Achievements app, when installed as a CIA
#define LUMA_PATCHGAMES_BIT 2                       // in Luma's config (loader: singleOptions PATCHGAMES)
#define ALLOW_PATCHED_PATH  "/achievements/allow_patched"   // exists while the app's setting is on
#define LIGHT_RED           255             // gold on the notification light
#define LIGHT_GREEN         150
#define LIGHT_BLUE          10
#define START_DELAY_MS      5000            // after a game launches, before its start banner
#define START_BANNER_MS     5000
#define SMDH_SIZE           0x36C0

extern char *fake_heap_start, *fake_heap_end;
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;
extern u32 __stacksize__;       // libctru: the main thread's stack (32 KB), at the start of the heap area

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
    s32 heapResult;         // svcControlMemory result for the stack's pages
    u32 heapSize;           // the stack's size if they were allocated (0 = none)
    u32 freeMemory;         // memory left for the system modules at start (Heap_SystemFree)
} TrackerStatus;

enum { STAGE_HEAPS_ENTERED = 1, STAGE_HEAPS, STAGE_SRV, STAGE_FS, STAGE_MAIN, STAGE_LOGGED, STAGE_POLLING, STAGE_GAME };

volatile TrackerStatus g_status = { { 0x54484341, 0x54415453 }, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Replaces libctru's default, which gives an app all the free memory in its region
// (a linear heap size of 0 means "everything left"); taking it all crashed the
// console. Only the main thread's stack, no heap yet (heap.c grows it when needed)
// and never a linear heap.
//
// libctru's initSystem moves the main thread's stack to the start of the heap area
// right after this returns ([fake_heap_start, + __stacksize__), 32 KB), and the heap
// starts after it; so those pages must be there. (The old fixed 256 KB heap held it.)
void __system_allocateHeaps(void)
{
    u32 address;

    g_status.stage = STAGE_HEAPS_ENTERED;
    g_status.freeMemory = Heap_SystemFree();
    g_status.heapResult = svcControlMemory(&address, HEAP_BASE, 0, __stacksize__, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);
    g_status.heapSize = R_SUCCEEDED(g_status.heapResult) ? __stacksize__ : 0;

    __ctru_heap = HEAP_BASE;
    __ctru_heap_size = __stacksize__;
    __ctru_linear_heap = 0;
    __ctru_linear_heap_size = 0;
    fake_heap_start = (char *)HEAP_BASE;
    fake_heap_end = fake_heap_start + __stacksize__;

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
    char    line[512];
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
    char stats[420];
    Overlay_Stats(stats, sizeof(stats));
    Log("banner: %s", stats);
}

//------------------------------------------------------------------------------
// Game memory and notifications
//------------------------------------------------------------------------------

// Game memory is read through gamemem.c (one mapping at a time, copied; see there).
#define STATS_POLLS     600     // one stats window: about a minute
#define STATS_EVERY     10      // a routine "poll:" line every 10 windows (~10 minutes)

static u32 g_statsWindows;      // stats windows since the current game started

// Counts a poll, in windows of STATS_POLLS polls. Logs a window ("poll:" lines in
// tracker.log: how long checking took and how game memory was read; a banner's
// seconds count in a poll's time) when something went wrong in it (a skipped poll:
// memory that was readable couldn't be read; or a failed unmap), for the first
// window of a game, and every STATS_EVERY windows; so a normal session adds one
// line per ~10 minutes, not one a minute. Map calls count each range or page
// mapped, failed ones too (a few failed maps are normal: junk pointers).
static void PollStats(u64 ticks)
{
    static u32          polls;
    static u64          total, longest;
    static GameMemStats sum;
    GameMemStats        poll;

    GameMem_TakeStats(&poll);
    polls++;
    sum.reads += poll.reads;
    sum.failedReads += poll.failedReads;
    sum.maps += poll.maps;
    sum.mapFailures += poll.mapFailures;
    sum.unmapFailures += poll.unmapFailures;
    sum.skippedPolls += poll.skippedPolls;
    if (poll.mapFailures + poll.unmapFailures > 0)
    {
        sum.lastError = poll.lastError;
        sum.lastErrorAddress = poll.lastErrorAddress;
    }
    total += ticks;
    if (ticks > longest)
        longest = ticks;
    if (polls < STATS_POLLS)
        return;

    g_statsWindows++;
    bool trouble = sum.skippedPolls > 0 || sum.unmapFailures > 0;
    if (!trouble && g_statsWindows != 1 && g_statsWindows % STATS_EVERY != 0)
    {
        polls = 0;
        total = longest = 0;
        memset(&sum, 0, sizeof(sum));
        return;
    }

    // The pages the last poll read: which pointers led where (e.g. an enemy's actor).
    u32  pages[16];
    char list[16 * 9 + 1] = "";
    int  count = GameMem_LastPages(pages, 16);
    for (int i = 0; i < count; i++)
        snprintf(list + i * 9, sizeof(list) - (size_t)i * 9, " %08lX", (unsigned long)pages[i]);
    Log("poll: %lu polls (%lu skipped), avg %llu us, max %llu us, per poll %lu reads (%lu.%02lu unreadable), "
        "%lu.%02lu map calls; failed maps %lu, unmaps %lu (last %08lX at %08lX); pages:%s",
        (unsigned long)polls, (unsigned long)sum.skippedPolls,
        total * 1000000 / SYSCLOCK_ARM11 / polls, longest * 1000000 / SYSCLOCK_ARM11,
        (unsigned long)(sum.reads / polls), (unsigned long)(sum.failedReads / polls), (unsigned long)(sum.failedReads * 100 / polls % 100),
        (unsigned long)(sum.maps / polls), (unsigned long)(sum.maps * 100 / polls % 100),
        (unsigned long)sum.mapFailures, (unsigned long)sum.unmapFailures,
        (unsigned long)sum.lastError, (unsigned long)sum.lastErrorAddress, list);
    polls = 0;
    total = longest = 0;
    memset(&sum, 0, sizeof(sum));
}

bool Platform_ReadMemory(uint32_t address, uint8_t size, uint32_t *value)
{
    return GameMem_Read(address, size, value);
}

// Holds the notification light (the one by the hinge) on the unlock colour, or turns it off.
static void SetNotificationLight(bool on)
{
    InfoLedPattern pattern;
    Handle         session = 0;

    memset(&pattern, 0, sizeof(pattern));
    pattern.delay = 0x10;
    pattern.smoothing = 0x10;
    if (on)
    {
        memset(pattern.redPattern, LIGHT_RED, sizeof(pattern.redPattern));
        memset(pattern.greenPattern, LIGHT_GREEN, sizeof(pattern.greenPattern));
        memset(pattern.bluePattern, LIGHT_BLUE, sizeof(pattern.bluePattern));
    }

    // Not blocking: the service can be full, and then Rosalina's way in is to share a
    // session someone else already has.
    srvSetBlockingPolicy(true);
    Result res = srvGetServiceHandle(&session, "mcu::HWC");
    srvSetBlockingPolicy(false);
    if (R_FAILED(res))
        res = svcControlService(0, &session, "mcu::HWC");       // SERVICEOP_STEAL_CLIENT_SESSION
    if (R_SUCCEEDED(res))
    {
        Handle *handle = mcuHwcGetSessionHandle();
        *handle = session;
        res = MCUHWC_SetInfoLedPattern(&pattern);
        *handle = 0;
        svcCloseHandle(session);
    }
    if (R_FAILED(res))
        Log("light: %08lX", res);
}

void Platform_Unlocked(const char *name, const char *description)
{
    (void)description;
    int  unlocked, total;
    char progress[16];

    Achievements_Progress(&unlocked, &total);
    snprintf(progress, sizeof(progress), "%d/%d", unlocked, total);

    Log("unlocked: %s (%s)", name, progress);
    SetNotificationLight(true);
    Overlay_Show("Achievement unlocked!", name, progress, BANNER_MS);
    SetNotificationLight(false);
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
// Saves a supported game's SMDH as /achievements/<TitleID>/title.smdh, once, for the
// viewer app: a game card's title and icon can't be read when it isn't inserted.
static void CacheSmdh(u64 titleId, const u8 *smdh)
{
    char    path[64];
    Handle  file;
    u64     size = 0;
    u32     written = 0;

    snprintf(path, sizeof(path), "/achievements/%016llX/title.smdh", (unsigned long long)titleId);
    if (R_FAILED(OpenSdFile(&file, path, FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE)))
        return;
    Result res = FSFILE_GetSize(file, &size);
    if (R_SUCCEEDED(res) && size == 0)
    {
        res = FSFILE_Write(file, &written, 0, smdh, SMDH_SIZE, FS_WRITE_FLUSH);
        Log("title: saved %s: %08lX", path, res);
    }
    FSFILE_Close(file);
}

static bool ReadGameTitle(u32 pid, u64 titleId, char *out, size_t capacity)
{
    u8 *smdh = malloc(SMDH_SIZE);   // only while reading (14 KB)

    out[0] = '\0';
    if (smdh != NULL && ReadSmdh(pid, titleId, smdh))
    {
        if (Achievements_Count() > 0)
            CacheSmdh(Achievements_GameId(), smdh);     // another release: the game's folder
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
    free(smdh);
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

// True if the user chose in the app to earn achievements in patched games.
static bool PatchedGamesAllowed(void)
{
    Handle file;

    if (R_FAILED(OpenSdFile(&file, ALLOW_PATCHED_PATH, FS_OPEN_READ)))
        return false;
    FSFILE_Close(file);
    return true;
}

// The name of a Luma patch or mod for this game in /luma/titles/<TitleID>/, or
// NULL. A patched game may not behave as its achievement conditions expect.
// (exheader.bin and locale.txt are left out: they don't change the game's logic.)
// With Luma's "Enable game patching" off the files aren't applied, so they don't count.
static const char *FindGamePatch(u64 titleId)
{
    static const char *const names[] = { "code.bin", "code.bps", "code.ips", "code.ups" };
    char path[64];
    s64  config;

    if (R_SUCCEEDED(svcGetSystemInfo(&config, 0x10000, 3)) && !(config & BIT(LUMA_PATCHGAMES_BIT)))
        return NULL;

    for (u32 i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        Handle file;

        snprintf(path, sizeof(path), "/luma/titles/%016llX/%s", (unsigned long long)titleId, names[i]);
        if (R_SUCCEEDED(OpenSdFile(&file, path, FS_OPEN_READ)))
        {
            FSFILE_Close(file);
            return names[i];
        }
    }

    FS_Archive  sd;
    Handle      dir;

    if (R_FAILED(FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""))))
        return NULL;
    snprintf(path, sizeof(path), "/luma/titles/%016llX/romfs", (unsigned long long)titleId);
    bool found = R_SUCCEEDED(FSUSER_OpenDirectory(&dir, sd, fsMakePath(PATH_ASCII, path)));
    if (found)
        FSDIR_Close(dir);
    FSUSER_CloseArchive(sd);
    return found ? "romfs" : NULL;
}

// "<game>, <unlocked>/<total> achievements unlocked" (or why achievements are
// off), the game's name cut with "..." to fit.
static void ShowStartBanner(bool supported, bool patched, bool noMemory)
{
    char    suffix[48], title[96], message[160];
    int     unlocked, total;

    if (patched)
        snprintf(suffix, sizeof(suffix), ", patched, achievements off");
    else if (noMemory)
        snprintf(suffix, sizeof(suffix), ", achievements off: low memory");
    else if (supported)
    {
        Achievements_Progress(&unlocked, &total);
        snprintf(suffix, sizeof(suffix), ", %d/%d achievements unlocked", unlocked, total);
    }
    else
        snprintf(suffix, sizeof(suffix), ", achievements not supported");

    Overlay_Ellipsize(title, sizeof(title), g_gameTitle, OVERLAY_BANNER_W - Overlay_TextWidth(suffix));
    snprintf(message, sizeof(message), "%s%s", title, suffix);

    Log("start banner: %s", message);
    Overlay_Show("3DS Achievements", message, "", START_BANNER_MS);
    LogBannerStats();
}

// True while this title is really the Homebrew Launcher: Luma runs a 3DSX in
// place of the title in its shared config (hbldr_3dsx_tid, which Rosalina can
// switch to another title). Same check as Luma's loader, ignoring the platform bits.
static bool IsHomebrewHost(u64 titleId)
{
    u64 host = *(volatile u64 *)(OS_SHAREDCFG_VADDR + 0x800);
    return host != 0 && ((titleId ^ host) & ~0xF0000000ULL) == 0;
}

// Whether process pid is an application (title ID 00040000xxxxxxxx), and its title ID.
static bool IsApplication(u32 pid, u64 *titleId)
{
    Handle  process;
    s64     tid;

    if (R_FAILED(svcOpenProcess(&process, pid)))
        return false;
    Result res = svcGetProcessInfo(&tid, process, PROCESS_TITLE_ID);
    svcCloseHandle(process);
    *titleId = (u64)tid;
    return R_SUCCEEDED(res) && ((u64)tid >> 32) == 0x00040000;
}

// The running application, if any. Checks the one found last time first (process
// IDs aren't reused, so while it's alive it's still the game) and only lists every
// process (~100 kernel calls each poll) when it's gone.
static bool FindApplication(u32 *pid, u64 *titleId)
{
    static u32 last;
    u32 pids[64];
    s32 count;

    if (last != 0 && IsApplication(last, titleId))
    {
        *pid = last;
        return true;
    }
    last = 0;
    if (R_FAILED(svcGetProcessList(&count, pids, 64)))
        return false;
    for (s32 i = 0; i < count; i++)
        if (IsApplication(pids[i], titleId))
        {
            *pid = last = pids[i];
            return true;
        }
    return false;
}

//------------------------------------------------------------------------------
// Start with the console (the app's setting, autostart_files.h)
//------------------------------------------------------------------------------

#define ERRDISP_TITLE_ID    0x0004003000008A02ULL

static bool g_bootPending;      // started with the console: AUTOSTART_TRY stays until the HOME Menu is up
static bool g_exitWhenUp;       // and the user turned the tracker off: it leaves then

static bool SdExists(const char *path)
{
    Handle file;

    if (R_FAILED(OpenSdFile(&file, path, FS_OPEN_READ)))
        return false;
    FSFILE_Close(file);
    return true;
}

// Deletes a file, or a folder if it's empty.
static void SdDelete(const char *path, bool folder)
{
    FS_Archive sd;

    if (R_FAILED(FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""))))
        return;
    if (folder)
        FSUSER_DeleteDirectory(sd, fsMakePath(PATH_ASCII, path));
    else
        FSUSER_DeleteFile(sd, fsMakePath(PATH_ASCII, path));
    FSUSER_CloseArchive(sd);
}

// The system is up: an application, a system application (System Settings...) or an applet
// (the HOME Menu) is running. ErrDisp, an applet too, doesn't count.
static bool SystemUp(void)
{
    u32 pids[64];
    s32 count;

    if (R_FAILED(svcGetProcessList(&count, pids, 64)))
        return true;        // can't tell: nothing to guard
    for (s32 i = 0; i < count; i++)
    {
        Handle  process;
        s64     tid;

        if (R_FAILED(svcOpenProcess(&process, pids[i])))
            continue;
        Result res = svcGetProcessInfo(&tid, process, PROCESS_TITLE_ID);
        svcCloseHandle(process);
        u32 high = (u32)((u64)tid >> 32);
        if (R_SUCCEEDED(res) && (high == 0x00040000 || high == 0x00040010 || high == 0x00040030)
            && ((u64)tid & ~0xF0000000ULL) != ERRDISP_TITLE_ID)
            return true;
    }
    return false;
}

// Turns the setting off: removes the app's copy of the module's exheader (only if the tracker
// is first in its list) and the record, and tells the app why.
static void AutostartOff(void)
{
    char                text[256], path[64];
    u32                 length;
    unsigned long long  host = 0;

    if (Platform_ReadFile(AUTOSTART_RECORD, text, sizeof(text) - 1, &length))
    {
        const char *at = strstr(text, "host ");
        if (at != NULL)
            host = strtoull(at + 5, NULL, 16);
    }
    if (host != 0)
    {
        Handle  file;
        u64     first = 0;
        u32     read = 0;

        snprintf(path, sizeof(path), AUTOSTART_TITLES "/%016llX/exheader.bin", host);
        if (R_SUCCEEDED(OpenSdFile(&file, path, FS_OPEN_READ)))
        {
            FSFILE_Read(file, &read, AUTOSTART_DEPS_OFFSET, &first, sizeof(first));
            FSFILE_Close(file);
            if (read == sizeof(first) && first == TRACKER_TID)
            {
                SdDelete(path, false);
                snprintf(path, sizeof(path), AUTOSTART_TITLES "/%016llX", host);
                SdDelete(path, true);
                Log("autostart: removed " AUTOSTART_TITLES "/%016llX/exheader.bin", host);
            }
        }
    }
    SdDelete(AUTOSTART_RECORD, false);
    Platform_AppendFile(AUTOSTART_FAILED, "a start of the 3DS that started the tracker didn't reach the HOME Menu\n");
}

// When the console started the tracker (nothing is running yet), it guards the boot: if the
// last start that started it never got to the HOME Menu, maybe because of it, the setting is
// turned off and the tracker leaves at once (false). Otherwise this start is marked until the
// HOME Menu is up.
static bool BootGuard(void)
{
    if (SystemUp())
        return true;        // started by the app
    Log("autostart: started with the console");
    if (SdExists(AUTOSTART_TRY))
    {
        Log("autostart: the last start of the 3DS that started the tracker didn't reach the HOME Menu: turned off");
        AutostartOff();
        SdDelete(AUTOSTART_TRY, false);
        return false;
    }
    g_bootPending = Platform_AppendFile(AUTOSTART_TRY, "1\n");
    g_exitWhenUp = SdExists(AUTOSTART_OFF);
    if (g_exitWhenUp)
        Log("autostart: the tracker is turned off in the app: it leaves once the HOME Menu is up");
    return !(g_exitWhenUp && !g_bootPending);
}

// The game closed (or another one started): everything it took goes back to the system.
static void UnloadGame(void)
{
    bool tracked = Achievements_Count() > 0;

    Achievements_Unload();
    GameMem_Release();
    Heap_Trim();
    if (tracked)
        Log("memory: game closed, heap back to %lu KB, %lu KB free", Heap_Size() / 1024, Heap_SystemFree() / 1024);
}

int main(void)
{
    Handle  notification = 0;
    u64     loadedTitle = 0;
    bool    active = false;
    bool    patched = false;        // has definitions, but isn't tracked (see FindGamePatch)
    bool    noMemory = false;       // couldn't be loaded: too little memory left (see heap.c)
    bool    haveTitle = false;      // from the SMDH, not the process name
    bool    startShown = false;     // this game's start banner
    u64     startAt = 0;

    g_status.stage = STAGE_MAIN;
    Heap_Init();
    Log("tracker started");
    Log("memory: %lu KB free at start (limit %lu KB, Base region %lu KB now); stack %lu KB (%08lX); "
        "no heap until a game with achievements runs", g_status.freeMemory / 1024, Heap_LimitFree() / 1024,
        Heap_RegionFree() / 1024, g_status.heapSize / 1024, g_status.heapResult);
    g_status.stage = STAGE_LOGGED;
    if (!BootGuard())
    {
        Log("tracker stopping");
        return 0;
    }
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

        if (g_bootPending && SystemUp())
        {
            SdDelete(AUTOSTART_TRY, false);
            g_bootPending = false;
            Log("autostart: the HOME Menu is up");
            if (g_exitWhenUp)
                break;
        }

        u32 pid;
        u64 titleId;
        if (!FindApplication(&pid, &titleId) || IsHomebrewHost(titleId) || titleId == APP_TITLE_ID)     // not games
        {
            if (loadedTitle != 0)
                UnloadGame();
            loadedTitle = 0;
            active = false;
            continue;
        }

        g_status.stage = STAGE_GAME;
        g_status.gameTitleLow = (u32)titleId;

        if (titleId != loadedTitle)
        {
            if (loadedTitle != 0)
                UnloadGame();
            loadedTitle = titleId;
            Heap_TakeRefused();
            active = Achievements_Load(titleId);
            u32 refused = Heap_TakeRefused();
            noMemory = !active && refused > 0;    // it may have achievements: they couldn't be loaded
            haveTitle = ReadGameTitle(pid, titleId, g_gameTitle, sizeof(g_gameTitle));
            Log("game %016llX (%s): %d achievement line(s)", (unsigned long long)titleId, g_gameTitle,
                Achievements_Count());
            if (Achievements_GameId() != titleId)
                Log("game: another release of %016llX (version.txt)", (unsigned long long)Achievements_GameId());
            const char *patch = active ? FindGamePatch(titleId) : NULL;
            patched = patch != NULL && !PatchedGamesAllowed();
            if (patched)
                active = false;     // the menu can still list its achievements
            if (patch != NULL)
                Log("game is patched (%s): %s", patch, patched ? "not tracking" : "tracking, allowed in the app");

            // What it took: its achievements, then the copies of its memory (GameMem_BeginPoll
            // takes them again if they couldn't be had yet). Trimmed first: the file buffers
            // and the title's icon data are freed by now, and malloc doesn't count free room
            // at the heap's end when it grows it, so they'd make it ask for more than needed.
            Heap_Trim();
            bool copies = !active || GameMem_Acquire();
            refused += Heap_TakeRefused();
            if (Achievements_Count() > 0 || refused > 0)
                Log("memory: heap %lu KB, %lu KB free%s", Heap_Size() / 1024, Heap_SystemFree() / 1024,
                    noMemory ? "; not enough left to load this game's achievements"
                    : !copies ? "; not enough left to read the game yet" : "");
            startShown = false;
            startAt = osGetTime() + START_DELAY_MS;
            g_statsWindows = 0;     // this game's first stats window gets logged
        }

        // Once per launch, a few seconds in (the game is drawing by then and
        // FS can read its title), unless it's suspended behind HOME.
        u32 applet;
        if (!startShown && osGetTime() >= startAt && Menu_GameInFront(&applet))
        {
            if (!haveTitle)
            {
                haveTitle = ReadGameTitle(pid, titleId, g_gameTitle, sizeof(g_gameTitle));
                Heap_Trim();
            }
            ShowStartBanner(active, patched, noMemory);
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
            else if (!Menu_Open(g_game, Achievements_GameId()))     // the game's folder (trophy.bin)
                Log("menu: couldn't pause the game (active applet %lX)", applet);
            else
                Log("menu: touch %08lX", Menu_TouchResult());
            Heap_Trim();            // the title's icon data, the menu's trophy icons
        }
        else
        {
            u64 start = svcGetSystemTick();
            if (GameMem_BeginPoll(g_game, pid))     // false: memory unreadable, don't check with zeros
                Achievements_Update();
            GameMem_EndPoll();
            PollStats(svcGetSystemTick() - start);
        }
        svcCloseHandle(g_game);
    }

    Log("tracker stopping");
    svcCloseHandle(notification);
    return 0;
}
