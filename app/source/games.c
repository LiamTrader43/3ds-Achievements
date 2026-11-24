#include "games.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "achievements.h"

#define SMDH_SIZE   0x36C0
#define SMDH_ICON   0x24C0      // 48x48 icon

// One of a game's files: in its games folder (ACHIEVEMENTS_GAMES), or where cards kept it before
// if it's only there (a folder Games_MoveOldFolders couldn't move).
static void GamePath(u64 titleId, const char *name, char *path, size_t capacity)
{
    struct stat info;
    char        old[64];

    snprintf(path, capacity, ACHIEVEMENTS_GAMES "/%016llX/%s", (unsigned long long)titleId, name);
    snprintf(old, sizeof(old), ACHIEVEMENTS_OLD "/%016llX/%s", (unsigned long long)titleId, name);
    if (stat(path, &info) != 0 && stat(old, &info) == 0)
        snprintf(path, capacity, "%s", old);
}

// Platform functions for the tracker's achievements.c. The app only reads the
// files; it never checks conditions or writes progress.
bool Platform_ReadFile(const char *path, char *buffer, uint32_t capacity, uint32_t *length)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    *length = (uint32_t)fread(buffer, 1, capacity, f);
    buffer[*length] = '\0';
    fclose(f);
    return true;
}

bool Platform_ReadMemory(uint32_t address, uint8_t size, uint32_t *value)
{
    (void)address; (void)size; (void)value;
    return false;
}

bool Platform_AppendFile(const char *path, const char *text)
{
    (void)path; (void)text;
    return false;
}

bool Platform_WriteFile(const char *path, const char *text, uint32_t length)
{
    (void)path; (void)text; (void)length;
    return false;
}

void Platform_Unlocked(const char *name, const char *description)
{
    (void)name; (void)description;
}

void Platform_PartDone(const char *name, const char *part, int done, int total)
{
    (void)name; (void)part; (void)done; (void)total;
}

// A game's folder is named with its title ID, 16 hex digits.
static bool ParseFolderName(const char *name, u64 *titleId)
{
    if (strlen(name) != 16)
        return false;
    for (int i = 0; i < 16; i++)
        if (!isxdigit((unsigned char)name[i]))
            return false;
    *titleId = strtoull(name, NULL, 16);
    return true;
}

static bool IsSmdh(const u8 *smdh, size_t size)
{
    return size >= SMDH_ICON + sizeof(u16) * ICON_SIZE * ICON_SIZE && memcmp(smdh, "SMDH", 4) == 0;
}

// The SMDH ("icon" in the ExeFS) of an installed title: SD, game card, then NAND.
static bool ReadSmdh(u64 titleId, u8 *smdh)
{
    static const u32 iconPath[5] = { 0, 0, 2, 0x6E6F6369, 0 };     // ExeFS file "icon"
    static const u32 media[3] = { MEDIATYPE_SD, MEDIATYPE_GAME_CARD, MEDIATYPE_NAND };

    for (int i = 0; i < 3; i++)
    {
        u32     archivePath[4] = { (u32)titleId, (u32)(titleId >> 32), media[i], 0 };
        FS_Path archive = { PATH_BINARY, sizeof(archivePath), archivePath };
        FS_Path path = { PATH_BINARY, sizeof(iconPath), iconPath };
        Handle  file;
        u32     read = 0;

        if (R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, archive, path, FS_OPEN_READ, 0)))
            continue;
        Result res = FSFILE_Read(file, &read, 0, smdh, SMDH_SIZE);     // ExeFS files are read in one call
        FSFILE_Close(file);
        if (R_SUCCEEDED(res) && IsSmdh(smdh, read))
            return true;
    }
    return false;
}

// English short title, else Japanese; characters outside ASCII become '?'.
static void ReadTitle(const u8 *smdh, char *out)
{
    out[0] = '\0';
    for (int language = 1; language >= 0 && out[0] == '\0'; language--)
    {
        const u8 *title = smdh + 8 + language * 0x200;
        int n = 0;
        for (int i = 0; i < 0x40 && n < TITLE_CHARS; i++)
        {
            u16 c = (u16)(title[2 * i] | title[2 * i + 1] << 8);
            if (c == 0)
                break;
            out[n++] = c == '\n' ? ' ' : c < 0x80 ? (char)c : '?';
        }
        out[n] = '\0';
    }
}

