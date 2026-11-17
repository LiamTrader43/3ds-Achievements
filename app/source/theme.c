// The themes (theme.h): the preinstalled ones, compiled into the app, and the add-ons in
// /achievements/themes, each loaded the first time it's used (theme_loader.c).
// /achievements/theme.log says what add-ons were found and loaded, and why anything wasn't.
#include "theme.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "theme_loader.h"

#ifndef THEME_PATH
#define THEME_PATH  "sdmc:/achievements/theme.txt"
#endif
#ifndef THEMES_DIR
#define THEMES_DIR  "sdmc:/achievements/themes"
#endif
#ifndef THEME_LOG
#define THEME_LOG   "sdmc:/achievements/theme.log"
#endif

#define MOST_THEMES 16

// The preinstalled themes (Makefile BUILTIN_THEMES compiles them in), in the order the settings
// go through them; modern first, the one used when no other can be.
static const Theme *const BUILT_IN[] = { &THEME_MODERN, &THEME_3DS };
#define BUILT_INS   ((int)(sizeof(BUILT_IN) / sizeof(BUILT_IN[0])))

typedef struct
{
    ThemeInfo    info;
    char         file[64];          // in THEMES_DIR (add-ons)
    const Theme *theme;             // preinstalled, or an add-on once loaded
    bool         failed;            // couldn't be loaded: skipped from then on
} Entry;

static Entry s_themes[MOST_THEMES]; // BUILT_IN, then the add-ons sorted by name
static int   s_count;
static int   s_current;
static char  s_error[96];

const Theme *g_theme = &THEME_MODERN;

static void Log(const char *format, ...)
{
    FILE *log = fopen(THEME_LOG, "a");
    if (log == NULL)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(log, format, args);
    va_end(args);
    fputc('\n', log);
    fclose(log);
}

static int Find(const char *id)
{
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_themes[i].info.id, id) == 0)
            return i;
    return -1;
}

static void ScanFiles(void);

// The preinstalled themes, then every .theme file's ThemeInfo (nothing in them runs yet).
static void Scan(void)
{
    memset(s_themes, 0, sizeof(s_themes));
    for (s_count = 0; s_count < BUILT_INS; s_count++)
    {
        Entry *t = &s_themes[s_count];
        snprintf(t->info.id, sizeof(t->info.id), "%s", BUILT_IN[s_count]->id);
        snprintf(t->info.name, sizeof(t->info.name), "%s", BUILT_IN[s_count]->name);
        t->theme = BUILT_IN[s_count];
    }
    ScanFiles();
}

// Reads the ThemeInfo of every .theme file not listed yet (nothing in them runs yet), then puts
// the add-ons in order by name.
static void ScanFiles(void)
{
    DIR *dir = opendir(THEMES_DIR);
    if (dir == NULL)
    {
        Log("no add-on themes (" THEMES_DIR " isn't there)");
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        size_t length = strlen(e->d_name);
        if (length <= 6 || length >= sizeof(s_themes[0].file) || strcmp(e->d_name + length - 6, ".theme") != 0)
            continue;
        bool listed = false;
        for (int k = BUILT_INS; k < s_count; k++)
            listed = listed || strcmp(s_themes[k].file, e->d_name) == 0;
        if (listed)
            continue;               // (read before: Theme_Rescan)
        if (s_count == MOST_THEMES)
        {
            Log("%s: skipped, too many themes", e->d_name);
            continue;
        }

        Entry *t = &s_themes[s_count];
        char   path[128], why[64];
        memcpy(t->file, e->d_name, length + 1);
        snprintf(path, sizeof(path), "%s/%s", THEMES_DIR, t->file);
        if (!ThemeLoader_Info(path, &t->info, why, sizeof(why)))
            Log("%s: skipped, %s", t->file, why);
        else if (strncmp(t->file, t->info.id, length - 6) != 0 || t->info.id[length - 6] != '\0')
            Log("%s: skipped, its theme is %s (the file should be %s.theme)", t->file, t->info.id, t->info.id);
        else if (Find(t->info.id) >= 0)
            Log("%s: skipped, there's already a theme %s%s", t->file, t->info.id,
                Find(t->info.id) < BUILT_INS ? " (part of the app)" : "");
        else
        {
            Log("%s: %s, made for theme API %lu", t->file, t->info.name, (unsigned long)t->info.apiVersion);
            s_count++;
        }
    }
    closedir(dir);

    // The add-ons by name, so they always come in the same order.
    for (int i = BUILT_INS + 1; i < s_count; i++)
        for (int j = i; j > BUILT_INS && strcmp(s_themes[j - 1].info.name, s_themes[j].info.name) > 0; j--)
        {
            Entry swap = s_themes[j];
            s_themes[j] = s_themes[j - 1];
            s_themes[j - 1] = swap;
        }
}

// Uses theme i, loading it first if it's an add-on not loaded yet. False if it can't be.
static bool Use(int i)
{
    Entry *t = &s_themes[i];
    if (t->theme == NULL)
    {
        char   path[128], why[64];
        size_t bytes = 0;
        if (t->failed)
            return false;
        snprintf(path, sizeof(path), "%s/%s", THEMES_DIR, t->file);
        u64 start = osGetTime();
        t->theme = ThemeLoader_Load(path, &bytes, why, sizeof(why));
        if (t->theme == NULL)
        {
            t->failed = true;
            snprintf(s_error, sizeof(s_error), "%s: %s", t->info.name, why);
            Log("%s: couldn't load it: %s", t->file, why);
            return false;
        }
        Log("%s: loaded in %lu ms, %lu KB", t->file, (unsigned long)(osGetTime() - start), (unsigned long)(bytes / 1024));
    }
    s_current = i;
    g_theme = t->theme;
    Draw_SetFont(g_theme->font);
    return true;
}

bool Theme_Load(void)
{
    char  line[32] = "";
    FILE *file = fopen(THEME_LOG, "w");     // a new log each time the app opens
    if (file != NULL)
        fclose(file);

    Scan();
    s_current = 0;
    g_theme = &THEME_MODERN;
    Draw_SetFont(g_theme->font);

    file = fopen(THEME_PATH, "r");
    if (file != NULL)
    {
        if (fgets(line, sizeof(line), file) != NULL)
            line[strcspn(line, " \t\r\n")] = '\0';
        fclose(file);
    }
    int i = Find(line);
    if (line[0] != '\0' && i < 0)
        Log("theme.txt asks for %s, which isn't here", line);
    bool ok = i <= 0 || Use(i);
    Log("using %s%s", g_theme->name, i < 0 || ok ? "" : " instead");
    return ok;
}

void Theme_Rescan(void)
{
    char id[sizeof(s_themes[0].info.id)];
    memcpy(id, s_themes[s_current].info.id, sizeof(id));
    ScanFiles();
    int i = Find(id);                   // (the add-ons were put in order again)
    s_current = i >= 0 ? i : 0;
}

int Theme_Count(void)
{
    return s_count;
}

bool Theme_UseNext(void)
{
    bool ok = true;
    for (int k = 1; k < s_count; k++)
    {
        int i = (s_current + k) % s_count;
        if (s_themes[i].failed)
            continue;               // already said why
        if (Use(i))
        {
            FILE *file = fopen(THEME_PATH, "w");
            if (file != NULL)
            {
                fprintf(file, "%s\n", g_theme->id);
                fclose(file);
            }
            break;
        }
        ok = false;
    }
    return ok;
}

const char *Theme_Error(void)
{
    return s_error;
}
