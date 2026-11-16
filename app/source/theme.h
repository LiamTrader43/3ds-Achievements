#ifndef THEME_H
#define THEME_H

// The app's look. Every theme shows the same things (ui.h) with the same controls (main.c);
// it only decides how they're drawn.
//
//   modern   dark screens, the game's art behind a row of icon tiles (theme_modern.c)
//   3ds      like the 3DS's own System Settings: cream screens, white buttons, grey tabs
//            along the bottom (theme_3ds.c)
//
// The theme is the id on the first line of /achievements/theme.txt; modern without one.

#include "draw.h"

typedef struct
{
    const char *id;             // in theme.txt
    const char *name;           // what it's called
    int         font;           // the text's face (DRAW_FONT_*)

    // The bottom screen's list of achievements, for scrolling, the selection and taps: boxes
    // boxHeight apart from listTop down, shown in listHeight pixels.
    int     listTop, listHeight, boxHeight;
    // The button prompts' tap areas (g_tabs): tabWidth pixels from each side, from row tabTop down.
    int     tabWidth, tabTop;
    // Left/Right go through at least this many positions, empty past the last game.
    int     minPositions;
    // The app opens with no game open (g_picking): Left/Right go through the games and the
    // bottom screen shows the highlighted one's stats; A opens it (its achievements), B goes
    // back. Without it, the selected game's achievements are always shown.
    bool    gameList;
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
    // (g_count can be 0). game can be an empty position past the last game. With g_picking:
    // game's stats.
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
} Theme;

extern const Theme  THEME_MODERN, THEME_3DS;
extern const Theme *g_theme;

// Uses the theme saved in /achievements/theme.txt, modern without one.
void            Theme_Load(void);

// The theme with this id, or NULL.
const Theme    *Theme_Find(const char *id);

#endif
