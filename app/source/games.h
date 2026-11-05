#ifndef GAMES_H
#define GAMES_H

// The games that have a definitions file in /achievements/ (the same files the
// tracker reads), with their title and icon from the installed game.

#include <3ds.h>

#include "draw.h"

#define MAX_GAMES   64
#define TITLE_CHARS 64

typedef struct
{
    u64     titleId;
    char    title[TITLE_CHARS + 1];
    bool    hasTitle;                       // false: title is the title ID
    bool    hasIcon;
    u16     icon[ICON_SIZE * ICON_SIZE];    // SMDH large icon
    int     unlocked, total;            // achievements
    int     earned, points;             // score: points unlocked, points in total
} Game;

// Finds every game with at least one valid achievement, sorted by title.
int     Games_Scan(Game *games, int capacity);

#endif
