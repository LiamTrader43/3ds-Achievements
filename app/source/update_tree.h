#ifndef UPDATE_TREE_H
#define UPDATE_TREE_H

// Reads GitHub's listing of the main branch (no 3DS code here, so it can be tested on a PC).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_MAX_FILES    96
#define UPDATE_STORE_FOLDER "store"                 // the store's files (store_file.h)
#define UPDATE_STORE_MOST   (256u * 1024u)          // the biggest store file taken

typedef struct
{
    char        titleId[17];    // the game's folder, 16 hex digits, uppercase; or "store"
    char        name[24];       // definitions.txt, hero.bin, trophy.bin, icons.bin, title.smdh or version.txt;
                                // a store file's <name>.bin
    char        sha[41];        // GitHub's ID for this version of the file
    uint32_t    size;
    bool        store;          // one of the store's files
    bool        wanted;         // needs downloading
} UpdateFile;

// Finds every downloadable file in a "git/trees/main?recursive=1" response: games' files (only
// the six known file names, at their allowed sizes, in a folder named with a title ID) and the
// store's (achievements/store/<name>.bin, name 1-15 of a-z 0-9 _ -), so nothing in the listing
// can choose where a file gets written. Returns the number found, or -1 if GitHub cut the
// listing short.
int     UpdateTree_Parse(const char *json, UpdateFile *files, int capacity);

// Whether name is a store file's: <name>.bin, name 1-15 of a-z 0-9 _ - (store_file.c's rule).
bool    UpdateTree_StoreName(const char *name);

// True if data is the file GitHub calls sha (the SHA-1 of "blob <size>\0" + the data).
bool    UpdateTree_Matches(const uint8_t *data, uint32_t size, const char *sha);

#endif
