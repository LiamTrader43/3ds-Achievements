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

// Starts or stops the tracker and remembers the choice.
Result      Tracker_SetEnabled(bool enabled);

#endif
