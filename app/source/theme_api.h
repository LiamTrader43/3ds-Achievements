#ifndef THEME_API_H
#define THEME_API_H

// Theme add-ons: a theme built on its own instead of into the app, put in /achievements/themes/
// on the SD card, and loaded when it's chosen. Its code runs in the app, but it doesn't link to
// it: everything it uses from the app comes through the ThemeApi the app hands it (the
// functions and globals listed below), and it hands back its Theme (theme.h).
//
// Making one: a folder app/themes/<id>/ with its .c files (laid out like app/themes/3ds/, which
// is preinstalled instead: Makefile BUILTIN_THEMES). Each includes the app's headers it needs,
// then theme_module.h last, and ends with THEME_MODULE_DEFINE(itsTheme, "<id>", "<Name>",
// "<what it looks like, in a line>").
// `make` in app/ builds it into app/themes/<id>.theme (Makefile: "Theme add-ons"); that file
// goes in /achievements/themes/. The same source can be compiled into the app instead.
// A theme can use what the ThemeApi lists, the memory/string functions in
// app/themes/module_rt.c, and its own code and data; nothing else from the C library or
// libctru links (the build fails if it tries).
//
// Compatibility: a theme says which THEME_API_VERSION it was built for (ThemeInfo). The app
// loads themes built for THEME_API_OLDEST up to its own version.
//   - Adding to the API: add entries at the END of the last list below, THEME_API_ADDED (never
//     in between or to an earlier list: that would move what comes after it; never remove
//     one), add hooks at the end of Theme, and raise THEME_API_VERSION. Older themes
//     keep working; a theme built for the new version won't load in an older app.
//     A theme can still be built for an older version, so that older apps load it too:
//     theme_module.h's THEME_MODULE_API (it may then use only what that version had, and
//     THEME_API_SIZE_<version> must give that version's sizeof(ThemeApi)).
//   - Changing anything a theme already uses (a function's parameters, the layout of Game,
//     PopupText, UiSetting, ShareView, Theme..., what a hook means): raise THEME_API_VERSION
//     and set THEME_API_OLDEST to it. Every theme then has to be built again.

#include <3ds.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "draw.h"
#include "share.h"
#include "theme.h"
#include "ui.h"
#include "update.h"

#define THEME_API_VERSION   6       // 2: ThemeInfo.description, Theme.description/pickColumns, the menu (g_menu)
                                    // 3: g_missable
                                    // 4: achievements done in parts (g_partsDone ... g_partDone)
                                    // 5: each achievement's own picture (Ui_AchievementIcon)
                                    // 6: measured progress (Ui_Measured)
#define THEME_API_OLDEST    2

// What a theme says about itself (its THEME_MODULE_DEFINE), read before any of its code runs.
#define THEME_INFO_MAGIC    "A3THEME"
typedef struct ThemeInfo
{
    char    magic[8];           // THEME_INFO_MAGIC
    u32     apiVersion;         // the THEME_API_VERSION it was built for
    char    id[16];             // its id in theme.txt; the file is <id>.theme
    char    name[32];           // what the settings call it
    char    description[96];    // what it looks like, in a line (the theme list shows it)
} ThemeInfo;

// The .theme file, made by the linker (app/themes/theme.ld, whose header words and numbers
// must match these). In the theme's memory (offsets from where it's loaded):
//   0..codeEnd           this header, then code, read-only data and the relocations: made
//                        runnable, never written (up to codeSize, page-aligned; zero after codeEnd)
//   codeSize..           data, ending in THEME_FILE_END, then zeroed up to memorySize
// The file is the code part (0..codeEnd) and right after it the data part (up to fileSize, the
// end marker's end), so nothing in it is padding. Each relocation (8 bytes: memory offset,
// type 23 = add where the theme was loaded) changes a word of the data part.
#define THEME_FILE_MAGIC    0x48543341      // "A3TH"
#define THEME_FILE_FORMAT   2       // 2: ThemeInfo has a description
#define THEME_FILE_END      0x444E4554      // "TEND"
typedef struct
{
    u32       magic;            // THEME_FILE_MAGIC
    u32       format;           // THEME_FILE_FORMAT
    u32       entry;            // the ThemeEntry
    u32       codeEnd;          // the code part's size in the file (the data part follows it)
    u32       codeSize;         // where the data part goes in memory (page-aligned)
    u32       relStart, relEnd; // the relocations
    u32       fileSize;
    u32       memorySize;
    ThemeInfo info;
} ThemeHeader;

