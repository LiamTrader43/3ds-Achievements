// "Start with the console": see autostart.h and ../sysmodule/tracker/source/autostart_files.h.
//
// Turning it on reads, through Luma's loader, the exheader of NS and of every module pm starts
// with it, works out the boot from them (boot_plan.c), picks the module that can start the
// tracker safely, and writes that module's exheader with the tracker first in its dependency
// list to /luma/titles/<module>/exheader.bin. Then it checks that the loader gives the new
// file, works the boot out again from what the loader now gives, and only then keeps it.

#include "autostart.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "autostart_files.h"
#include "boot_plan.h"

#ifndef AUTOSTART_SD
#define AUTOSTART_SD        "sdmc:"         // (a PC test uses a folder of its own)
#endif
#define LOG_PATH            AUTOSTART_SD "/achievements/autostart.log"
#define RECORD_PATH         AUTOSTART_SD AUTOSTART_RECORD
#define RECORD_TMP          AUTOSTART_SD AUTOSTART_RECORD ".tmp"
#define TRY_PATH            AUTOSTART_SD AUTOSTART_TRY
#define FAILED_PATH         AUTOSTART_SD AUTOSTART_FAILED
#define TRACKER_CXI         AUTOSTART_SD "/luma/sysmodules/0004013000AC4102.cxi"
#define NOT_WRITTEN         AUTOSTART_RESULT(8)     // the SD card couldn't be written
#define NS_TITLE            0x0004013000008002ULL   // NS, unless the kernel names another
#define LUMA_MIN_MAJOR      13                      // the pm and loader this was worked out from
#define LUMA_MODULES        BIT(1)                  // Luma's config: "Enable loading external FIRMs and modules"
#define LUMA_PATCHING       BIT(2)                  // "Enable game patching"
#define N3DS_BIT            0x20000000ULL
#define PROGRAM_ID_OFFSET   0x200                   // in an exheader: the program's own title ID

//------------------------------------------------------------------------------
// The system: Luma, its loader, AM (a PC test brings its own)
//------------------------------------------------------------------------------

#ifdef AUTOSTART_TEST
Result  Sys_LumaInfo(u32 *version, u32 *config);
bool    Sys_IsNew3ds(void);
u64     Sys_NsTitle(void);
Result  Sys_LoaderOpen(Handle *loader);
void    Sys_LoaderClose(Handle loader);
Result  Sys_LoaderRegister(Handle loader, u64 id, u64 *program);
Result  Sys_LoaderUnregister(Handle loader, u64 program);
Result  Sys_LoaderInfo(Handle loader, u64 program, u8 *exheader);
int     Sys_TitleVersion(u64 id);
#else
Result svcControlService(u32 op, Handle *out, const char *name);    // luma_svc.s

static Result Sys_LumaInfo(u32 *version, u32 *config)
{
    s64     v = 0, c = 0;
    Result  res = svcGetSystemInfo(&v, 0x10000, 0);    // major << 24 | minor << 16 | build << 8

    if (R_SUCCEEDED(res))
        res = svcGetSystemInfo(&c, 0x10000, 3);
    *version = (u32)v;
    *config = (u32)c;
    return res;
}

static bool Sys_IsNew3ds(void)
{
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    return n3ds;
}

static u64 Sys_NsTitle(void)
{
    return OS_KernelConfig->ns_tid != 0 ? OS_KernelConfig->ns_tid : NS_TITLE;
}

// The loader has room for two sessions: pm keeps one and Rosalina opens the other now and
// then. If both are taken, pm's is shared (Luma's svcControlService).
static Result Sys_LoaderOpen(Handle *loader)
{
    srvSetBlockingPolicy(true);     // don't wait for one
    Result res = srvGetServiceHandle(loader, "Loader");
    srvSetBlockingPolicy(false);
    if (R_FAILED(res))
        res = svcControlService(0, loader, "Loader");     // SERVICEOP_STEAL_CLIENT_SESSION
    return res;
}

