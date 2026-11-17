#include "store.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "rewards.h"
#include "store_file.h"

#define PURCHASES_PATH  "/achievements/purchases.txt"
#define PURCHASES_TEMP  PURCHASES_PATH ".tmp"
#define LINE_CHARS      96
#define FILE_CAPACITY   8192                // purchases.txt is rewritten whole only below this
#define THEMES_DIR      "/achievements/themes"

// What's for sale, in the order the store lists it. To add something: an ID that never changes
// (purchases are saved under it), a name, a one-line description, a price in Play Coins and
// optionally a 48x48 icon; then whatever it unlocks checks Store_OwnedId("ITS_ID"). A theme:
// its id as the item's theme, and the theme's folder in the Makefile's STORE_THEMES.
#ifndef STORE_ITEMS                         // (a PC test can bring its own list)
static const StoreItem ITEMS[] =
{
    // { "EXAMPLE", "Name", "What it gives you.", 50, NULL, NULL },
    { NULL, NULL, NULL, 0, NULL, NULL }     // the end of the list
};
#endif
#define ITEM_COUNT  ((int)(sizeof(ITEMS) / sizeof(*ITEMS)) - 1)

static bool s_owned[ITEM_COUNT + 1];

// A line of purchases.txt without its line ending and the spaces around it.
static char *Trim(char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ' || line[length - 1] == '\t'))
        line[--length] = '\0';
    return line;
}

static int Find(const char *id)
{
    for (int i = 0; i < ITEM_COUNT; i++)
        if (strcmp(ITEMS[i].id, id) == 0)
            return i;
    return -1;
}

void Store_Load(void)
{
    char line[LINE_CHARS];

    memset(s_owned, 0, sizeof(s_owned));
    FILE *f = fopen(PURCHASES_PATH, "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *id = Trim(line);
        int   i = id[0] != '#' ? Find(id) : -1;     // IDs no longer for sale are left alone
        if (i >= 0)
            s_owned[i] = true;
    }
    fclose(f);
}

// Where item i's theme goes.
static void ThemePath(int i, char *path, size_t capacity)
{
    snprintf(path, capacity, THEMES_DIR "/%s.theme", ITEMS[i].theme);
}

void Store_Restore(void)
{
    for (int i = 0; i < ITEM_COUNT; i++)
    {
        char path[64];
        if (!s_owned[i] || ITEMS[i].theme == NULL)
            continue;
        ThemePath(i, path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (f != NULL)
        {
            fclose(f);
            continue;
        }
        mkdir(THEMES_DIR, 0777);
        StoreFile_Install(ITEMS[i].theme, path);      // (without its file, it's put back once it arrives)
    }
}

bool Store_FilesWanted(void)
{
    for (int i = 0; i < ITEM_COUNT; i++)
        if (ITEMS[i].theme != NULL && !StoreFile_Ready(ITEMS[i].theme))
            return true;
    return false;
}

int Store_Count(void)
{
    return ITEM_COUNT;
}

const StoreItem *Store_Item(int i)
{
    return i >= 0 && i < ITEM_COUNT ? &ITEMS[i] : NULL;
}

bool Store_Owned(int i)
{
    return i >= 0 && i < ITEM_COUNT && s_owned[i];
}

bool Store_OwnedId(const char *id)
{
    return Store_Owned(Find(id));
}

int Store_OwnedCount(void)
{
    int count = 0;
    for (int i = 0; i < ITEM_COUNT; i++)
        count += s_owned[i];
    return count;
}

// Adds id at the end of purchases.txt (starting the file with a comment).
static bool Append(const char *id)
{
    FILE *f = fopen(PURCHASES_PATH, "r");
    bool  exists = f != NULL;
    if (f != NULL)
        fclose(f);

    f = fopen(PURCHASES_PATH, "a");
    if (f == NULL)
        return false;
    if (!exists)
        fprintf(f, "# Bought in the 3DS Achievements store, one item per line\n");
    fprintf(f, "%s\n", id);
    return fclose(f) == 0;
}

// Takes every line that is id out of purchases.txt (the file is written whole to a new one,
// then put in place of the old).
static bool Remove(const char *id)
{
    static char text[FILE_CAPACITY];
    char  line[LINE_CHARS];
    size_t length = 0;

    FILE *f = fopen(PURCHASES_PATH, "r");
    if (f == NULL)
        return false;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char copy[LINE_CHARS];
        memcpy(copy, line, sizeof(copy));
        if (strcmp(Trim(copy), id) == 0)
            continue;
        size_t n = strlen(line);
        if (length + n >= sizeof(text))
        {
            fclose(f);
            return false;
        }
        memcpy(text + length, line, n);
        length += n;
    }
    fclose(f);

    f = fopen(PURCHASES_TEMP, "w");
    if (f == NULL)
        return false;
    bool ok = fwrite(text, 1, length, f) == length;
    ok = fclose(f) == 0 && ok;
    if (!ok)
    {
        remove(PURCHASES_TEMP);
        return false;
    }
    remove(PURCHASES_PATH);
    return rename(PURCHASES_TEMP, PURCHASES_PATH) == 0;
}

Result Store_Buy(int i, int *coins)
{
    *coins = -1;
    if (i < 0 || i >= ITEM_COUNT)
        return MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, RD_OUT_OF_RANGE);
    if (s_owned[i])
        return 0;

    // Enough coins? Then saved as bought before they're taken: if anything goes wrong after,
    // the worst is an item got for nothing, never coins spent on nothing.
    Result res = Rewards_Coins(coins);
    if (R_FAILED(res))
        return res;
    if (*coins < ITEMS[i].price)
        return REWARDS_NOT_ENOUGH;

    // What it installs goes in place first: nothing is spent on an item that can't be given.
    char path[64] = "";
    if (ITEMS[i].theme != NULL)
    {
        ThemePath(i, path, sizeof(path));
        mkdir(THEMES_DIR, 0777);
        res = StoreFile_Install(ITEMS[i].theme, path);
        if (R_FAILED(res))
            return res;
    }
    if (!Append(ITEMS[i].id))
    {
        if (path[0] != '\0')
            remove(path);
        return STORE_NOT_SAVED;
    }
    res = Rewards_Spend(ITEMS[i].price, coins);
    if (R_FAILED(res))
    {
        Remove(ITEMS[i].id);
        if (path[0] != '\0')
            remove(path);
        return res;
    }
    s_owned[i] = true;
    return 0;
}
