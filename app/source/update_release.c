#include "update_release.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- A little JSON: just enough to walk GitHub's answer without being fooled by nesting.

static const char *SkipSpace(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

// Points past the string starting at p (its opening quote), or NULL.
static const char *SkipString(const char *p)
{
    for (p++; *p != '\0'; p++)
    {
        if (*p == '\\')
        {
            if (p[1] == '\0')
                return NULL;
            p++;
        }
        else if (*p == '"')
            return p + 1;
    }
    return NULL;
}

// Points past the value starting at p (a string, number, word, object or array), or NULL.
static const char *SkipValue(const char *p)
{
    if (*p == '"')
        return SkipString(p);
    if (*p == '{' || *p == '[')
    {
        int depth = 0;
        while (*p != '\0')
        {
            if (*p == '"')
            {
                p = SkipString(p);
                if (p == NULL)
                    return NULL;
                continue;
            }
            if (*p == '{' || *p == '[')
                depth++;
            else if (*p == '}' || *p == ']')
            {
                if (--depth == 0)
                    return p + 1;
            }
            p++;
        }
        return NULL;
    }
    const char *start = p;
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t')
        p++;
    return p != start ? p : NULL;
}

// The value of key among the members of the object starting at p (its '{'), not looking
// inside nested objects; NULL if it isn't there.
static const char *Member(const char *p, const char *key)
{
    size_t length = strlen(key);

    if (p == NULL || *p != '{')
        return NULL;
    p = SkipSpace(p + 1);
    while (*p == '"')
    {
        const char *name = p + 1;
        const char *after = SkipString(p);
        if (after == NULL)
            return NULL;
        bool match = (size_t)(after - 1 - name) == length && strncmp(name, key, length) == 0;
        p = SkipSpace(after);
        if (*p != ':')
            return NULL;
        p = SkipSpace(p + 1);
        if (match)
            return p;
        p = SkipValue(p);
        if (p == NULL)
            return NULL;
        p = SkipSpace(p);
        if (*p != ',')
            return NULL;
        p = SkipSpace(p + 1);
    }
    return NULL;
}

// Copies the string value at p into out. False if it isn't a string, has escapes or doesn't fit.
static bool ReadString(const char *p, char *out, size_t capacity)
{
    if (p == NULL || *p != '"')
        return false;
    const char *end = SkipString(p);
    if (end == NULL)
        return false;
    size_t length = (size_t)(end - 1 - (p + 1));
    if (length >= capacity || memchr(p + 1, '\\', length) != NULL)
        return false;
    memcpy(out, p + 1, length);
    out[length] = '\0';
    return true;
}

// ---- Versions

// Splits "1.4.0" into numbers; false if it isn't 1-4 numbers separated by dots.
static bool ParseVersion(const char *text, unsigned long parts[4])
{
    int count = 0;

    memset(parts, 0, 4 * sizeof(*parts));
    while (count < 4)
    {
        if (!isdigit((unsigned char)*text))
            return false;
        char *end;
        parts[count++] = strtoul(text, &end, 10);
        if (end - text > 6)
            return false;
        text = end;
        if (*text == '\0')
            return true;
        if (*text != '.')
            return false;
        text++;
    }
    return false;
}

int UpdateRelease_Compare(const char *a, const char *b)
{
    unsigned long pa[4], pb[4];
    bool va = ParseVersion(a, pa), vb = ParseVersion(b, pb);

    if (!va || !vb)
        return (int)va - (int)vb;
    for (int i = 0; i < 4; i++)
        if (pa[i] != pb[i])
            return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

bool UpdateRelease_Parse(const char *json, const char *urlPrefix, UpdateRelease *release)
{
    char tag[24], digest[80];

    memset(release, 0, sizeof(*release));
    const char *root = SkipSpace(json);
    if (!ReadString(Member(root, "tag_name"), tag, sizeof(tag)))
        return false;
    const char *version = tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag;
    unsigned long parts[4];
    if (!ParseVersion(version, parts) || strlen(version) >= sizeof(release->version))
        return false;

    const char *p = Member(root, "assets");
    if (p == NULL || *p != '[')
        return false;
    p = SkipSpace(p + 1);
    while (*p == '{')
    {
        char name[64];
        if (ReadString(Member(p, "name"), name, sizeof(name)) && strcmp(name, UPDATE_RELEASE_ASSET) == 0)
        {
            const char *size = Member(p, "size");
            if (!ReadString(Member(p, "browser_download_url"), release->url, sizeof(release->url)) ||
                strncmp(release->url, urlPrefix, strlen(urlPrefix)) != 0 || size == NULL || !isdigit((unsigned char)*size))
                break;
            unsigned long bytes = strtoul(size, NULL, 10);
            if (bytes == 0 || bytes > 64ul * 1024 * 1024)
                break;
            release->size = (uint32_t)bytes;

            // "sha256:<64 hex digits>"; GitHub leaves it out (or null) for older uploads.
            if (ReadString(Member(p, "digest"), digest, sizeof(digest)) && strncmp(digest, "sha256:", 7) == 0 &&
                strlen(digest + 7) == 64)
            {
                for (int i = 0; i < 64; i++)
                {
                    if (!isxdigit((unsigned char)digest[7 + i]))
                        break;
                    release->sha256[i] = (char)tolower((unsigned char)digest[7 + i]);
                }
                if (strlen(release->sha256) != 64)
                    release->sha256[0] = '\0';
            }
            strcpy(release->version, version);
            return true;
        }
        p = SkipValue(p);
        if (p == NULL)
            break;
        p = SkipSpace(p);
        if (*p != ',')
            break;
        p = SkipSpace(p + 1);
    }
    memset(release, 0, sizeof(*release));
    return false;
}

// ---- SHA-256

static const uint32_t K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t Right(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void Sha256_Block(UpdateSha256 *s, const uint8_t *data)
{
    uint32_t w[64];

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)data[i * 4] << 24 | (uint32_t)data[i * 4 + 1] << 16 | (uint32_t)data[i * 4 + 2] << 8 | data[i * 4 + 3];
    for (int i = 16; i < 64; i++)
    {
        uint32_t s0 = Right(w[i - 15], 7) ^ Right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = Right(w[i - 2], 17) ^ Right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = s->state[0], b = s->state[1], c = s->state[2], d = s->state[3];
    uint32_t e = s->state[4], f = s->state[5], g = s->state[6], h = s->state[7];
    for (int i = 0; i < 64; i++)
    {
        uint32_t t1 = h + (Right(e, 6) ^ Right(e, 11) ^ Right(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (Right(a, 2) ^ Right(a, 13) ^ Right(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

void UpdateSha256_Start(UpdateSha256 *s)
{
    static const uint32_t INITIAL[8] =
    {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(s->state, INITIAL, sizeof(INITIAL));
    s->length = 0;
    s->used = 0;
}

void UpdateSha256_Add(UpdateSha256 *s, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    s->length += size;
    while (size > 0)
    {
        if (s->used == 0 && size >= 64)
        {
            Sha256_Block(s, bytes);
            bytes += 64;
            size -= 64;
            continue;
        }
        size_t take = 64 - s->used < size ? 64 - s->used : size;
        memcpy(s->block + s->used, bytes, take);
        s->used += (uint32_t)take;
        bytes += take;
        size -= take;
        if (s->used == 64)
        {
            Sha256_Block(s, s->block);
            s->used = 0;
        }
    }
}

void UpdateSha256_Finish(UpdateSha256 *s, char hex[65])
{
    uint64_t bits = s->length * 8;
    static const uint8_t one = 0x80, zero = 0;

    UpdateSha256_Add(s, &one, 1);
    while (s->used != 56)
        UpdateSha256_Add(s, &zero, 1);
    uint8_t tail[8];
    for (int i = 0; i < 8; i++)
        tail[i] = (uint8_t)(bits >> (56 - 8 * i));
    UpdateSha256_Add(s, tail, 8);

    for (int i = 0; i < 8; i++)
        snprintf(hex + i * 8, 9, "%08lx", (unsigned long)s->state[i]);
}
