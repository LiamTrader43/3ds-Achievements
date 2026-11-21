#include "achievements.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_runtime_types.h"   // rcheevos (../rcheevos), for "rc:" conditions
#include "rc_internal.h"        // rc_trigger_get_memrefs, to move a release's addresses

#define MAX_LINES       256     // lines read from a definitions file (the table is sized to the real count)
#define FILE_CAPACITY   0x10000 // definitions.txt / progress.txt read at most this much (only while loading)
#define MATCH_POLLS     3       // consecutive polls a condition must hold
#define RC_PREFIX       "rc:"
#define DEFAULT_POINTS  10      // an achievement's points when its line gives none
#define MAX_POINTS      1000
#define MAX_SHIFTS      8       // "shift" lines in a version.txt
#define VERSION_CAPACITY 1024
#define PLUGIN_KEYWORD  "@block-plugin"     // "@block-plugin <text>": see Achievements_PluginBlocked
#define MISSABLE_KEYWORD "@missable"        // "@missable <id> [<id> ...]": see Achievements_Missable
#define PARTS_KEYWORD   "@parts"            // "<id> @parts <name> | ...": an achievement done in parts
#define PART_KEYWORD    "@part"             // "@part <id> <part> <condition> <name>": one of its parts
#define TITLE_KEYWORD   "@title"            // "@title <name>": the game's name, instead of its SMDH title
#define VERSION_KEYWORD "@version"          // "@version <name> <address> <size> <value>": see Achievements_VersionMatches
#define MAX_VERSIONS    8
#define VERSION_NAME    16                  // the longest version name kept + 1 (it shares the banner with the game's name)
#define TITLE_CAPACITY  65                  // the longest name kept + 1 (the app's Game.title holds 64)
#define ID_CAPACITY     32                  // an ID's (and a part's own ID's) longest length + 1
#define MAX_PLUGIN_BLOCKS 8
#define PLUGIN_TEXT     64

// Longest name and description kept, with the closing zero. Both the tracker's menu and the
// app show them in full (A on an achievement); the app compiles this file with more room.
#ifndef NAME_CAPACITY
#define NAME_CAPACITY   64
#endif
#ifndef DESCRIPTION_CAPACITY
#define DESCRIPTION_CAPACITY 256
#endif

#define ALIGN8(n)       (((size_t)(n) + 7) & ~(size_t)7)

typedef struct
{
    char            id[48];     // a part's is "<achievement id>/<part id>" (what progress.txt gets)
    uint32_t        address;
    uint8_t         size;
    bool            atLeast;
    uint32_t        expected;
    rc_trigger_t   *trigger;    // set instead for an "rc:" condition (in g_block)
    char            name[NAME_CAPACITY];
    char            description[DESCRIPTION_CAPACITY];
    uint16_t        points;
    bool            unlocked;
    bool            missable;   // named on a "@missable" line
    bool            isParent;   // "<id> @parts ...": no condition of its own, done once all its parts are
    bool            isPart;     // "@part ...": saved like an achievement, but only counts for its parent
    int16_t         parent;     // a part's achievement (its first line), -1 for none
    uint8_t         matches;
    uint32_t        measured;   // the trigger's latest Measured value above 0 (or measured.txt's)
} Achievement;

// Another release of a game (e.g. another region) may keep some data at other
// addresses: addresses first..last of the definitions are read at address + amount.
typedef struct
{
    uint32_t    first, last;
    int32_t     amount;
} Shift;

// A loaded game takes one allocation, g_block: its lines (g_ach), then their rc
// conditions. Nothing is held while no game is loaded, so the tracker leaves that
// memory to the system (it shares the Base region with every system module).
static void        *g_block;
static Achievement *g_ach;
static uint8_t     *g_spare, *g_spareEnd;   // room left in g_block for conditions, while loading
static int          g_count;
static char         g_unlockPath[64];
static char         g_measuredPath[64];
static bool         g_measuredChanged;      // a value changed since measured.txt was written
static uint64_t     g_gameId;
static Shift        g_shift[MAX_SHIFTS];
static int          g_shiftCount;
static char         g_pluginBlocks[MAX_PLUGIN_BLOCKS][PLUGIN_TEXT];
static int          g_pluginBlockCount;
static char         g_title[TITLE_CAPACITY];    // "@title": the game's name ("" for none)

// "@version" lines: the value a version of the game the definitions are for has at an address.
typedef struct
{
    uint32_t    address, value;
    uint8_t     size;
    bool        valid;                  // false: a line this build can't read, which never matches
} VersionCheck;

static VersionCheck g_versions[MAX_VERSIONS];
static int          g_versionCount;
static char         g_versionName[VERSION_NAME];    // the first "@version" line's name ("" for none)

static bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Copies the trimmed text [start, end) into out (truncating to fit).
static void CopyTrimmed(char *out, size_t capacity, const char *start, const char *end)
{
    while (start < end && IsSpace(*start)) start++;
    while (end > start && IsSpace(end[-1])) end--;

    size_t length = (size_t)(end - start);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(out, start, length);
    out[length] = '\0';
}

