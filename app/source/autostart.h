#ifndef AUTOSTART_H
#define AUTOSTART_H

// "Start with the console" (the settings): Luma starts the tracker when the 3DS is turned on,
// so the app doesn't have to be opened after each power-on. How it works:
// ../sysmodule/tracker/source/autostart_files.h. Every step goes to /achievements/autostart.log.

#include <3ds.h>

#define AUTOSTART_RESULT(n)     MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_APPLICATION, 0x380 + (n))
#define AUTOSTART_OLD_LUMA      AUTOSTART_RESULT(0)     // Luma3DS older than 13.0
#define AUTOSTART_NO_MODULES    AUTOSTART_RESULT(1)     // Luma's "Enable loading external FIRMs and modules" is off
#define AUTOSTART_NO_PATCHING   AUTOSTART_RESULT(2)     // Luma's "Enable game patching" is off
#define AUTOSTART_NO_TRACKER    AUTOSTART_RESULT(3)     // /luma/sysmodules/0004013000AC4102.cxi is missing
#define AUTOSTART_NO_HOST       AUTOSTART_RESULT(4)     // no module can start it safely
#define AUTOSTART_NOT_USED      AUTOSTART_RESULT(5)     // Luma didn't use the file (it was removed)
#define AUTOSTART_BOOT_FAILED   AUTOSTART_RESULT(6)     // turned off: a start of the 3DS didn't reach the HOME Menu
#define AUTOSTART_UPDATED       AUTOSTART_RESULT(7)     // turned off: a system update changed the module it used
// Anything else is the system's (Luma's loader, the SD card).

// The setting.
bool    Autostart_On(void);

// Luma uses the file now: both of its options are on (the setting can be on without it).
bool    Autostart_Usable(void);

// Turns it on: finds a module that can start the tracker safely and writes its file. Nothing
// is left changed if it fails.
Result  Autostart_Enable(void);

// Turns it off: removes the file (only the app's own).
Result  Autostart_Disable(void);

// When the app opens: AUTOSTART_BOOT_FAILED or AUTOSTART_UPDATED if it was turned off since
// (the tracker after a start of the 3DS that never reached the HOME Menu, or this after a
// system update changed the module it uses: its old exheader mustn't stay), else 0.
Result  Autostart_Check(void);

#endif