static void Sys_LoaderClose(Handle loader)
{
    svcCloseHandle(loader);
}

static Result Sys_LoaderRegister(Handle loader, u64 id, u64 *program)
{
    FS_ProgramInfo info;
    u32 *cmd = getThreadCommandBuffer();

    memset(&info, 0, sizeof(info));
    info.programId = id;
    info.mediaType = MEDIATYPE_NAND;
    cmd[0] = IPC_MakeHeader(2, 8, 0);       // RegisterProgram(program, update)
    memcpy(&cmd[1], &info, sizeof(info));
    memcpy(&cmd[5], &info, sizeof(info));
    Result res = svcSendSyncRequest(loader);
    if (R_SUCCEEDED(res))
        res = (Result)cmd[1];
    if (R_SUCCEEDED(res))
        memcpy(program, &cmd[2], sizeof(*program));
    return res;
}

static Result Sys_LoaderUnregister(Handle loader, u64 program)
{
    u32 *cmd = getThreadCommandBuffer();

    cmd[0] = IPC_MakeHeader(3, 2, 0);       // UnregisterProgram
    memcpy(&cmd[1], &program, sizeof(program));
    Result res = svcSendSyncRequest(loader);
    return R_SUCCEEDED(res) ? (Result)cmd[1] : res;
}

// The exheader as the loader gives it to pm: with Luma's exheader.bin from the SD card, if any.
static Result Sys_LoaderInfo(Handle loader, u64 program, u8 *exheader)
{
    u32 *cmd = getThreadCommandBuffer(), *buffers = getThreadStaticBuffers();
    u32  saved[2] = { buffers[0], buffers[1] };

    cmd[0] = IPC_MakeHeader(4, 2, 0);       // GetProgramInfo
    memcpy(&cmd[1], &program, sizeof(program));
    buffers[0] = IPC_Desc_StaticBuffer(AUTOSTART_EXHEADER_SIZE, 0);
    buffers[1] = (u32)exheader;
    Result res = svcSendSyncRequest(loader);
    buffers[0] = saved[0];
    buffers[1] = saved[1];
    return R_SUCCEEDED(res) ? (Result)cmd[1] : res;
}

// The installed version of a title on the NAND, or -1.
static int Sys_TitleVersion(u64 id)
{
    AM_TitleEntry entry;
    int version = -1;

    if (R_SUCCEEDED(amInit()))
    {
        if (R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_NAND, 1, &id, &entry)))
            version = entry.version;
        amExit();
    }
    return version;
}
#endif

//------------------------------------------------------------------------------
// The log and files
//------------------------------------------------------------------------------

static FILE *s_log;

static void OpenLog(const char *mode)
{
    s_log = fopen(LOG_PATH, mode);
}

static void CloseLog(void)
{
    if (s_log != NULL)
        fclose(s_log);
    s_log = NULL;
}

static void Log(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char *format, ...)
{
    va_list args;

    if (s_log == NULL)
        return;
    va_start(args, format);
    vfprintf(s_log, format, args);
    va_end(args);
    fputc('\n', s_log);
}

static bool Exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    fclose(file);
    return true;
}

// Module id's folder in /luma/titles, and a file in it.
static void TitlePath(char *out, size_t capacity, u64 id, const char *name)
{
    snprintf(out, capacity, AUTOSTART_SD AUTOSTART_TITLES "/%016llX%s%s", (unsigned long long)id,
             name != NULL ? "/" : "", name != NULL ? name : "");
}

// Something on the SD card replaces module id: Luma's exheader.bin for it, or a .cxi of its own
// (the loader then gives the .cxi's exheader and never reads an exheader.bin).
static bool Replaced(u64 id)
{
    char path[80];

    TitlePath(path, sizeof(path), id, "exheader.bin");
    if (Exists(path))
        return true;
    snprintf(path, sizeof(path), AUTOSTART_SD "/luma/sysmodules/%016llX.cxi", (unsigned long long)id);
    return Exists(path);
}