// Reads the next whitespace-separated token from *pos up to end.
static bool NextToken(const char **pos, const char *end, char *out, size_t capacity)
{
    const char *p = *pos;
    while (p < end && IsSpace(*p)) p++;
    const char *start = p;
    while (p < end && !IsSpace(*p)) p++;
    *pos = p;
    if (p == start || (size_t)(p - start) >= capacity)
        return false;
    memcpy(out, start, (size_t)(p - start));
    out[p - start] = '\0';
    return true;
}

// Hex with a 0x prefix, otherwise decimal (never octal).
static bool ParseU32(const char *token, uint32_t *out)
{
    int base = 10;
    if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
    {
        token += 2;
        base = 16;
    }
    char *end;
    *out = strtoul(token, &end, base);
    return end != token && *end == '\0';
}

// The rest of a line: "<name> | <description> | <points>". The first '|' ends
// the name; a last '|' followed by a whole number gives the points (otherwise
// it's part of the description). No name: the ID. No points: DEFAULT_POINTS.
static void ParseText(const char *pos, const char *end, Achievement *a)
{
    const char *bar = memchr(pos, '|', (size_t)(end - pos));
    const char *descriptionEnd = end;

    a->points = DEFAULT_POINTS;
    CopyTrimmed(a->name, sizeof(a->name), pos, bar ? bar : end);
    if (a->name[0] == '\0')
        memcpy(a->name, a->id, strlen(a->id) + 1);  // an ID (< ID_CAPACITY here) always fits name[64]
    if (bar == NULL)
    {
        a->description[0] = '\0';
        return;
    }

    const char *last = end;
    while (last > bar + 1 && last[-1] != '|')
        last--;
    if (last > bar + 1)
    {
        char     token[16];
        uint32_t points;
        CopyTrimmed(token, sizeof(token), last, end);
        if (token[0] >= '0' && token[0] <= '9' && ParseU32(token, &points) && points <= MAX_POINTS)
        {
            a->points = (uint16_t)points;
            descriptionEnd = last - 1;
        }
    }
    CopyTrimmed(a->description, sizeof(a->description), bar + 1, descriptionEnd);
}

// An address as this release keeps it (see Shift).
static uint32_t ShiftAddress(uint32_t address)
{
    for (int i = 0; i < g_shiftCount; i++)
        if (address >= g_shift[i].first && address <= g_shift[i].last)
            return address + (uint32_t)g_shift[i].amount;
    return address;
}

// Moves the addresses a parsed condition reads directly. Offsets added to a
// pointer read from memory (AddAddress, "I:") are kept apart by rcheevos (as
// modified memrefs) and stay as written: they're relative to wherever the
// pointer leads, which this release already reports itself.
static void ShiftTrigger(rc_trigger_t *trigger)
{
    rc_memrefs_t *memrefs = g_shiftCount > 0 ? rc_trigger_get_memrefs(trigger) : NULL;
    if (memrefs == NULL)
        return;
    for (rc_memref_list_t *list = &memrefs->memrefs; list != NULL; list = list->next)
        for (uint32_t i = 0; i < list->count; i++)
            list->items[i].address = ShiftAddress(list->items[i].address);
}

// "<id> rc:<RetroAchievements condition string> <name> | <description>": the
// condition (no spaces, e.g. "0xH1234=1_d0xH5678=1_0xH5678=2") is parsed and
// evaluated by rcheevos, which adds AND, ResetIf/PauseIf, hit counts, deltas
// and every comparison. Like on RetroAchievements, it can't unlock until it
// has been false once.
#define CONDITION_CAPACITY 4096    // a condition's longest length + 1 (1024 before 2026-11-19: longer lines were skipped)
#define PARSE_ROOM      8192    // heap room rcheevos' temporaries get while it parses one condition

static bool g_noRoom;           // a condition couldn't be given that room: the load fails

// rcheevos allocates small temporaries while it parses (its lists of memory addresses,
// ~1.5 KB) and doesn't check that it got them. So before each parse, make sure the heap
// has room: take that much and give it back, and it's there for them. (The tracker's
// heap grows only while memory is left, and gives back nothing on its own.)
static bool ParseRoom(void)
{
    void *room = malloc(PARSE_ROOM);
    if (room == NULL)
    {
        g_noRoom = true;
        return false;
    }
    free(room);
    return true;
}

// The condition after "rc:", up to the next space, into out; *after is where it ends.
static bool ReadCondition(const char *pos, const char *end, char *out, size_t capacity, const char **after)
{
    const char *start = pos;

    while (pos < end && !IsSpace(*pos)) pos++;
    size_t length = (size_t)(pos - start);
    if (length >= capacity)
        return false;
    memcpy(out, start, length);
    out[length] = '\0';
    *after = pos;
    return true;
}

static bool ParseCondition(const char *pos, const char *end, Achievement *a)
{
    char condition[CONDITION_CAPACITY];

    if (!ReadCondition(pos, end, condition, sizeof(condition), &pos) || !ParseRoom())
        return false;

    // Its room in g_block was measured first (ConditionBytes); none left means the
    // file changed between the two reads.
    int    size = rc_trigger_size(condition);
    size_t need = ALIGN8(size);
    if (size <= 0 || g_spare == NULL || (size_t)(g_spareEnd - g_spare) < need)
        return false;
    a->trigger = rc_parse_trigger(g_spare, condition, NULL, 0);
    if (a->trigger == NULL)
        return false;
    g_spare += need;
    ShiftTrigger(a->trigger);

    ParseText(pos, end, a);
    return true;
}

