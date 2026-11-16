// 3DS Achievements app: starts the background tracker sysmodule when it isn't
// running yet, and shows every game with a definitions file in /achievements/ and
// its achievements, without starting the game. Reads the same files as the
// tracker. SELECT turns the tracker on or off. Y shares the selected game's progress
// as an image (share.c).
//
// Top screen: the games (Left/Right). Bottom screen: the selected game's achievements
// (Up/Down, A shows one in full). How they look is up to the theme (theme.h); this file
// does the rest.
#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "bench.h"
#include "draw.h"
#include "games.h"
#include "share.h"
#include "theme.h"
#include "tracker.h"
#include "ui.h"
#include "update.h"

// How the app moves (each theme draws it its own way).
#define HERO_DELAY_MS   150             // a game stays selected this long before its background loads
#define HERO_FADE_MS    350
#define HERO_OUT_MS     130             // a game's background fading out as you leave it
#define BOTTOM_FADE_MS  150             // the bottom screen fading in (a list loading, the full view)
#define TAP_SLOP        6               // a touch moving less is a tap
#define RESTART_MS      2500            // "Updated" shows this long, then the app restarts

static bool         s_gamesWaiting;         // game files to offer once the app update's popup closes
static u64          s_restartAt;            // after an app update: when the app closes for it

// A game's achievements, trophies and background, read on a thread of their own so the
// screens keep moving meanwhile (reading and parsing them takes a while on an Old 3DS).
// Nothing else may use achievements.c while it runs. The background goes straight into
// g_hero and g_heroDim, which aren't shown until the game is loaded.
#define LOADER_STACK    0x10000
typedef struct
{
    int         game;
    bool        loaded;                     // its definitions were read
    bool        hasIcons, heroReady;
    u16         unlocked[ICON_SIZE * ICON_SIZE], locked[ICON_SIZE * ICON_SIZE];
    int         count;
    const char *names[MAX_ACHIEVEMENTS], *descriptions[MAX_ACHIEVEMENTS];
    uint16_t    points[MAX_ACHIEVEMENTS];
    bool        isUnlocked[MAX_ACHIEVEMENTS];
    int         gameUnlocked, total, earned, gamePoints;
    int         recent[RECENT_MAX], recentCount;
} Loaded;
static Loaded        s_loaded;
static Thread        s_loader;
static volatile bool s_loaderDone;

// The loaded game's most recently unlocked achievements, newest first, as indices into its
// list: each unlock is appended to progress.txt, so its last lines are the latest.
static int RecentUnlocks(const bool *unlocked, int count, int *recent)
{
    static char text[8192];
    const char *ids[MAX_ACHIEVEMENTS];
    char        path[64];
    uint32_t    length = 0;
    int         n = 0;

    snprintf(path, sizeof(path), "/achievements/%016llX/progress.txt", (unsigned long long)Achievements_GameId());
    if (Achievements_Ids(ids, MAX_ACHIEVEMENTS) != count || !Platform_ReadFile(path, text, sizeof(text) - 1, &length))
        return 0;
    for (int end = (int)length; end > 0 && n < RECENT_MAX; )
    {
        // The line before end, without its spaces.
        int start = end - 1;
        while (start > 0 && text[start - 1] != '\n')
            start--;
        int lineEnd = end;
        end = start > 0 ? start - 1 : 0;
        while (lineEnd > start && (text[lineEnd - 1] == '\n' || text[lineEnd - 1] == '\r' || text[lineEnd - 1] == ' ' ||
                                   text[lineEnd - 1] == '\t'))
            lineEnd--;
        while (start < lineEnd && (text[start] == ' ' || text[start] == '\t'))
            start++;
        if (lineEnd == start)
            continue;

        for (int i = 0; i < count; i++)
        {
            if (!unlocked[i] || strlen(ids[i]) != (size_t)(lineEnd - start) ||
                memcmp(ids[i], text + start, (size_t)(lineEnd - start)) != 0)
                continue;
            bool seen = false;
            for (int k = 0; k < n; k++)
                seen = seen || recent[k] == i;
            if (!seen)
                recent[n++] = i;
            break;
        }
    }
    return n;
}

