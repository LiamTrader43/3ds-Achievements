#ifndef ICONS_H
#define ICONS_H

// A game's own picture for each achievement (optional): /achievements/<TitleID>/icons.bin,
// made by tools/achievement_icons.py. Read by the tracker's in-game list and the app (which
// compiles this file too); plain C, so it can be tested on a PC. An achievement without a
// picture there gets the game's trophy.bin, or the usual trophy.
//
// The file, little-endian:
//   0   4   "A3IC"
//   4   2   format, 1
//   6   2   number of entries, at most ICONS_MOST
//   8       the entries, 40 bytes each:
//           +0   32  the achievement's ID, padded with zeros (1-31 characters)
//           +32  4   where its unlocked picture is in the file
//           +36  4   where its locked picture is, or 0: made from the unlocked one
//                    (Icons_MakeLocked, greyed and darker)
//   then the pictures, ICONS_PICTURE_BYTES each: 48x48 RGB565 in SMDH tile order, as
//   trophy.bin's, a pixel of 0 being see-through. Each at a multiple of 4, after the entries.
// A newer format changes the format number, so this reader leaves it alone. An entry that
// doesn't fit these rules, or names an ID the game doesn't have, is skipped; an ID listed
// twice takes its first entry.

#include <stdbool.h>
#include <stdint.h>

#define ICONS_MAGIC         0x43493341u         // "A3IC"
#define ICONS_FORMAT        1
#define ICONS_SIZE          48
#define ICONS_PIXELS        (ICONS_SIZE * ICONS_SIZE)
#define ICONS_PICTURE_BYTES (ICONS_PIXELS * 2)
#define ICONS_HEADER_BYTES  8
#define ICONS_ID_BYTES      32
#define ICONS_ENTRY_BYTES   40
#define ICONS_MOST          256                 // entries (achievements.c's MAX_LINES)
#define ICONS_FILE_MOST     (ICONS_HEADER_BYTES + ICONS_MOST * ICONS_ENTRY_BYTES + 2 * ICONS_MOST * ICONS_PICTURE_BYTES)

// Where an achievement's pictures are in the file: 0 = none (no locked picture: make it).
typedef struct
{
    uint32_t    unlocked, locked;
} IconsPlace;

// How many bytes of the file the header and entries take, from its first ICONS_HEADER_BYTES
// (and its size): 0 if it isn't an icons.bin this reader knows.
uint32_t Icons_IndexBytes(const uint8_t *header, uint32_t fileSize);

// Finds each of ids[0..count)'s pictures in index (the file's first Icons_IndexBytes bytes).
// places[i] is { 0, 0 } for an achievement without any. Returns how many have a picture.
int      Icons_Find(const uint8_t *index, uint32_t fileSize, const char *const *ids, int count, IconsPlace *places);

// The locked picture made from an unlocked one: grey and darker (as tools/trophy_art.py makes a
// trophy.bin's). See-through pixels stay see-through, and no other pixel becomes one.
void     Icons_MakeLocked(const uint16_t *unlocked, uint16_t *locked);

#endif
