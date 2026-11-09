#ifndef UPDATE_H
#define UPDATE_H

// Checks the GitHub repo's main branch for new games or achievements and downloads them.
// Only definitions.txt, hero.bin and trophy.bin are ever written; progress is left alone.

#include <3ds.h>

// Starts the check in the background.
void    Update_Start(void);

// True once, when the check has finished and found something to download.
bool    Update_Poll(int *newGames, int *updatedGames);

// Downloads everything Update_Poll found, calling progress before each file.
Result  Update_Download(void (*progress)(int done, int total));

// Stops a check that's still running. Call before exiting.
void    Update_Stop(void);

#endif