static void LoadThread(void *arg)
{
    (void)arg;
    Loaded *l = &s_loaded;
    u64 titleId = g_games[l->game].titleId;

    l->hasIcons = Games_LoadTrophies(titleId, l->unlocked, l->locked);
    l->count = l->recentCount = 0;
    l->loaded = Achievements_Load(titleId);
    if (l->loaded)
    {
        l->count = Achievements_List(l->names, l->isUnlocked, MAX_ACHIEVEMENTS);
        Achievements_Descriptions(l->descriptions, MAX_ACHIEVEMENTS);
        Achievements_Points(l->points, MAX_ACHIEVEMENTS);
        Achievements_Progress(&l->gameUnlocked, &l->total);
        Achievements_Score(&l->earned, &l->gamePoints);
        l->recentCount = RecentUnlocks(l->isUnlocked, l->count, l->recent);
    }
    l->heroReady = Games_LoadHero(titleId, g_hero);
    if (l->heroReady)
        Draw_DimHero(g_hero, g_heroDim);
    __atomic_store_n(&s_loaderDone, true, __ATOMIC_RELEASE);
}

// Starts loading game i (reads the progress file again, so it's current). Below the main
// thread's priority: it runs while the main thread waits for the screen.
static void StartLoading(int i)
{
    s32 priority = 0x30;
    s_loaded.game = i;
    s_loaderDone = false;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_loader = threadCreate(LoadThread, NULL, LOADER_STACK, priority + 1, -2, false);
    if (s_loader == NULL)
        LoadThread(NULL);       // loaded right here instead
}

// True once the load has finished; wait: until it has.
static bool LoadingDone(bool wait)
{
    if (s_loader != NULL)
    {
        if (!wait && !__atomic_load_n(&s_loaderDone, __ATOMIC_ACQUIRE))
            return false;
        threadJoin(s_loader, U64_MAX);
        threadFree(s_loader);
        s_loader = NULL;
    }
    return true;
}

// Shows what was loaded: the list in s_*, the counts in its Game.
static void UseLoaded(void)
{
    const Loaded *l = &s_loaded;
    Game *g = &g_games[l->game];

    g_hasGameIcons = l->hasIcons;
    memcpy(g_gameUnlocked, l->unlocked, sizeof(g_gameUnlocked));
    memcpy(g_gameLocked, l->locked, sizeof(g_gameLocked));
    memcpy(g_names, l->names, sizeof(g_names));
    memcpy(g_descriptions, l->descriptions, sizeof(g_descriptions));
    memcpy(g_points, l->points, sizeof(g_points));
    memcpy(g_unlocked, l->isUnlocked, sizeof(g_unlocked));
    memcpy(g_recent, l->recent, sizeof(g_recent));
    g_recentCount = l->recentCount;
    g_count = l->count;
    if (l->loaded)
    {
        g->unlocked = l->gameUnlocked, g->total = l->total;
        g->earned = l->earned, g->points = l->gamePoints;
    }
}

// Positions to scroll through: every game, and with some themes empty ones after them.
static int RowPositions(void)
{
    return g_gameCount > g_theme->minPositions ? g_gameCount : g_theme->minPositions;
}

// Boxes the list shows at once.
static int BoxRows(void)
{
    return g_theme->listHeight / g_theme->boxHeight;
}

// The furthest the list can scroll, in pixels.
static int MaxScroll(void)
{
    return g_count > BoxRows() ? (g_count - BoxRows()) * g_theme->boxHeight : 0;
}

// The scroll (in whole boxes) that keeps the selected box on the screen, starting from scroll.
static int ScrollToShow(int scroll)
{
    int boxH = g_theme->boxHeight;
    if (g_selected * boxH < scroll)
        return g_selected * boxH;
    if ((g_selected + 1) * boxH > scroll + g_theme->listHeight)
        return (g_selected + 1) * boxH - g_theme->listHeight;
    return scroll;
}

