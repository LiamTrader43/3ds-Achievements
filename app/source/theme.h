#ifndef THEME_H
#define THEME_H

// The app's look. Every theme shows the same things (ui.h) with the same controls (main.c);
// it only decides how they're drawn.
//
// Preinstalled, compiled into the app (Makefile BUILTIN_THEMES; theme.c BUILT_IN):
//   modern   dark screens, the game's art behind a row of icon tiles (theme_modern.c)
//   3ds      like the 3DS's own System Settings (themes/3ds/theme_3ds.c)
// Add-ons: /achievements/themes/<id>.theme, each built on its own from themes/<id>/
// (theme_api.h), loaded the first time it's used.
//
// The theme is the id on the first line of /achievements/theme.txt; modern without one (or if
// that theme can't be loaded).

#include "draw.h"

typedef struct
{
    const char *id;             // in theme.txt
    const char *name;           // what it's called
    const char *description;    // what it looks like, in a line (the settings' theme list)
    int         font;           // the text's face (DRAW_FONT_*)

    // The bottom screen's list of achievements, for scrolling, the selection and taps: boxes
    // boxHeight apart from listTop down, shown in listHeight pixels.
    int     listTop, listHeight, boxHeight;
    // The button prompts' tap areas (g_tabs): tabWidth pixels from each side, from row tabTop down.
    int     tabWidth, tabTop;
    // Left/Right go through at least this many positions, empty past the last game; the store
    // is always the last (ui.h: Ui_IsStore, Ui_RowPositions).
    int     minPositions;
    // The app opens with no game open (g_picking): Left/Right go through the games and the
    // bottom screen shows the highlighted one's stats; A opens it (its achievements), B goes
    // back. Without it, the selected game's achievements are always shown.
    bool    gameList;
    // While choosing, the games are laid out this many across: Up/Down move a row (0 or 1: Up/Down
    // do nothing there).
    int     pickColumns;
    // The bottom screen fades in from this fade (a list loading, the full view opening).
    Color   fadeTop, fadeBottom;

    // The first frames, while the games are read (both screens cleared).
    void    (*drawLoading)(void);
    // The games were read again: the first one is selected, nothing is moving.
    void    (*reset)(void);
    // Eases what moves on the top screen toward position selected, ms after the last frame.
    // True while something still moves (the top screen is redrawn).
    bool    (*animate)(int selected, int ms);
    // True if drawing the top screen sets every pixel, so it isn't cleared first, with the
    // game's background faded in this far (0-256).
    bool    (*topCovers)(int heroLevel);
    // The top screen: what goes into every eye's image at once (or NULL), then the rest once for
    // each eye's image (Draw_Eye), then any popup over it.
    void    (*drawTopBackground)(int heroLevel);
    void    (*drawTop)(int selected, int heroLevel);
    // alone: nothing else is drawn in this frame (downloads), both screens cleared.
    void    (*drawPopup)(bool alone);
    // The bottom screen, setting every pixel: position game's achievements scrolled by scroll
    // pixels, or with details the selected one in full. loaded: its achievements were read
    // (g_count can be 0). game can be an empty position past the last game, or the store
    // (Ui_IsStore: g_count items for sale, A buys). With g_picking: game's (or the store's) stats.
    void    (*drawBottom)(int game, int scroll, bool details, bool loaded);
    // A frame with nothing else redrawn and no popup: moves what keeps moving (or NULL if
    // nothing does). False if there was nothing to move.
    bool    (*drawIdle)(void);
    // True when something on the top screen changed by itself (a clock) and it must be
    // redrawn (or NULL if nothing does).
    bool    (*topChanged)(void);
    // The share screen (Share_View says what to show), each setting every pixel of its
    // screen. The bottom is also drawn once before the share screen opens (SHARE_OPENING),
    // while the image is made.
    void    (*drawShareTop)(void);
    void    (*drawShareBottom)(void);
    // The settings menu (X), item highlighted (SETTING_*; Ui_Setting says what each is). The top
    // once for each eye (or NULL: the usual top screen), the bottom setting every pixel; its
    // lines are laid out like the achievements' boxes (listTop, boxHeight), for taps.
    void    (*drawSettingsTop)(int item);
    void    (*drawSettingsBottom)(int item);
} Theme;

extern const Theme  THEME_MODERN, THEME_3DS;
extern const Theme *g_theme;

// Finds the add-ons in /achievements/themes and uses the theme saved in theme.txt (loading it
// if it's an add-on), modern without one. False if that theme couldn't be loaded (modern is
// used; Theme_Error says why).
bool            Theme_Load(void);

// The themes to choose from: the preinstalled ones, then the add-ons found (by name). Which is in
// use, what each says about itself, and whether it's an add-on.
int             Theme_Count(void);
int             Theme_Current(void);
const struct ThemeInfo *Theme_Info(int i);     // (theme_api.h)
bool            Theme_IsAddOn(int i);

// Adds the add-ons that appeared since the app opened (a theme just bought in the store); the
// theme in use stays.
void            Theme_Rescan(void);

// Uses theme i from now on (loading it first if it's an add-on) and saves it in theme.txt.
// False if it couldn't be loaded (Theme_Error says why); the theme in use stays.
bool            Theme_Use(int i);

// Why the last theme that failed to load couldn't be: "<name>: <reason>".
const char     *Theme_Error(void);

// Taps on the games while choosing one (g_picking), for a theme that lays them out on the bottom
// screen: the theme says (theme API 7, e.g. each time it draws them) which position is at x, y
// with position game highlighted, or -1 for none. Kept only while that theme is in use.
void            Theme_PickTaps(const Theme *theme, int (*at)(int game, int x, int y));
// The position at x, y on the bottom screen while choosing, or -1 (none, or the theme in use
// didn't say).
int             Theme_PickAt(int game, int x, int y);

// What a theme says about itself (theme API 7, e.g. in its reset): THEME_FLAG_* for theme, kept only
// while that theme is in use. Theme_Flags: the theme in use's (0 if it didn't say).
#define THEME_FLAG_NO_BACKGROUND    1       // its top screen doesn't show the game's background:
                                            // heroLevel isn't faded (no redrawing for it), and the
                                            // store's isn't made (g_heroReady stays false there)
void            Theme_SetFlags(const Theme *theme, u32 flags);
u32             Theme_Flags(void);

#endif
