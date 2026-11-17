#include "update.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <curl/curl.h>

#include "hero.h"
#include "update_release.h"
#include "update_tree.h"
#include "update_zip.h"

#ifndef APP_VERSION
#error "APP_VERSION comes from the Makefile's VERSION"
#endif

#define OWNER_REPO     "LiamTrader43/3ds-Achievements"
#define BRANCH          "main"
#define TREE_URL        "https://api.github.com/repos/" OWNER_REPO "/git/trees/" BRANCH "?recursive=1"
#define RAW_URL         "https://raw.githubusercontent.com/" OWNER_REPO "/" BRANCH "/achievements"
#define RELEASE_URL     "https://api.github.com/repos/" OWNER_REPO "/releases/latest"
#define DOWNLOAD_PREFIX "https://github.com/" OWNER_REPO "/releases/download/"     // the only place the zip may come from
#define DIRECTORY       "/achievements"
#define VERSIONS_PATH   DIRECTORY "/versions.txt"       // the GitHub ID of each file on the card
#define LOG_PATH        DIRECTORY "/update.log"         // what the last check found
#define BUFFER_SIZE     (320 * 1024)
_Static_assert(HERO_BYTES < BUFFER_SIZE, "a background must fit in the download buffer");
_Static_assert(UPDATE_STORE_MOST < BUFFER_SIZE, "so must a store file");
#define STACK_SIZE      0x20000     // the TLS handshake needs a lot of stack
#define SOC_BUFFER_SIZE 0x100000
#define CONNECT_SECONDS 15L
#define TRANSFER_SECONDS 120L
#define STOP_WAIT_NS    3000000000LL

// The app's own update: the release zip and what's taken from it (the paths Universal-Updater
// uses too). The tracker and the app are only replaced once the new copies are complete and
// checked, and the zip is deleted afterwards.
#define APP_ZIP_PATH    DIRECTORY "/app-update.zip"
#define ZIP_SECONDS     900L        // a whole release on slow Wi-Fi
#define STALL_SECONDS   30L         // but not one that has stopped coming
#define FILE_BUFFER     (64 * 1024)
#define PATH_CAPACITY   256
#define TRACKER_ENTRY   "luma/sysmodules/0004013000AC4102.cxi"
#define TRACKER_PATH    "sdmc:/" TRACKER_ENTRY
#define APP_3DSX_ENTRY  "3ds/achievements-viewer.3dsx"
#define APP_3DSX_PATH   "sdmc:/" APP_3DSX_ENTRY             // if the Homebrew Launcher didn't say
#define APP_CIA_ENTRY   "cias/achievements.cia"
#define STORE_PREFIX    "achievements/store/"                // the store's files (store_file.h), in the zip
#define STORE_DIR       "sdmc:/achievements/store"           // and on the SD card
#define STORE_MOST      32

#define ERROR_BAD_STATUS    ((Result)0xE0000001)
#define ERROR_TOO_BIG       ((Result)0xE0000002)
#define ERROR_MISMATCH      ((Result)0xE0000003)        // GitHub's copies are still catching up
#define ERROR_WRITE         ((Result)0xE0000004)
#define ERROR_THREAD        ((Result)0xE0000005)
#define ERROR_NO_CURL       ((Result)0xE0000006)
#define ERROR_NO_ENTRY      ((Result)0xE0000007)        // the release zip lacks a file the update needs
#define ERROR_APP_PATH      ((Result)0xE0000008)        // where the running .3dsx is isn't known
#define ERROR_NO_RELEASE    ((Result)0xE0000009)        // no newer release was found
#define ERROR_CURL(code)    ((Result)(0xE1000000u | (u32)(code)))
#define ERROR_ZIP(code)     ((Result)(0xE2000000u | (u32)(code)))

typedef enum { STATE_IDLE, STATE_CHECKING, STATE_CHECKED, STATE_REPORTED } State;