static u64 ReadU64(const u8 *data)
{
    u64 value;
    memcpy(&value, data, sizeof(value));
    return value;
}

// True if this exheader starts the tracker: it's first in the dependency list (the app's copy).
static bool StartsTracker(const u8 *exheader)
{
    return ReadU64(exheader + AUTOSTART_DEPS_OFFSET) == TRACKER_TID;
}

//------------------------------------------------------------------------------
// Reading the boot
//------------------------------------------------------------------------------

typedef struct
{
    Handle  loader;
    bool    n3ds;
} Reader;

// Module id's exheader from the loader, registered the way pm does (on a New 3DS its New 3DS
// version first).
static Result ReadExheader(const Reader *reader, u64 id, u8 *exheader)
{
    u64     program;
    Result  res;

    if (reader->n3ds)
    {
        res = Sys_LoaderRegister(reader->loader, (id & ~BOOT_N3DS_MASK) | N3DS_BIT, &program);
        if (R_FAILED(res))
            res = Sys_LoaderRegister(reader->loader, id & ~BOOT_N3DS_MASK, &program);
    }
    else
        res = Sys_LoaderRegister(reader->loader, id, &program);
    if (R_FAILED(res))
        return res;
    res = Sys_LoaderInfo(reader->loader, program, exheader);
    Sys_LoaderUnregister(reader->loader, program);
    return res;
}

// boot_plan.c's BootRead.
static bool ReadModule(void *context, uint64_t id, uint64_t deps[BOOT_MAX], bool *overridden)
{
    u8 exheader[AUTOSTART_EXHEADER_SIZE];

    Result res = ReadExheader(context, id, exheader);
    if (R_FAILED(res))
    {
        Log("  %016llX can't be read (%08lX)", (unsigned long long)id, (unsigned long)(u32)res);
        return false;
    }
    memcpy(deps, exheader + AUTOSTART_DEPS_OFFSET, BOOT_MAX * sizeof(deps[0]));
    *overridden = Replaced(id) || Replaced(ReadU64(exheader + PROGRAM_ID_OFFSET));
    return true;
}

static void LogBoot(const Boot *boot)
{
    Log("boot: NS %016llX, %d modules listed%s", (unsigned long long)boot->ns, boot->listed,
        boot->complete ? "" : " (not in full: NS couldn't be read, or more than 48)");
    for (int i = 0; i < boot->listed; i++)
    {
        const BootModule *module = NULL;
        for (int j = 0; j < boot->moduleCount && module == NULL; j++)
            if (boot->modules[j].id == boot->list[i])
                module = &boot->modules[j];
        if (module == NULL)
            Log("  %2d %016llX%s", i + 1, (unsigned long long)boot->list[i],
                Boot_IsFirmModule(boot->list[i]) ? " running (FIRM)" : "");
        else if (module->count < 0)
            Log("  %2d %016llX can't be read", i + 1, (unsigned long long)module->id);
        else
            Log("  %2d %016llX %d dependencies%s%s", i + 1, (unsigned long long)module->id, module->count,
                module->overridden ? ", replaced on the SD card" : "",
                module->rawCount >= BOOT_MAX ? ", its list is full" : "");
    }
}

static void LogTry(const char *what, const BootTry *attempt)
{
    Log("%s: the tracker %d of %d listed, %d modules start after it, the last %016llX", what, attempt->at + 1,
        attempt->listed, attempt->startsAfter, (unsigned long long)attempt->lastStarted);
}

//------------------------------------------------------------------------------
// The file and the record
//------------------------------------------------------------------------------

// The tracker first in the exheader's dependency list, the others one down. False if it's full.
static bool InsertTracker(u8 *exheader)
{
    u64 deps[BOOT_MAX];

    memcpy(deps, exheader + AUTOSTART_DEPS_OFFSET, sizeof(deps));
    if (deps[BOOT_MAX - 1] != 0)
        return false;
    memmove(deps + 1, deps, (BOOT_MAX - 1) * sizeof(deps[0]));
    deps[0] = TRACKER_TID;
    memcpy(exheader + AUTOSTART_DEPS_OFFSET, deps, sizeof(deps));
    return true;
}

