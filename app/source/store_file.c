// The store's files (store_file.h): checked with HMAC-SHA256, decrypted with AES-256-CTR
// (mbedTLS, which the app has for its downloads).
#include "store_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#if defined(__has_include)
#if __has_include("store_key.h")
#include "store_key.h"              // STORE_KEY: only in release builds (gitignored)
#endif
#endif

#define HEADER_SIZE     (4 + 4 + 16 + 4)
#define MAC_SIZE        32
#define FORMAT          1
#define MOST_SIZE       (4u * 1024u * 1024u)
#define NAME_MOST       15

static const unsigned char MAGIC[4] = { 'A', '3', 'S', 'F' };

static void Wipe(void *data, size_t size)
{
    volatile unsigned char *p = data;
    while (size-- > 0)
        *p++ = 0;
}

#ifdef STORE_KEY
static int HexDigit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
#endif

// The key: AES's 32 bytes, then the HMAC's. False in a build without one.
static bool Key(unsigned char aes[32], unsigned char mac[32])
{
#ifdef STORE_KEY
    static const char hex[] = STORE_KEY;
    unsigned char key[64];
    if (sizeof(hex) != 2 * sizeof(key) + 1)
        return false;
    for (size_t i = 0; i < sizeof(key); i++)
    {
        int high = HexDigit(hex[2 * i]), low = HexDigit(hex[2 * i + 1]);
        if (high < 0 || low < 0)
            return false;
        key[i] = (unsigned char)(high << 4 | low);
    }
    memcpy(aes, key, 32);
    memcpy(mac, key + 32, 32);
    Wipe(key, sizeof(key));
    return true;
#else
    (void)aes;
    (void)mac;
    return false;
#endif
}

bool StoreFile_HaveKey(void)
{
    unsigned char aes[32], mac[32];
    bool have = Key(aes, mac);
    Wipe(aes, sizeof(aes));
    Wipe(mac, sizeof(mac));
    return have;
}

static bool NameOk(const char *name)
{
    size_t length = strlen(name);
    if (length == 0 || length > NAME_MOST)
        return false;
    for (const char *c = name; *c != '\0'; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return false;
    return true;
}

static uint32_t Get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// The HMAC-SHA256 of name, a 0 byte and data.
static bool Hmac(const unsigned char key[32], const char *name, const unsigned char *data, size_t size,
                 unsigned char out[MAC_SIZE])
{
    mbedtls_md_context_t md;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool ok = info != NULL;

    mbedtls_md_init(&md);
    ok = ok && mbedtls_md_setup(&md, info, 1) == 0 && mbedtls_md_hmac_starts(&md, key, 32) == 0 &&
         mbedtls_md_hmac_update(&md, (const unsigned char *)name, strlen(name) + 1) == 0 &&     // with its 0
         mbedtls_md_hmac_update(&md, data, size) == 0 && mbedtls_md_hmac_finish(&md, out) == 0;
    mbedtls_md_free(&md);
    return ok;
}

// Reads /achievements/store/<name>.bin whole (malloc'd) and checks it: its header, its size and
// its HMAC. *size: the file's.
static Result Load(const char *name, const unsigned char macKey[32], unsigned char **data, uint32_t *size)
{
    char path[64];

    *data = NULL;
    if (!NameOk(name))
        return STORE_FILE_MISSING;
    snprintf(path, sizeof(path), STORE_FILE_DIR "/%s.bin", name);
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return STORE_FILE_MISSING;
    long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (length < HEADER_SIZE + MAC_SIZE || (unsigned long)length > MOST_SIZE || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return STORE_FILE_DAMAGED;
    }
    unsigned char *bytes = malloc((size_t)length);
    bool read = bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!read)
    {
        free(bytes);
        return bytes == NULL ? STORE_FILE_WRITE : STORE_FILE_DAMAGED;
    }

    unsigned char tag[MAC_SIZE];
    uint32_t body = (uint32_t)length - MAC_SIZE, difference = 0;
    if (memcmp(bytes, MAGIC, 4) != 0 || Get32(bytes + 4) != FORMAT || Get32(bytes + 24) != body - HEADER_SIZE ||
        !Hmac(macKey, name, bytes, body, tag))
    {
        free(bytes);
        return STORE_FILE_DAMAGED;
    }
    for (int i = 0; i < MAC_SIZE; i++)
        difference |= (uint32_t)(tag[i] ^ bytes[body + i]);         // every byte, so it takes the same time
    if (difference != 0)
    {
        free(bytes);
        return STORE_FILE_DAMAGED;
    }
    *data = bytes;
    *size = (uint32_t)length;
    return 0;
}

