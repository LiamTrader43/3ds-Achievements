#ifndef TRACKER_H
#define TRACKER_H

// The background tracker sysmodule (title 0004013000AC4102), which Luma3DS loads
// from /luma/sysmodules/0004013000AC4102.cxi: starting and stopping it.

#include <3ds.h>

// True if the tracker is running.
bool        Tracker_Running(void);

// Starts the tracker unless it's already running.
Result      Tracker_Start(void);

// True if the user turned the tracker off, so the app shouldn't start it by itself.
bool        Tracker_TurnedOff(void);

// Stops the tracker and starts it again, from its .cxi as it is now (after an update).
// Does nothing if it isn't running.
Result      Tracker_Restart(void);

// Starts or stops the tracker and remembers the choice.
Result      Tracker_SetEnabled(bool enabled);

// True if achievements can be earned in games with Luma patches or mods (off by default).
bool        Tracker_PatchedAllowed(void);

// Remembers that choice. The tracker reads it each time a game starts.
void        Tracker_SetPatchedAllowed(bool allowed);

// The buttons that open the in-game achievement list (hotkey.h): /achievements/hotkey.txt, else R + A + Left.
u32         Tracker_Hotkey(void);

// Saves them (R + A + Left removes the file). False if it couldn't be written. The tracker reads it each time a
// game starts.
bool        Tracker_SetHotkey(u32 keys);

// Luma's Rosalina menu's combinations: the one Luma started with (svcGetSystemInfo 0x10000/0x101, L + Down + Select
// if none was set) and, if different, the one saved in /luma/config.ini since (used from the next boot, and maybe
// already: Rosalina changes it at once). Returns how many (0 without Luma).
int         Tracker_RosalinaCombos(u32 combos[2]);

#endif
