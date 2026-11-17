#include "update_zip.h"

#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#define END_SIGNATURE       0x06054b50u     // end of central directory
#define CENTRAL_SIGNATURE   0x02014b50u
#define LOCAL_SIGNATURE     0x04034b50u
#define END_SIZE            22
#define CENTRAL_SIZE        46
#define LOCAL_SIZE          30
#define MAX_COMMENT         0xFFFF
#define MAX_DIRECTORY       (1024 * 1024)   // a release zip's directory is a few KB
#define IN_SIZE             (16 * 1024)
#define OUT_SIZE            (32 * 1024)

static uint32_t Get16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t Get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool ReadAt(FILE *file, long offset, void *data, size_t size)
{
    return fseek(file, offset, SEEK_SET) == 0 && fread(data, 1, size, file) == size;
}

// Finds the central directory: its offset and size, from the end record.
static bool FindDirectory(FILE *zip, uint32_t *offset, uint32_t *size)
{
    if (fseek(zip, 0, SEEK_END) != 0)
        return false;
    long fileSize = ftell(zip);
    if (fileSize < END_SIZE)
        return false;

    long     tail = fileSize < END_SIZE + MAX_COMMENT ? fileSize : END_SIZE + MAX_COMMENT;
    uint8_t *buffer = malloc((size_t)tail);
    bool     found = false;
    if (buffer == NULL || !ReadAt(zip, fileSize - tail, buffer, (size_t)tail))
    {
        free(buffer);
        return false;
    }
    // The end record is the last thing in the file but its comment; look from the back.
    for (long i = tail - END_SIZE; i >= 0 && !found; i--)
    {
        const uint8_t *e = buffer + i;
        if (Get32(e) != END_SIGNATURE || i + END_SIZE + (long)Get16(e + 20) != tail)
            continue;
        *size = Get32(e + 12);
        *offset = Get32(e + 16);
        found = Get16(e + 4) == 0 && Get16(e + 6) == 0 &&          // one disk only
                *offset != 0xFFFFFFFFu && *size <= MAX_DIRECTORY &&     // no zip64
                (uint64_t)*offset + *size <= (uint64_t)(fileSize - tail + i);
    }
    free(buffer);
    return found;
}

bool UpdateZip_Find(FILE *zip, const char *name, UpdateZipEntry *entry)
{
    uint32_t offset, size;
    size_t   nameLength = strlen(name);
    bool     found = false;

    if (zip == NULL || !FindDirectory(zip, &offset, &size))
        return false;
    uint8_t *directory = malloc(size > 0 ? size : 1);
    if (directory == NULL || !ReadAt(zip, (long)offset, directory, size))
    {
        free(directory);
        return false;
    }

    for (uint32_t at = 0; at + CENTRAL_SIZE <= size && !found; )
    {
        const uint8_t *c = directory + at;
        if (Get32(c) != CENTRAL_SIGNATURE)
            break;
        uint32_t length = Get16(c + 28), extra = Get16(c + 30), comment = Get16(c + 32);
        if (at + CENTRAL_SIZE + length > size)
            break;
        if (length == nameLength && memcmp(c + CENTRAL_SIZE, name, length) == 0)
        {
            entry->method = (uint16_t)Get16(c + 10);
            entry->crc = Get32(c + 16);
            entry->compressedSize = Get32(c + 20);
            entry->size = Get32(c + 24);
            entry->headerOffset = Get32(c + 42);
            found = !(Get16(c + 8) & 1) &&                          // not encrypted
                    entry->compressedSize != 0xFFFFFFFFu && entry->size != 0xFFFFFFFFu &&
                    entry->headerOffset != 0xFFFFFFFFu &&
                    (entry->method == 0 || entry->method == 8);
            if (!found)
                break;                                              // a name only appears once
        }
        at += CENTRAL_SIZE + length + extra + comment;
    }
    free(directory);
    return found;
}

int UpdateZip_Extract(FILE *zip, const UpdateZipEntry *entry, UpdateZipWriter writer, void *user)
{
    uint8_t header[LOCAL_SIZE];

    if (!ReadAt(zip, (long)entry->headerOffset, header, sizeof(header)))
        return UPDATE_ZIP_READ;
    if (Get32(header) != LOCAL_SIGNATURE)
        return UPDATE_ZIP_FORMAT;
    long data = (long)entry->headerOffset + LOCAL_SIZE + (long)Get16(header + 26) + (long)Get16(header + 28);
    if (fseek(zip, data, SEEK_SET) != 0)
        return UPDATE_ZIP_READ;

    uint8_t *in = malloc(IN_SIZE), *out = malloc(OUT_SIZE);
    if (in == NULL || out == NULL)
    {
        free(in);
        free(out);
        return UPDATE_ZIP_READ;
    }

    z_stream z;
    memset(&z, 0, sizeof(z));
    bool     inflating = entry->method == 8;
    int      result = 0;
    uint32_t left = entry->compressedSize;      // still to read from the zip
    uint32_t written = 0;
    uLong    crc = crc32(0L, Z_NULL, 0);

    if (inflating && inflateInit2(&z, -MAX_WBITS) != Z_OK)     // raw deflate, no zlib header
        result = UPDATE_ZIP_DATA;

    bool finished = false;
    while (result == 0 && !finished)
    {
        uint32_t take = left < IN_SIZE ? left : IN_SIZE;
        if (take > 0 && fread(in, 1, take, zip) != take)
        {
            result = UPDATE_ZIP_READ;
            break;
        }
        left -= take;

        if (!inflating)
        {
            if (take > 0)
            {
                crc = crc32(crc, in, take);
                written += take;
                if (!writer(user, in, take))
                    result = UPDATE_ZIP_WRITE;
            }
            finished = left == 0;
            continue;
        }

        z.next_in = in;
        z.avail_in = take;
        do
        {
            z.next_out = out;
            z.avail_out = OUT_SIZE;
            int status = inflate(&z, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END && !(status == Z_BUF_ERROR && take > 0))
            {
                result = UPDATE_ZIP_DATA;
                break;
            }
            uint32_t made = OUT_SIZE - z.avail_out;
            if (made > 0)
            {
                if ((uint64_t)written + made > entry->size)
                {
                    result = UPDATE_ZIP_CHECK;      // more than the zip said it holds
                    break;
                }
                crc = crc32(crc, out, made);
                written += made;
                if (!writer(user, out, made))
                {
                    result = UPDATE_ZIP_WRITE;
                    break;
                }
            }
            if (status == Z_STREAM_END)
            {
                finished = true;
                break;
            }
        } while (z.avail_out == 0 || z.avail_in > 0);

        if (result == 0 && !finished && left == 0 && take == 0)
            result = UPDATE_ZIP_DATA;               // the data ended before the deflate stream did
    }

    if (inflating)
        inflateEnd(&z);
    free(in);
    free(out);
    if (result == 0 && (written != entry->size || (uint32_t)crc != entry->crc))
        result = UPDATE_ZIP_CHECK;
    return result;
}

bool UpdateZip_Same(const char *path, const UpdateZipEntry *entry)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;

    uint8_t *buffer = malloc(OUT_SIZE);
    uLong    crc = crc32(0L, Z_NULL, 0);
    uint32_t total = 0;
    size_t   got;
    while (buffer != NULL && (got = fread(buffer, 1, OUT_SIZE, file)) > 0)
    {
        crc = crc32(crc, buffer, (uInt)got);
        total += (uint32_t)got;
    }
    bool same = buffer != NULL && !ferror(file) && total == entry->size && (uint32_t)crc == entry->crc;
    free(buffer);
    fclose(file);
    return same;
}