// Whether [pos, end) starts with the word (followed by a space or the end).
static bool StartsWord(const char *pos, const char *end, const char *word)
{
    size_t length = strlen(word);
    return (size_t)(end - pos) >= length && memcmp(pos, word, length) == 0
           && (pos + length == end || IsSpace(pos[length]));
}

static bool ParseLine(const char *line, const char *end, Achievement *a)
{
    char        address[16], size[8], expected[16];
    const char *pos = line;
    uint32_t    sizeValue;

    a->trigger = NULL;
    a->unlocked = false;
    a->missable = false;
    a->isParent = false;
    a->isPart = false;
    a->parent = -1;
    a->matches = 0;
    if (!NextToken(&pos, end, a->id, ID_CAPACITY))
        return false;
    while (pos < end && IsSpace(*pos)) pos++;
    if (StartsWord(pos, end, PARTS_KEYWORD))
    {
        // "<id> @parts <name> | <description> | <points>": done once all its parts are
        // (see ParsePart). Older builds skip the line ("@parts" isn't an address).
        a->isParent = true;
        ParseText(pos + strlen(PARTS_KEYWORD), end, a);
        return true;
    }
    if ((size_t)(end - pos) > strlen(RC_PREFIX) && memcmp(pos, RC_PREFIX, strlen(RC_PREFIX)) == 0)
        return ParseCondition(pos + strlen(RC_PREFIX), end, a);

    if (!NextToken(&pos, end, address, sizeof(address))
        || !NextToken(&pos, end, size, sizeof(size)) || !NextToken(&pos, end, expected, sizeof(expected)))
        return false;

    const char *number = expected;
    a->atLeast = expected[0] == '>' && expected[1] == '=';
    if (a->atLeast)
        number += 2;

    if (!ParseU32(address, &a->address) || !ParseU32(size, &sizeValue) || !ParseU32(number, &a->expected))
        return false;
    if (sizeValue != 1 && sizeValue != 2 && sizeValue != 4)
        return false;
    if (sizeValue < 4 && (a->expected >> (sizeValue * 8)) != 0)
        return false;
    a->size = (uint8_t)sizeValue;
    a->address = ShiftAddress(a->address);

    ParseText(pos, end, a);
    return true;
}

// "@part <id> <part> ...": one of achievement <id>'s parts (an "@parts" line). Older builds
// skip it like every line starting with '@'.
static bool IsPartLine(const char *line, const char *end)
{
    return StartsWord(line, end, PART_KEYWORD);
}

// Where a part line's own line starts: "<part> <condition> <name>", after "@part <id>".
static const char *PartRest(const char *line, const char *end, char *parent, size_t capacity)
{
    const char *pos = line + strlen(PART_KEYWORD);
    return NextToken(&pos, end, parent, capacity) ? pos : NULL;
}

// "@part <id> <part> <condition> <name> | <description>": the rest is read like an achievement's
// line (either kind of condition; lines sharing <id> and <part> are alternatives), and the part
// is kept as "<id>/<part>", the name progress.txt gets when it's done. It counts for nothing on
// its own: <id> is done once every one of its parts is.
static bool ParsePart(const char *line, const char *end, Achievement *a)
{
    char        parent[ID_CAPACITY];
    const char *rest = PartRest(line, end, parent, sizeof(parent));

    if (rest == NULL || !ParseLine(rest, end, a) || a->isParent)
        return false;
    size_t parentLength = strlen(parent), partLength = strlen(a->id);
    if (parentLength + 1 + partLength >= sizeof(a->id))
        return false;
    memmove(a->id + parentLength + 1, a->id, partLength + 1);
    memcpy(a->id, parent, parentLength);
    a->id[parentLength] = '/';
    a->isPart = true;
    return true;
}

// The room a line's rc condition takes in g_block (0 for other lines).
static size_t ConditionBytes(const char *line, const char *end)
{
    char        id[32], condition[CONDITION_CAPACITY];
    const char *pos = line;

    if (!NextToken(&pos, end, id, sizeof(id)))
        return 0;
    while (pos < end && IsSpace(*pos)) pos++;
    if ((size_t)(end - pos) <= strlen(RC_PREFIX) || memcmp(pos, RC_PREFIX, strlen(RC_PREFIX)) != 0
        || !ReadCondition(pos + strlen(RC_PREFIX), end, condition, sizeof(condition), &pos) || !ParseRoom())
        return 0;
    int size = rc_trigger_size(condition);
    return size > 0 ? ALIGN8(size) : 0;
}

// The next line with something on it (not blank, not a comment): its first
// character and its end. False at the end of the file.
static bool NextLine(const char **pos, const char *end, const char **first, const char **lineEnd)
{
    while (*pos < end)
    {
        const char *line = *pos;
        const char *next = memchr(line, '\n', (size_t)(end - line));
        *lineEnd = next ? next : end;
        *pos = next ? next + 1 : end;
        while (line < *lineEnd && IsSpace(*line)) line++;
        if (line < *lineEnd && *line != '#')
        {
            *first = line;
            return true;
        }
    }
    return false;
}