static u8               s_buffer[BUFFER_SIZE];
static UpdateFile       s_files[UPDATE_MAX_FILES];
static int              s_fileCount;
static Thread           s_thread;
static u32             *s_socBuffer;
static bool             s_socReady, s_sslReady, s_curlReady;
static volatile bool    s_abort;
static volatile bool    s_downloadFinished;
static volatile int     s_downloadDone;
static volatile Result  s_downloadResult;
static char             s_error[CURL_ERROR_SIZE];
static volatile State   s_state;
static Result           s_checkResult;
static u32              s_checkStatus, s_checkSize;
static int              s_checkParsed;

// The latest release, and the app's own update.
static UpdateRelease    s_release;
static bool             s_releaseParsed, s_appNewer;
static Result           s_releaseResult;
static u32              s_releaseStatus;
static char             s_releaseError[CURL_ERROR_SIZE];
static char             s_appPath[PATH_CAPACITY];       // the running .3dsx, or "" if not known
static volatile u32     s_zipBytes;                     // downloaded so far
static volatile bool    s_installing;                   // the download is done, the files are being replaced
static volatile Result  s_appResult;
static bool             s_trackerChanged, s_changed;

typedef struct
{
    u32             used;
    u32             limit;      // the most bytes taken
    bool            overflow;
    FILE           *file;       // where the body goes; NULL = s_buffer
    UpdateSha256   *sha;        // and its hash, for a file
    bool            failed;     // writing the file failed
} Sink;

static size_t WriteBody(char *data, size_t size, size_t count, void *user)
{
    Sink  *sink = user;
    size_t bytes = size * count;

    if (sink->used + bytes > sink->limit)
    {
        sink->overflow = true;
        return 0;                   // stops the transfer
    }
    if (sink->file != NULL)
    {
        if (fwrite(data, 1, bytes, sink->file) != bytes)
        {
            sink->failed = true;
            return 0;
        }
        UpdateSha256_Add(sink->sha, data, bytes);
        s_zipBytes = sink->used + (u32)bytes;
    }
    else
        memcpy(s_buffer + sink->used, data, bytes);
    sink->used += (u32)bytes;
    return bytes;
}

static int KeepGoing(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)user; (void)a; (void)b; (void)c; (void)d;
    return s_abort ? 1 : 0;
}

// Fetches url into the sink; status gets the HTTP status and error curl's message.
static Result Http(const char *url, Sink *sink, long seconds, u32 *status, char *error)
{
    long code = 0;

    error[0] = '\0';
    CURL *curl = curl_easy_init();
    if (curl == NULL)
        return ERROR_NO_CURL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, sink);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "3DS-Achievements");      // GitHub requires one
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, seconds);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);                // nothing at all for STALL_SECONDS
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, STALL_SECONDS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);                 // no certificate list on the card
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, KeepGoing);

    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    *status = (u32)code;
    if (sink->overflow)
        return ERROR_TOO_BIG;
    if (sink->failed)
        return ERROR_WRITE;
    if (result != CURLE_OK)
        return ERROR_CURL(result);
    if (code != 200)
        return ERROR_BAD_STATUS;
    return 0;
}

// Fetches url into s_buffer (with room for a '\0' after it).
static Result HttpGet(const char *url, u32 *size, u32 *status, char *error)
{
    Sink   sink = { 0, BUFFER_SIZE - 1, false, NULL, NULL, false };
    Result res = Http(url, &sink, TRANSFER_SECONDS, status, error);

    *size = R_SUCCEEDED(res) ? sink.used : 0;
    return res;
}

static bool FileExists(const char *path);
static void FilePath(const UpdateFile *f, const char *suffix, char *path, size_t capacity);

// Whether the card's copy of f is the file GitHub lists (read whole into s_buffer, its Git ID
// worked out).
static bool SameAsCard(const UpdateFile *f)
{
    char path[80];
    FilePath(f, "", path, sizeof(path));
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    size_t size = fread(s_buffer, 1, BUFFER_SIZE, file);
    fclose(file);
    return size == f->size && UpdateTree_Matches(s_buffer, (uint32_t)size, f->sha);
}

