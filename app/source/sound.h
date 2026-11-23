// The app's sound effects (sounds.h: sets of them, made by tools/app_sounds.js), played through
// the console's DSP with libctru's ndsp. That needs the DSP firmware /3ds/dspfirm.cdc (made by
// DSP1): without it the app stays silent. The Sounds setting picks a set, or none:
// /achievements/sounds.txt holds its id or "off"; without it the app uses SOUND_SET_DEFAULT.
#ifndef SOUND_H
#define SOUND_H

#include <3ds.h>

#include "sounds.h"

void    Sound_Init(void);           // when the app opens: starts the DSP with the set chosen
void    Sound_Exit(void);
void    Sound_Play(SoundId id);     // in the set in use; nothing if sounds are off or can't play

int         Sound_SetCount(void);
const char *Sound_SetName(int set);
const char *Sound_SetDescription(int set);
int         Sound_Current(void);        // the set in use, or -1: off
void        Sound_Use(int set);         // -1: off; saved in sounds.txt
bool        Sound_Broken(void);         // a set is chosen, but the DSP couldn't be started (no dspfirm.cdc?)

#endif
