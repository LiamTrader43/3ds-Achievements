#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rewards.h"
#include "store_file.h"
#include "theme_loader.h"

#define PURCHASES_PATH  "/achievements/purchases.txt"
#define PURCHASES_TEMP  PURCHASES_PATH ".tmp"
#define LINE_CHARS      96
#define FILE_CAPACITY   8192                // purchases.txt is rewritten whole only below this
#define THEMES_DIR      "/achievements/themes"
#define LIST_NAME       "items"             // the list's store file: /achievements/store/items.bin
#define INSTALLED_PATH  STORE_FILE_DIR "/installed.txt"     // which version of each store file a
#define INSTALLED_TEMP  INSTALLED_PATH ".tmp"               // bought theme was installed from
#define ID_MOST         31
#define NAME_MOST       47
#define DESCRIPTION_MOST 191
#define THEME_MOST      15

// What's for sale. The list is a store file (store_file.h), made from app/store/items.txt by
// tools/store_build.py: a line per item,
//     <ID> <price> <kind> <value> <name> | <description>
// e.g. "THEME_DS 100 theme ds DS theme | The app in the style of the original DS menu." The ID
// is saved in purchases.txt, so it never changes once the item is sold. The only kind so far is
// "theme" (value: the theme's id; its file is the store file <id>.bin). A line of a kind this app
// doesn't know, or a theme it can't load (made for another version of the app, or not downloaded
// yet), isn't shown, unless it was bought already.
#ifdef STORE_ITEMS                          // (a PC test can bring its own list: ITEMS[])
#define MAX_ITEMS   ((int)(sizeof(ITEMS) / sizeof(*ITEMS)))
#else
#define MAX_ITEMS   64

typedef struct
{
    char id[ID_MOST + 1];
    char name[NAME_MOST + 1];
    char description[DESCRIPTION_MOST + 1];
    char theme[THEME_MOST + 1];
} ItemText;

static ItemText s_text[MAX_ITEMS];
#endif

static StoreItem s_items[MAX_ITEMS];
static bool      s_owned[MAX_ITEMS];
static int       s_count;

// A line without its line ending and the spaces around it.
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
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_items[i].id, id) == 0)
            return i;
    return -1;
}

static bool CharsOk(const char *text, size_t most, bool upper)
{
    size_t length = strlen(text);
    if (length == 0 || length > most)
        return false;
    for (const char *c = text; *c != '\0'; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || (upper && *c >= 'A' && *c <= 'Z')))
            return false;
    return true;
}

// Whether this app can use the theme in the store file <theme>.bin (it's here, and it's a
// theme made for this app's theme API).
static bool ThemeUsable(const char *theme)
{
    ThemeHeader header;
    return R_SUCCEEDED(StoreFile_Peek(theme, &header, sizeof(header))) && header.magic == THEME_FILE_MAGIC &&
           header.format == THEME_FILE_FORMAT && header.info.apiVersion >= THEME_API_OLDEST &&
           header.info.apiVersion <= THEME_API_VERSION && strncmp(header.info.id, theme, sizeof(header.info.id)) == 0;
}

#ifndef STORE_ITEMS
// Takes the next word of *text (ended by a space or a tab) and moves past it.
static char *Word(char **text)
{
    char *p = *text;
    while (*p == ' ' || *p == '\t')
        p++;
    char *word = p;
    while (*p != '\0' && *p != ' ' && *p != '\t')
        p++;
    if (*p != '\0')
        *p++ = '\0';
    *text = p;
    return word;
}