// Writes module id's exheader.bin: to a .tmp first, read back, then renamed. (The loader stops
// the console on a file of the wrong size, so it must never find a half-written one.)
static Result WriteCopy(u64 id, const u8 *exheader)
{
    char    folder[64], path[80], tmp[80];
    u8      back[AUTOSTART_EXHEADER_SIZE];

    TitlePath(folder, sizeof(folder), id, NULL);
    TitlePath(path, sizeof(path), id, "exheader.bin");
    TitlePath(tmp, sizeof(tmp), id, "exheader.tmp");
    mkdir(AUTOSTART_SD "/luma", 0777);
    mkdir(AUTOSTART_SD AUTOSTART_TITLES, 0777);
    mkdir(folder, 0777);

    FILE *file = fopen(tmp, "wb");
    bool ok = file != NULL && fwrite(exheader, 1, AUTOSTART_EXHEADER_SIZE, file) == AUTOSTART_EXHEADER_SIZE;
    if (file != NULL && fclose(file) != 0)
        ok = false;
    if (ok)
    {
        file = fopen(tmp, "rb");
        ok = file != NULL && fread(back, 1, sizeof(back), file) == sizeof(back) && fgetc(file) == EOF
             && memcmp(back, exheader, sizeof(back)) == 0;
        if (file != NULL)
            fclose(file);
    }
    if (ok)
        ok = rename(tmp, path) == 0;
    if (!ok)
    {
        remove(tmp);
        rmdir(folder);      // only if it's empty
        Log("couldn't write %s", path);
        return NOT_WRITTEN;
    }
    Log("wrote %s", path + strlen(AUTOSTART_SD));
    return 0;
}

// Removes module id's exheader.bin if it's the app's, and the folder if that leaves it empty.
static Result RemoveCopy(u64 id)
{
    char    folder[64], path[80];
    u8      exheader[AUTOSTART_EXHEADER_SIZE];

    TitlePath(folder, sizeof(folder), id, NULL);
    TitlePath(path, sizeof(path), id, "exheader.bin");
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    bool ours = fread(exheader, 1, sizeof(exheader), file) == sizeof(exheader) && StartsTracker(exheader);
    fclose(file);
    if (!ours)
    {
        Log("%s isn't the app's: left as it is", path + strlen(AUTOSTART_SD));
        return 0;
    }
    if (remove(path) != 0)
    {
        Log("couldn't remove %s", path + strlen(AUTOSTART_SD));
        return NOT_WRITTEN;
    }
    rmdir(folder);
    Log("removed %s", path + strlen(AUTOSTART_SD));
    return 0;
}

// Removes every copy in /luma/titles that starts the tracker (one whose record is gone, too).
static Result RemoveAllCopies(void)
{
    Result res = 0;

    DIR *folder = opendir(AUTOSTART_SD AUTOSTART_TITLES);
    if (folder == NULL)
        return 0;
    struct dirent *entry;
    while ((entry = readdir(folder)) != NULL)
    {
        char *end;
        unsigned long long id = strtoull(entry->d_name, &end, 16);
        if (strlen(entry->d_name) == 16 && *end == '\0' && R_FAILED(RemoveCopy(id)))
            res = NOT_WRITTEN;
    }
    closedir(folder);
    return res;
}

static bool ReadRecord(u64 *host, int *version)
{
    char line[96];

    FILE *file = fopen(RECORD_PATH, "rb");
    if (file == NULL)
        return false;
    *host = 0;
    *version = -1;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        unsigned long long id;
        int number;
        if (sscanf(line, "host %llx", &id) == 1)
            *host = id;
        else if (sscanf(line, "version %d", &number) == 1)
            *version = number;
    }
    fclose(file);
    return true;
}

