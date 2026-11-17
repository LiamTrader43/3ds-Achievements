#ifndef THEME_LOADER_H
#define THEME_LOADER_H

// Loading theme add-ons: .theme files (theme_api.h: ThemeHeader).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "theme_api.h"

// Reads a .theme file's ThemeInfo (from its header), without running anything in it. False if
// it isn't a theme this app can load (why says why, in a few words).
bool         ThemeLoader_Info(const char *path, ThemeInfo *info, char *why, size_t capacity);

// Loads the theme in a .theme file and starts it: its Theme, or NULL (why says why). It stays
// loaded until the app closes. bytes: the memory it took.
const Theme *ThemeLoader_Load(const char *path, size_t *bytes, char *why, size_t capacity);

// A .theme file in memory (page-aligned, malloc'd) with its relocations applied: what
// ThemeLoader_Load makes runnable and starts. Offsets are from image.
typedef struct
{
    uint8_t    *image;
    uint32_t    imageSize;              // bytes allocated (whole pages)
    uint32_t    codeSize;               // the pages made runnable: 0..codeSize
    uint32_t    entry;
    int         relocations;
    ThemeInfo   info;
} ThemeImage;

// Reads size bytes at offset of a .theme file into to; false if it can't.
typedef bool (*ThemeRead)(void *source, uint32_t offset, void *to, uint32_t size);

// Checks a .theme file of size bytes and reads it straight into place (exposed for tests on
// the PC; the app only uses ThemeLoader_Load).
bool         ThemeLoader_Prepare(ThemeRead read, void *source, uint32_t size, ThemeImage *out, char *why,
                                 size_t capacity);

#endif
