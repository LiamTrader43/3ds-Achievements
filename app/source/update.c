#include "update.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <curl/curl.h>

#include "update_tree.h"

#define OWNER_REPO      "LiamTrader43/3ds-Achievements"
#define BRANCH          "main"
#define TREE_URL        "https://api.github.com/repos/" OWNER_REPO "/git/trees/" BRANCH "?recursive=1"
#define RAW_URL         "https://raw.githubusercontent.com/" OWNER_REPO "/" BRANCH "/achievements"
#define DIRECTORY       "/achievements"
#define VERSIONS_PATH   DIRECTORY "/versions.txt"       // the GitHub ID of each file on the card
#define LOG_PATH        DIRECTORY "/update.log"         // what the last check found
#define BUFFER_SIZE     (320 * 1024)
#define STACK_SIZE      0x20000     // the TLS handshake needs a lot of stack
#define SOC_BUFFER_SIZE 0x100000
#define CONNECT_SECONDS 15L
#define TRANSFER_SECONDS 120L
#define STOP_WAIT_NS    3000000000LL

#define ERROR_BAD_STATUS    ((Result)0xE0000001)
#define ERROR_TOO_BIG       ((Result)0xE0000002)
#define ERROR_MISMATCH      ((Result)0xE0000003)        // GitHub's copies are still catching up
#define ERROR_WRITE         ((Result)0xE0000004)
#define ERROR_THREAD        ((Result)0xE0000005)
#define ERROR_NO_CURL       ((Result)0xE0000006)
#define ERROR_CURL(code)    ((Result)(0xE1000000u | (u32)(code)))

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

typedef struct
{
    u32  used;
    bool overflow;
} Sink;

static size_t WriteBody(char *data, size_t size, size_t count, void *user)
{
    Sink  *sink = user;
    size_t bytes = size * count;

    if (sink->used + bytes > BUFFER_SIZE - 1)
    {
        sink->overflow = true;
        return 0;                   // stops the transfer
    }
    memcpy(s_buffer + sink->used, data, bytes);
    sink->used += (u32)bytes;
    return bytes;
}

static int KeepGoing(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)user; (void)a; (void)b; (void)c; (void)d;
    return s_abort ? 1 : 0;
}

static Result HttpGet(const char *url, u32 *size)
{
    Sink sink = { 0, false };
    long status = 0;

    *size = 0;
    s_error[0] = '\0';
    CURL *curl = curl_easy_init();
    if (curl == NULL)
        return ERROR_NO_CURL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, s_error);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "3DS-Achievements");      // GitHub requires one
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TRANSFER_SECONDS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);                 // no certificate list on the card
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, KeepGoing);

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    s_checkStatus = (u32)status;
    if (sink.overflow)
        return ERROR_TOO_BIG;
    if (code != CURLE_OK)
        return ERROR_CURL(code);
    if (status != 200)
        return ERROR_BAD_STATUS;
    *size = sink.used;
    return 0;
}

static void CheckThread(void *arg)
{
    u32 size;

    (void)arg;
    s_checkResult = HttpGet(TREE_URL, &size);
    s_checkSize = size;
    if (R_SUCCEEDED(s_checkResult))
    {
        s_buffer[size] = '\0';
        s_checkParsed = UpdateTree_Parse((const char *)s_buffer, s_files, UPDATE_MAX_FILES);
        s_fileCount = s_checkParsed > 0 ? s_checkParsed : 0;
    }
    s_state = STATE_CHECKED;
}

void Update_Start(void)
{
    s32 priority = 0x30;

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
    snprintf(path, capacity, DIRECTORY "/%.16s/%.15s%s", f->titleId, f->name, suffix);
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

bool Update_Poll(int *newGames, int *updatedGames)
{
    if (s_state != STATE_CHECKED)
        return false;
    s_state = STATE_REPORTED;
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = NULL;

    *newGames = *updatedGames = 0;
    for (int i = 0; i < s_fileCount; i++)
    {
        char path[64];
        FilePath(&s_files[i], "", path, sizeof(path));
        s_files[i].wanted = !FileExists(path) || !VersionMatches(&s_files[i]);
    }

    // Count games, not files.
    for (int i = 0; i < s_fileCount; i++)
    {
        bool first = s_files[i].wanted;
        for (int j = 0; j < i && first; j++)
            if (s_files[j].wanted && strcmp(s_files[j].titleId, s_files[i].titleId) == 0)
                first = false;
        if (!first)
            continue;

        char path[64];
        snprintf(path, sizeof(path), DIRECTORY "/%.16s/definitions.txt", s_files[i].titleId);
        if (FileExists(path))
            (*updatedGames)++;
        else
            (*newGames)++;
    }

    FILE *log = fopen(LOG_PATH, "w");
    if (log != NULL)
    {
        int wanted = 0;
        for (int i = 0; i < s_fileCount; i++)
            wanted += s_files[i].wanted ? 1 : 0;
        fprintf(log, "check: result %08lX, http %lu, %lu bytes, %d files listed, %d to download, %d new games, %d updated, error \"%s\"\n",
                (unsigned long)s_checkResult, (unsigned long)s_checkStatus, (unsigned long)s_checkSize, s_checkParsed, wanted,
                *newGames, *updatedGames, s_error);
        fclose(log);
    }
    return *newGames + *updatedGames > 0;
}

// Fetches one file; it only replaces the old one once it's fully here and verified.
static Result DownloadFile(const UpdateFile *f)
{
    char url[160], path[64], temporary[72], folder[40];
    u32  size;

    snprintf(url, sizeof(url), RAW_URL "/%.16s/%.15s", f->titleId, f->name);
    Result res = HttpGet(url, &size);
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
                fprintf(log, "download %.16s/%.15s: result %08lX, http %lu, error \"%s\"\n", s_files[i].titleId, s_files[i].name,
                        (unsigned long)res, (unsigned long)s_checkStatus, s_error);
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