static bool WriteRecord(u64 host, int version)
{
    FILE *file = fopen(RECORD_TMP, "wb");
    if (file == NULL)
        return false;
    fprintf(file, "# Start with the console (3DS Achievements): Luma starts the tracker with this system module,\n"
                  "# through /luma/titles/%016llX/exheader.bin. Deleting both turns it off.\n"
                  "host %016llX\nversion %d\n", (unsigned long long)host, (unsigned long long)host, version);
    bool ok = fclose(file) == 0;
    remove(RECORD_PATH);
    if (!ok || rename(RECORD_TMP, RECORD_PATH) != 0)
    {
        remove(RECORD_TMP);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
// The setting
//------------------------------------------------------------------------------

static Boot s_boot;

// Keeps module host (its own title ID: program) as the one starting the tracker.
static Result Keep(u64 program)
{
    int version = Sys_TitleVersion(program);
    if (!WriteRecord(program, version))
    {
        Log("couldn't write " AUTOSTART_RECORD);
        return NOT_WRITTEN;
    }
    Log("kept: %016llX, version %d", (unsigned long long)program, version);
    return 0;
}

static Result TurnOn(Handle loader)
{
    Reader  reader = { loader, Sys_IsNew3ds() };
    u64     ns = Sys_NsTitle(), host = 0;
    u8      exheader[AUTOSTART_EXHEADER_SIZE], check[AUTOSTART_EXHEADER_SIZE];
    BootTry attempt;
    Result  res;

    Log("%s 3DS", reader.n3ds ? "New" : "Old");
    Boot_Read(&s_boot, ns, reader.n3ds, ReadModule, &reader);
    LogBoot(&s_boot);
    Boot_Try(&s_boot, 0, 0, &attempt);
    if (!attempt.ok)
        return AUTOSTART_NO_HOST;

    // Already started by a module (a file from before, its record gone): keep that one.
    host = Boot_FindHost(&s_boot, TRACKER_TID);
    if (host != 0)
    {
        Log("the tracker already starts with %016llX", (unsigned long long)host);
        res = ReadExheader(&reader, host, exheader);
        if (R_FAILED(res))
            return res;
        u64 program = ReadU64(exheader + PROGRAM_ID_OFFSET);
        Boot_Try(&s_boot, 0, TRACKER_TID, &attempt);
        LogTry("as it is", &attempt);
        if (!attempt.ok)
        {
            Log("not safe: removed");
            RemoveCopy(program);    // (only the app's own)
            return AUTOSTART_NO_HOST;
        }
        return Keep(program);
    }

    // NS's first dependency is left for 3ds-recorder's Launch On Startup, which uses it.
    u64 avoid = s_boot.nsCount > 0 ? s_boot.nsDeps[0] : 0;
    if (!Boot_Choose(&s_boot, TRACKER_TID, avoid, &host, &attempt))
    {
        Log("no module can start the tracker safely");
        return AUTOSTART_NO_HOST;
    }
    Log("host %016llX", (unsigned long long)host);
    LogTry("planned", &attempt);

    res = ReadExheader(&reader, host, exheader);
    if (R_FAILED(res))
        return res;
    u64 program = ReadU64(exheader + PROGRAM_ID_OFFSET);
    if (Replaced(program) || !InsertTracker(exheader))
        return AUTOSTART_NO_HOST;
    res = WriteCopy(program, exheader);
    if (R_FAILED(res))
        return res;

    // Luma uses it: the loader gives the new file now.
    res = ReadExheader(&reader, host, check);
    if (R_FAILED(res) || memcmp(check, exheader, sizeof(check)) != 0)
    {
        Log("the loader doesn't give the new file (%08lX)", (unsigned long)(u32)res);
        RemoveCopy(program);
        return R_FAILED(res) ? res : AUTOSTART_NOT_USED;
    }

    // The boot worked out again from what the loader gives now: as planned, and safe.
    Boot_Read(&s_boot, ns, reader.n3ds, ReadModule, &reader);
    Boot_Try(&s_boot, 0, TRACKER_TID, &attempt);
    LogTry("read again", &attempt);
    if (!attempt.ok || Boot_FindHost(&s_boot, TRACKER_TID) != host)
    {
        Log("not as planned: removed");
        RemoveCopy(program);
        return AUTOSTART_NO_HOST;
    }
    res = Keep(program);
    if (R_FAILED(res))
        RemoveCopy(program);
    return res;
}

bool Autostart_On(void)
{
    return Exists(RECORD_PATH);
}

bool Autostart_Usable(void)
{
    u32 version, config;
    return R_SUCCEEDED(Sys_LumaInfo(&version, &config)) && (config & LUMA_MODULES) && (config & LUMA_PATCHING);
}

Result Autostart_Enable(void)
{
    u32     version = 0, config = 0;
    Handle  loader = 0;
    Result  res = Sys_LumaInfo(&version, &config);

    OpenLog("w");
    Log("turning on: Luma %lu.%lu.%lu, config %08lX (%08lX)", (unsigned long)(version >> 24),
        (unsigned long)((version >> 16) & 0xFF), (unsigned long)((version >> 8) & 0xFF), (unsigned long)config,
        (unsigned long)(u32)res);
    remove(TRY_PATH);       // a mark left from before would count as a failed start
    if (R_FAILED(res) || (version >> 24) < LUMA_MIN_MAJOR)
        res = AUTOSTART_OLD_LUMA;
    else if (!(config & LUMA_MODULES))
        res = AUTOSTART_NO_MODULES;
    else if (!(config & LUMA_PATCHING))
        res = AUTOSTART_NO_PATCHING;
    else if (!Exists(TRACKER_CXI))
        res = AUTOSTART_NO_TRACKER;
    else if (Autostart_On())
        res = 0;
    else if (R_FAILED(res = Sys_LoaderOpen(&loader)))
        Log("couldn't open the loader");
    else
    {
        res = TurnOn(loader);
        Sys_LoaderClose(loader);
    }
    if (R_SUCCEEDED(res))
        Log("on");
    else
        Log("not turned on: %08lX", (unsigned long)(u32)res);
    CloseLog();
    return res;
}

Result Autostart_Disable(void)
{
    u64     host = 0;
    int     version;
    Result  res = 0;

    OpenLog("a");
    Log("turning off");
    if (ReadRecord(&host, &version) && host != 0)
        res = RemoveCopy(host);
    if (R_SUCCEEDED(res))
        res = RemoveAllCopies();    // any other copy that starts it (its record lost)
    if (R_SUCCEEDED(res) && remove(RECORD_PATH) != 0 && Exists(RECORD_PATH))
        res = NOT_WRITTEN;
    remove(TRY_PATH);
    if (R_SUCCEEDED(res))
        Log("off");
    else
        Log("not turned off: %08lX", (unsigned long)(u32)res);
    CloseLog();
    return res;
}

Result Autostart_Check(void)
{
    u64     host;
    int     version;
    char    path[80];

    if (Exists(FAILED_PATH))
    {
        // The tracker turned it off (and removed the file). Whatever it left, cleaned up here.
        OpenLog("a");
        Log("the tracker turned it off: a start of the 3DS that started it didn't reach the HOME Menu");
        CloseLog();
        Autostart_Disable();
        remove(FAILED_PATH);
        return AUTOSTART_BOOT_FAILED;
    }
    if (!ReadRecord(&host, &version) || host == 0)
        return 0;
    TitlePath(path, sizeof(path), host, "exheader.bin");
    if (!Exists(path))
    {
        remove(RECORD_PATH);    // its file was deleted: it's off
        return 0;
    }

    // A system update changed the module: the copy of its old exheader mustn't stay (the
    // module's new code with its old layout). Turned off; turning it on again makes a new one.
    int now = Sys_TitleVersion(host);
    if (version >= 0 && now >= 0 && now != version)
    {
        OpenLog("a");
        Log("%016llX is version %d now, was %d: a system update", (unsigned long long)host, now, version);
        CloseLog();
        Autostart_Disable();
        return AUTOSTART_UPDATED;
    }
    return 0;
}