// Reads one line of the list into item i. False if it isn't a whole line or not one for sale here.
static bool ParseItem(char *line, int i)
{
    ItemText *t = &s_text[i];
    char *rest = line, *end;
    const char *id = Word(&rest), *priceText = Word(&rest), *kind = Word(&rest), *value = Word(&rest);
    long price = strtol(priceText, &end, 10);
    if (!CharsOk(id, ID_MOST, true) || Find(id) >= 0 || priceText[0] < '0' || priceText[0] > '9' || *end != '\0' ||
        price > PLAY_COINS_MAX || strcmp(kind, "theme") != 0 || !CharsOk(value, THEME_MOST, false) ||
        strcmp(value, LIST_NAME) == 0)
        return false;

    char *bar = strchr(rest, '|');
    if (bar != NULL)
        *bar = '\0';
    const char *name = Trim(rest), *description = bar != NULL ? Trim(bar + 1) : "";
    if (name[0] == '\0' || strlen(name) > NAME_MOST || strlen(description) > DESCRIPTION_MOST)
        return false;

    strcpy(t->id, id);
    strcpy(t->name, name);
    strcpy(t->description, description);
    strcpy(t->theme, value);
    s_items[i] = (StoreItem){ t->id, t->name, t->description, (int)price, NULL, t->theme };
    return true;
}
#endif

// What's for sale: the list's lines that make items (theme items whose theme can be used are
// checked after the purchases are read).
static void ReadList(void)
{
    s_count = 0;
#ifdef STORE_ITEMS
    for (int i = 0; i < MAX_ITEMS && ITEMS[i].id != NULL; i++)
        s_items[s_count++] = ITEMS[i];
#else
    char *list;
    uint32_t size;
    if (R_FAILED(StoreFile_Read(LIST_NAME, &list, &size)))
        return;
    for (char *line = list; line != NULL && *line != '\0' && s_count < MAX_ITEMS;)
    {
        char *next = strchr(line, '\n');
        if (next != NULL)
            *next++ = '\0';
        char *text = Trim(line);
        if (text[0] != '#' && text[0] != '\0' && ParseItem(text, s_count))
            s_count++;
        line = next;
    }
    free(list);
#endif
}

void Store_Load(void)
{
    char line[LINE_CHARS];

    ReadList();
    memset(s_owned, 0, sizeof(s_owned));
    FILE *f = fopen(PURCHASES_PATH, "r");
    if (f != NULL)
    {
        while (fgets(line, sizeof(line), f) != NULL)
        {
            char *id = Trim(line);
            int   i = id[0] != '#' ? Find(id) : -1;     // IDs no longer for sale are left alone
            if (i >= 0)
                s_owned[i] = true;
        }
        fclose(f);
    }

    // Themes this app can't use are left out, unless they were bought.
    int kept = 0;
    for (int i = 0; i < s_count; i++)
    {
        if (s_items[i].theme != NULL && !s_owned[i] && !ThemeUsable(s_items[i].theme))
            continue;
        s_items[kept] = s_items[i];
        s_owned[kept] = s_owned[i];
#ifndef STORE_ITEMS
        if (kept != i)
        {
            s_text[kept] = s_text[i];
            s_items[kept] = (StoreItem){ s_text[kept].id, s_text[kept].name, s_text[kept].description, s_items[i].price,
                                         NULL, s_text[kept].theme };
        }
#endif
        kept++;
    }
    s_count = kept;
}

// Where item i's theme goes.
static void ThemePath(int i, char *path, size_t capacity)
{
    snprintf(path, capacity, THEMES_DIR "/%s.theme", s_items[i].theme);
}

// installed.txt: "<theme> <version>" per line (StoreFile_Version of the store file it came from).
static bool InstalledVersion(const char *theme, char version[65])
{
    char line[LINE_CHARS];
    size_t length = strlen(theme);
    bool found = false;

    version[0] = '\0';
    FILE *f = fopen(INSTALLED_PATH, "r");
    if (f == NULL)
        return false;
    while (!found && fgets(line, sizeof(line), f) != NULL)
    {
        char *text = Trim(line);
        if (strncmp(text, theme, length) == 0 && text[length] == ' ' && strlen(text + length + 1) == 64)
        {
            strcpy(version, text + length + 1);
            found = true;
        }
    }
    fclose(f);
    return found;
}

