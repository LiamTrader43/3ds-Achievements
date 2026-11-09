#ifndef UPDATE_TREE_H
#define UPDATE_TREE_H

// Reads GitHub's listing of the main branch (no 3DS code here, so it can be tested on a PC).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_MAX_FILES    96

typedef struct
{
    char        titleId[17];    // the game's folder, 16 hex digits, uppercase
    char        name[16];       // definitions.txt, hero.bin or trophy.bin
    char        sha[41];        // GitHub's ID for this version of the file
    uint32_t    size;
    bool        wanted;         // needs downloading
} UpdateFile;

// Finds every downloadable game file in a "git/trees/main?recursive=1" response. Only
// the three known file names, at their allowed sizes, in a folder named with a title ID
// are accepted, so nothing in the listing can choose where a file gets written.
// Returns the number found, or -1 if GitHub cut the listing short.
int     UpdateTree_Parse(const char *json, UpdateFile *files, int capacity);

// True if data is the file GitHub calls sha (the SHA-1 of "blob <size>\0" + the data).
bool    UpdateTree_Matches(const uint8_t *data, uint32_t size, const char *sha);

#endif
