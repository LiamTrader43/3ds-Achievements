#ifndef SHARE_SERVER_H
#define SHARE_SERVER_H

// A tiny web server for the share screen: on the local network, GET or HEAD of one of the
// given paths answers with that file, anything else with 404. Up to a few connections at
// once, in a background thread.

#include <3ds.h>

typedef struct
{
    const char *path;           // e.g. "/share.png"
    const char *type;           // Content-Type
    const char *disposition;    // Content-Disposition (e.g. a file name to save as), or NULL
    const u8   *data;           // kept by the caller until ShareServer_Stop
    u32         size;
    bool        counts;         // a complete transfer counts in ShareServer_Sent
} ShareFile;

// Starts serving files[0..count-1] at http://<ip>:<port>. ip is the 3DS's own address
// (network byte order). Tries port, then the next few ports if it's taken, and returns
// the one it got, or 0 if it couldn't start. log (may be NULL) gets one line per event,
// from the server's thread.
int     ShareServer_Start(u32 ip, int port, const ShareFile *files, int count,
                          void (*log)(const char *line));

// Stops the server (within about 0.1 s) and closes everything.
void    ShareServer_Stop(void);

// How many complete transfers of files that count there have been.
int     ShareServer_Sent(void);

#endif