static void CheckThread(void *arg)
{
    u32 size;

    (void)arg;
    s_checkResult = HttpGet(TREE_URL, &size, &s_checkStatus, s_error);
    s_checkSize = size;
    if (R_SUCCEEDED(s_checkResult))
    {
        s_buffer[size] = '\0';
        s_checkParsed = UpdateTree_Parse((const char *)s_buffer, s_files, UPDATE_MAX_FILES);
        s_fileCount = s_checkParsed > 0 ? s_checkParsed : 0;
    }

    // The store's files are compared with the card's copies themselves (they also come in the
    // release zip, which doesn't note them in versions.txt).
    for (int i = 0; i < s_fileCount && !s_abort; i++)
        if (s_files[i].store)
            s_files[i].wanted = !SameAsCard(&s_files[i]);

    // Then whether there's a newer app.
    if (!s_abort)
    {
        s_releaseResult = HttpGet(RELEASE_URL, &size, &s_releaseStatus, s_releaseError);
        if (R_SUCCEEDED(s_releaseResult))
        {
            s_buffer[size] = '\0';
            s_releaseParsed = UpdateRelease_Parse((const char *)s_buffer, DOWNLOAD_PREFIX, &s_release);
            s_appNewer = s_releaseParsed && UpdateRelease_Compare(s_release.version, APP_VERSION) > 0;
        }
    }
    s_state = STATE_CHECKED;
}

// Where the running .3dsx is: what the Homebrew Launcher passed, if it's a .3dsx on the SD
// card, else the usual place if there's one there.
static void FindAppPath(const char *path)
{
    size_t length = path != NULL ? strlen(path) : 0;

    s_appPath[0] = '\0';
    if (length > 11 && length < sizeof(s_appPath) && strncmp(path, "sdmc:/", 6) == 0 &&
        strcasecmp(path + length - 5, ".3dsx") == 0 && FileExists(path))
        strcpy(s_appPath, path);
    else if (FileExists(APP_3DSX_PATH))
        strcpy(s_appPath, APP_3DSX_PATH);
}

const char *Update_AppVersion(void)
{
    return APP_VERSION;
}

void Update_Start(const char *appPath)
{
    s32 priority = 0x30;

    if (envIsHomebrew())
        FindAppPath(appPath);
    s_socBuffer = memalign(0x1000, SOC_BUFFER_SIZE);
    if (s_socBuffer == NULL || R_FAILED(socInit(s_socBuffer, SOC_BUFFER_SIZE)))
        return;
    s_socReady = true;
    if (R_FAILED(sslcInit(0)))          // the TLS library's random numbers come from here
        return;
    s_sslReady = true;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return;
    s_curlReady = true;

    s_state = STATE_CHECKING;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_thread = threadCreate(CheckThread, NULL, STACK_SIZE, priority + 1, -2, false);
    if (s_thread == NULL)
        s_state = STATE_REPORTED;
}

static void FilePath(const UpdateFile *f, const char *suffix, char *path, size_t capacity)
{
    snprintf(path, capacity, DIRECTORY "/%.16s/%.23s%s", f->titleId, f->name, suffix);
}

static bool FileExists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

// True if versions.txt says the card has this exact version of the file.
static bool VersionMatches(const UpdateFile *f)
{
    char  wanted[80], line[80];
    bool  found = false;

    snprintf(wanted, sizeof(wanted), "%.16s/%.15s %.40s", f->titleId, f->name, f->sha);
    FILE *file = fopen(VERSIONS_PATH, "r");
    if (file == NULL)
        return false;
    while (!found && fgets(line, sizeof(line), file) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        found = strcmp(line, wanted) == 0;
    }
    fclose(file);
    return found;
}

