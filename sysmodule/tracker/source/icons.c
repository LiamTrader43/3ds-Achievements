#include "icons.h"

#include <string.h>

#define DARKEST     0x0821      // the darkest grey that isn't see-through (0 is)

static uint32_t Read16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t Read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t Icons_IndexBytes(const uint8_t *header, uint32_t fileSize)
{
    if (fileSize < ICONS_HEADER_BYTES || fileSize > ICONS_FILE_MOST || Read32(header) != ICONS_MAGIC ||
        Read16(header + 4) != ICONS_FORMAT)
        return 0;
    uint32_t count = Read16(header + 6);
    uint32_t bytes = ICONS_HEADER_BYTES + count * ICONS_ENTRY_BYTES;
    return count <= ICONS_MOST && bytes <= fileSize ? bytes : 0;
}

// A picture's place, if it's one: inside the file, after the entries, at a multiple of 4.
static bool PictureAt(uint32_t offset, uint32_t indexBytes, uint32_t fileSize)
{
    return offset >= indexBytes && offset % 4 == 0 && offset <= fileSize && fileSize - offset >= ICONS_PICTURE_BYTES;
}

int Icons_Find(const uint8_t *index, uint32_t fileSize, const char *const *ids, int count, IconsPlace *places)
{
    for (int i = 0; i < count; i++)
        places[i] = (IconsPlace){ 0, 0 };
    uint32_t indexBytes = Icons_IndexBytes(index, fileSize);
    if (indexBytes == 0)
        return 0;

    int found = 0;
    uint32_t entries = Read16(index + 6);
    for (uint32_t e = 0; e < entries; e++)
    {
        const uint8_t *entry = index + ICONS_HEADER_BYTES + e * ICONS_ENTRY_BYTES;
        size_t length = strnlen((const char *)entry, ICONS_ID_BYTES);
        uint32_t unlocked = Read32(entry + ICONS_ID_BYTES), locked = Read32(entry + ICONS_ID_BYTES + 4);
        if (length == 0 || length == ICONS_ID_BYTES || !PictureAt(unlocked, indexBytes, fileSize) ||
            (locked != 0 && !PictureAt(locked, indexBytes, fileSize)))
            continue;
        for (int i = 0; i < count; i++)
            if (places[i].unlocked == 0 && ids[i] != NULL && strlen(ids[i]) == length &&
                memcmp(ids[i], entry, length) == 0)
            {
                places[i] = (IconsPlace){ unlocked, locked };
                found++;
                break;
            }
    }
    return found;
}

void Icons_MakeLocked(const uint16_t *unlocked, uint16_t *locked)
{
    for (int i = 0; i < ICONS_PIXELS; i++)
    {
        uint32_t v = unlocked[i];
        if (v == 0)
        {
            locked[i] = 0;
            continue;
        }
        uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        r = r << 3 | r >> 2;
        g = g << 2 | g >> 4;
        b = b << 3 | b >> 2;
        uint32_t grey = (r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16;     // as Pillow's greyscale
        grey = grey * 45 / 100;
        uint16_t out = (uint16_t)((grey >> 3) << 11 | (grey >> 2) << 5 | grey >> 3);
        locked[i] = out != 0 ? out : DARKEST;
    }
}
