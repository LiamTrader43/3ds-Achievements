#ifndef UPDATE_RELEASE_H
#define UPDATE_RELEASE_H

// Reads GitHub's description of the latest release and checks the download it points to
// (no 3DS code here, so it can be tested on a PC).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_RELEASE_ASSET    "3ds-achievements.zip"  // the release's zip (Universal-Updater uses it too)

typedef struct
{
    char        version[16];    // the tag without its "v", e.g. "1.4.0"
    char        url[192];       // where the zip downloads from
    uint32_t    size;           // its size in bytes
    char        sha256[65];     // its SHA-256 as GitHub gives it (lowercase hex), or "" if it doesn't
} UpdateRelease;

// Finds the tag and the 3ds-achievements.zip asset in a "releases/latest" response. Only a
// download address starting with urlPrefix (this repository's releases) is accepted, and
// only a tag that is a version number ("v1.4.0").
bool    UpdateRelease_Parse(const char *json, const char *urlPrefix, UpdateRelease *release);

// Compares versions like "1.4.0" and "1.3.10" (missing parts count as 0): below 0, 0 or
// above 0, like strcmp. Text that isn't a version counts as older than any version.
int     UpdateRelease_Compare(const char *a, const char *b);

// SHA-256, a piece at a time.
typedef struct
{
    uint32_t    state[8];
    uint64_t    length;
    uint8_t     block[64];
    uint32_t    used;
} UpdateSha256;

void    UpdateSha256_Start(UpdateSha256 *s);
void    UpdateSha256_Add(UpdateSha256 *s, const void *data, size_t size);
void    UpdateSha256_Finish(UpdateSha256 *s, char hex[65]);    // lowercase

#endif