// Records that the card now has this version, replacing any older line for the file.
static void SaveVersion(const UpdateFile *f)
{
    static char kept[4096];
    char   prefix[40], line[80];
    size_t used = 0;

    snprintf(prefix, sizeof(prefix), "%.16s/%.15s ", f->titleId, f->name);
    FILE *file = fopen(VERSIONS_PATH, "r");
    if (file != NULL)
    {
        while (fgets(line, sizeof(line), file) != NULL)
        {
            size_t length = strlen(line);
            if (strncmp(line, prefix, strlen(prefix)) != 0 && used + length < sizeof(kept))
            {
                memcpy(kept + used, line, length);
                used += length;
            }
        }
        fclose(file);
    }

    file = fopen(VERSIONS_PATH, "w");
    if (file == NULL)
        return;
    fwrite(kept, 1, used, file);
    fprintf(file, "%s%s\n", prefix, f->sha);
    fclose(file);
}

bool Update_Poll(int *newGames, int *updatedGames, int *storeFiles, const char **appVersion)
{
    if (s_state != STATE_CHECKED)
        return false;
    s_state = STATE_REPORTED;
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = NULL;

    *appVersion = s_appNewer ? s_release.version : NULL;
    *newGames = *updatedGames = *storeFiles = 0;
    for (int i = 0; i < s_fileCount; i++)
    {
        char path[80];
        FilePath(&s_files[i], "", path, sizeof(path));
        if (s_files[i].store)
            *storeFiles += s_files[i].wanted ? 1 : 0;       // (already compared: CheckThread)
        else
            s_files[i].wanted = !FileExists(path) || !VersionMatches(&s_files[i]);
    }

    // Count games, not files. Another release of a game (a folder with only a
    // version.txt) isn't a game of its own: it counts as an update, and only if
    // nothing else does (its game may be new or updated in the same download).
    bool releaseWanted = false;
    for (int i = 0; i < s_fileCount; i++)
    {
        bool first = s_files[i].wanted && !s_files[i].store;
        for (int j = 0; j < i && first; j++)
            if (s_files[j].wanted && strcmp(s_files[j].titleId, s_files[i].titleId) == 0)
                first = false;
        if (!first)
            continue;

        bool hasDefinitions = false;
        for (int j = 0; j < s_fileCount; j++)
            if (strcmp(s_files[j].titleId, s_files[i].titleId) == 0 && strcmp(s_files[j].name, "definitions.txt") == 0)
                hasDefinitions = true;
        if (!hasDefinitions)
        {
            releaseWanted = true;
            continue;
        }

        char path[64];
        snprintf(path, sizeof(path), DIRECTORY "/%.16s/definitions.txt", s_files[i].titleId);
        if (FileExists(path))
            (*updatedGames)++;
        else
            (*newGames)++;
    }
    if (releaseWanted && *newGames + *updatedGames == 0)
        *updatedGames = 1;

    FILE *log = fopen(LOG_PATH, "w");
    if (log != NULL)
    {
        int wanted = 0;
        for (int i = 0; i < s_fileCount; i++)
            wanted += s_files[i].wanted ? 1 : 0;
        fprintf(log, "check: result %08lX, http %lu, %lu bytes, %d files listed, %d to download, %d new games, %d updated, "
                "%d store files, error \"%s\"\n",
                (unsigned long)s_checkResult, (unsigned long)s_checkStatus, (unsigned long)s_checkSize, s_checkParsed, wanted,
                *newGames, *updatedGames, *storeFiles, s_error);
        fprintf(log, "release: result %08lX, http %lu, %s, latest %s, this app %s (%s), %s, error \"%s\"\n",
                (unsigned long)(u32)s_releaseResult, (unsigned long)s_releaseStatus, s_releaseParsed ? "read" : "not read",
                s_releaseParsed ? s_release.version : "?", APP_VERSION,
                !envIsHomebrew() ? "CIA" : s_appPath[0] != '\0' ? s_appPath : ".3dsx, path unknown",
                s_appNewer ? "update offered" : "no update", s_releaseError);
        fclose(log);
    }
    return true;
}