// The game's name from a "@title" line: the first one, trimmed; a character outside ASCII (a UTF-8
// sequence) becomes one '?', as in SMDH titles (the fonts only have ASCII).
static void SetTitle(const char *start, const char *end)
{
    char text[TITLE_CAPACITY];
    CopyTrimmed(text, sizeof(text), start, end);
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0' && n + 1 < sizeof(g_title); p++)
        if (*p < 0x80)
            g_title[n++] = (char)*p;
        else if (*p >= 0xC0)                    // the first byte of a character; its other bytes are skipped
            g_title[n++] = '?';
    g_title[n] = '\0';
}

// Whether line starts with the keyword and a space.
static bool IsKeyword(const char *line, const char *end, const char *keyword)
{
    size_t length = strlen(keyword);
    return (size_t)(end - line) > length && memcmp(line, keyword, length) == 0 && IsSpace(line[length]);
}

// "@version <name> <address> <size> <value>": the version's name for the banner (one word, "1.2"),
// then an address and value read like a simple line's (hex with 0x, else decimal; size 1, 2 or 4;
// the value must fit the size; the address moved by version.txt's shifts), nothing after. The name
// comes first so that builds from before "@version" can't read the line as a simple achievement
// (they'd take the address for its size). A line that doesn't read this way still counts, but
// never matches: a game isn't tracked on a check this build doesn't understand.
static void ParseVersion(const char *pos, const char *end)
{
    VersionCheck *v = &g_versions[g_versionCount++];
    char          name[64], address[16], size[8], value[16];
    uint32_t      sizeValue = 0;

    v->valid = NextToken(&pos, end, name, sizeof(name)) && NextToken(&pos, end, address, sizeof(address))
               && NextToken(&pos, end, size, sizeof(size)) && NextToken(&pos, end, value, sizeof(value))
               && ParseU32(address, &v->address)
               && ParseU32(size, &sizeValue) && (sizeValue == 1 || sizeValue == 2 || sizeValue == 4)
               && ParseU32(value, &v->value) && (sizeValue == 4 || (v->value >> (sizeValue * 8)) == 0);
    while (pos < end && IsSpace(*pos))
        pos++;
    if (!v->valid || pos != end)            // anything after the value
    {
        v->valid = false;
        return;
    }
    v->size = (uint8_t)sizeValue;
    v->address = ShiftAddress(v->address);
    if (g_versionCount == 1)
        snprintf(g_versionName, sizeof(g_versionName), "%.*s", (int)sizeof(g_versionName) - 1, name);  // cut short
}

// A line starting with '@' is for the tracker, never an achievement (older builds skip it as an
// invalid line). "@block-plugin <text>", "@title <name>" and "@version ..." are recorded here,
// "@missable" lines by MarkMissable once every achievement is read; other keywords are left for
// later builds.
static bool TrackerLine(const char *line, const char *end)
{
    if (*line != '@')
        return false;
    if (IsKeyword(line, end, VERSION_KEYWORD) && g_versionCount < MAX_VERSIONS)
        ParseVersion(line + strlen(VERSION_KEYWORD), end);
    else if (IsKeyword(line, end, PLUGIN_KEYWORD) && g_pluginBlockCount < MAX_PLUGIN_BLOCKS)
    {
        char *text = g_pluginBlocks[g_pluginBlockCount];
        CopyTrimmed(text, PLUGIN_TEXT, line + strlen(PLUGIN_KEYWORD), end);
        if (text[0] != '\0')
            g_pluginBlockCount++;
    }
    else if (IsKeyword(line, end, TITLE_KEYWORD) && g_title[0] == '\0')
        SetTitle(line + strlen(TITLE_KEYWORD), end);
    return true;
}

const char *Achievements_Title(void)
{
    return g_title;
}

static char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// Whether text appears in path, ignoring case.
static bool ContainsIgnoringCase(const char *path, const char *text)
{
    size_t length = strlen(text);
    for (const char *p = path; *p != '\0'; p++)
    {
        size_t i = 0;
        while (i < length && p[i] != '\0' && Lower(p[i]) == Lower(text[i]))
            i++;
        if (i == length)
            return true;
    }
    return length == 0;
}

int Achievements_PluginBlockCount(void)
{
    return g_pluginBlockCount;
}

int Achievements_VersionCount(void)
{
    return g_versionCount;
}

bool Achievements_VersionMatches(uint32_t *seen)
{
    bool read = false;
    if (seen != NULL)
        *seen = 0xFFFFFFFFu;
    for (int i = 0; i < g_versionCount; i++)
    {
        uint32_t value;
        if (!g_versions[i].valid || !Platform_ReadMemory(g_versions[i].address, g_versions[i].size, &value))
            continue;
        if (seen != NULL && !read)
            *seen = value;
        read = true;
        if (value == g_versions[i].value)
            return true;
    }
    return g_versionCount == 0;
}

