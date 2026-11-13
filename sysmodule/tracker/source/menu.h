#ifndef MENU_H
#define MENU_H

#include <3ds.h>

// Whether the menu hotkey (R + A + Left) is held.
bool    Menu_HotkeyHeld(void);

// Whether the game is in front, not suspended behind the HOME Menu (from
// APT). Unknown counts as yes. `active` gets APT's active applet ID (0 if
// unknown), for the log.
bool    Menu_GameInFront(u32 *active);

// Pauses the game, shows the achievement list on the bottom screen (laid out like
// the app's: Up/Down select an achievement, A or a tap shows it in full, B goes back)
// until B or Select closes it, then resumes the game once every button is released.
// Returns false, showing nothing, if the game couldn't be paused.
bool    Menu_Open(Handle game, u64 titleId);

// How setting up touch input went the last time the menu opened, for the log.
Result  Menu_TouchResult(void);

#endif