// Fetches one file; it only replaces the old one once it's fully here and verified.
static Result DownloadFile(const UpdateFile *f)
{
    char url[160], path[80], temporary[88], folder[40];
    u32  size;

    snprintf(url, sizeof(url), RAW_URL "/%.16s/%.23s", f->titleId, f->name);
    Result res = HttpGet(url, &size, &s_checkStatus, s_error);
    if (R_FAILED(res))
        return res;
    if (size != f->size || !UpdateTree_Matches(s_buffer, size, f->sha))
        return ERROR_MISMATCH;

    snprintf(folder, sizeof(folder), DIRECTORY "/%.16s", f->titleId);
    mkdir(folder, 0777);
    FilePath(f, "", path, sizeof(path));
    FilePath(f, ".tmp", temporary, sizeof(temporary));

    FILE *file = fopen(temporary, "wb");
    if (file == NULL)
        return ERROR_WRITE;
    bool written = fwrite(s_buffer, 1, size, file) == size;
    fclose(file);
    if (!written)
    {
        remove(temporary);
        return ERROR_WRITE;
    }
    remove(path);
    if (rename(temporary, path) != 0)
        return ERROR_WRITE;
    if (!f->store)
        SaveVersion(f);
    return 0;
}

static void DownloadThread(void *arg)
{
    (void)arg;
    s_downloadResult = 0;
    for (int i = 0; i < s_fileCount && !s_abort; i++)
    {
        if (!s_files[i].wanted)
            continue;
        Result res = DownloadFile(&s_files[i]);
        if (R_FAILED(res))
        {
            FILE *log = fopen(LOG_PATH, "a");
            if (log != NULL)
            {
                fprintf(log, "download %.16s/%.23s: result %08lX, http %lu, error \"%s\"\n", s_files[i].titleId, s_files[i].name,
                        (unsigned long)(u32)res, (unsigned long)s_checkStatus, s_error);
                fclose(log);
            }
            s_downloadResult = res;
            break;
        }
        s_files[i].wanted = false;
        s_downloadDone++;
    }
    s_downloadFinished = true;
}

Result Update_Download(void (*progress)(int done, int total))
{
    s32 priority = 0x30;
    int total = 0;

    for (int i = 0; i < s_fileCount; i++)
        total += s_files[i].wanted ? 1 : 0;

    s_downloadDone = 0;
    s_downloadFinished = false;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_thread = threadCreate(DownloadThread, NULL, STACK_SIZE, priority + 1, -2, false);
    if (s_thread == NULL)
        return ERROR_THREAD;

    while (!s_downloadFinished)
        progress(s_downloadDone, total);    // draws a frame, so this waits for the screen

    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = NULL;
    return s_downloadResult;
}

// ---- The app's own update

