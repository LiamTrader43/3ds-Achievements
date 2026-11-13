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

void    Share_DrawTop(void);
void    Share_DrawBottom(void);

// Stops sharing and frees the image.
void    Share_Close(void);

#endif
