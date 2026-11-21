#include "hotkey.h"

#include <stdio.h>
#include <string.h>

static const struct
{
    uint32_t    key;
    const char *name;       // as shown
    const char *word;       // in hotkey.txt
} BUTTONS[] = {
    { HOTKEY_L, "L", "L" },                 { HOTKEY_R, "R", "R" },
    { HOTKEY_A, "A", "A" },                 { HOTKEY_B, "B", "B" },
    { HOTKEY_X, "X", "X" },                 { HOTKEY_Y, "Y", "Y" },
    { HOTKEY_UP, "Up", "UP" },              { HOTKEY_DOWN, "Down", "DOWN" },
    { HOTKEY_LEFT, "Left", "LEFT" },        { HOTKEY_RIGHT, "Right", "RIGHT" },
    { HOTKEY_SELECT, "Select", "SELECT" },  { HOTKEY_START, "Start", "START" },
};
#define BUTTON_COUNT (int)(sizeof(BUTTONS) / sizeof(BUTTONS[0]))

int Hotkey_Count(uint32_t keys)
{
    int n = 0;
    for (int i = 0; i < BUTTON_COUNT; i++)
        n += (keys & BUTTONS[i].key) != 0;
    return n;
}

bool Hotkey_Valid(uint32_t keys)
{
    int n = Hotkey_Count(keys);
    return (keys & ~HOTKEY_BUTTONS) == 0 && n >= HOTKEY_LEAST && n <= HOTKEY_MOST;
}

static bool SameWord(const char *a, size_t length, const char *word)
{
    if (strlen(word) != length)
        return false;
    for (size_t i = 0; i < length; i++)
    {
        char c = a[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if (c != word[i])
            return false;
    }
    return true;
}

uint32_t Hotkey_Parse(const char *text)
{
    uint32_t keys = 0;
    const char *p = text;
    while (*p != '\0')
    {
        while (*p == ' ' || *p == '\t' || *p == '+' || *p == ',' || *p == '\r' || *p == '\n')
            p++;
        if (*p == '\0')
            break;
        const char *start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '+' && *p != ',' && *p != '\r' && *p != '\n')
            p++;
        int i = 0;
        while (i < BUTTON_COUNT && !SameWord(start, (size_t)(p - start), BUTTONS[i].word))
            i++;
        if (i == BUTTON_COUNT)
            return 0;                       // not a button
        keys |= BUTTONS[i].key;
    }
    return Hotkey_Valid(keys) ? keys : 0;
}

static void Join(uint32_t keys, const char *separator, bool words, char *out, size_t size)
{
    size_t used = 0;
    if (size == 0)
        return;
    out[0] = '\0';
    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        if (!(keys & BUTTONS[i].key))
            continue;
        int n = snprintf(out + used, size - used, "%s%s", used > 0 ? separator : "",
                         words ? BUTTONS[i].word : BUTTONS[i].name);
        if (n < 0 || (size_t)n >= size - used)
            return;                         // cut short (out is still terminated)
        used += (size_t)n;
    }
}

void Hotkey_Text(uint32_t keys, const char *separator, char *out, size_t size)
{
    Join(keys, separator, false, out, size);
}

void Hotkey_FileText(uint32_t keys, char *out, size_t size)
{
    Join(keys, " ", true, out, size);
    size_t length = strlen(out);
    if (length + 1 < size)
        out[length] = '\n', out[length + 1] = '\0';
}

bool Hotkey_Overlaps(uint32_t keys, uint32_t combo)
{
    if (keys == 0 || combo == 0)
        return false;
    uint32_t both = keys & combo;
    return both == keys || both == combo;
}

// Luma's names for the bits of a combination (arm9/source/config.c keyNames; "?" = no button).
static const char *const LUMA_NAMES[32] = {
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "X", "Y",
    "?", "?",
    "ZL", "ZR",
    "?", "?", "?", "?",
    "Touch",
    "?", "?", "?",
    "CStick Right", "CStick Left", "CStick Up", "CStick Down",
    "CPad Right", "CPad Left", "CPad Up", "CPad Down",
};

static bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool SameName(const char *a, size_t length, const char *name)
{
    if (strlen(name) != length)
        return false;
    for (size_t i = 0; i < length; i++)
    {
        char x = a[i], y = name[i];
        if (x >= 'a' && x <= 'z')
            x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = (char)(y - 'a' + 'A');
        if (x != y)
            return false;
    }
    return true;
}

// [p, end): names joined by '+', each with any spaces around it.
static uint32_t ParseLuma(const char *p, const char *end)
{
    uint32_t combo = 0;
    for (;;)
    {
        const char *plus = p;
        while (plus < end && *plus != '+')
            plus++;
        const char *a = p, *b = plus;
        while (a < b && IsSpace(*a))
            a++;
        while (b > a && IsSpace(b[-1]))
            b--;
        int i = 0;
        while (i < 32 && (LUMA_NAMES[i][0] == '?' || !SameName(a, (size_t)(b - a), LUMA_NAMES[i])))
            i++;
        if (i == 32)
            return 0;                       // unknown, or empty (a '+' at either end, "++")
        combo |= 1u << i;
        if (plus == end)
            return combo;
        p = plus + 1;
    }
}

uint32_t Hotkey_ParseLuma(const char *text)
{
    return ParseLuma(text, text + strlen(text));
}

uint32_t Hotkey_LumaConfigCombo(const char *ini)
{
    // Like Luma's ini reader: [sections], "name = value" (or ':'), ';' or '#' starting a comment line, and
    // ';' after a space starting one at the end of a line. Luma reads the option only in [rosalina].
    bool rosalina = false;
    const char *line = ini;
    while (*line != '\0')
    {
        const char *next = line;
        while (*next != '\0' && *next != '\n')
            next++;
        const char *a = line, *b = next;
        while (a < b && IsSpace(*a))
            a++;
        while (b > a && IsSpace(b[-1]))
            b--;
        if (a < b && *a == '[')
        {
            const char *close = a + 1;
            while (close < b && *close != ']')
                close++;
            rosalina = close < b && (size_t)(close - a - 1) == 8 && memcmp(a + 1, "rosalina", 8) == 0;
        }
        else if (a < b && *a != ';' && *a != '#' && rosalina)
        {
            const char *eq = a;
            while (eq < b && *eq != '=' && *eq != ':')
                eq++;
            const char *nameEnd = eq;
            while (nameEnd > a && IsSpace(nameEnd[-1]))
                nameEnd--;
            if (eq < b && (size_t)(nameEnd - a) == 19 && memcmp(a, "rosalina_menu_combo", 19) == 0)
            {
                const char *v = eq + 1, *vEnd = v;
                while (vEnd < b && !(*vEnd == ';' && IsSpace(vEnd[-1])))
                    vEnd++;
                while (v < vEnd && IsSpace(*v))
                    v++;
                while (vEnd > v && IsSpace(vEnd[-1]))
                    vEnd--;
                return v < vEnd ? ParseLuma(v, vEnd) : 0;
            }
        }
        line = *next == '\n' ? next + 1 : next;
    }
    return 0;
}

HotkeyWarning Hotkey_Warning(uint32_t keys)
{
    const uint32_t reset1 = HOTKEY_L | HOTKEY_R | HOTKEY_START, reset2 = HOTKEY_L | HOTKEY_R | HOTKEY_SELECT;
    const uint32_t play = HOTKEY_A | HOTKEY_B | HOTKEY_X | HOTKEY_Y | HOTKEY_L | HOTKEY_R;

    if ((keys & reset1) == reset1 || (keys & reset2) == reset2)
        return HOTKEY_RESET;
    if (Hotkey_Count(keys) <= 2)
        return HOTKEY_FEW;
    if ((keys & ~play) == 0)
        return HOTKEY_IN_PLAY;
    if (keys & (HOTKEY_START | HOTKEY_SELECT))
        return HOTKEY_GAME_MENU;
    return HOTKEY_FINE;
}