const char *Achievements_VersionName(void)
{
    return g_versionName;
}

bool Achievements_PluginBlocked(const char *path)
{
    for (int i = 0; i < g_pluginBlockCount; i++)
        if (strcmp(g_pluginBlocks[i], "*") == 0 || (path != NULL && ContainsIgnoringCase(path, g_pluginBlocks[i])))
            return true;
    return false;
}

static void MarkUnlocked(const char *id)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_ach[i].id, id) == 0)
            g_ach[i].unlocked = true;
}

// "@missable <id> [<id> ...]" anywhere in the file (before or after those achievements' lines):
// each ID's achievement can be missed for good in a playthrough. Unknown IDs match nothing.
static void MarkMissable(const char *file, const char *end)
{
    const size_t keyword = strlen(MISSABLE_KEYWORD);
    const char  *pos = file, *first, *lineEnd;

    while (NextLine(&pos, end, &first, &lineEnd))
    {
        if ((size_t)(lineEnd - first) <= keyword || memcmp(first, MISSABLE_KEYWORD, keyword) != 0
            || !IsSpace(first[keyword]))
            continue;
        const char *p = first + keyword;
        char        id[32];
        while (p < lineEnd)
        {
            bool ok = NextToken(&p, lineEnd, id, sizeof(id));
            for (int i = 0; ok && i < g_count; i++)
                if (strcmp(g_ach[i].id, id) == 0)
                    g_ach[i].missable = true;
        }
    }
}

static bool IsFirstLine(int i);

// Links each part to its achievement: the first "@parts" line with the ID before its '/'.
// A part whose achievement isn't in the file stays unlinked and is never checked.
static void FindParents(void)
{
    for (int i = 0; i < g_count; i++)
    {
        Achievement *a = &g_ach[i];
        if (!a->isPart)
            continue;
        size_t length = (size_t)(strchr(a->id, '/') - a->id);
        for (int j = 0; j < g_count && a->parent < 0; j++)
            if (g_ach[j].isParent && strlen(g_ach[j].id) == length && memcmp(g_ach[j].id, a->id, length) == 0)
                a->parent = (int16_t)j;
    }
}

// How many parts the achievement on line p has (lines sharing a part's ID count once), and
// how many of them are done.
static void PartsOf(int p, int *done, int *total)
{
    *done = *total = 0;
    for (int i = 0; i < g_count; i++)
        if (g_ach[i].isPart && g_ach[i].parent == p && IsFirstLine(i))
        {
            (*total)++;
            if (g_ach[i].unlocked)
                (*done)++;
        }
}

// Whether line i is an achievement: the first line with its ID, not a part, and (one done in
// parts) with at least one part.
static bool Listed(int i)
{
    if (g_ach[i].isPart || !IsFirstLine(i))
        return false;
    if (!g_ach[i].isParent)
        return true;
    int done, total;
    PartsOf(i, &done, &total);
    return total > 0;
}

// A title ID written as 16 hex digits (no 0x).
static bool ParseTitleId(const char *token, uint64_t *out)
{
    if (strlen(token) != 16)
        return false;
    char *end;
    *out = strtoull(token, &end, 16);
    return *end == '\0';
}

// /achievements/<titleId>/version.txt marks another release of a game (e.g.
// another region), which shares that game's definitions and progress:
//
//     game  0004000000033500                   the game's folder
//     shift 0x0871E840 0x08723FFF +0x92FD0     optional, up to 8: addresses in
//                                              this range are read moved by the amount
//
// '#' starts a comment. Any other line makes the file invalid, so a release
// with differences this version can't apply isn't tracked with wrong addresses.
static bool ReadVersion(uint64_t titleId, uint64_t *gameId, Shift *shifts, int *shiftCount)
{
    char        path[64], file[VERSION_CAPACITY + 1], word[24], first[24], last[24], amount[24];
    uint32_t    length;
    bool        haveGame = false;

    *shiftCount = 0;
    snprintf(path, sizeof(path), "/achievements/%016llX/version.txt", (unsigned long long)titleId);
    if (!Platform_ReadFile(path, file, VERSION_CAPACITY, &length))
        return false;

    for (const char *line = file, *end = file + length; line < end; )
    {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *lineEnd = next ? next : end;
        const char *hash = memchr(line, '#', (size_t)(lineEnd - line));
        const char *pos = line;
        if (hash != NULL)
            lineEnd = hash;
        line = next ? next + 1 : end;

        if (!NextToken(&pos, lineEnd, word, sizeof(word)))
            continue;                                   // blank or comment
        if (strcmp(word, "game") == 0 && !haveGame)
        {
            if (!NextToken(&pos, lineEnd, word, sizeof(word)) || !ParseTitleId(word, gameId) || *gameId == titleId)
                return false;
            haveGame = true;
        }
        else if (strcmp(word, "shift") == 0 && *shiftCount < MAX_SHIFTS)
        {
            Shift   *s = &shifts[*shiftCount];
            uint32_t size;
            if (!NextToken(&pos, lineEnd, first, sizeof(first)) || !NextToken(&pos, lineEnd, last, sizeof(last))
                || !NextToken(&pos, lineEnd, amount, sizeof(amount))
                || (amount[0] != '+' && amount[0] != '-')
                || !ParseU32(first, &s->first) || !ParseU32(last, &s->last) || !ParseU32(amount + 1, &size)
                || s->last < s->first || size > 0x7FFFFFFF)
                return false;
            s->amount = amount[0] == '-' ? -(int32_t)size : (int32_t)size;
            (*shiftCount)++;
        }
        else
            return false;
        if (NextToken(&pos, lineEnd, word, sizeof(word)))
            return false;                               // anything after the values
    }
    return haveGame;
}