static void SetInstalledVersion(const char *theme, const char *version)
{
    static char text[2048];
    char   line[LINE_CHARS];
    size_t length = strlen(theme), used = 0;

    FILE *f = fopen(INSTALLED_PATH, "r");
    if (f != NULL)
    {
        while (fgets(line, sizeof(line), f) != NULL)
        {
            size_t n = strlen(line);
            if ((strncmp(line, theme, length) == 0 && line[length] == ' ') || used + n >= sizeof(text))
                continue;
            memcpy(text + used, line, n);
            used += n;
        }
        fclose(f);
    }
    f = fopen(INSTALLED_TEMP, "w");
    if (f == NULL)
        return;
    bool ok = fwrite(text, 1, used, f) == used && fprintf(f, "%s %s\n", theme, version) > 0;
    if (fclose(f) != 0 || !ok)
    {
        remove(INSTALLED_TEMP);
        return;
    }
    remove(INSTALLED_PATH);
    rename(INSTALLED_TEMP, INSTALLED_PATH);
}

// Decrypts item i's theme into place, and notes which version of its store file it came from.
static Result InstallTheme(int i, const char *path)
{
    char version[65];
    bool known = StoreFile_Version(s_items[i].theme, version);
    mkdir(THEMES_DIR, 0777);
    Result res = StoreFile_Install(s_items[i].theme, path);
    if (R_SUCCEEDED(res) && known)
        SetInstalledVersion(s_items[i].theme, version);
    return res;
}

void Store_Restore(void)
{
    for (int i = 0; i < s_count; i++)
    {
        char path[64], version[65], installed[65], why[64];
        ThemeInfo info;
        if (!s_owned[i] || s_items[i].theme == NULL || !StoreFile_Version(s_items[i].theme, version) ||
            !ThemeUsable(s_items[i].theme))
            continue;               // (without a store file it can use, it's put back once that arrives)
        // Put back if it's gone, can't be loaded (made for an older app) or came from another
        // version of its store file (the theme was updated).
        ThemePath(i, path, sizeof(path));
        if (ThemeLoader_Info(path, &info, why, sizeof(why)) && InstalledVersion(s_items[i].theme, installed) &&
            strcmp(installed, version) == 0)
            continue;
        InstallTheme(i, path);      // (a damaged store file leaves the old theme where it is)
    }
}

int Store_Count(void)
{
    return s_count;
}

const StoreItem *Store_Item(int i)
{
    return i >= 0 && i < s_count ? &s_items[i] : NULL;
}

bool Store_Owned(int i)
{
    return i >= 0 && i < s_count && s_owned[i];
}

bool Store_OwnedId(const char *id)
{
    return Store_Owned(Find(id));
}

int Store_OwnedCount(void)
{
    int count = 0;
    for (int i = 0; i < s_count; i++)
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
    if (i < 0 || i >= s_count)
        return MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, RD_OUT_OF_RANGE);
    if (s_owned[i])
        return 0;

    // Enough coins? Then saved as bought before they're taken: if anything goes wrong after,
    // the worst is an item got for nothing, never coins spent on nothing.
    Result res = Rewards_Coins(coins);
    if (R_FAILED(res))
        return res;
    if (*coins < s_items[i].price)
        return REWARDS_NOT_ENOUGH;

    // What it installs goes in place first: nothing is spent on an item that can't be given.
    char path[64] = "";
    if (s_items[i].theme != NULL)
    {
        ThemePath(i, path, sizeof(path));
        res = InstallTheme(i, path);
        if (R_FAILED(res))
            return res;
    }
    if (!Append(s_items[i].id))
    {
        if (path[0] != '\0')
            remove(path);
        return STORE_NOT_SAVED;
    }
    res = Rewards_Spend(s_items[i].price, coins);
    if (R_FAILED(res))
    {
        Remove(s_items[i].id);
        if (path[0] != '\0')
            remove(path);
        return res;
    }
    s_owned[i] = true;
    return 0;
}
