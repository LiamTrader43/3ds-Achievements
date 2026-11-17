#ifndef UPDATE_ZIP_H
#define UPDATE_ZIP_H

// Reads files out of a release zip (stored or deflated, as zip tools write them) and checks
// each one's size and CRC. No 3DS code here, so it can be tested on a PC.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum
{
    UPDATE_ZIP_READ = 1,        // the zip couldn't be read
    UPDATE_ZIP_FORMAT,          // not a zip this can read (encrypted, zip64, another method)
    UPDATE_ZIP_DATA,            // the compressed data is damaged
    UPDATE_ZIP_CHECK,           // the unpacked file isn't the one the zip describes (size or CRC)
    UPDATE_ZIP_WRITE,           // the writer refused the data
};

typedef struct
{
    uint32_t    crc, compressedSize, size, headerOffset;
    uint16_t    method;         // 0 stored, 8 deflated
} UpdateZipEntry;

// Hands unpacked data on; false stops the unpacking.
typedef bool (*UpdateZipWriter)(void *user, const void *data, size_t size);

// Finds the file called name (its whole path in the zip, e.g. "3ds/achievements-viewer.3dsx").
bool    UpdateZip_Find(FILE *zip, const char *name, UpdateZipEntry *entry);

// Unpacks the entry a piece at a time, then checks its size and CRC. 0 or an UPDATE_ZIP_ error.
int     UpdateZip_Extract(FILE *zip, const UpdateZipEntry *entry, UpdateZipWriter writer, void *user);

// True if the file at path is exactly the entry's file (same size and CRC).
bool    UpdateZip_Same(const char *path, const UpdateZipEntry *entry);

#endif
