#ifndef UPDATE_H
#define UPDATE_H

// Checks GitHub when the app opens: the repo's main branch for new games or achievements, and
// its latest release for a newer version of the app. Game downloads only ever write
// definitions.txt, hero.bin, trophy.bin and version.txt (progress is left alone); an app
// update replaces the tracker's .cxi and the app itself.

#include <3ds.h>

// Starts the check in the background. appPath is where the running .3dsx was opened from
// (argv[0]), or NULL; it's only used when the app runs as a .3dsx, not as the CIA.
void        Update_Start(const char *appPath);

// True once, when the check has finished. Then newGames and updatedGames count the games with
// files to download, and appVersion is the newer release's version, or NULL if there isn't one.
bool        Update_Poll(int *newGames, int *updatedGames, const char **appVersion);

// Downloads every game file Update_Poll found, calling progress before each file.
Result      Update_Download(void (*progress)(int done, int total));

// Downloads the newer release Update_Poll found and installs it: the tracker's .cxi (if it
// changed), then the app as it's running (its .3dsx, or the CIA). progress gets the KB
// downloaded and the release's size in KB, then (0, 0) while installing. *trackerChanged says
// the tracker's file was replaced (it needs restarting to use it); *changed says anything was,
// which can be true even if a later step failed.
Result      Update_InstallApp(void (*progress)(int done, int total), bool *trackerChanged, bool *changed);

// After an update: whether the app can start its new version by itself when it closes (the
// CIA always can; a .3dsx needs Luma's 3dsx loader).
bool        Update_CanRestart(void);

// Makes the new version start when the app closes. Call it last, right before closing: a .3dsx
// left set up this way would otherwise open instead of the Homebrew Launcher next time.
bool        Update_Restart(void);

// This app's version, e.g. "1.4.0" (the Makefile's VERSION).
const char *Update_AppVersion(void);

// True once sockets are set up (Update_Start), for other users of the network.
bool        Update_NetworkReady(void);

// Stops a check that's still running. Call before exiting.
void        Update_Stop(void);

#endif
