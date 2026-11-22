#ifndef GAMES_H
#define GAMES_H

// The games with a folder in /achievements/ holding definitions (the same files the
// tracker reads), with their title and icon from the installed game.

#include <3ds.h>

#include "draw.h"
#include "hero.h"
#include "icons.h"

#define MAX_GAMES   64
#define MAX_RELEASES 4      // other releases (regions) of one game, from their version.txt
#define TITLE_CHARS 64

typedef struct
{
    u64     titleId;                        // the game's folder (definitions, progress, art)
    u64     releases[MAX_RELEASES];         // other releases sharing it (/achievements/<id>/version.txt)
    int     releaseCount;
    char    title[TITLE_CHARS + 1];
    bool    hasTitle;                       // false: title is the title ID
    bool    hasIcon;
    u16     icon[ICON_SIZE * ICON_SIZE];    // SMDH large icon
    int     unlocked, total;            // achievements
    int     earned, points;             // score: points unlocked, points in total
} Game;

// Moves game folders from /achievements, where cards kept them before 2026-11-21, into
// /achievements/games (ACHIEVEMENTS_GAMES). Call it while no game runs (the tracker writes
// progress.txt). A game already in the games folder (from a newer release zip, say) is merged:
// progress.txt keeps every line of both, a known game file there already is kept and the old
// copy deleted, any other file moved if it isn't there yet. Returns the folders emptied.
int     Games_MoveOldFolders(void);

// Finds every game with at least one valid achievement, sorted by title. Another
// release of a game (a folder with a version.txt) isn't a game of its own: its title
// ID is added to the game's releases, and its title and icon are used when it's the
// release that's installed.
int     Games_Scan(Game *games, int capacity);

// Reads hero.bin in the game's folder, the game's background (tools/hero_art.py
// makes it), into pixels (HERO_BYTES, the wide format of hero.h; an older screen-sized
// file gets its edges mirrored into the margins). False if the game doesn't have one.
bool    Games_LoadHero(u64 titleId, u8 *pixels);

// Reads /achievements/<TitleID>/trophy.bin, the game's own achievement icons
// (tools/trophy_art.py makes it). False if the game doesn't have any.
bool    Games_LoadTrophies(u64 titleId, u16 *unlocked, u16 *locked);

// A game's picture for each achievement (/achievements/<TitleID>/icons.bin, made by
// tools/achievement_icons.py; the tracker's icons.h has its format): unlocked[i] and
// locked[i] for achievement i of the list, NULL for one without a picture.
typedef struct
{
    void       *memory;                 // the file, then the locked pictures made for it
    const u16  *unlocked[ICONS_MOST], *locked[ICONS_MOST];
    int         count;                  // achievements with a picture
} GameIcons;

// Reads the pictures of the achievements ids[0..count) (in list order). Without a file, or
// memory for it, there are none (icons->count 0). Games_FreeIcons gives back the memory.
void    Games_LoadIcons(u64 titleId, const char *const *ids, int count, GameIcons *icons);
void    Games_FreeIcons(GameIcons *icons);

#endif
