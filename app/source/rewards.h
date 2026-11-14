#ifndef REWARDS_H
#define REWARDS_H

// Play Coins for achievements: REWARD_COINS for every REWARD_POINTS achievement points
// earned, across all games, given when the app opens (the tracker doesn't). The coins are the
// system's own: gamecoin.dat in shared extdata F000000B, in the console's internal memory,
// at most PLAY_COINS_MAX. How many rewards have been given, and coins earned that didn't
// fit under the limit, are kept in /achievements/play_coins.txt, so a reward is only ever
// given once and the coins that didn't fit are given later.

#include <3ds.h>

#define REWARD_POINTS   25      // achievement points per reward
#define REWARD_COINS    5       // Play Coins per reward
#define PLAY_COINS_MAX  300     // the most the system holds

typedef struct
{
    int     rewards;    // rewards earned so far: points / REWARD_POINTS (never goes down)
    int     earned;     // coins earned since the app was last opened
    int     given;      // coins added to the system's this time (earned now or kept from before)
    int     owed;       // coins earned that couldn't be added yet, kept for next time
    int     coins;      // the system's Play Coins now (-1: couldn't be read)
    Result  result;     // of opening, reading and writing gamecoin.dat (0: fine)
} Rewards;

// Gives the coins earned for the points of unlocked achievements (all games together) and
// any kept from before. True if there's something to tell: coins were earned or given.
bool    Rewards_Update(int points, Rewards *out);

// The system's Play Coins now, only read (-1 if they can't be).
int     Rewards_Coins(void);

#endif
