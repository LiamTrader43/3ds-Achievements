#ifndef AUTOSTART_FILES_H
#define AUTOSTART_FILES_H

// "Start with the console", the app's setting (app/source/autostart.c): shared by the app and
// the tracker.
//
// Luma's loader lets a file on the SD card replace a module's exheader,
// /luma/titles/<module>/exheader.bin (only with "Enable game patching" and "Enable loading
// external FIRMs and modules" on), and pm starts a module's dependencies with it. So the app
// copies the exheader of a module started at boot (the host, app/source/boot_plan.c) with the
// tracker first in its dependency list, and the tracker starts with the console.
//
// The app keeps a record of it (AUTOSTART_RECORD). The tracker guards the boot: when the
// console starts it, it leaves AUTOSTART_TRY until the HOME Menu is up; if it finds the file
// left by the last start, that boot never got there, so it removes the copy and the record and
// leaves AUTOSTART_FAILED for the app to tell the user.

#define TRACKER_TID             0x0004013000AC4102ULL

#define AUTOSTART_RECORD        "/achievements/autostart.txt"       // "host <16 hex digits>" ...: the setting is on
#define AUTOSTART_TRY           "/achievements/autostart_try"       // a boot started the tracker, not done yet
#define AUTOSTART_FAILED        "/achievements/autostart_failed"    // the tracker turned the setting off
#define AUTOSTART_TITLES        "/luma/titles"                      // + "/<16 hex digits>/exheader.bin"
#define AUTOSTART_OFF           "/achievements/tracker_off"         // the user turned the tracker off

#define AUTOSTART_EXHEADER_SIZE 0x400   // ExHeader_Info. Luma's loader stops the console on any size but this or 0x800
#define AUTOSTART_DEPS_OFFSET   0x40    // its dependency list: 48 title IDs, 0 after the last

#endif