// The app's functions a theme can call.
#define THEME_API_FUNCTIONS(X) \
    X(Draw_Eyes) X(Draw_Eye) X(Draw_Depth) \
    X(Draw_Rect) X(Draw_RoundRect) X(Draw_RoundRectGradient) X(Draw_ClipRows) \
    X(Draw_SetFont) X(Draw_Text) X(Draw_TextWidth) X(Draw_TextFit) X(Draw_TextShadow) \
    X(Draw_Icon) X(Draw_TrophyIcon) X(Draw_IconScaled) \
    X(Draw_DimHero) X(Draw_Hero) X(Draw_Image) \
    X(Draw_FrameShine) X(Draw_FrameShineAgain) X(Draw_FrameShineOff) \
    X(Draw_Shadow) X(Draw_Darken) X(Draw_RoundRing) X(Draw_StampKeep) X(Draw_StampAgain) \
    X(Draw_Gradient) X(Draw_FadeToGradient) X(Draw_Into) X(Draw_Width) \
    X(Ui_PopupText) X(Ui_Trophy) X(Ui_EllipsizeScaled) X(Ui_Ellipsize) \
    X(Ui_DrawWrappedScaled) X(Ui_DrawWrapped) X(Ui_Totals) X(Ui_Setting) X(Ui_Approach) X(Ui_Battery) \
    X(Update_AppVersion) X(Share_View) X(Share_QrModule) \
    X(osGetTime) X(svcGetSystemTick) \
    X(snprintf) X(vsnprintf) X(strftime) X(time) X(gmtime) \
    X(Draw_TrophyScaled) \
    X(Ui_StorePosition) X(Ui_RowPositions) X(Ui_IsStore) X(Ui_StoreIcon) \
    X(Store_Count) X(Store_Item) X(Store_Owned) X(Store_OwnedCount) \
    X(Ui_MenuCount) X(Ui_MenuTitle)

// The app's globals (ui.h) a theme reads. They're the app's: a theme doesn't change them.
#define THEME_API_DATA(X) \
    X(g_games) X(g_gameCount) X(g_picking) \
    X(g_names) X(g_descriptions) X(g_points) X(g_unlocked) X(g_count) X(g_selected) \
    X(g_gameUnlocked) X(g_gameLocked) X(g_hasGameIcons) X(g_recent) X(g_recentCount) \
    X(g_hero) X(g_heroDim) X(g_heroReady) \
    X(g_trackerRunning) X(g_patchedAllowed) X(g_popup) X(g_popupResult) \
    X(g_tabs) X(g_tabPressed) \
    X(g_storeTile) X(g_coins) X(g_rewards) X(g_buyItem) X(g_paidOwed) \
    X(g_menu) X(g_menuScroll) \
    X(g_missable) \
    X(g_partsDone) X(g_partsTotal) X(g_partFirst) X(g_partNames) X(g_partDone)

// Added since (functions or globals), after everything above so it stays where it was.
#define THEME_API_ADDED(X)     X(Ui_AchievementIcon) X(Ui_Measured)

// A pointer to each of them, in the lists' order.
typedef struct ThemeApi
{
    u32     version;            // THEME_API_VERSION of the app (always first)
    u32     size;               // sizeof(ThemeApi) in the app
#define THEME_API_FIELD(name) __typeof__(name) *name;
    THEME_API_FUNCTIONS(THEME_API_FIELD)
    THEME_API_DATA(THEME_API_FIELD)
    THEME_API_ADDED(THEME_API_FIELD)
#undef THEME_API_FIELD
} ThemeApi;

// sizeof(ThemeApi) in an app of each THEME_API_VERSION from THEME_API_OLDEST on (a theme built
// for one checks that the app's is at least that big).
#define THEME_API_SIZE_2    offsetof(ThemeApi, g_missable)
#define THEME_API_SIZE_3    offsetof(ThemeApi, g_partsDone)
#define THEME_API_SIZE_4    offsetof(ThemeApi, Ui_AchievementIcon)
#define THEME_API_SIZE_5    offsetof(ThemeApi, Ui_Measured)
#define THEME_API_SIZE_6    sizeof(ThemeApi)

// A theme's entry point (the .theme file's ELF entry): keeps api and returns its Theme, or
// NULL if it can't run with this app.
typedef const Theme *(*ThemeEntry)(const ThemeApi *api);

// The app's ThemeApi (theme_api.c).
extern const ThemeApi g_themeApi;

#endif