static int CompareGames(const void *a, const void *b)
{
    const Game *x = a, *y = b;
    if (x->hasTitle != y->hasTitle)
        return x->hasTitle ? -1 : 1;    // games without a title (not installed) last
    return strcasecmp(x->title, y->title);
}

// The cached SMDH (title.smdh in the game's folder, written by the tracker while the game
// runs, or by this app): a game card's icon when it isn't inserted.
static bool ReadCachedSmdh(u64 titleId, u8 *smdh)
{
    char path[64];
    GamePath(titleId, "title.smdh", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    size_t size = fread(smdh, 1, SMDH_SIZE, f);
    fclose(f);
    return IsSmdh(smdh, size);
}

// Keeps the cache current with the installed title or inserted card.
static void WriteCachedSmdh(u64 titleId, const u8 *smdh)
{
    static u8 old[SMDH_SIZE];
    if (ReadCachedSmdh(titleId, old) && memcmp(old, smdh, SMDH_SIZE) == 0)
        return;

    char path[64];
    GamePath(titleId, "title.smdh", path, sizeof(path));     // where it's read from (the games folder normally)
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    fwrite(smdh, 1, SMDH_SIZE, f);
    fclose(f);
}

bool Games_LoadHero(u64 titleId, u8 *pixels)
{
    char path[64];

    GamePath(titleId, "hero.bin", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    const size_t column = HERO_HEIGHT * 3;
    bool ok;
    if (size == HERO_BYTES)
        ok = fread(pixels, 1, HERO_BYTES, f) == HERO_BYTES;
    else if (size == HERO_SCREEN_BYTES)
    {
        // An older, screen-sized background: in the middle, its edge columns mirrored outward.
        ok = fread(pixels + HERO_MARGIN * column, 1, HERO_SCREEN_BYTES, f) == HERO_SCREEN_BYTES;
        for (int i = 0; i < HERO_MARGIN; i++)
        {
            memcpy(pixels + (HERO_MARGIN - 1 - i) * column, pixels + (HERO_MARGIN + i) * column, column);
            memcpy(pixels + (HERO_WIDTH - HERO_MARGIN + i) * column, pixels + (HERO_WIDTH - HERO_MARGIN - 1 - i) * column, column);
        }
    }
    else
        ok = false;
    fclose(f);
    return ok;
}

bool Games_LoadTrophies(u64 titleId, u16 *unlocked, u16 *locked)
{
    char path[64];

    GamePath(titleId, "trophy.bin", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    bool ok = fread(unlocked, sizeof(u16), ICON_SIZE * ICON_SIZE, f) == ICON_SIZE * ICON_SIZE &&
              fread(locked, sizeof(u16), ICON_SIZE * ICON_SIZE, f) == ICON_SIZE * ICON_SIZE;
    fclose(f);
    return ok;
}

void Games_LoadIcons(u64 titleId, const char *const *ids, int count, GameIcons *icons)
{
    static IconsPlace places[ICONS_MOST];
    char path[64];

    memset(icons, 0, sizeof(*icons));
    if (count > ICONS_MOST)
        count = ICONS_MOST;
    GamePath(titleId, "icons.bin", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return;

    // The whole file in one read (much quicker than a read per picture), then room for the
    // locked pictures it leaves to be made.
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *data = size >= ICONS_HEADER_BYTES && size <= (long)ICONS_FILE_MOST ? malloc((size_t)size) : NULL;
    bool read = data != NULL && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    int found = read ? Icons_Find(data, (u32)size, ids, count, places) : 0;
    if (found == 0)
    {
        free(data);
        return;
    }
    int made = 0;
    for (int i = 0; i < count; i++)
        made += places[i].unlocked != 0 && places[i].locked == 0;
    size_t madeAt = ((size_t)size + 3) & ~(size_t)3;
    u8 *memory = realloc(data, madeAt + (size_t)made * ICONS_PICTURE_BYTES);
    if (memory == NULL)
    {
        free(data);
        return;
    }

    u16 *next = (u16 *)(memory + madeAt);
    for (int i = 0; i < count; i++)
    {
        if (places[i].unlocked == 0)
            continue;
        icons->unlocked[i] = (const u16 *)(memory + places[i].unlocked);
        if (places[i].locked != 0)
            icons->locked[i] = (const u16 *)(memory + places[i].locked);
        else
        {
            Icons_MakeLocked(icons->unlocked[i], next);
            icons->locked[i] = next;
            next += ICONS_PIXELS;
        }
    }
    icons->memory = memory;
    icons->count = found;
}

void Games_FreeIcons(GameIcons *icons)
{
    free(icons->memory);
    memset(icons, 0, sizeof(*icons));
}

int Games_Scan(Game *games, int capacity)
{
    static u8 smdh[SMDH_SIZE];
    int count = 0;

    static u64 releaseIds[MAX_GAMES], releaseGames[MAX_GAMES];
    static bool named[MAX_GAMES];               // the definitions' "@title" is in its title already
    int releases = 0;

    // The games folder, then the old place for any game still there (Games_MoveOldFolders
    // couldn't move it); a title found in both counts once.
    static const char *const roots[] = { ACHIEVEMENTS_GAMES, ACHIEVEMENTS_OLD };
    for (int r = 0; r < 2; r++)
    {
        DIR *dir = opendir(roots[r]);
        if (dir == NULL)
            continue;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && count < capacity)
        {
            Game *g = &games[count];
            if (!ParseFolderName(entry->d_name, &g->titleId))
                continue;
            bool seen = false;
            for (int i = 0; i < count && !seen; i++)
                seen = games[i].titleId == g->titleId;
            for (int i = 0; i < releases && !seen; i++)
                seen = releaseIds[i] == g->titleId;
            if (seen || !Achievements_Load(g->titleId))
                continue;
            if (Achievements_GameId() != g->titleId)    // another release (version.txt) of a game
            {
                if (releases < MAX_GAMES)
                {
                    releaseIds[releases] = g->titleId;
                    releaseGames[releases++] = Achievements_GameId();
                }
                continue;
            }
            Achievements_Progress(&g->unlocked, &g->total);
            Achievements_Score(&g->earned, &g->points);
            g->releaseCount = 0;
            named[count] = Achievements_Title()[0] != '\0';
            if (named[count])
                snprintf(g->title, sizeof(g->title), "%s", Achievements_Title());
            count++;
        }
        closedir(dir);  // before writing any cache file into the folder
    }

    for (int r = 0; r < releases; r++)
        for (int i = 0; i < count; i++)
            if (games[i].titleId == releaseGames[r] && games[i].releaseCount < MAX_RELEASES)
                games[i].releases[games[i].releaseCount++] = releaseIds[r];

    for (int i = 0; i < count; i++)
    {
        Game *g = &games[i];

        // The installed or inserted release, the game's own first. Every release counts here, also
        // past the MAX_RELEASES that Game.releases keeps.
        g->hasIcon = ReadSmdh(g->titleId, smdh);
        for (int r = 0; r < releases && !g->hasIcon; r++)
            if (releaseGames[r] == g->titleId)
                g->hasIcon = ReadSmdh(releaseIds[r], smdh);
        if (g->hasIcon)
            WriteCachedSmdh(g->titleId, smdh);
        else
            g->hasIcon = ReadCachedSmdh(g->titleId, smdh);

        if (!named[i])                          // else the definitions' "@title", set above
            g->title[0] = '\0';
        if (g->hasIcon)
        {
            if (!named[i])
                ReadTitle(smdh, g->title);
            memcpy(g->icon, smdh + SMDH_ICON, sizeof(g->icon));
        }
        g->hasTitle = g->title[0] != '\0';
        if (!g->hasTitle)
            snprintf(g->title, sizeof(g->title), "%016llX", (unsigned long long)g->titleId);
    }

    qsort(games, count, sizeof(Game), CompareGames);
    return count;
}

// ---- Moving game folders into /achievements/games (cards from before 2026-11-21)

#define FOLDERS_MOST    128
#define FILES_MOST      32
#define PROGRESS_MOST   0x10000     // achievements.c's FILE_CAPACITY

// The files a game's folder can have that a newer copy replaces: an older one left in the old
// place is deleted when the games folder has its own.
static const char *const GAME_FILES[] = { "definitions.txt", "version.txt", "hero.bin", "trophy.bin", "icons.bin",
                                          "title.smdh", "measured.txt" };

static bool IsDirectory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool Exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static char *ReadWhole(const char *path, uint32_t *length)
{
    char *text = malloc(PROGRESS_MOST + 1);
    if (text != NULL && !Platform_ReadFile(path, text, PROGRESS_MOST, length))
    {
        free(text);
        return NULL;
    }
    return text;
}

// The line [start, end) without spaces around it.
static void Trim(const char **start, const char **end)
{
    while (*start < *end && isspace((unsigned char)**start))
        (*start)++;
    while (*end > *start && isspace((unsigned char)(*end)[-1]))
        (*end)--;
}

static bool HasLine(const char *text, uint32_t length, const char *line, size_t lineLength)
{
    for (const char *p = text, *end = text + length; p < end; )
    {
        const char *next = memchr(p, '\n', (size_t)(end - p));
        const char *a = p, *b = next != NULL ? next : end;
        Trim(&a, &b);
        if ((size_t)(b - a) == lineLength && memcmp(a, line, lineLength) == 0)
            return true;
        p = next != NULL ? next + 1 : end;
    }
    return false;
}

// Adds to progress.txt at `to` every line of `from` it doesn't have. True if `from` can go.
static bool MergeProgress(const char *from, const char *to)
{
    uint32_t fromLength = 0, toLength = 0;
    char    *source = ReadWhole(from, &fromLength);
    char    *target = source != NULL ? ReadWhole(to, &toLength) : NULL;
    bool     ok = source != NULL;

    if (ok && target == NULL)
        ok = rename(from, to) == 0;         // the games folder has none: just move it
    else if (ok)
    {
        FILE *out = fopen(to, "ab");
        ok = out != NULL;
        bool lineEnd = toLength == 0 || target[toLength - 1] == '\n';
        for (const char *p = source, *end = source + fromLength; ok && p < end; )
        {
            const char *next = memchr(p, '\n', (size_t)(end - p));
            const char *a = p, *b = next != NULL ? next : end;
            Trim(&a, &b);
            if (b > a && !HasLine(target, toLength, a, (size_t)(b - a)))
            {
                ok = (lineEnd || fputc('\n', out) != EOF) && fwrite(a, 1, (size_t)(b - a), out) == (size_t)(b - a)
                     && fputc('\n', out) != EOF;
                lineEnd = true;
            }
            p = next != NULL ? next + 1 : end;
        }
        if (out != NULL && fclose(out) != 0)
            ok = false;
        if (ok)
            remove(from);
    }
    free(source);
    free(target);
    return ok;
}

// Moves what can be moved from one game's old folder into its games folder, which exists.
static void MergeFolder(const char *from, const char *to)
{
    char  names[FILES_MOST][64];
    int   count = 0;
    DIR  *dir = opendir(from);
    if (dir == NULL)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < FILES_MOST)
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && strlen(entry->d_name) < 64)
            snprintf(names[count++], sizeof(names[0]), "%s", entry->d_name);
    closedir(dir);                          // before changing anything in it

    for (int i = 0; i < count; i++)
    {
        char source[128], target[128];
        snprintf(source, sizeof(source), "%.63s/%.63s", from, names[i]);
        snprintf(target, sizeof(target), "%.63s/%.63s", to, names[i]);
        if (strcmp(names[i], "progress.txt") == 0)
            MergeProgress(source, target);
        else if (!Exists(target))
            rename(source, target);
        else
            for (size_t k = 0; k < sizeof(GAME_FILES) / sizeof(GAME_FILES[0]); k++)
                if (strcmp(names[i], GAME_FILES[k]) == 0)
                    remove(source);     // the games folder's is the newer copy
    }
    rmdir(from);                            // only if it's empty now
}

int Games_MoveOldFolders(void)
{
    char  names[FOLDERS_MOST][17];
    int   count = 0, moved = 0;
    u64   titleId;

    DIR *dir = opendir(ACHIEVEMENTS_OLD);
    if (dir == NULL)
        return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < FOLDERS_MOST)
    {
        char path[64];
        snprintf(path, sizeof(path), ACHIEVEMENTS_OLD "/%.16s", entry->d_name);
        if (ParseFolderName(entry->d_name, &titleId) && IsDirectory(path))
            snprintf(names[count++], sizeof(names[0]), "%.16s", entry->d_name);
    }
    closedir(dir);                          // before renaming anything in it
    if (count == 0)
        return 0;

    mkdir(ACHIEVEMENTS_GAMES, 0777);
    for (int i = 0; i < count; i++)
    {
        char from[64], to[64];
        snprintf(from, sizeof(from), ACHIEVEMENTS_OLD "/%.16s", names[i]);
        snprintf(to, sizeof(to), ACHIEVEMENTS_GAMES "/%.16s", names[i]);
        if (!IsDirectory(to) && rename(from, to) == 0)
        {
            moved++;
            continue;
        }
        mkdir(to, 0777);
        MergeFolder(from, to);
        if (!IsDirectory(from))
            moved++;
    }
    return moved;
}