static void AppLog(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void AppLog(const char *format, ...)
{
    va_list arguments;
    FILE   *log = fopen(LOG_PATH, "a");

    if (log == NULL)
        return;
    va_start(arguments, format);
    vfprintf(log, format, arguments);
    va_end(arguments);
    fputc('\n', log);
    fclose(log);
}

static bool WriteToFile(void *user, const void *data, size_t size)
{
    return fwrite(data, 1, size, (FILE *)user) == size;
}

// Unpacks the entry to path + ".tmp", checked; nothing is left behind if it fails.
static Result ExtractBeside(FILE *zip, const UpdateZipEntry *entry, const char *path)
{
    char temporary[PATH_CAPACITY + 8];

    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (file == NULL)
        return ERROR_WRITE;
    setvbuf(file, NULL, _IOFBF, FILE_BUFFER);
    int  result = UpdateZip_Extract(zip, entry, WriteToFile, file);
    bool closed = fclose(file) == 0;
    if (result == 0 && !closed)
        result = UPDATE_ZIP_WRITE;
    if (result != 0)
    {
        remove(temporary);
        return result == UPDATE_ZIP_WRITE ? ERROR_WRITE : ERROR_ZIP(result);
    }
    return 0;
}

// Puts path + ".tmp" in place of path. The old file is only deleted once the new one is in
// place, and is put back if that fails.
static Result Replace(const char *path)
{
    char temporary[PATH_CAPACITY + 8], old[PATH_CAPACITY + 8];

    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    snprintf(old, sizeof(old), "%s.old", path);
    remove(old);
    bool had = FileExists(path);
    if (had && rename(path, old) != 0)
    {
        remove(temporary);
        return ERROR_WRITE;
    }
    if (rename(temporary, path) != 0)
    {
        if (had)
            rename(old, path);
        remove(temporary);
        return ERROR_WRITE;
    }
    remove(old);
    return 0;
}

typedef struct
{
    Handle  handle;
    u64     offset;
} CiaSink;

static bool WriteToCia(void *user, const void *data, size_t size)
{
    CiaSink *sink = user;
    u32      written = 0;

    if (R_FAILED(FSFILE_Write(sink->handle, &written, sink->offset, data, (u32)size, 0)) || written != size)
        return false;
    sink->offset += size;
    return true;
}

// Installs the CIA in the zip over the app's own (on the SD card). The system only takes it
// once it's all there; anything wrong before then cancels it and leaves the old app.
static Result InstallCia(FILE *zip, const UpdateZipEntry *entry)
{
    Result res = amInit();
    if (R_FAILED(res))
        return res;

    Handle handle;
    res = AM_StartCiaInstall(MEDIATYPE_SD, &handle);
    if (R_SUCCEEDED(res))
    {
        CiaSink sink = { handle, 0 };
        int     result = UpdateZip_Extract(zip, entry, WriteToCia, &sink);
        if (result != 0)
        {
            AM_CancelCIAInstall(handle);
            res = result == UPDATE_ZIP_WRITE ? ERROR_WRITE : ERROR_ZIP(result);
        }
        else
            res = AM_FinishCiaInstall(handle);
    }
    amExit();
    return res;
}

// Takes the store's files (STORE_PREFIX) out of a release zip, each one that differs from the
// SD card's written beside it and put in its place. *found: how many the zip has, *updated:
// how many were written. Keeps going past a failure; returns the first.
static Result ExtractStoreFiles(FILE *zip, int *found, int *updated)
{
    static UpdateZipItem items[STORE_MOST];
    Result res = 0;

    *found = *updated = 0;
    int count = UpdateZip_List(zip, STORE_PREFIX, items, STORE_MOST);
    if (count < 0)
        return ERROR_ZIP(UPDATE_ZIP_FORMAT);
    *found = count;
    mkdir(STORE_DIR, 0777);
    for (int i = 0; i < count; i++)
    {
        // Only a store file's name, right in the store's folder.
        const char *name = items[i].name + strlen(STORE_PREFIX);
        char path[PATH_CAPACITY];
        snprintf(path, sizeof(path), STORE_DIR "/%s", name);
        if (!UpdateTree_StoreName(name) || UpdateZip_Same(path, &items[i].entry))
            continue;
        Result one = ExtractBeside(zip, &items[i].entry, path);
        if (R_SUCCEEDED(one))
            one = Replace(path);
        if (R_SUCCEEDED(one))
            (*updated)++;
        else if (R_SUCCEEDED(res))
            res = one;
    }
    return res;
}

// Takes the tracker and the app out of the downloaded zip. New copies are written beside the
// old files and checked first; then the tracker is replaced, then the app. In that order a
// failure halfway leaves an old app, which offers the update again next time.
static Result InstallFromZip(void)
{
    const bool      homebrew = envIsHomebrew();
    UpdateZipEntry  tracker, app;
    Result          res = 0;

    FILE *zip = fopen(APP_ZIP_PATH, "rb");
    if (zip == NULL)
        return ERROR_ZIP(UPDATE_ZIP_READ);
    if (!UpdateZip_Find(zip, TRACKER_ENTRY, &tracker) || !UpdateZip_Find(zip, homebrew ? APP_3DSX_ENTRY : APP_CIA_ENTRY, &app))
        res = ERROR_NO_ENTRY;
    else if (homebrew && s_appPath[0] == '\0')
        res = ERROR_APP_PATH;

    bool newTracker = R_SUCCEEDED(res) && !UpdateZip_Same(TRACKER_PATH, &tracker);
    bool newApp = R_SUCCEEDED(res) && (!homebrew || !UpdateZip_Same(s_appPath, &app));
    if (newTracker)
    {
        mkdir("sdmc:/luma", 0777);
        mkdir("sdmc:/luma/sysmodules", 0777);
        res = ExtractBeside(zip, &tracker, TRACKER_PATH);
    }
    if (R_SUCCEEDED(res) && newApp && homebrew)
        res = ExtractBeside(zip, &app, s_appPath);

    if (R_SUCCEEDED(res) && newTracker)
    {
        res = Replace(TRACKER_PATH);
        s_trackerChanged = s_changed = R_SUCCEEDED(res);
    }
    if (R_SUCCEEDED(res) && newApp)
    {
        res = homebrew ? Replace(s_appPath) : InstallCia(zip, &app);
        s_changed = s_changed || R_SUCCEEDED(res);
    }
    // Then the store's files in the zip (newer ones also come from the repository like games'
    // files); a failure here isn't the update's: the next check fetches what's missing.
    if (R_SUCCEEDED(res))
    {
        int found, updated;
        Result store = ExtractStoreFiles(zip, &found, &updated);
        AppLog("store files: %d in the zip, %d written, result %08lX", found, updated, (unsigned long)(u32)store);
    }
    fclose(zip);

    if (R_FAILED(res))
    {
        char temporary[PATH_CAPACITY + 8];
        remove(TRACKER_PATH ".tmp");
        snprintf(temporary, sizeof(temporary), "%s.tmp", s_appPath);
        if (homebrew && s_appPath[0] != '\0')
            remove(temporary);
    }
    AppLog("app install: result %08lX, tracker %s, app %s (%s)", (unsigned long)(u32)res,
           newTracker ? (s_trackerChanged ? "replaced" : "not replaced") : "unchanged",
           newApp ? (R_SUCCEEDED(res) ? "replaced" : "not replaced") : "unchanged",
           homebrew ? s_appPath : "CIA");
    return res;
}

// Downloads release's zip to path and checks it against what GitHub says it is (its size and,
// if GitHub gives it, its SHA-256). *bytes, *status, hash (65 chars) and error say how it went.
static Result DownloadZip(const UpdateRelease *release, const char *path, u32 *bytes, u32 *status, char *hash, char *error)
{
    UpdateSha256 sha;

    hash[0] = '\0';
    *bytes = *status = 0;
    UpdateSha256_Start(&sha);
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return ERROR_WRITE;
    setvbuf(file, NULL, _IOFBF, FILE_BUFFER);
    Sink   sink = { 0, release->size, false, file, &sha, false };
    Result res = Http(release->url, &sink, ZIP_SECONDS, status, error);
    if (fclose(file) != 0 && R_SUCCEEDED(res))
        res = ERROR_WRITE;
    UpdateSha256_Finish(&sha, hash);
    *bytes = sink.used;
    if (R_SUCCEEDED(res) && (sink.used != release->size || (release->sha256[0] != '\0' && strcmp(hash, release->sha256) != 0)))
        res = ERROR_MISMATCH;
    return res;
}

// Downloads the release zip, checks it against what GitHub says it is, then installs it.
static Result InstallApp(void)
{
    char hash[65];
    u32  status, bytes;

    Result res = DownloadZip(&s_release, APP_ZIP_PATH, &bytes, &status, hash, s_releaseError);
    AppLog("app download %s: result %08lX, http %lu, %lu of %lu bytes, sha256 %s, GitHub's %s, error \"%s\"", s_release.version,
           (unsigned long)(u32)res, (unsigned long)status, (unsigned long)bytes, (unsigned long)s_release.size, hash,
           s_release.sha256[0] != '\0' ? s_release.sha256 : "not given", s_releaseError);

    if (R_SUCCEEDED(res))
    {
        s_installing = true;
        res = InstallFromZip();
    }
    remove(APP_ZIP_PATH);
    return res;
}

static void AppThread(void *arg)
{
    (void)arg;
    s_appResult = InstallApp();
    s_downloadFinished = true;
}

Result Update_InstallApp(void (*progress)(int done, int total), bool *trackerChanged, bool *changed)
{
    s32 priority = 0x30;

    *trackerChanged = *changed = false;
    if (!s_appNewer)
        return ERROR_NO_RELEASE;
    s_zipBytes = 0;
    s_installing = false;
    s_trackerChanged = s_changed = false;
    s_downloadFinished = false;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_thread = threadCreate(AppThread, NULL, STACK_SIZE, priority + 1, -2, false);
    if (s_thread == NULL)
        return ERROR_THREAD;

    const int total = (int)((s_release.size + 1023) / 1024);
    while (!s_downloadFinished)                 // each call draws a frame, so this waits for the screen
        progress(s_installing ? 0 : (int)((s_zipBytes + 1023) / 1024), s_installing ? 0 : total);

    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = NULL;
    *trackerChanged = s_trackerChanged;
    *changed = s_changed;
    if (R_SUCCEEDED(s_appResult))
        s_appNewer = false;
    return s_appResult;
}

// ---- Starting the new version

// Luma's 3dsx loader, the "hb:ldr" port: the Homebrew Launcher opens a .3dsx by naming it here
// (command 2: its path on the SD card, without "sdmc:"; command 3: its argument list, a u32
// count then the strings) and closing. A .3dsx closing restarts the title it runs in (libctru
// chainloads to itself), and the loader then loads what was named, or boot.3dsx (the Homebrew
// Launcher) when nothing was: it forgets the name after each load.
#define HBLDR_PORT          "hb:ldr"
#define HBLDR_SET_TARGET    2
#define HBLDR_SET_ARGV      3
#define HBLDR_ARGV_SIZE     0x400
_Static_assert(4 + PATH_CAPACITY <= HBLDR_ARGV_SIZE, "the argument list holds the app's path");

static Result HbldrSend(Handle port, u32 command, const void *buffer, size_t size, int staticBuffer)
{
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(command, 0, 2);
    cmdbuf[1] = IPC_Desc_StaticBuffer(size, staticBuffer);
    cmdbuf[2] = (u32)buffer;
    Result res = svcSendSyncRequest(port);
    return R_SUCCEEDED(res) ? (Result)cmdbuf[1] : res;
}

bool Update_CanRestart(void)
{
    Handle port;

    if (!envIsHomebrew())
        return true;
    if (s_appPath[0] == '\0' || R_FAILED(svcConnectToPort(&port, HBLDR_PORT)))
        return false;
    svcCloseHandle(port);
    return true;
}

bool Update_Restart(void)
{
    static u32 argv[HBLDR_ARGV_SIZE / 4];
    Handle     port;
    Result     res = ERROR_APP_PATH;

    if (!envIsHomebrew())
    {
        aptSetChainloaderToSelf();          // the title starts again: the new version, installed over it
        AppLog("app restart: CIA");
        return true;
    }

    if (s_appPath[0] != '\0' && R_SUCCEEDED(res = svcConnectToPort(&port, HBLDR_PORT)))
    {
        const char *target = s_appPath + 5;     // "sdmc:/3ds/..." -> "/3ds/..."
        memset(argv, 0, sizeof(argv));
        argv[0] = 1;                            // argv[0] is the whole path, as the Homebrew Launcher gives it
        strcpy((char *)&argv[1], s_appPath);
        res = HbldrSend(port, HBLDR_SET_TARGET, target, strlen(target) + 1, 0);
        if (R_SUCCEEDED(res))
            res = HbldrSend(port, HBLDR_SET_ARGV, argv, sizeof(argv), 1);
        svcCloseHandle(port);
    }
    AppLog("app restart: %s, result %08lX", s_appPath[0] != '\0' ? s_appPath : ".3dsx, path unknown",
           (unsigned long)(u32)res);
    return R_SUCCEEDED(res);
}

bool Update_NetworkReady(void)
{
    return s_socReady;
}

void Update_Stop(void)
{
    s_abort = true;
    if (s_thread != NULL)
        threadJoin(s_thread, STOP_WAIT_NS);
    if (s_curlReady)
        curl_global_cleanup();
    if (s_sslReady)
        sslcExit();
    if (s_socReady)
        socExit();
    free(s_socBuffer);
}
