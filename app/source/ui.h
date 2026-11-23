#ifndef UI_H
#define UI_H

// What the app shows, shared by main.c, which changes it, and the themes (theme_*.c), which
// draw it. Every theme shows the same things with the same controls; only the drawing differs.

#include <3ds.h>
#include <stdint.h>

#include "draw.h"
#include "games.h"
#include "hero.h"
#include "rewards.h"
#include "store.h"

#define MAX_ACHIEVEMENTS    256     // the tracker's MAX_LINES

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
extern bool         g_missable[MAX_ACHIEVEMENTS];   // can be missed for good ("@missable" lines)
// Achievements done in parts ("@parts"): parts done and in total (0 and 0: not done in parts), and
// each one's parts: g_partNames/g_partDone from g_partFirst[i], g_partsTotal[i] of them.
extern uint8_t      g_partsDone[MAX_ACHIEVEMENTS];
extern uint8_t      g_partsTotal[MAX_ACHIEVEMENTS];
extern uint16_t     g_partFirst[MAX_ACHIEVEMENTS];
extern const char  *g_partNames[MAX_ACHIEVEMENTS];
extern bool         g_partDone[MAX_ACHIEVEMENTS];
// Measured progress (a number the game keeps, from the tracker's measured.txt): the latest value
// (0 = none known), the target (0 = not measured), shown as a percentage. Themes use Ui_Measured.
extern uint32_t     g_measured[MAX_ACHIEVEMENTS];
extern uint32_t     g_measuredTarget[MAX_ACHIEVEMENTS];
extern bool         g_measuredPercent[MAX_ACHIEVEMENTS];
extern int          g_count;
extern int          g_selected;             // the achievement Up/Down have selected
extern u16          g_gameUnlocked[ICON_SIZE * ICON_SIZE], g_gameLocked[ICON_SIZE * ICON_SIZE];
extern bool         g_hasGameIcons;         // this game has its own achievement icons
extern GameIcons    g_icons;                // and maybe a picture for each achievement (icons.bin)

// Its most recently unlocked achievements, newest first: indices into g_names.
#define RECENT_MAX  3
extern int          g_recent[RECENT_MAX];
extern int          g_recentCount;

// The selected game's background (hero.bin), and as the top screen shows it (Draw_DimHero).
extern u8           g_hero[HERO_BYTES];
extern u8           g_heroDim[HERO_BYTES];
extern bool         g_heroReady;

// The store: a position in the row after the games (Ui_IsStore), selling things for the
// system's Play Coins (store.c, rewards.c).
extern Game         g_storeTile;            // its tile in the row: the store's bag, "Store"
extern int          g_coins;                // the Play Coins there are (-1: couldn't be read)
extern Rewards      g_rewards;              // the Play Coins given when the app opened
extern int          g_buyItem;              // the item the buying popups are about
extern int          g_paidOwed;             // coins kept from before, added after a purchase

// The settings the hints show.
extern bool         g_trackerRunning;
extern bool         g_patchedAllowed;
extern bool         g_autostart;            // "Start with the console" (autostart.c)
extern bool         g_autostartUsable;      // Luma's options for it are on

// The message over the top screen, if any.
typedef enum
{
    POPUP_NONE, POPUP_START_FAILED, POPUP_STOP_FAILED,
    POPUP_UPDATE, POPUP_DOWNLOADING, POPUP_UPDATE_FAILED,
    POPUP_APP_UPDATE, POPUP_APP_DOWNLOADING, POPUP_APP_UPDATED, POPUP_APP_FAILED,
    POPUP_THEME_FAILED,             // an add-on theme couldn't be loaded (Theme_Error)
    POPUP_COINS,                    // Play Coins earned (g_rewards)
    POPUP_BUY, POPUP_BOUGHT, POPUP_NOT_ENOUGH, POPUP_BUY_FAILED,    // the store (g_buyItem)
    POPUP_AUTOSTART_ON,             // "Start with the console" turned on
    POPUP_AUTOSTART_FAILED,         // couldn't turn it on or off (g_popupResult, autostart.h)
    POPUP_AUTOSTART_OFF,            // it was turned off since the app was last open (why: g_popupResult)
    POPUP_AUTOSTART_CONFIRM,        // before turning it on: what game patching also does (A turns it on)
    POPUP_HOTKEY,                   // choosing the in-game list's buttons: hold them (g_hotkeyHeld, g_hotkeyProgress)
    POPUP_HOTKEY_SET,               // they're saved (g_hotkey)
    POPUP_HOTKEY_FAILED,            // they couldn't be saved
    POPUP_HOTKEY_WARN               // they could get in the way while playing (g_hotkeyChosen): A uses them anyway
} Popup;

