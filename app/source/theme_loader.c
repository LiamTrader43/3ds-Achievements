// Loads theme add-ons (theme_loader.h): a .theme file (theme_api.h: ThemeHeader) read straight
// into page-aligned memory, its relocations applied, its code pages made runnable
// (svcControlProcessMemory: the only way to get executable memory, which Luma lets homebrew
// use), then its entry point is called with the app's ThemeApi.
#include "theme_loader.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE        0x1000u
#define MOST_IMAGE  (4u * 1024u * 1024u)    // a theme can't take more memory than this
#define REL_NONE    0
#define REL_RELATIVE 23                     // R_ARM_RELATIVE: add where the theme was loaded

static bool Fail(char *why, size_t capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(why, capacity, format, args);
    va_end(args);
    return false;
}

static uint32_t Read32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, 4);
    return value;
}

// The header of a file of size bytes makes sense.
static bool CheckHeader(const ThemeHeader *h, uint32_t size, char *why, size_t capacity)
{
    const ThemeInfo *info = &h->info;

    if (size < sizeof(*h) || h->magic != THEME_FILE_MAGIC)
        return Fail(why, capacity, "not a theme file");
    if (h->format > THEME_FILE_FORMAT)
        return Fail(why, capacity, "made for a newer version of the app");
    if (h->format != THEME_FILE_FORMAT || memcmp(info->magic, THEME_INFO_MAGIC, sizeof(info->magic)) != 0 ||
        memchr(info->id, '\0', sizeof(info->id)) == NULL || memchr(info->name, '\0', sizeof(info->name)) == NULL ||
        info->id[0] == '\0' || info->name[0] == '\0')
        return Fail(why, capacity, "damaged file");
    for (const char *c = info->id; *c != '\0'; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_'))
            return Fail(why, capacity, "damaged file");
    if (info->apiVersion > THEME_API_VERSION)
        return Fail(why, capacity, "made for a newer version of the app");
    if (info->apiVersion < THEME_API_OLDEST)
        return Fail(why, capacity, "made for an older version of the app");

    if (h->fileSize > size)
        return Fail(why, capacity, "the file is cut short");
    // (In this order, so nothing below can overflow: everything is under 4 MB.)
    uint32_t entry = h->entry & ~1u;            // (a Thumb entry has its lowest bit set)
    if (h->fileSize != size || h->memorySize > MOST_IMAGE || h->codeSize > h->memorySize ||
        h->codeSize % PAGE != 0 || h->codeEnd < sizeof(*h) || h->codeEnd > h->codeSize ||
        h->fileSize < h->codeEnd + 4 || h->fileSize - h->codeEnd > h->memorySize - h->codeSize ||
        entry < sizeof(*h) || entry >= h->codeEnd || (h->entry == entry && entry % 4 != 0) ||
        h->relStart < sizeof(*h) || h->relStart > h->relEnd || h->relEnd > h->codeEnd ||
        h->relStart % 4 != 0 || (h->relEnd - h->relStart) % 8 != 0)
        return Fail(why, capacity, "damaged file");
    return true;
}

bool ThemeLoader_Prepare(ThemeRead read, void *source, uint32_t size, ThemeImage *out, char *why, size_t capacity)
{
    ThemeHeader h;

    memset(out, 0, sizeof(*out));
    if (size < sizeof(h))
        return Fail(why, capacity, "not a theme file");
    if (!read(source, 0, &h, sizeof(h)))
        return Fail(why, capacity, "couldn't read it");
    if (!CheckHeader(&h, size, why, capacity))
        return false;

    // The code part and the data part go straight to where they'll be; the rest is zeros.
    uint32_t dataEnd = h.codeSize + (h.fileSize - h.codeEnd);
    uint32_t imageSize = (h.memorySize + PAGE - 1) & ~(PAGE - 1);
    uint8_t *image = memalign(PAGE, imageSize);
    if (image == NULL)
        return Fail(why, capacity, "not enough memory");
    if (!read(source, 0, image, h.codeEnd) || !read(source, h.codeEnd, image + h.codeSize, h.fileSize - h.codeEnd))
    {
        free(image);
        return Fail(why, capacity, "couldn't read it");
    }
    memset(image + h.codeEnd, 0, h.codeSize - h.codeEnd);
    memset(image + dataEnd, 0, imageSize - dataEnd);
    if (Read32(image + dataEnd - 4) != THEME_FILE_END)
    {
        free(image);
        return Fail(why, capacity, "damaged file");
    }

    // Each relocation adds the image's address to a word of its data.
    int count = 0;
    for (uint32_t at = h.relStart; at < h.relEnd; at += 8)
    {
        uint32_t offset = Read32(image + at), info = Read32(image + at + 4);
        if ((info & 0xFF) == REL_NONE)
            continue;
        if ((info & 0xFF) != REL_RELATIVE || (info >> 8) != 0 || offset < h.codeSize || offset > dataEnd - 4)
        {
            free(image);
            return Fail(why, capacity, (info & 0xFF) != REL_RELATIVE || (info >> 8) != 0 ?
                        "needs things from outside it" : "damaged file");
        }
        uint32_t value = Read32(image + offset) + (uint32_t)(uintptr_t)image;
        memcpy(image + offset, &value, 4);
        count++;
    }

    out->image = image;
    out->imageSize = imageSize;
    out->codeSize = h.codeSize;
    out->entry = h.entry;
    out->relocations = count;
    out->info = h.info;
    return true;
}

// A ThemeRead for an open file.
static bool FileRead(void *source, uint32_t offset, void *to, uint32_t size)
{
    FILE *file = source;
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(to, 1, size, file) == size;
}

// Opens a .theme file and finds its size.
static FILE *Open(const char *path, uint32_t *size, char *why, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        Fail(why, capacity, "couldn't open it");
        return NULL;
    }
    long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (length <= 0 || (unsigned long)length > MOST_IMAGE)
    {
        Fail(why, capacity, length > 0 ? "too big" : "couldn't read it");
        fclose(file);
        return NULL;
    }
    *size = (uint32_t)length;
    return file;
}

