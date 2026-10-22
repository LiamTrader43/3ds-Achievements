#include "achievements.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES       128
#define FILE_CAPACITY   0x8000
#define MATCH_POLLS     3       // consecutive polls a condition must hold

typedef struct
{
    char        id[32];
    uint32_t    address;
    uint8_t     size;
    bool        atLeast;
    uint32_t    expected;
    char        name[64];
    char        description[128];
    bool        unlocked;
    uint8_t     matches;
} Achievement;

static Achievement  g_ach[MAX_LINES];
static int          g_count;
static char         g_unlockPath[64];
static char         g_file[FILE_CAPACITY + 1];

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

static bool ParseLine(const char *line, const char *end, Achievement *a)
{
    char        address[16], size[8], expected[16];
    const char *pos = line;
    uint32_t    sizeValue;

    if (!NextToken(&pos, end, a->id, sizeof(a->id)) || !NextToken(&pos, end, address, sizeof(address))
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

    // The first '|' separates the name from the description.
    const char *bar = memchr(pos, '|', (size_t)(end - pos));
    CopyTrimmed(a->name, sizeof(a->name), pos, bar ? bar : end);
    CopyTrimmed(a->description, sizeof(a->description), bar ? bar + 1 : end, end);
    if (a->name[0] == '\0')
        memcpy(a->name, a->id, strlen(a->id) + 1);  // id[32] always fits name[64]

    a->unlocked = false;
    a->matches = 0;
    return true;
}

static void MarkUnlocked(const char *id)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_ach[i].id, id) == 0)
            g_ach[i].unlocked = true;
}

bool Achievements_Load(uint64_t titleId)
{
    char        path[64];
    uint32_t    length;

    g_count = 0;
    snprintf(path, sizeof(path), "/achievements/%016llX.txt", (unsigned long long)titleId);
    snprintf(g_unlockPath, sizeof(g_unlockPath), "/achievements/%016llX_unlocked.txt", (unsigned long long)titleId);

    if (!Platform_ReadFile(path, g_file, FILE_CAPACITY, &length))
        return false;

    for (const char *line = g_file, *end = g_file + length; line < end && g_count < MAX_LINES; )
    {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *lineEnd = next ? next : end;
        const char *first = line;
        while (first < lineEnd && IsSpace(*first)) first++;

        if (first < lineEnd && *first != '#' && ParseLine(first, lineEnd, &g_ach[g_count]))
            g_count++;
        line = next ? next + 1 : end;
    }

    // Unknown IDs (e.g. removed achievements) simply match nothing.
    if (g_count > 0 && Platform_ReadFile(g_unlockPath, g_file, FILE_CAPACITY, &length))
    {
        for (const char *line = g_file, *end = g_file + length; line < end; )
        {
            const char *next = memchr(line, '\n', (size_t)(end - line));
            char id[32];
            CopyTrimmed(id, sizeof(id), line, next ? next : end);
            if (id[0] != '\0')
                MarkUnlocked(id);
            line = next ? next + 1 : end;
        }
    }
    return g_count > 0;
}

void Achievements_Update(void)
{
    for (int i = 0; i < g_count; i++)
    {
        Achievement *a = &g_ach[i];
        uint32_t     value;

        if (a->unlocked)
            continue;

        bool match = Platform_ReadMemory(a->address, a->size, &value)
                     && (a->atLeast ? value >= a->expected : value == a->expected);
        if (!match)
        {
            a->matches = 0;
            continue;
        }
        if (++a->matches < MATCH_POLLS)
            continue;

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