// What the bottom screen shows: a game's stats, its achievements or one in full, and where.
typedef struct
{
    int  game, scroll, selected, count, tabPressed;
    bool details, loaded, picking;
} BottomView;

// The tabs the bottom screen shows (g_tabs) for what's on it: the share screen, position game
// (an empty one has none), its stats while one is being chosen, its achievements, or one in full.
static void SetTabs(bool sharing, bool details, int game, bool loaded)
{
    static const UiTab BACK = { KEY_B, "B", "Back" }, OPEN = { KEY_A, "A", "Open" };
    static const UiTab SHARE = { KEY_Y, "Y", "Share" }, SAVE = { KEY_A, "A", "Save to SD" };

    memset(g_tabs, 0, sizeof(g_tabs));
    if (sharing)
    {
        ShareView v;
        Share_View(&v);
        if (v.state != SHARE_OPENING)
            g_tabs[TAB_LEFT] = BACK;
        if (v.state != SHARE_OPENING && v.state != SHARE_NO_MEMORY)
            g_tabs[TAB_RIGHT] = SAVE;
    }
    else if (game >= g_gameCount)
        return;
    else if (g_picking)
        g_tabs[TAB_RIGHT] = OPEN;
    else if (details)
        g_tabs[TAB_LEFT] = BACK;
    else
    {
        if (g_theme->gameList)
            g_tabs[TAB_LEFT] = BACK;
        if (loaded)
            g_tabs[TAB_RIGHT] = SHARE;
    }
}

// The tab at (x, y) on the bottom screen, or -1.
static int TabAt(int x, int y)
{
    if (y < g_theme->tabTop)
        return -1;
    if (x < g_theme->tabWidth && g_tabs[TAB_LEFT].key != 0)
        return TAB_LEFT;
    if (x >= BOTTOM_WIDTH - g_theme->tabWidth && g_tabs[TAB_RIGHT].key != 0)
        return TAB_RIGHT;
    return -1;
}

// The menu's top screen, once for each eye's image when 3D is on (the background goes into
// both first).
static void DrawTopScreen(int game, int heroLevel)
{
    if (g_theme->drawTopBackground != NULL)
        g_theme->drawTopBackground(heroLevel);
    for (int eye = 0; eye < Draw_Eyes(); eye++)
    {
        Draw_Eye(eye);
        g_theme->drawTop(game, heroLevel);
        if (g_popup != POPUP_NONE)
            g_theme->drawPopup(false);
    }
    Draw_Eye(0);
}


// Shown while files download; nothing else on the screen is changing.
static void ShowProgress(Popup popup, int done, int total)
{
    g_popup = popup;
    g_downloadDone = done;
    g_downloadTotal = total;
    Draw_Begin(true, true);
    for (int eye = 0; eye < Draw_Eyes(); eye++)
    {
        Draw_Eye(eye);
        g_theme->drawPopup(true);
    }
    Draw_End();
}

static void ShowDownloadProgress(int done, int total)
{
    ShowProgress(POPUP_DOWNLOADING, done, total);
}

// The app's own update: KB downloaded of total, then total 0 while it installs.
static void ShowAppProgress(int done, int total)
{
    ShowProgress(POPUP_APP_DOWNLOADING, done, total);
}

// Downloads and installs the newer app (again, after a failure), then says how it went.
static void InstallAppUpdate(void)
{
    bool trackerChanged, changed;
    Result res = Update_InstallApp(ShowAppProgress, &trackerChanged, &changed);
    g_appChanged = g_appChanged || changed;     // an earlier try may have replaced something already

    // The new tracker runs from now on, if the old one was running.
    if (trackerChanged && g_trackerRunning)
    {
        if (R_FAILED(Tracker_Restart()) || !Tracker_Running())
            g_trackerNotRestarted = true;
        g_trackerRunning = Tracker_Running();
    }
    g_popup = R_SUCCEEDED(res) ? POPUP_APP_UPDATED : POPUP_APP_FAILED;
    g_popupResult = res;
    if (R_SUCCEEDED(res))
    {
        g_canRestart = Update_CanRestart();
        s_restartAt = osGetTime() + RESTART_MS;
    }
}