uint64_t Achievements_GameId(void)
{
    return g_gameId;
}

void Achievements_Unload(void)
{
    free(g_block);
    g_block = NULL;
    g_ach = NULL;
    g_count = 0;
    g_pluginBlockCount = 0;
    g_title[0] = '\0';
    g_versionCount = 0;
    g_versionName[0] = '\0';
    g_measuredChanged = false;
}

// measured.txt: "<id> <value>" per line; values for IDs not in the definitions are ignored.
static void LoadMeasured(char *file, uint32_t length)
{
    for (const char *line = file, *fileEnd = file + length; line < fileEnd; )
    {
        const char *next = memchr(line, '\n', (size_t)(fileEnd - line));
        const char *pos = line, *end = next ? next : fileEnd;
        char        id[sizeof(g_ach[0].id)], number[16];
        uint32_t    value;
        if (NextToken(&pos, end, id, sizeof(id)) && NextToken(&pos, end, number, sizeof(number))
            && ParseU32(number, &value))
            for (int i = 0; i < g_count; i++)
                if (g_ach[i].trigger != NULL && strcmp(g_ach[i].id, id) == 0)
                    g_ach[i].measured = value;
        line = next ? next + 1 : fileEnd;
    }
}

bool Achievements_Load(uint64_t titleId)
{
    char        path[64];
    uint32_t    length;
    const char *pos, *end, *first, *lineEnd;

    Achievements_Unload();
    g_gameId = titleId;
    g_shiftCount = 0;
    g_noRoom = false;

    // The file is read twice: first to measure what its lines need, then, once that's
    // allocated, to parse them. The file's buffer is freed last and so goes back to
    // the system at once (a heap only shrinks from its end). Too little memory for any
    // of it loads nothing, rather than some of the achievements.
    char *file = malloc(FILE_CAPACITY + 1);
    if (file == NULL)
        return false;

    // The game's own folder, or (another release) the game named by its version.txt.
    snprintf(path, sizeof(path), "/achievements/%016llX/definitions.txt", (unsigned long long)titleId);
    if (!Platform_ReadFile(path, file, FILE_CAPACITY, &length))
    {
        uint64_t    gameId = 0;
        Shift       shifts[MAX_SHIFTS];
        int         shiftCount;
        if (!ReadVersion(titleId, &gameId, shifts, &shiftCount))
        {
            free(file);
            return false;
        }
        snprintf(path, sizeof(path), "/achievements/%016llX/definitions.txt", (unsigned long long)gameId);
        if (!Platform_ReadFile(path, file, FILE_CAPACITY, &length))
        {
            free(file);
            return false;
        }
        g_gameId = gameId;
        memcpy(g_shift, shifts, sizeof(shifts));
        g_shiftCount = shiftCount;
    }
    snprintf(g_unlockPath, sizeof(g_unlockPath), "/achievements/%016llX/progress.txt", (unsigned long long)g_gameId);
    snprintf(g_measuredPath, sizeof(g_measuredPath), "/achievements/%016llX/measured.txt", (unsigned long long)g_gameId);

    int    lines = 0;
    size_t conditionBytes = 0;
    for (pos = file, end = file + length; lines < MAX_LINES && NextLine(&pos, end, &first, &lineEnd); )
    {
        char        parent[ID_CAPACITY];
        const char *line = *first != '@' ? first : IsPartLine(first, lineEnd)
                           ? PartRest(first, lineEnd, parent, sizeof(parent)) : NULL;
        if (line != NULL)                   // the tracker's own lines (TrackerLine) aren't achievements
        {
            conditionBytes += ConditionBytes(line, lineEnd);
            lines++;
        }
    }
    free(file);
    if (lines == 0 || g_noRoom)
        return false;

    size_t tableBytes = ALIGN8((size_t)lines * sizeof(Achievement));
    g_block = malloc(tableBytes + conditionBytes);
    file = g_block != NULL ? malloc(FILE_CAPACITY + 1) : NULL;
    if (file == NULL || !Platform_ReadFile(path, file, FILE_CAPACITY, &length))
    {
        free(file);
        Achievements_Unload();
        return false;
    }
    g_ach = g_block;
    g_spare = (uint8_t *)g_block + tableBytes;
    g_spareEnd = g_spare + conditionBytes;

    // To the end of the file: a tracker line may come after the last achievement.
    for (pos = file, end = file + length; NextLine(&pos, end, &first, &lineEnd); )
    {
        bool parsed = IsPartLine(first, lineEnd)
                      ? g_count < lines && ParsePart(first, lineEnd, &g_ach[g_count])
                      : !TrackerLine(first, lineEnd) && g_count < lines && ParseLine(first, lineEnd, &g_ach[g_count]);
        if (parsed)
            g_count++;
    }
    g_spare = g_spareEnd = NULL;
    MarkMissable(file, file + length);
    FindParents();

    // Unknown IDs (e.g. removed achievements) simply match nothing.
    if (g_count > 0 && Platform_ReadFile(g_unlockPath, file, FILE_CAPACITY, &length))
    {
        for (const char *line = file, *fileEnd = file + length; line < fileEnd; )
        {
            const char *next = memchr(line, '\n', (size_t)(fileEnd - line));
            char id[sizeof(g_ach[0].id)];
            CopyTrimmed(id, sizeof(id), line, next ? next : fileEnd);
            if (id[0] != '\0')
                MarkUnlocked(id);
            line = next ? next + 1 : fileEnd;
        }
    }
    if (g_count > 0 && Platform_ReadFile(g_measuredPath, file, FILE_CAPACITY, &length))
        LoadMeasured(file, length);
    free(file);
    if (g_count == 0 || g_noRoom)
        Achievements_Unload();
    return g_count > 0;
}

