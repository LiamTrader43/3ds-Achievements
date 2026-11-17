#ifndef STORE_FILE_H
#define STORE_FILE_H

// Store items' files (store.c): each ships encrypted as /achievements/store/<name>.bin, made by
// tools/store_pack.py when the app is built ("make store"), and is only decrypted to where it's
// used once it's bought. The key is compiled into release builds only (source/store_key.h, made
// by tools/store_key.py, never in the repository), so a file is no use without the app.
//
// A .bin: "A3SF", u32 format (1), a 16-byte IV, u32 size, size bytes encrypted with AES-256-CTR,
// then the HMAC-SHA256 of the item's name, a 0 byte and everything before it. The key is 64
// bytes: AES's, then the HMAC's.

#include <3ds.h>
#include <stdbool.h>

#define STORE_FILE_DIR      "/achievements/store"

#define STORE_FILE_MISSING  ((Result)0xE0000101)    // the item's .bin isn't on the SD card (yet)
#define STORE_FILE_DAMAGED  ((Result)0xE0000102)    // it isn't whole, or wasn't made with this key
#define STORE_NO_KEY        ((Result)0xE0000103)    // this build has no key (not a release build)
#define STORE_FILE_WRITE    ((Result)0xE0000104)    // what it holds couldn't be written

// Whether this build can open store files.
bool    StoreFile_HaveKey(void);

// Whether /achievements/store/<name>.bin is here, whole and made with this build's key.
bool    StoreFile_Ready(const char *name);

// Decrypts /achievements/store/<name>.bin to path: written to path + ".tmp" first, then put in
// place of path. Nothing is left behind if it fails.
Result  StoreFile_Install(const char *name, const char *path);

#endif