// The direction key pressed this frame, or held (repeating after 400 ms).
static u32 Direction(u32 down, u32 held, u64 *repeatAt)
{
    const u32 directions = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    u64 now = osGetTime();

    if (down & directions)
    {
        *repeatAt = now + 400;
        return down & directions;
    }
    if ((held & directions) && now >= *repeatAt)
    {
        *repeatAt = now + 80;
        return held & directions;
    }
    return 0;
}

static int Clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

int main(int argc, char **argv)
{
    Draw_Init();
    Theme_Load();

    // A loading frame, while the files and icons are read.
    for (int i = 0; i < 2; i++)
    {
        Draw_Begin(true, true);
        g_theme->drawLoading();
        Draw_End();
    }
#ifdef BENCH
    // The benchmark leaves the tracker and the updater alone (their popups would stop the script).
    (void)argc;
    (void)argv;
    g_gameCount = Games_Scan(g_games, MAX_GAMES);
#else
    bool wanted = !Tracker_TurnedOff();
    Result startResult = wanted ? Tracker_Start() : 0;
    g_gameCount = Games_Scan(g_games, MAX_GAMES);
    g_trackerRunning = Tracker_Running();
    g_patchedAllowed = Tracker_PatchedAllowed();
    if (wanted && !g_trackerRunning)
    {
        g_popup = POPUP_START_FAILED;
        g_popupResult = startResult;
    }

    Update_Start(argc > 0 ? argv[0] : NULL);    // the Homebrew Launcher passes the .3dsx's path
#endif

    g_theme->reset();
    g_picking = g_theme->gameList;  // such a theme starts on the list of games
    int     game = 0;
    int     scroll = 0;             // how far the list is scrolled, in pixels
    int     touchY = 0;             // where the current drag started
    int     touchScroll = 0;        // and how far the list was scrolled then
    bool    touchActive = false;    // a touch that began on the list is still down
    bool    touchMoved = false;     // and it has moved: a drag, not a tap
    bool    details = false;        // the selected achievement is shown in full
    int     bottomFade = 256;       // the bottom screen fading in (0-256): a list loading, the full view
    int     redraw = 2;             // both screens: the frames left to redraw (both buffers of each)
    int     redrawTop = 0;          // like redraw, for the top screen alone (the row, the background, 3D, hints)
    int     redrawBottom = 0;       // and for the bottom screen alone (the list)
    u64     repeatAt = 0;
    int     heroFor = -1;           // the game g_hero was last loaded for
    bool    loading = false;        // a game is being loaded (s_loaded.game)
    bool    fadeHeld = false;       // the bottom screen's fade starts on this frame: it doesn't move on yet
    u64     heroAt = 0;             // when to load the selected game's background
    int     heroLevel = 0;          // how far the background has faded in, 0-256
    bool    sharing = false;        // the share screen is open
    bool    restart = false;        // the app was updated: its new version starts once it closes
    int     tabTouch = -1;          // the tab a touch began on, or -1
    bool    tabInside = false;      // and the touch is still on it
    u64     lastFrame = osGetTime();

    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
#ifdef BENCH
        if (!Bench_Input(&down, &held, &up))
            break;
#endif

        // A tap on a tab presses its button: when the touch that began on it lets go still on
        // it. Meanwhile the touch is the tab's, not the list's. Not under a popup.
        if (g_popup != POPUP_NONE)
            tabTouch = -1;
        else
        {
            SetTabs(sharing, details, game, heroFor == game);
            touchPosition touch;
            if ((down & KEY_TOUCH) && tabTouch < 0)
            {
                hidTouchRead(&touch);
                tabTouch = TabAt(touch.px, touch.py);
                tabInside = true;
            }
            if (tabTouch >= 0)
            {
                if (held & KEY_TOUCH)
                {
                    hidTouchRead(&touch);
                    tabInside = TabAt(touch.px, touch.py) == tabTouch;
                }
                if (up & KEY_TOUCH)
                {
                    if (tabInside)
                        down |= g_tabs[tabTouch].key;
                    tabTouch = -1;
                }
                down &= ~KEY_TOUCH, held &= ~KEY_TOUCH, up &= ~KEY_TOUCH;
            }
        }
        int pressed = tabTouch >= 0 && tabInside ? tabTouch : -1;
        if (pressed != g_tabPressed)
        {
            g_tabPressed = pressed;
            redrawBottom = 2;
        }

        u32 dir = Direction(down, held, &repeatAt);

        if (down & KEY_START)
        {
            restart = g_popup == POPUP_APP_UPDATED;
            break;
        }

        // A newer app is offered first; new achievements after it, if it's put off.
        if (g_popup == POPUP_NONE && !sharing && Update_Poll(&g_newGames, &g_updatedGames, &g_appVersion))
        {
            bool games = g_newGames + g_updatedGames > 0;
            if (g_appVersion != NULL)
            {
                g_popup = POPUP_APP_UPDATE;
                s_gamesWaiting = games;
            }
            else if (games)
                g_popup = POPUP_UPDATE;
            redrawTop = 2;
        }

        if (sharing)
        {
            if (down & KEY_B)
            {
                Share_Close();
                sharing = false;
                redraw = 2;
            }
            else
            {
                if (down & KEY_A)
                {
                    Share_Save();
                    redraw = 2;
                }
                if (Share_Poll())
                    redraw = 2;
            }
        }
        else if (g_popup == POPUP_UPDATE)
        {
            if (down & KEY_A)
            {
                // The files are about to change, and the games are read again after.
                LoadingDone(true);
                loading = false;
                Result res = Update_Download(ShowDownloadProgress);
                g_popup = R_FAILED(res) ? POPUP_UPDATE_FAILED : POPUP_NONE;
                g_popupResult = res;

                // Whatever arrived shows up right away.
                g_gameCount = Games_Scan(g_games, MAX_GAMES);
                game = 0;
                g_theme->reset();
                g_count = 0;
                g_heroReady = false;
                heroFor = -1;
                heroAt = 0;
                scroll = 0;
                g_selected = 0;
                details = false;
                g_picking = g_theme->gameList;
                redraw = 2;
            }
            else if (down & KEY_B)
            {
                g_popup = POPUP_NONE;
                redrawTop = 2;
            }
        }
        else if (g_popup == POPUP_APP_UPDATE || g_popup == POPUP_APP_FAILED)
        {
            if (down & KEY_A)                   // update, or try again
            {
                LoadingDone(true);
                loading = false;
                InstallAppUpdate();
                redraw = 2;
            }
            else if (down & KEY_B)
            {
                g_popup = s_gamesWaiting ? POPUP_UPDATE : POPUP_NONE;
                s_gamesWaiting = false;
                redrawTop = 2;
            }
        }
        else if (g_popup == POPUP_APP_UPDATED)
        {
            // The old version's still running: it closes, and the new one starts.
            if ((down & KEY_A) || osGetTime() >= s_restartAt)
            {
                restart = true;
                break;
            }
        }
        else if (g_popup != POPUP_NONE)
        {
            if (down & (KEY_A | KEY_B))
            {
                g_popup = s_gamesWaiting ? POPUP_UPDATE : POPUP_NONE;
                s_gamesWaiting = false;
                redrawTop = 2;
            }
        }
        else
        {
            if (down & KEY_SELECT)
            {
                bool on = !g_trackerRunning;
                Result res = Tracker_SetEnabled(on);
                g_trackerRunning = Tracker_Running();
                if (g_trackerRunning != on)
                {
                    g_popup = on ? POPUP_START_FAILED : POPUP_STOP_FAILED;
                    g_popupResult = res;
                }
                redrawTop = 2;
            }

            if (down & KEY_X)
            {
                Tracker_SetPatchedAllowed(!g_patchedAllowed);
                g_patchedAllowed = Tracker_PatchedAllowed();    // what was actually saved
                redrawTop = 2;
            }

            // Share: only once the selected game's achievements have loaded.
            if ((down & KEY_Y) && game < g_gameCount && heroFor == game)
            {
                Draw_Begin(true, true);
                DrawTopScreen(game, heroLevel);
                g_theme->drawShareBottom();     // not open yet: the image is being made
                Draw_End();
                sharing = Share_Open(&g_games[game], g_heroReady ? g_hero : NULL, Ui_Trophy(true));
                redraw = 2;
            }

            if (g_gameCount > 0 && g_picking)
            {
                // Choosing a game: Left/Right go along the games, the bottom screen shows the
                // highlighted one's stats, A opens it.
                if ((down & KEY_A) && game < g_gameCount)
                {
                    g_picking = false;
                    details = false;
                    bottomFade = 0;
                    redraw = 2;
                }
                int next = Clamp(game + ((dir & KEY_RIGHT) ? 1 : 0) - ((dir & KEY_LEFT) ? 1 : 0), 0, RowPositions() - 1);
                if (next != game)
                {
                    game = next;
                    g_count = 0;                // its achievements load once the selection settles
                    g_recentCount = 0;
                    heroFor = -1;
                    heroAt = osGetTime() + HERO_DELAY_MS;
                    scroll = 0;
                    g_selected = 0;
                    redraw = 2;
                }
            }
            else if (g_gameCount > 0 && details)
            {
                // One achievement in full: B or a tap goes back to the list, Up/Down show the
                // one before or after (the list follows, so it's on screen when you go back).
                int was = g_selected;
                if (dir & KEY_UP)
                    g_selected--;
                if (dir & KEY_DOWN)
                    g_selected++;
                g_selected = Clamp(g_selected, 0, g_count - 1);
                if (g_selected != was)
                {
                    scroll = Clamp(ScrollToShow(scroll), 0, MaxScroll());
                    redrawBottom = 2;
                }
                if (down & (KEY_B | KEY_TOUCH))
                {
                    details = false;
                    bottomFade = 0;
                    redrawBottom = 2;
                }
            }
            else if (g_gameCount > 0 && g_theme->gameList && (down & KEY_B))
            {
                // Back to choosing a game.
                g_picking = true;
                touchActive = false;
                bottomFade = 0;
                redraw = 2;
            }
            else if (g_gameCount > 0)
            {
                // A shows the selected achievement in full.
                if ((down & KEY_A) && g_count > 0 && heroFor == game)
                {
                    details = true;
                    bottomFade = 0;
                    touchActive = false;
                    redrawBottom = 2;
                }

                int next = Clamp(game + ((dir & KEY_RIGHT) ? 1 : 0) - ((dir & KEY_LEFT) ? 1 : 0), 0, RowPositions() - 1);
                if (next != game)
                {
                    game = next;
                    g_count = 0;                // the list loads once the selection settles
                    g_recentCount = 0;
                    heroFor = -1;
                    heroAt = osGetTime() + HERO_DELAY_MS;
                    scroll = 0;
                    g_selected = 0;
                    redraw = 2;
                }

                // Up/Down move the selection one box, L/R a page. The list scrolls (by whole
                // boxes) only to keep the selected box on the screen.
                const int boxH = g_theme->boxHeight, rows = BoxRows();
                int move = 0;
                if (dir & KEY_UP)       move = -1;
                if (dir & KEY_DOWN)     move = 1;
                if (down & KEY_L)       move = -rows;
                if (down & KEY_R)       move = rows;
                int top = scroll;
                if (move != 0 && g_count > 0)
                {
                    int was = g_selected;
                    int firstShown = (scroll + boxH - 1) / boxH, lastShown = (scroll + g_theme->listHeight) / boxH - 1;
                    bool page = move == rows || move == -rows;
                    if (page)
                        top = ((scroll + boxH / 2) / boxH + move) * boxH;
                    if (!page && g_selected < firstShown)
                        g_selected = firstShown;        // dragged off the screen: start from what's shown
                    else if (!page && g_selected > lastShown)
                        g_selected = lastShown;
                    else
                        g_selected += move;
                    g_selected = Clamp(g_selected, 0, g_count - 1);
                    top = ScrollToShow(top);
                    if (g_selected != was)
                        redrawBottom = 2;
                }

                // Dragging on the touch screen moves the list with the stylus; a tap (a touch
                // that hardly moves) on an achievement shows it in full.
                if ((down & KEY_TOUCH) || ((held & KEY_TOUCH) && touchActive))
                {
                    touchPosition touch;
                    hidTouchRead(&touch);
                    if (down & KEY_TOUCH)
                    {
                        touchActive = true;
                        touchMoved = false;
                        touchY = touch.py;
                        touchScroll = scroll;
                    }
                    if (abs(touch.py - touchY) > TAP_SLOP)
                        touchMoved = true;
                    top = touchScroll + touchY - touch.py;
                }
                if ((up & KEY_TOUCH) && touchActive)
                {
                    touchActive = false;
                    int listTop = g_theme->listTop, i = (scroll + touchY - listTop) / boxH;
                    if (!touchMoved && touchY >= listTop && touchY < listTop + g_theme->listHeight && i < g_count &&
                        heroFor == game)
                    {
                        g_selected = i;
                        details = true;
                        bottomFade = 0;
                        redrawBottom = 2;
                    }
                }

                top = Clamp(top, 0, MaxScroll());
                if (top != scroll)
                {
                    scroll = top;
                    redrawBottom = 2;
                }
            }
        }

        // The old background finishes fading out before the next game is loaded over it.
        if (!loading && g_gameCount > 0 && heroFor != game && osGetTime() >= heroAt && heroLevel == 0)
        {
            if (game < g_gameCount)
            {
                StartLoading(game);
                loading = true;
            }
            else
            {
                heroFor = game;             // the empty positions have nothing to load
                g_heroReady = false;
                redraw = 2;
            }
        }
        if (loading && LoadingDone(false))
        {
            loading = false;
            if (s_loaded.game == game)      // still selected (else the one that is loads next)
            {
                BENCH_TIME(BENCH_LOAD, UseLoaded());
                heroFor = game;
                g_heroReady = s_loaded.heroReady;
                if (!g_picking)
                {
                    bottomFade = 0;         // its list fades in, from its first frame drawn
                    fadeHeld = true;
                }
                redraw = 2;
            }
        }

        // Everything that eases: the row and the background.
        {
            u64 now = osGetTime();
            int milliseconds = (int)(now - lastFrame);
            lastFrame = now;
            if (milliseconds > 100)
                milliseconds = 100;         // after a pause, don't jump

            if (g_gameCount > 0 && g_theme->animate(game, milliseconds))
                redrawTop = 2;

            int target = g_heroReady && heroFor == game ? 256 : 0;
            if (heroLevel != target)
            {
                int step = milliseconds * 256 / (target != 0 ? HERO_FADE_MS : HERO_OUT_MS);
                heroLevel = Ui_Approach(heroLevel, target, step > 0 ? step : 1);
                redrawTop = 2;
            }

            if (bottomFade < 256)
            {
                int step = milliseconds * 256 / BOTTOM_FADE_MS;
                if (!fadeHeld)
                    bottomFade = Ui_Approach(bottomFade, 256, step > 0 ? step : 1);
                fadeHeld = false;
                redrawBottom = 2;
            }
        }

        // 3D follows the slider on the menu; the share screen is a flat picture.
#ifdef BENCH
        float slider = Bench_Slider();
#else
        float slider = osGet3DSliderState();
#endif
        if (Draw_Stereo(sharing ? 0.0f : slider))
            redrawTop = 2;
        if (!sharing && g_theme->topChanged != NULL && g_theme->topChanged())
            redrawTop = 2;

        // Some themes keep something moving (modern: the light around the selected game's frame).
        // In a frame where nothing else changed, only it is redrawn. The share screen stays
        // still: it shows the image as it's shared.
        bool idle = g_theme->drawIdle != NULL && !sharing && g_gameCount > 0 && g_popup == POPUP_NONE;
        if (redraw > 0 || redrawTop > 0 || redrawBottom > 0 || idle)
        {
            bool top = redraw > 0 || redrawTop > 0;
            bool bottom = redraw > 0 || redrawBottom > 0;
            // The theme's bottom screen sets every pixel, and its top screen may: no clearing first.
            int  topMode = !top ? DRAW_KEEP : !sharing && g_theme->topCovers(heroLevel) ? DRAW_COVER : DRAW_CLEAR;
            int  bottomMode = !bottom ? DRAW_KEEP : !sharing ? DRAW_COVER : DRAW_CLEAR;

#ifdef BENCH
            Bench_DrawStart();
#endif
            SetTabs(sharing, details, game, heroFor == game);
            Draw_Begin(topMode, bottomMode);
            if (sharing)
            {
                if (top)
                    g_theme->drawShareTop();
                if (bottom)
                    g_theme->drawShareBottom();
            }
            else
            {
                if (top)
                    DrawTopScreen(game, heroLevel);     // with any popup
#ifdef BENCH
                u64 bottomStart = svcGetSystemTick();
#endif
                // While the bottom screen fades in, the list (or full view) is drawn once and
                // kept; each step of the fade is made from the kept copy.
                static u8 kept[BOTTOM_WIDTH * SCREEN_HEIGHT * 3] __attribute__((aligned(4)));
                static BottomView keptView;
                static bool keptValid;
                BottomView view;
                memset(&view, 0, sizeof(view));     // compared whole, padding too
                view.game = game, view.scroll = scroll, view.selected = g_selected, view.count = g_count;
                view.details = details, view.loaded = heroFor == game, view.picking = g_picking;
                view.tabPressed = g_tabPressed;
                bool sameView = keptValid && memcmp(&view, &keptView, sizeof(view)) == 0;
                if (bottom && game < g_gameCount && (sameView || bottomFade < 256))
                {
                    if (!sameView)
                    {
                        Draw_Into(GFX_BOTTOM, kept);
                        g_theme->drawBottom(game, view.scroll, details && g_count > 0, heroFor == game);
                        Draw_Into(GFX_BOTTOM, NULL);
                        keptView = view, keptValid = true;
                    }
                    BENCH_TIME(BENCH_FADE, Draw_FadeToGradient(GFX_BOTTOM, 0, SCREEN_HEIGHT, g_theme->fadeTop,
                                                               g_theme->fadeBottom, bottomFade, kept));
                }
                else
                {
                    if (bottom)
                        g_theme->drawBottom(game, view.scroll, details && g_count > 0, heroFor == game);
                    keptValid = false;
                }
#ifdef BENCH
                Bench_Add(BENCH_BOTTOM, svcGetSystemTick() - bottomStart);
#endif
            }
            if (!top && idle)
                BENCH_TIME(BENCH_LIGHT, g_theme->drawIdle());
#ifdef BENCH
            Bench_DrawDone();
            Draw_End();
            Bench_EndDone();
#else
            Draw_End();
#endif
            if (redraw > 0)
                redraw--;
            if (redrawTop > 0)
                redrawTop--;
            if (redrawBottom > 0)
                redrawBottom--;
        }
        else
            gspWaitForVBlank();
    }

    if (sharing)
        Share_Close();
    LoadingDone(true);
#ifdef BENCH
    (void)restart;
    Bench_Report();
#else
    Update_Stop();
    // Not when the app was closed from the HOME Menu (the loop ends without restart): the
    // system closes it then, and a .3dsx named to Luma's loader would open instead of the
    // Homebrew Launcher next time.
    if (restart)
        Update_Restart();
#endif
    Draw_Exit();
    return 0;
}
