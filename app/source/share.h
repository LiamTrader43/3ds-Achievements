#ifndef SHARE_H
#define SHARE_H

// The share screen (Y): the share image of a game on the top screen, and on the bottom a
// QR code a phone scans to open a page on the 3DS (share_server.c) that shows the image
// with a button to save it. A saves the image to /achievements/share/. What happens goes
// to /achievements/share.log.

#include <3ds.h>

#include "games.h"

// Makes the image of game g (hero: its background or NULL; trophy: its 48x48 unlocked
// icon) and starts sharing it. False if there's no memory for it.
bool    Share_Open(const Game *g, const u8 *hero, const u16 *trophy);

// Saves the image to /achievements/share/<game> <date>.png. The bottom screen says how it went.
bool    Share_Save(void);

// Call every frame: true when the bottom screen changed (Wi-Fi came up, image sent).
bool    Share_Poll(void);

// What the share screen shows, for the theme to draw.
typedef enum
{
    SHARE_OPENING,          // not open yet: the image is being drawn (Share_Open hasn't returned)
    SHARE_MAKING,           // the PNG is being made
    SHARE_NO_MEMORY,        // there wasn't memory for the image
    SHARE_NO_WIFI,          // ready, but the 3DS isn't on a Wi-Fi network (A still saves it)
    SHARE_SERVER_FAILED,    // ready, but the server couldn't start
    SHARE_SERVING,          // the QR code is up
} ShareState;

typedef enum { SHARE_NEUTRAL, SHARE_GOOD, SHARE_BAD } ShareTone;

typedef struct
{
    ShareState  state;
    const u8   *preview;    // the image, SHARE_PREVIEW pixels square, RGB rows top to bottom
    const char *url;        // SHARE_SERVING: what the QR code holds
    int         qrSize;     // and how many modules it has across (Share_QrModule)
    const char *status;     // the latest thing that happened ("" if nothing yet)
    ShareTone   tone;       // and whether it went well
} ShareView;

void    Share_View(ShareView *view);

// Whether module (x, y) of the QR code is dark.
bool    Share_QrModule(int x, int y);

// Stops sharing and frees the image.
void    Share_Close(void);

#endif