bool ThemeLoader_Info(const char *path, ThemeInfo *info, char *why, size_t capacity)
{
    uint32_t size;
    FILE    *file = Open(path, &size, why, capacity);
    if (file == NULL)
        return false;
    ThemeHeader h;
    bool ok;
    if (size < sizeof(h))
        ok = Fail(why, capacity, "not a theme file");
    else if (!FileRead(file, 0, &h, sizeof(h)))
        ok = Fail(why, capacity, "couldn't read it");
    else
        ok = CheckHeader(&h, size, why, capacity);
    if (ok)
        *info = h.info;
    fclose(file);
    return ok;
}

#ifndef THEME_LOADER_TEST

// Luma's own system call (k11_extension; luma_svc.s).
void svcInvalidateInstructionCacheRange(void *addr, u32 len);

static bool IsLuma(void)
{
    s64 version;
    return R_SUCCEEDED(svcGetSystemInfo(&version, 0x10000, 0));
}

// Changes the permissions of the code pages. Needs a real handle to this process (not
// CUR_PROCESS_HANDLE). flush: first writes the whole image out of the data cache.
static Result Protect(const ThemeImage *t, u32 perm, bool flush)
{
    Handle self;
    Result res = svcDuplicateHandle(&self, CUR_PROCESS_HANDLE);
    if (R_FAILED(res))
        return res;
    if (flush)
        svcFlushProcessDataCache(self, (u32)t->image, t->imageSize);
    res = svcControlProcessMemory(self, (u32)t->image, 0, t->codeSize, MEMOP_PROT, perm);
    svcCloseHandle(self);
    return res;
}

// A hook is NULL or in the theme's code.
static bool InCode(const ThemeImage *t, const void *hook)
{
    uintptr_t at = (uintptr_t)hook, start = (uintptr_t)t->image;
    return hook == NULL || (at >= start + sizeof(ThemeHeader) && at < start + t->codeSize);
}

// What's wrong with the Theme a theme handed back, or NULL.
static const char *CheckTheme(const Theme *theme, const ThemeImage *t)
{
    if (theme == NULL)
        return "it didn't start";
    if (theme->id == NULL || theme->name == NULL || strcmp(theme->id, t->info.id) != 0)
        return "its id doesn't match its file";
    if (theme->drawLoading == NULL || theme->reset == NULL || theme->animate == NULL || theme->topCovers == NULL ||
        theme->drawTop == NULL || theme->drawPopup == NULL || theme->drawBottom == NULL ||
        theme->drawShareTop == NULL || theme->drawShareBottom == NULL || theme->drawSettingsBottom == NULL)
        return "something is missing from it";
    const void *hooks[] =
    {
        (const void *)theme->drawLoading, (const void *)theme->reset, (const void *)theme->animate,
        (const void *)theme->topCovers, (const void *)theme->drawTopBackground, (const void *)theme->drawTop,
        (const void *)theme->drawPopup, (const void *)theme->drawBottom, (const void *)theme->drawIdle,
        (const void *)theme->topChanged, (const void *)theme->drawShareTop, (const void *)theme->drawShareBottom,
        (const void *)theme->drawSettingsTop, (const void *)theme->drawSettingsBottom,
    };
    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
        if (!InCode(t, hooks[i]))
            return "damaged file";
    if (theme->boxHeight <= 0 || theme->listHeight <= 0 || theme->minPositions < 1 || theme->tabWidth < 0 ||
        (theme->font != DRAW_FONT_INTER && theme->font != DRAW_FONT_SYSTEM))
        return "its layout doesn't make sense";
    return NULL;
}

const Theme *ThemeLoader_Load(const char *path, size_t *bytes, char *why, size_t capacity)
{
    ThemeImage t;
    uint32_t   size;

    FILE *file = Open(path, &size, why, capacity);
    if (file == NULL)
        return NULL;
    bool ok = ThemeLoader_Prepare(FileRead, file, size, &t, why, capacity);
    fclose(file);
    if (!ok)
        return NULL;

    // The code was written like data: out of the data cache, its pages made runnable (and no
    // longer writable), and nothing old of those addresses left in the instruction cache.
    Result res = Protect(&t, MEMPERM_READ | MEMPERM_EXECUTE, true);
    if (R_FAILED(res))
    {
        free(t.image);
        Fail(why, capacity, "couldn't make it runnable (%08lX)", res);
        return NULL;
    }
    if (IsLuma())
        svcInvalidateInstructionCacheRange(t.image, t.codeSize);

    ThemeEntry   entry = (ThemeEntry)(uintptr_t)(t.image + t.entry);    // keeps the Thumb bit, if any
    const Theme *theme = entry(&g_themeApi);
    const char  *problem = CheckTheme(theme, &t);
    if (problem != NULL)
    {
        // Its pages go back to being data before the memory is given back (never, if that fails).
        if (R_SUCCEEDED(Protect(&t, MEMPERM_READ | MEMPERM_WRITE, false)))
            free(t.image);
        Fail(why, capacity, "%s", problem);
        return NULL;
    }
    *bytes = t.imageSize;
    return theme;
}

#endif