extern Popup        g_popup;
extern Result       g_popupResult;
extern int          g_newGames, g_updatedGames;
extern int          g_storeFiles;           // the store's files to download (POPUP_UPDATE)
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
    char        titleText[48], lineText[64], lineText2[64];    // room for the title and lines when made up
} PopupText;

void    Ui_PopupText(PopupText *t);

// The row's positions: the games, any empty ones (up to the theme's Theme.minPositions), then
// the store, last (there's no row, and no store, without games).
int     Ui_StorePosition(void);
int     Ui_RowPositions(void);
bool    Ui_IsStore(int position);

// Sets up the store's tile and reads what's been bought (once, when the app opens).
void    Ui_StoreInit(void);

// Item i's icon (48x48 like a trophy): its own, or the store's bag.
const u16 *Ui_StoreIcon(int i);

// The store's background (hero.h's format), for themes that show one: a warm glow behind the
// row, fading into the dark.
void    Ui_StoreBackground(u8 *pixels);

// The achievement trophy to draw: the game's own icons if it has them.
const u16 *Ui_Trophy(bool unlocked);

// Achievement i's picture (48x48 like a trophy): its own from the game's icons.bin, else the
// trophy (Ui_Trophy).
const u16 *Ui_AchievementIcon(int i, bool unlocked);

// Achievement i's measured progress as text, while it's locked and a value is known: "4,200/5,000"
// for a list (full: "4,200 of 5,000"), or "84%". False, and out untouched, otherwise.
bool    Ui_Measured(int i, bool full, char *out, size_t size);

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

// The settings menu (X): each line's name, its value now, and what it does. Theme opens a list
// of the themes (MENU_THEMES), drawn like the settings: g_menu says which list is showing,
// g_menuScroll its first line shown.
enum { SETTING_TRACKER, SETTING_AUTOSTART, SETTING_PATCHED, SETTING_HOTKEY, SETTING_SOUNDS, SETTING_THEME, SETTING_COUNT };
extern u32          g_hotkey;               // the buttons that open the in-game list (hotkey.h)
extern u32          g_hotkeyHeld;           // POPUP_HOTKEY: the buttons held now
extern int          g_hotkeyProgress;       // POPUP_HOTKEY: how long they've been held, 0..HOTKEY_HOLD_MS
extern bool         g_hotkeyRosalina;       // POPUP_HOTKEY: the buttons held overlap Rosalina's menu (can't be chosen)
extern u32          g_hotkeyChosen;         // POPUP_HOTKEY_WARN: the buttons held long enough
extern u32          g_rosalina[2];          // Luma's Rosalina menu's combinations (Tracker_RosalinaCombos)
extern int          g_rosalinaCount;
#define HOTKEY_HOLD_MS 1500                 // held this long, they become the hotkey

// Whether keys overlap a combination of Rosalina's menu (hotkey.h Hotkey_Overlaps).
bool    Ui_HotkeyRosalina(u32 keys);
enum { MENU_SETTINGS, MENU_THEMES, MENU_SOUNDS };     // MENU_SOUNDS: the sound sets, then Off
extern int          g_menu;
extern int          g_menuScroll;
typedef struct
{
    const char *name, *value, *description;
    bool        on;                 // the value is "on" (or a choice, like the theme)
} UiSetting;

void    Ui_Setting(int i, UiSetting *s);       // line i of the list showing (g_menu)
int     Ui_MenuCount(void);                    // its lines
const char *Ui_MenuTitle(void);               // "Settings" or "Theme"

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
