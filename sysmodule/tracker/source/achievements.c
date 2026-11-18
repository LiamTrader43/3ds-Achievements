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
    char            id[32];
    uint32_t        address;
    uint8_t         size;
    bool            atLeast;
    uint32_t        expected;
    rc_trigger_t   *trigger;    // set instead for an "rc:" condition (in g_block)
    char            name[NAME_CAPACITY];
    char            description[DESCRIPTION_CAPACITY];
    uint16_t        points;
    bool            unlocked;
    uint8_t         matches;
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
static uint64_t     g_gameId;
static Shift        g_shift[MAX_SHIFTS];
static int          g_shiftCount;
static char         g_pluginBlocks[MAX_PLUGIN_BLOCKS][PLUGIN_TEXT];
static int          g_pluginBlockCount;

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
        memcpy(a->name, a->id, strlen(a->id) + 1);  // id[32] always fits name[64]
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
#define CONDITION_CAPACITY 1024
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

static bool ParseLine(const char *line, const char *end, Achievement *a)
{
    char        address[16], size[8], expected[16];
    const char *pos = line;
    uint32_t    sizeValue;

    a->trigger = NULL;
    a->unlocked = false;
    a->matches = 0;
    if (!NextToken(&pos, end, a->id, sizeof(a->id)))
        return false;
    while (pos < end && IsSpace(*pos)) pos++;
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

// A line starting with '@' is for the tracker, never an achievement (older builds skip it as an
// invalid line). "@block-plugin <text>" is recorded; other keywords are left for later builds.
static bool TrackerLine(const char *line, const char *end)
{
    const size_t keyword = strlen(PLUGIN_KEYWORD);

    if (*line != '@')
        return false;
    if ((size_t)(end - line) > keyword && memcmp(line, PLUGIN_KEYWORD, keyword) == 0 && IsSpace(line[keyword])
        && g_pluginBlockCount < MAX_PLUGIN_BLOCKS)
    {
        char *text = g_pluginBlocks[g_pluginBlockCount];
        CopyTrimmed(text, PLUGIN_TEXT, line + keyword, end);
        if (text[0] != '\0')
            g_pluginBlockCount++;
    }
    return true;
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

    int    lines = 0;
    size_t conditionBytes = 0;
    for (pos = file, end = file + length; lines < MAX_LINES && NextLine(&pos, end, &first, &lineEnd); )
        if (*first != '@')                  // the tracker's own lines (TrackerLine) aren't achievements
        {
            conditionBytes += ConditionBytes(first, lineEnd);
            lines++;
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
        if (!TrackerLine(first, lineEnd) && g_count < lines && ParseLine(first, lineEnd, &g_ach[g_count]))
            g_count++;
    g_spare = g_spareEnd = NULL;

    // Unknown IDs (e.g. removed achievements) simply match nothing.
    if (g_count > 0 && Platform_ReadFile(g_unlockPath, file, FILE_CAPACITY, &length))
    {
        for (const char *line = file, *fileEnd = file + length; line < fileEnd; )
        {
            const char *next = memchr(line, '\n', (size_t)(fileEnd - line));
            char id[32];
            CopyTrimmed(id, sizeof(id), line, next ? next : fileEnd);
            if (id[0] != '\0')
                MarkUnlocked(id);
            line = next ? next + 1 : fileEnd;
        }
    }
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

void Achievements_Update(void)
{
    for (int i = 0; i < g_count; i++)
    {
        Achievement *a = &g_ach[i];
        uint32_t     value;

        if (a->unlocked)
            continue;

        if (a->trigger != NULL)
        {
            // One poll is one rcheevos frame (deltas compare with the last poll).
            if (rc_evaluate_trigger(a->trigger, Peek, NULL, NULL) != RC_TRIGGER_STATE_TRIGGERED)
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

        // Lines sharing an ID are alternative conditions for one achievement.
        MarkUnlocked(a->id);

        char line[sizeof(a->id) + 1];
        size_t length = strlen(a->id);
        memcpy(line, a->id, length);
        line[length] = '\n';
        line[length + 1] = '\0';
        Platform_AppendFile(g_unlockPath, line);
        Platform_Unlocked(a->name, a->description);
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
        if (!IsFirstLine(i))
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
        if (!IsFirstLine(i))
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
        if (IsFirstLine(i))
            descriptions[n++] = g_ach[i].description;
    return n;
}

int Achievements_Ids(const char **ids, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (IsFirstLine(i))
            ids[n++] = g_ach[i].id;
    return n;
}

int Achievements_Points(uint16_t *points, int capacity)
{
    int n = 0;
    for (int i = 0; i < g_count && n < capacity; i++)
        if (IsFirstLine(i))
            points[n++] = g_ach[i].points;
    return n;
}

void Achievements_Score(int *earned, int *total)
{
    *earned = *total = 0;
    for (int i = 0; i < g_count; i++)
    {
        if (!IsFirstLine(i))
            continue;
        *total += g_ach[i].points;
        if (g_ach[i].unlocked)
            *earned += g_ach[i].points;
    }
}