// rcheevos reads memory through this. Unreadable memory reads as 0.
static uint32_t Peek(uint32_t address, uint32_t numBytes, void *userData)
{
    uint32_t value;
    (void)userData;
    return numBytes <= 4 && Platform_ReadMemory(address, (uint8_t)numBytes, &value) ? value : 0;
}

// Saves line i's ID as done (every line with that ID): an achievement or a part.
static void SaveDone(int i)
{
    const char *id = g_ach[i].id;
    char        line[sizeof(g_ach[0].id) + 1];
    size_t      length = strlen(id);

    MarkUnlocked(id);
    memcpy(line, id, length);
    line[length] = '\n';
    line[length + 1] = '\0';
    Platform_AppendFile(g_unlockPath, line);
}

// The first line with line i's ID: its name and description are the achievement's (or part's).
static int FirstLineOf(int i)
{
    for (int j = 0; j < i; j++)
        if (strcmp(g_ach[j].id, g_ach[i].id) == 0)
            return j;
    return i;
}

static void Unlock(int i)
{
    SaveDone(i);
    i = FirstLineOf(i);
    Platform_Unlocked(g_ach[i].name, g_ach[i].description);
}

void Achievements_Update(void)
{
    for (int i = 0; i < g_count; i++)
    {
        Achievement *a = &g_ach[i];
        uint32_t     value;

        if (a->unlocked)
            continue;
        if (a->isParent)
        {
            // Done through its parts (below); this catches parts that were all done
            // before it was saved (e.g. the console turned off in between).
            int done, total;
            PartsOf(i, &done, &total);
            if (total > 0 && done == total && IsFirstLine(i))
                Unlock(i);
            continue;
        }
        if (a->isPart && a->parent < 0)
            continue;                           // a part of no achievement in the file

        if (a->trigger != NULL)
        {
            // One poll is one rcheevos frame (deltas compare with the last poll).
            int state = rc_evaluate_trigger(a->trigger, Peek, NULL, NULL);
            // Its Measured value, if it has one: 0 = nothing to measure now (a MeasuredIf is false).
            uint32_t m = a->trigger->measured_value;
            if (a->trigger->measured_target != 0 && m != RC_MEASURED_UNKNOWN && m != 0 && m != a->measured)
            {
                a->measured = m;
                g_measuredChanged = true;
            }
            if (state != RC_TRIGGER_STATE_TRIGGERED)
                continue;
        }
        else
        {
            bool match = Platform_ReadMemory(a->address, a->size, &value)
                         && (a->atLeast ? value >= a->expected : value == a->expected);
            if (!match)
            {
                a->matches = 0;
                continue;
            }
            if (++a->matches < MATCH_POLLS)
                continue;
        }

        // Lines sharing an ID are alternative conditions for one achievement (or part).
        if (!a->isPart)
        {
            Unlock(i);
            continue;
        }
        int p = a->parent, done, total;
        SaveDone(i);
        PartsOf(p, &done, &total);
        if (done < total)
            Platform_PartDone(g_ach[p].name, g_ach[FirstLineOf(i)].name, done, total);
        else if (!g_ach[p].unlocked)
            Unlock(p);                          // the last part: the achievement itself
    }
}

int Achievements_Count(void)
{
    return g_count;
}

// Whether line i is the first with its ID (later lines are extra conditions).
static bool IsFirstLine(int i)
{
    for (int j = 0; j < i; j++)
        if (strcmp(g_ach[j].id, g_ach[i].id) == 0)
            return false;
    return true;
}

void Achievements_Progress(int *unlocked, int *total)
{
    *unlocked = *total = 0;
    for (int i = 0; i < g_count; i++)
    {
        if (!Listed(i))
            continue;
        (*total)++;
        if (g_ach[i].unlocked)
            (*unlocked)++;
    }
}

int Achievements_List(const char **names, bool *unlocked, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
    {
        if (!Listed(i))
            continue;
        names[n] = g_ach[i].name;
        unlocked[n] = g_ach[i].unlocked;
        n++;
    }
    return n;
}

