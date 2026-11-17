#ifndef UI_H
#define UI_H

// What the app shows, shared by main.c, which changes it, and the themes (theme_*.c), which
// draw it. Every theme shows the same things with the same controls; only the drawing differs.

#include <3ds.h>
#include <stdint.h>

#include "draw.h"
#include "games.h"
#include "hero.h"

#define MAX_ACHIEVEMENTS    128     // the tracker's MAX_LINES

// The games found.
extern Game         g_games[MAX_GAMES];
extern int          g_gameCount;

// A game is being chosen: none is open yet, so the bottom screen shows the highlighted game's
// stats instead of its achievements (themes with Theme.gameList).
extern bool         g_picking;

// The selected game's achievements, once loaded (g_count is 0 until then).
extern const char  *g_names[MAX_ACHIEVEMENTS];
extern const char  *g_descriptions[MAX_ACHIEVEMENTS];
extern uint16_t     g_points[MAX_ACHIEVEMENTS];
extern bool         g_unlocked[MAX_ACHIEVEMENTS];
extern int          g_count;
extern int          g_selected;             // the achievement Up/Down have selected
extern u16          g_gameUnlocked[ICON_SIZE * ICON_SIZE], g_gameLocked[ICON_SIZE * ICON_SIZE];
extern bool         g_hasGameIcons;         // this game has its own achievement icons

// Its most recently unlocked achievements, newest first: indices into g_names.
#define RECENT_MAX  3
extern int          g_recent[RECENT_MAX];
extern int          g_recentCount;

// The selected game's background (hero.bin), and as the top screen shows it (Draw_DimHero).
extern u8           g_hero[HERO_BYTES];
extern u8           g_heroDim[HERO_BYTES];
extern bool         g_heroReady;

// The settings the hints show.
extern bool         g_trackerRunning;
extern bool         g_patchedAllowed;

// The message over the top screen, if any.
typedef enum
{
    POPUP_NONE, POPUP_START_FAILED, POPUP_STOP_FAILED,
    POPUP_UPDATE, POPUP_DOWNLOADING, POPUP_UPDATE_FAILED,
    POPUP_APP_UPDATE, POPUP_APP_DOWNLOADING, POPUP_APP_UPDATED, POPUP_APP_FAILED,
    POPUP_THEME_FAILED              // an add-on theme couldn't be loaded (Theme_Error)
} Popup;

extern Popup        g_popup;
extern Result       g_popupResult;
extern int          g_newGames, g_updatedGames;
extern int          g_downloadDone, g_downloadTotal;
extern const char  *g_appVersion;           // a newer release of the app, or NULL
extern bool         g_appChanged;           // a failed app update still replaced something
extern bool         g_trackerNotRestarted;  // the tracker was updated but couldn't be restarted
extern bool         g_canRestart;           // after an app update: the new version starts by itself

// What the popup says, for a theme to lay out. Short notices have a few centred lines;
// problems have the error code, then lines explaining it.
#define POPUP_LINES 4
typedef struct
{
    const char *title;
    bool        problem;                    // something failed
    bool        centred;                    // the lines are short notices (else left-aligned, after the code)
    bool        emphasizeFirst;             // the first line matters most
    int         lineCount;
    const char *lines[POPUP_LINES];
    bool        showCode;                   // "Error 0x..." before the lines
    Result      code;
    bool        progress;                   // a bar: done of total
    int         done, total;
    const char *buttonA, *buttonB;          // what A and B do, or NULL (no button)
    char        titleText[32], lineText[64];    // room for the title and first line when made up
} PopupText;

void    Ui_PopupText(PopupText *t);

// The achievement trophy to draw: the game's own icons if it has them.
const u16 *Ui_Trophy(bool unlocked);

// Copies text cut to maxWidth pixels at the given text scale, ending in "..." if it was longer.
void    Ui_EllipsizeScaled(char *out, size_t capacity, const char *text, int maxWidth, int scale);
void    Ui_Ellipsize(char *out, size_t capacity, const char *text, int maxWidth);

// Draws text word-wrapped to width pixels at the given text scale, at most maxLines lines
// (the last one ends in "..." if the text goes on). Returns the y below the last line.
int     Ui_DrawWrappedScaled(gfxScreen_t screen, int x, int y, int width, int lineHeight, int scale,
                             Color color, const char *text, int maxLines);
int     Ui_DrawWrapped(gfxScreen_t screen, int x, int y, int width, int lineHeight, Color color,
                       const char *text, int maxLines);

// Every game's achievements added up.
typedef struct
{
    int unlocked, total;            // achievements
    int earned, points;
    int complete;                   // games with every achievement unlocked
} UiTotals;

void    Ui_Totals(UiTotals *t);

// The settings menu (X): each line's name, its value now, and what it does.
enum { SETTING_TRACKER, SETTING_PATCHED, SETTING_THEME, SETTING_COUNT };
typedef struct
{
    const char *name, *value, *description;
    bool        on;                 // the value is "on" (or a choice, like the theme)
} UiSetting;

void    Ui_Setting(int i, UiSetting *s);

// The button prompts in the bottom screen's corners: a button's name and what it does there
// (key 0: none). main.c says which are shown; every theme shows them in the bottom-left and
// bottom-right corners, each its own way. A tap on one (the theme's Theme.tabWidth x tabTop
// area) presses its button.
enum { TAB_LEFT, TAB_RIGHT };
typedef struct
{
    u32         key;                // KEY_A, KEY_B...
    const char *button, *label;     // "B", "Back"
} UiTab;
extern UiTab        g_tabs[2];
extern int          g_tabPressed;   // the prompt a finger is on, or -1

// Moves value toward target by at most step.
int     Ui_Approach(int value, int target, int step);

// The battery's level, 0-5 (-1 if it can't be read), read at most every 10 s; charging: it's
// charging (or NULL).
int     Ui_Battery(bool *charging);

#endif