bool StoreFile_Ready(const char *name)
{
    unsigned char aes[32], mac[32], *data;
    uint32_t size;

    if (!Key(aes, mac))
        return false;
    Result res = Load(name, mac, &data, &size);
    Wipe(aes, sizeof(aes));
    Wipe(mac, sizeof(mac));
    free(data);
    return R_SUCCEEDED(res);
}

// AES-256-CTR from the IV, in place.
static bool Crypt(const unsigned char aes[32], const unsigned char iv[16], unsigned char *data, size_t size)
{
    unsigned char counter[16], stream[16];
    size_t offset = 0;
    mbedtls_aes_context context;

    mbedtls_aes_init(&context);
    memcpy(counter, iv, sizeof(counter));
    bool ok = mbedtls_aes_setkey_enc(&context, aes, 256) == 0 &&
              mbedtls_aes_crypt_ctr(&context, size, &offset, counter, stream, data, data) == 0;
    mbedtls_aes_free(&context);
    Wipe(stream, sizeof(stream));
    return ok;
}

// Reads and checks /achievements/store/<name>.bin and decrypts it where it is: *data (malloc'd)
// holds the whole file, its contents (*plain bytes) from HEADER_SIZE.
static Result Decrypt(const char *name, unsigned char **data, uint32_t *size, uint32_t *plain)
{
    unsigned char aes[32], mac[32];

    *data = NULL;
    if (!Key(aes, mac))
        return STORE_NO_KEY;
    Result res = Load(name, mac, data, size);
    if (R_SUCCEEDED(res))
    {
        *plain = *size - HEADER_SIZE - MAC_SIZE;
        if (!Crypt(aes, *data + 8, *data + HEADER_SIZE, *plain))
        {
            Wipe(*data, *size);
            free(*data);
            *data = NULL;
            res = STORE_FILE_DAMAGED;
        }
    }
    Wipe(aes, sizeof(aes));
    Wipe(mac, sizeof(mac));
    return res;
}

Result StoreFile_Install(const char *name, const char *path)
{
    unsigned char *data;
    uint32_t size = 0, plain = 0;
    char temporary[96];

    Result res = Decrypt(name, &data, &size, &plain);
    if (R_FAILED(res))
        return res;

    // Written beside the old file, then put in its place.
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    bool written = file != NULL && fwrite(data + HEADER_SIZE, 1, plain, file) == plain;
    written = file != NULL && fclose(file) == 0 && written;
    if (written)
        remove(path);               // (the SD card's rename won't replace a file)
    if (!written || rename(temporary, path) != 0)
    {
        remove(temporary);
        res = STORE_FILE_WRITE;
    }
    Wipe(data, size);
    free(data);
    return res;
}

Result StoreFile_Read(const char *name, char **out, uint32_t *outSize)
{
    unsigned char *data;
    uint32_t size = 0, plain = 0;

    *out = NULL;
    *outSize = 0;
    Result res = Decrypt(name, &data, &size, &plain);
    if (R_FAILED(res))
        return res;
    memmove(data, data + HEADER_SIZE, plain);
    data[plain] = '\0';                     // (there's room: the header and HMAC were bigger)
    *out = (char *)data;
    *outSize = plain;
    return 0;
}

Result StoreFile_Peek(const char *name, void *out, uint32_t size)
{
    unsigned char aes[32], mac[32], header[HEADER_SIZE];
    char path[64];

    if (!Key(aes, mac))
        return STORE_NO_KEY;
    Wipe(mac, sizeof(mac));
    Result res = STORE_FILE_MISSING;
    snprintf(path, sizeof(path), STORE_FILE_DIR "/%s.bin", name);
    FILE *file = NameOk(name) ? fopen(path, "rb") : NULL;
    if (file != NULL)
    {
        res = fread(header, 1, HEADER_SIZE, file) == HEADER_SIZE && memcmp(header, MAGIC, 4) == 0 &&
              Get32(header + 4) == FORMAT && Get32(header + 24) >= size && fread(out, 1, size, file) == size &&
              Crypt(aes, header + 8, out, size) ? 0 : STORE_FILE_DAMAGED;
        fclose(file);
    }
    Wipe(aes, sizeof(aes));
    return res;
}

bool StoreFile_Version(const char *name, char hex[65])
{
    unsigned char tag[MAC_SIZE];
    char path[64];

    hex[0] = '\0';
    snprintf(path, sizeof(path), STORE_FILE_DIR "/%s.bin", name);
    FILE *file = NameOk(name) ? fopen(path, "rb") : NULL;
    if (file == NULL)
        return false;
    bool ok = fseek(file, -MAC_SIZE, SEEK_END) == 0 && fread(tag, 1, MAC_SIZE, file) == MAC_SIZE;
    fclose(file);
    for (int i = 0; ok && i < MAC_SIZE; i++)
        snprintf(hex + 2 * i, 3, "%02x", tag[i]);
    return ok;
}
