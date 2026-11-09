#ifndef GAMES_H
#define GAMES_H

// The games with a folder in /achievements/ holding definitions (the same files the
// tracker reads), with their title and icon from the installed game.

#include <3ds.h>

#include "draw.h"

#define MAX_GAMES   64
#define TITLE_CHARS 64
#define HERO_BYTES  (TOP_WIDTH * SCREEN_HEIGHT * 3)

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

// Reads /achievements/<TitleID>/hero.bin, the game's background (tools/hero_art.py
// makes it), into pixels. False if the game doesn't have one.
bool    Games_LoadHero(u64 titleId, u8 *pixels);

// Reads /achievements/<TitleID>/trophy.bin, the game's own achievement icons
// (tools/trophy_art.py makes it). False if the game doesn't have any.
bool    Games_LoadTrophies(u64 titleId, u16 *unlocked, u16 *locked);

#endif
