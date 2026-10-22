#ifndef MENU_H
#define MENU_H

#include <3ds.h>

// Whether the menu hotkey (R + Down + Select) is held.
bool    Menu_HotkeyHeld(void);

// Whether the game is in front, not suspended behind the HOME Menu (from
// APT). Unknown counts as yes. `active` gets APT's active applet ID (0 if
// unknown), for the log.
bool    Menu_GameInFront(u32 *active);

// Pauses the game, shows the achievement list on the bottom screen until B or
// Select is pressed, then resumes it once every button is released. Returns
// false, showing nothing, if the game couldn't be paused.
bool    Menu_Open(Handle game, const char *gameTitle);

#endif
