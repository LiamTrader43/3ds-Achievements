// Made by tools/app_sounds.js from app/sounds.json: don't edit, run it again. The app's sound
// sets, each a full set of sounds, 16-bit mono at SOUND_RATE Hz, one after another in
// data/sounds.bin (sounds_bin.h). A length of 0: that set has no sound for it.
#ifndef SOUNDS_H
#define SOUNDS_H

#define SOUND_RATE          32728
#define SOUND_SET_COUNT     5
#define SOUND_SET_DEFAULT   0       // Air

typedef enum
{
    SOUND_MOVE,       // Move
    SOUND_TICK,       // Scroll
    SOUND_BUMP,       // End of list
    SOUND_SELECT,     // Open
    SOUND_BACK,       // Back
    SOUND_TOGGLE_ON,  // Setting on
    SOUND_TOGGLE_OFF, // Setting off
    SOUND_NOTICE,     // Popup
    SOUND_ERROR,      // Problem
    SOUND_SAVED,      // Saved
    SOUND_COINS,      // Play Coins
    SOUND_BOUGHT,     // Bought
    SOUND_COMPLETE,   // Game complete
    SOUND_COUNT
} SoundId;

#ifdef SOUND_TABLES
// Each set: its id (what /achievements/sounds.txt holds), name and description; where its
// sounds start in data/sounds.bin and how much they take, in samples.
static const char *const SOUND_SET_ID[SOUND_SET_COUNT] = { "air", "felt", "glass", "string", "chip" };
static const char *const SOUND_SET_NAME[SOUND_SET_COUNT] = { "Air", "Felt", "Glass", "String", "Chip" };
static const char *const SOUND_SET_DESCRIPTION[SOUND_SET_COUNT] =
{
    "Soft pops and airy whooshes, like a modern console's TV mode.",
    "Muted wooden taps and rounded chimes. Warm and quiet.",
    "Soft bell tones, clean and quiet.",
    "Plucked guitar strings, like the app's banner jingle.",
    "Square waves like an old handheld. Suits the DS theme.",
};
static const unsigned int SOUND_SET_START[SOUND_SET_COUNT] = { 0, 316304, 604032, 820160, 1095360 };
static const unsigned int SOUND_SET_SIZE[SOUND_SET_COUNT] = { 316304, 287728, 216128, 275200, 116080 };
// Each sound in its set: where it starts (from the set's start) and its length, in samples.
static const unsigned int SOUND_START[SOUND_SET_COUNT][SOUND_COUNT] =
{
    { 0, 11520, 20656, 33472, 53360, 74656, 89632, 102928, 136928, 158208, 178784, 206960, 238368 },     // Air
    { 0, 9824, 17504, 27280, 44256, 61072, 77472, 90704, 120560, 141168, 160048, 182960, 213136 },     // Felt
    { 0, 4720, 7456, 13808, 28208, 40624, 51424, 61552, 85920, 100144, 113712, 134656, 158704 },     // Glass
    { 0, 6576, 10080, 14800, 32800, 49152, 66304, 81840, 114736, 127872, 144864, 172880, 203904 },     // String
    { 0, 1808, 2800, 5584, 10816, 15728, 18848, 21968, 31952, 42928, 50944, 62720, 77616 },     // Chip
};
static const unsigned int SOUND_LENGTH[SOUND_SET_COUNT][SOUND_COUNT] =
{
    { 11505, 9123, 12807, 19886, 21296, 14964, 13296, 33993, 21273, 20565, 28170, 31396, 77929 },
    { 9818, 7678, 9764, 16967, 16811, 16395, 13219, 29846, 20606, 18874, 22898, 30166, 74579 },
    { 4710, 2731, 6351, 14385, 12413, 10785, 10123, 24367, 14222, 13565, 20938, 24045, 57422 },
    { 6562, 3503, 4719, 17985, 16350, 17143, 15530, 32882, 13136, 16979, 28007, 31023, 71296 },
    { 1794, 978, 2777, 5230, 4902, 3108, 3107, 9978, 10963, 8010, 11774, 14886, 38454 },
};
#endif

#endif