int Achievements_Descriptions(const char **descriptions, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (Listed(i))
            descriptions[n++] = g_ach[i].description;
    return n;
}

int Achievements_Ids(const char **ids, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (Listed(i))
            ids[n++] = g_ach[i].id;
    return n;
}

int Achievements_Points(uint16_t *points, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (Listed(i))
            points[n++] = g_ach[i].points;
    return n;
}

int Achievements_Missable(bool *missable, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (Listed(i))
            missable[n++] = g_ach[i].missable;
    return n;
}

void Achievements_Score(int *earned, int *total)
{
    *earned = *total = 0;
    for (int i = 0; i < g_count; i++)
    {
        if (!Listed(i))
            continue;
        *total += g_ach[i].points;
        if (g_ach[i].unlocked)
            *earned += g_ach[i].points;
    }
}

int Achievements_Parts(uint8_t *done, uint8_t *total, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (Listed(i))
        {
            int d = 0, t = 0;
            if (g_ach[i].isParent)
                PartsOf(i, &d, &t);
            done[n] = (uint8_t)(d > 255 ? 255 : d);
            total[n] = (uint8_t)(t > 255 ? 255 : t);
            n++;
        }
    return n;
}

int Achievements_PartList(int index, const char **names, bool *done, int capacity)
{
    int p = -1;
    for (int i = 0, n = 0; i < g_count && p < 0; i++)
        if (Listed(i) && n++ == index)
            p = i;
    if (p < 0 || !g_ach[p].isParent)
        return 0;
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (g_ach[i].isPart && g_ach[i].parent == p && IsFirstLine(i))
        {
            names[n] = g_ach[i].name;
            done[n] = g_ach[i].unlocked;
            n++;
        }
    return n;
}

// The measured value, target and percent flag of the achievement whose first line is i: the
// highest over the lines sharing its ID (each line is its own trigger, e.g. one per save file).
static void MeasuredOf(int i, uint32_t *value, uint32_t *target, bool *percent)
{
    *value = *target = 0;
    *percent = false;
    for (int j = i; j < g_count; j++)
    {
        const rc_trigger_t *t = g_ach[j].trigger;
        if (t == NULL || t->measured_target == 0 || strcmp(g_ach[j].id, g_ach[i].id) != 0)
            continue;
        if (t->measured_target > *target)
            *target = t->measured_target;
        if (g_ach[j].measured > *value)
            *value = g_ach[j].measured;
        *percent |= t->measured_as_percent != 0;
    }
}

int Achievements_Measured(uint32_t *value, uint32_t *target, bool *percent, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (Listed(i))
        {
            MeasuredOf(i, &value[n], &target[n], &percent[n]);
            n++;
        }
    return n;
}

bool Achievements_MeasuredTextAt(int index, bool full, char *out, size_t size)
{
    for (int i = 0, n = 0; i < g_count; i++)
        if (Listed(i) && n++ == index)
        {
            uint32_t value, target;
            bool     percent;
            MeasuredOf(i, &value, &target, &percent);
            if (target == 0 || value == 0 || g_ach[i].unlocked)
                return false;
            Achievements_MeasuredText(value, target, percent, full, out, size);
            return true;
        }
    return false;
}

void Achievements_SaveMeasured(void)
{
    if (!g_measuredChanged || g_count == 0)
        return;
    size_t room = (size_t)g_count * (sizeof(g_ach[0].id) + 12) + 1;
    char  *text = malloc(room);
    if (text == NULL)
        return;                                 // tried again next time
    size_t length = 0;
    for (int i = 0; i < g_count; i++)
    {
        uint32_t value, target;
        bool     percent;
        if (!IsFirstLine(i) || g_ach[i].isPart || g_ach[i].unlocked)
            continue;                           // unlocked: its progress isn't shown any more
        MeasuredOf(i, &value, &target, &percent);
        if (target != 0 && value != 0)
            length += (size_t)snprintf(text + length, room - length, "%s %lu\n", g_ach[i].id, (unsigned long)value);
    }
    if (Platform_WriteFile(g_measuredPath, text, (uint32_t)length))
        g_measuredChanged = false;
    free(text);
}

// n with a comma every three digits ("12,345").
static void Grouped(uint32_t n, char *out, size_t size)
{
    char digits[16], *p = out;
    int  count = snprintf(digits, sizeof(digits), "%lu", (unsigned long)n);
    for (int i = 0; i < count && (size_t)(p - out) + 2 < size; i++)
    {
        if (i > 0 && (count - i) % 3 == 0)
            *p++ = ',';
        *p++ = digits[i];
    }
    *p = '\0';
}

void Achievements_MeasuredText(uint32_t value, uint32_t target, bool percent, bool full, char *out, size_t size)
{
    char a[16], b[16];
    if (value > target)
        value = target;
    if (percent)
    {
        snprintf(out, size, "%lu%%", target ? (unsigned long)((uint64_t)value * 100 / target) : 0ul);
        return;
    }
    Grouped(value, a, sizeof(a));
    Grouped(target, b, sizeof(b));
    snprintf(out, size, full ? "%s of %s" : "%s/%s", a, b);
}
