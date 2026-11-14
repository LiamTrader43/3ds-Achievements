#include "games.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"

#define DIRECTORY   "/achievements"
#define SMDH_SIZE   0x36C0
#define SMDH_ICON   0x24C0      // 48x48 icon

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

void Platform_Unlocked(const char *name, const char *description)
{
    (void)name; (void)description;
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

// The cached SMDH (/achievements/<TitleID>/title.smdh, written by the tracker while
// the game runs, or by this app): a game card's icon when it isn't inserted.
static void CachePath(u64 titleId, char *path, size_t capacity)
{
    snprintf(path, capacity, DIRECTORY "/%016llX/title.smdh", (unsigned long long)titleId);
}

static bool ReadCachedSmdh(u64 titleId, u8 *smdh)
{
    char path[64];
    CachePath(titleId, path, sizeof(path));
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
    CachePath(titleId, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    fwrite(smdh, 1, SMDH_SIZE, f);
    fclose(f);
}

bool Games_LoadHero(u64 titleId, u8 *pixels)
{
    char path[64];

    snprintf(path, sizeof(path), DIRECTORY "/%016llX/hero.bin", (unsigned long long)titleId);
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

    snprintf(path, sizeof(path), DIRECTORY "/%016llX/trophy.bin", (unsigned long long)titleId);
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    bool ok = fread(unlocked, sizeof(u16), ICON_SIZE * ICON_SIZE, f) == ICON_SIZE * ICON_SIZE &&
              fread(locked, sizeof(u16), ICON_SIZE * ICON_SIZE, f) == ICON_SIZE * ICON_SIZE;
    fclose(f);
    return ok;
}

int Games_Scan(Game *games, int capacity)
{
    static u8 smdh[SMDH_SIZE];
    int count = 0;

    static u64 releaseIds[MAX_GAMES], releaseGames[MAX_GAMES];
    int releases = 0;

    DIR *dir = opendir(DIRECTORY);
    if (dir == NULL)
        return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < capacity)
    {
        Game *g = &games[count];
        if (!ParseFolderName(entry->d_name, &g->titleId) || !Achievements_Load(g->titleId))
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
        count++;
    }
    closedir(dir);  // before writing any cache file into the folder

    for (int r = 0; r < releases; r++)
        for (int i = 0; i < count; i++)
            if (games[i].titleId == releaseGames[r] && games[i].releaseCount < MAX_RELEASES)
                games[i].releases[games[i].releaseCount++] = releaseIds[r];

    for (int i = 0; i < count; i++)
    {
        Game *g = &games[i];

        // The installed or inserted release, the game's own first.
        g->hasIcon = ReadSmdh(g->titleId, smdh);
        for (int r = 0; r < g->releaseCount && !g->hasIcon; r++)
            g->hasIcon = ReadSmdh(g->releases[r], smdh);
        if (g->hasIcon)
            WriteCachedSmdh(g->titleId, smdh);
        else
            g->hasIcon = ReadCachedSmdh(g->titleId, smdh);

        g->title[0] = '\0';
        if (g->hasIcon)
        {
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
