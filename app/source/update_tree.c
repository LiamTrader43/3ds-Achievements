#include "update_tree.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HERO_SIZE           288000u
#define TROPHY_SIZE         9216u
#define DEFINITIONS_MAX     32768u      // what the tracker can read
#define FOLDER_PREFIX       "achievements/"

static bool IsHex(const char *text, int count)
{
    for (int i = 0; i < count; i++)
        if (!isxdigit((unsigned char)text[i]))
            return false;
    return true;
}

// Points just past the ':' after "key" inside [from, to), or NULL.
static const char *FindValue(const char *from, const char *to, const char *key)
{
    char   quoted[16];
    size_t length = (size_t)snprintf(quoted, sizeof(quoted), "\"%s\"", key);

    for (const char *p = from; p != NULL && p + length <= to; p = strchr(p + 1, '"'))
    {
        if (strncmp(p, quoted, length) != 0)
            continue;
        p += length;
        while (p < to && (*p == ' ' || *p == ':'))
            p++;
        return p < to ? p : NULL;
    }
    return NULL;
}

// Copies a quoted value into out. False if it doesn't fit.
static bool ReadString(const char *value, char *out, size_t capacity)
{
    if (value == NULL || *value != '"')
        return false;
    value++;
    const char *end = strchr(value, '"');
    if (end == NULL || (size_t)(end - value) >= capacity)
        return false;
    memcpy(out, value, (size_t)(end - value));
    out[end - value] = '\0';
    return true;
}

static bool SizeAllowed(const char *name, uint32_t size)
{
    if (strcmp(name, "hero.bin") == 0)
        return size == HERO_SIZE;
    if (strcmp(name, "trophy.bin") == 0)
        return size == TROPHY_SIZE;
    if (strcmp(name, "definitions.txt") == 0)
        return size > 0 && size <= DEFINITIONS_MAX;
    return false;
}

int UpdateTree_Parse(const char *json, UpdateFile *files, int capacity)
{
    const char *end = json + strlen(json);
    int         count = 0;

    const char *truncated = FindValue(json, end, "truncated");
    if (truncated != NULL && strncmp(truncated, "true", 4) == 0)
        return -1;

    const char *entry = strstr(json, "\"path\"");
    while (entry != NULL && count < capacity)
    {
        const char *next = strstr(entry + 6, "\"path\"");
        const char *limit = next != NULL ? next : end;
        char        path[96], sha[41];

        const char *size = FindValue(entry, limit, "size");
        const size_t prefix = strlen(FOLDER_PREFIX);
        if (ReadString(FindValue(entry, limit, "path"), path, sizeof(path)) &&
            ReadString(FindValue(entry, limit, "sha"), sha, sizeof(sha)) &&
            size != NULL && strlen(sha) == 40 && IsHex(sha, 40) &&
            strncmp(path, FOLDER_PREFIX, prefix) == 0 && strlen(path) > prefix + 17 &&
            path[prefix + 16] == '/' && IsHex(path + prefix, 16))
        {
            UpdateFile *f = &files[count];
            const char *name = path + prefix + 17;
            uint32_t    bytes = (uint32_t)strtoul(size, NULL, 10);

            if (strchr(name, '/') == NULL && strlen(name) < sizeof(f->name) && SizeAllowed(name, bytes))
            {
                for (int i = 0; i < 16; i++)
                    f->titleId[i] = (char)toupper((unsigned char)path[prefix + i]);
                f->titleId[16] = '\0';
                strcpy(f->name, name);
                strcpy(f->sha, sha);
                f->size = bytes;
                f->wanted = false;
                count++;
            }
        }
        entry = next;
    }
    return count;
}

// ---- SHA-1

typedef struct
{
    uint32_t state[5];
    uint64_t length;
    uint8_t  block[64];
    uint32_t used;
} Sha1;

static uint32_t Rotate(uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void Sha1_Block(Sha1 *s, const uint8_t *data)
{
    uint32_t w[80];

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)data[i * 4] << 24 | (uint32_t)data[i * 4 + 1] << 16 | (uint32_t)data[i * 4 + 2] << 8 | data[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = Rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = s->state[0], b = s->state[1], c = s->state[2], d = s->state[3], e = s->state[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)         { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40)    { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60)    { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else                { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
        uint32_t t = Rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotate(b, 30);
        b = a;
        a = t;
    }
    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
}

static void Sha1_Add(Sha1 *s, const uint8_t *data, size_t size)
{
    s->length += size;
    while (size > 0)
    {
        if (s->used == 0 && size >= 64)
        {
            Sha1_Block(s, data);
            data += 64;
            size -= 64;
            continue;
        }
        size_t take = 64 - s->used < size ? 64 - s->used : size;
        memcpy(s->block + s->used, data, take);
        s->used += (uint32_t)take;
        data += take;
        size -= take;
        if (s->used == 64)
        {
            Sha1_Block(s, s->block);
            s->used = 0;
        }
    }
}

bool UpdateTree_Matches(const uint8_t *data, uint32_t size, const char *sha)
{
    Sha1 s = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, 0, { 0 }, 0 };
    char header[32];
    int  headerSize = snprintf(header, sizeof(header), "blob %lu", (unsigned long)size) + 1;    // with its '\0'

    Sha1_Add(&s, (const uint8_t *)header, (size_t)headerSize);
    Sha1_Add(&s, data, size);

    uint64_t bits = s.length * 8;
    static const uint8_t one = 0x80, zero = 0;
    Sha1_Add(&s, &one, 1);
    while (s.used != 56)
        Sha1_Add(&s, &zero, 1);
    uint8_t tail[8];
    for (int i = 0; i < 8; i++)
        tail[i] = (uint8_t)(bits >> (56 - 8 * i));
    Sha1_Add(&s, tail, 8);

    char text[41];
    for (int i = 0; i < 5; i++)
        snprintf(text + i * 8, 9, "%08lx", (unsigned long)s.state[i]);
    return strcasecmp(text, sha) == 0;
}
