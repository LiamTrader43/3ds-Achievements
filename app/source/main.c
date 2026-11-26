// 3DS Achievements app: starts the background tracker sysmodule when it isn't
// running yet, and shows every game with a definitions file in /achievements/ and
// its achievements, without starting the game. Reads the same files as the
// tracker. X opens the settings (the tracker, patched games, the theme). Y shares the
// selected game's progress as an image (share.c).
//
// Top screen: the games (Left/Right), then the store. Bottom screen: the highlighted game's
// stats; A opens it: its achievements (Up/Down, A shows one in full, B goes back). The store
// (store.c) works the same way: what's for sale, A buys with Play Coins (rewards.c gives them
// for achievement points when the app opens). How they look is up to the theme (theme.h);
// this file does the rest.
#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "autostart.h"
#include "bench.h"
#include "draw.h"
#include "games.h"
#include "hotkey.h"
#include "share.h"
#include "sound.h"
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
static bool         s_coinsWaiting;         // the Play Coins earned, once the popup showing closes
static Result       s_autostartOff;         // why "Start with the console" was turned off, to tell once the popup showing closes
static Result       s_coinsResult;          // of reading them, if they couldn't be (g_coins -1)
static u64          s_restartAt;            // after an app update: when the app closes for it

// The sound for what was pressed this frame (SoundId, or -1), played once the frame's input is
// handled; a popup that comes up has its own sound, which wins.
static int          s_sound = -1;
static char         s_shareStatus[48];      // the share screen's news last heard

static void PlayLater(SoundId id)
{
    s_sound = id;
}

// The sound a popup makes as it comes up (g_popup): -1 for none (the progress bars).
static int PopupSound(void)
{
    switch (g_popup)
    {
        case POPUP_NONE:
        case POPUP_DOWNLOADING:
        case POPUP_APP_DOWNLOADING:
            return -1;
        case POPUP_COINS:
            return SOUND_COINS;
        case POPUP_BOUGHT:
            return SOUND_BOUGHT;
        case POPUP_NOT_ENOUGH:
            return SOUND_ERROR;
        default:
            break;
    }
    PopupText t;
    Ui_PopupText(&t);
    return t.problem ? SOUND_ERROR : SOUND_NOTICE;
}

// The share screen's latest news, once: the image saved (or sent to a phone), or a problem.
static void ShareSounds(void)
{
    ShareView v;
    Share_View(&v);
    if (v.status == NULL || strcmp(v.status, s_shareStatus) == 0)
        return;
    snprintf(s_shareStatus, sizeof(s_shareStatus), "%s", v.status);
    if (v.tone == SHARE_GOOD)
        PlayLater(SOUND_SAVED);
    else if (v.tone == SHARE_BAD)
        PlayLater(SOUND_ERROR);
}

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
    GameIcons   icons;                      // each achievement's own picture, if any (until shown)
    int         count;
    const char *names[MAX_ACHIEVEMENTS], *descriptions[MAX_ACHIEVEMENTS];
    uint16_t    points[MAX_ACHIEVEMENTS];
    bool        isUnlocked[MAX_ACHIEVEMENTS], missable[MAX_ACHIEVEMENTS];
    uint8_t     partsDone[MAX_ACHIEVEMENTS], partsTotal[MAX_ACHIEVEMENTS];
    uint16_t    partFirst[MAX_ACHIEVEMENTS];
    const char *partNames[MAX_ACHIEVEMENTS];
    bool        partDone[MAX_ACHIEVEMENTS];
    uint32_t    measured[MAX_ACHIEVEMENTS], measuredTarget[MAX_ACHIEVEMENTS];
    bool        measuredPercent[MAX_ACHIEVEMENTS];
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
    static char text[16384];                // progress.txt: up to MAX_ACHIEVEMENTS IDs of 31 characters
    const char *ids[MAX_ACHIEVEMENTS];
    char        path[64];
    uint32_t    length = 0;
    int         n = 0;

    snprintf(path, sizeof(path), "%s/progress.txt", Achievements_Folder());
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
        Achievements_Missable(l->missable, MAX_ACHIEVEMENTS);
        Achievements_Parts(l->partsDone, l->partsTotal, MAX_ACHIEVEMENTS);
        for (int i = 0, parts = 0; i < l->count; i++)
        {
            l->partFirst[i] = (uint16_t)parts;
            if (l->partsTotal[i] > 0)
            {
                int n = Achievements_PartList(i, l->partNames + parts, l->partDone + parts, MAX_ACHIEVEMENTS - parts);
                l->partsTotal[i] = (uint8_t)n;      // as many as fit (all of them: parts are lines too)
                parts += n;
            }
        }
        Achievements_Measured(l->measured, l->measuredTarget, l->measuredPercent, MAX_ACHIEVEMENTS);
        Achievements_Progress(&l->gameUnlocked, &l->total);
        Achievements_Score(&l->earned, &l->gamePoints);
        l->recentCount = RecentUnlocks(l->isUnlocked, l->count, l->recent);

        static const char *ids[MAX_ACHIEVEMENTS];
        if (Achievements_Ids(ids, MAX_ACHIEVEMENTS) == l->count)
            Games_LoadIcons(Achievements_GameId(), ids, l->count, &l->icons);
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
    Games_FreeIcons(&s_loaded.icons);           // from a load that wasn't shown
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
    Loaded *l = &s_loaded;
    Game *g = &g_games[l->game];

    g_hasGameIcons = l->hasIcons;
    Games_FreeIcons(&g_icons);                  // the last game's pictures
    g_icons = l->icons;
    memset(&l->icons, 0, sizeof(l->icons));     // now g_icons'
    memcpy(g_gameUnlocked, l->unlocked, sizeof(g_gameUnlocked));
    memcpy(g_gameLocked, l->locked, sizeof(g_gameLocked));
    memcpy(g_names, l->names, sizeof(g_names));
    memcpy(g_descriptions, l->descriptions, sizeof(g_descriptions));
    memcpy(g_points, l->points, sizeof(g_points));
    memcpy(g_unlocked, l->isUnlocked, sizeof(g_unlocked));
    memcpy(g_missable, l->missable, sizeof(g_missable));
    memcpy(g_partsDone, l->partsDone, sizeof(g_partsDone));
    memcpy(g_partsTotal, l->partsTotal, sizeof(g_partsTotal));
    memcpy(g_partFirst, l->partFirst, sizeof(g_partFirst));
    memcpy(g_partNames, l->partNames, sizeof(g_partNames));
    memcpy(g_partDone, l->partDone, sizeof(g_partDone));
    memcpy(g_measured, l->measured, sizeof(g_measured));
    memcpy(g_measuredTarget, l->measuredTarget, sizeof(g_measuredTarget));
    memcpy(g_measuredPercent, l->measuredPercent, sizeof(g_measuredPercent));
    memcpy(g_recent, l->recent, sizeof(g_recent));
    g_recentCount = l->recentCount;
    g_count = l->count;
    if (l->loaded)
    {
        g->unlocked = l->gameUnlocked, g->total = l->total;
        g->earned = l->earned, g->points = l->gamePoints;
    }
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
    const Theme *theme;
    int  game, scroll, selected, count, tabPressed;
    int  coins, owned;                      // the store's Play Coins and items bought
    bool details, loaded, picking;
} BottomView;

// The tabs the bottom screen shows (g_tabs) for what's on it: the settings, the share screen,
// position game (an empty one has none), its stats while one is being chosen, its achievements,
// one in full, or what the store sells.
static void SetTabs(bool settings, bool sharing, bool details, int game, bool loaded)
{
    static const UiTab BACK = { KEY_B, "B", "Back" }, OPEN = { KEY_A, "A", "Open" };
    static const UiTab SHARE = { KEY_Y, "Y", "Share" }, SAVE = { KEY_A, "A", "Save to SD" };
    static const UiTab CHANGE = { KEY_A, "A", "Change" }, BUY = { KEY_A, "A", "Buy" }, USE = { KEY_A, "A", "Use" };

    memset(g_tabs, 0, sizeof(g_tabs));
    if (settings)
    {
        g_tabs[TAB_LEFT] = BACK;
        g_tabs[TAB_RIGHT] = g_menu == MENU_SETTINGS ? CHANGE : USE;
    }
    else if (sharing)
    {
        ShareView v;
        Share_View(&v);
        if (v.state != SHARE_OPENING)
            g_tabs[TAB_LEFT] = BACK;
        if (v.state != SHARE_OPENING && v.state != SHARE_NO_MEMORY)
            g_tabs[TAB_RIGHT] = SAVE;
    }
    else if (game >= g_gameCount && !Ui_IsStore(game))
        return;
    else if (g_picking)
        g_tabs[TAB_RIGHT] = OPEN;
    else if (details)
        g_tabs[TAB_LEFT] = BACK;
    else if (Ui_IsStore(game))
    {
        if (g_theme->gameList)
            g_tabs[TAB_LEFT] = BACK;
        if (loaded && g_count > 0)
            g_tabs[TAB_RIGHT] = BUY;
    }
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

// Closes the popup showing; the next one waiting comes up: new achievements after the app
// update's, then the Play Coins earned.
static void ClosePopup(void)
{
    g_popup = POPUP_NONE;
    if (s_gamesWaiting)
        g_popup = POPUP_UPDATE, s_gamesWaiting = false;
    else if (s_autostartOff != 0)
        g_popup = POPUP_AUTOSTART_OFF, g_popupResult = s_autostartOff, s_autostartOff = 0;
    else if (s_coinsWaiting)
        g_popup = POPUP_COINS, s_coinsWaiting = false;
}

// A or a tap on item i in the store: asks to buy it, or says why it can't be bought.
static void OpenBuy(int i)
{
    const StoreItem *item = Store_Item(i);
    if (item == NULL || Store_Owned(i))
        return;
    g_buyItem = i;
    if (g_coins < 0)                                // (they couldn't be read while the app loaded)
    {
        g_popupResult = s_coinsResult;
        g_popup = POPUP_BUY_FAILED;
    }
    else
        g_popup = g_coins >= item->price ? POPUP_BUY : POPUP_NOT_ENOUGH;
}

// A in the buy popup: buys it, then fills the room just made with coins kept from before.
static void BuyNow(void)
{
    int coins = -1;
    Result res = Store_Buy(g_buyItem, &coins);
    if (coins >= 0)
        g_coins = coins;
    g_popupResult = res;
    g_paidOwed = 0;
    if (R_SUCCEEDED(res))
    {
        Rewards_PayOwed(&g_paidOwed, &g_coins);
        g_popup = POPUP_BOUGHT;
        if (Store_Item(g_buyItem)->theme != NULL)
            Theme_Rescan();         // it's in the settings from now on
    }
    else
        g_popup = res == REWARDS_NOT_ENOUGH ? POPUP_NOT_ENOUGH : POPUP_BUY_FAILED;
}

// Changes setting i: turns the tracker or patched games on or off (Theme opens its list instead).
// Turns "Start with the console" on or off; the popup says how it went (none for off).
static void SetAutostart(bool on)
{
    Result res = on ? Autostart_Enable() : Autostart_Disable();
    g_autostart = Autostart_On();
    g_autostartUsable = Autostart_Usable();
    g_popupResult = res;
    g_popup = R_FAILED(res) ? POPUP_AUTOSTART_FAILED : on ? POPUP_AUTOSTART_ON : POPUP_NONE;
}

static bool s_hotkeyArmed;     // POPUP_HOTKEY: every button was up since it opened
static bool s_hotkeyCancel;    // B is held on its own (let go alone, it cancels)
static u64  s_hotkeySince;     // when the buttons held now were first held

static void OpenHotkey(void);
static void SaveHotkey(u32 keys);
static bool ChooseHotkey(u32 down, u32 held, u32 up);

static void ChangeSetting(int i)
{
    if (i == SETTING_TRACKER)
    {
        bool on = !g_trackerRunning;
        Result res = Tracker_SetEnabled(on);
        g_trackerRunning = Tracker_Running();
        if (g_trackerRunning != on)
        {
            g_popup = on ? POPUP_START_FAILED : POPUP_STOP_FAILED;
            g_popupResult = res;
        }
        PlayLater(g_trackerRunning ? SOUND_TOGGLE_ON : SOUND_TOGGLE_OFF);
    }
    else if (i == SETTING_AUTOSTART)
    {
        if (g_autostart)
        {
            SetAutostart(false);
            PlayLater(SOUND_TOGGLE_OFF);
        }
        else
            g_popup = POPUP_AUTOSTART_CONFIRM;      // what game patching also does first; A turns it on
    }
    else if (i == SETTING_PATCHED)
    {
        Tracker_SetPatchedAllowed(!g_patchedAllowed);
        g_patchedAllowed = Tracker_PatchedAllowed();    // what was actually saved
        PlayLater(g_patchedAllowed ? SOUND_TOGGLE_ON : SOUND_TOGGLE_OFF);
    }
    else if (i == SETTING_HOTKEY)
        OpenHotkey();
}

// Line i of the sound list (MENU_SOUNDS): a set (heard at once), or Off.
static void UseSounds(int i)
{
    int set = i < Sound_SetCount() ? i : -1;
    if (set < 0)
        Sound_Play(SOUND_TOGGLE_OFF);           // the last one heard
    Sound_Use(set);
    if (set >= 0)
        PlayLater(SOUND_SELECT);
}

// The sound list's line for the set in use (Off is the last).
static int SoundsLine(void)
{
    return Sound_Current() >= 0 ? Sound_Current() : Sound_SetCount();
}

// POPUP_HOTKEY: hold the new buttons (ChooseHotkey).
static void OpenHotkey(void)
{
    g_rosalinaCount = Tracker_RosalinaCombos(g_rosalina);  // Rosalina's menu can change it over the app
    g_popup = POPUP_HOTKEY;
    g_hotkeyHeld = 0;
    g_hotkeyProgress = 0;
    g_hotkeyRosalina = false;
    s_hotkeyArmed = false;
    s_hotkeyCancel = false;
}

// Saves the buttons chosen: POPUP_HOTKEY_SET, or POPUP_HOTKEY_FAILED.
static void SaveHotkey(u32 keys)
{
    if (Tracker_SetHotkey(keys))
    {
        g_hotkey = keys;
        g_popup = POPUP_HOTKEY_SET;
    }
    else
    {
        g_popup = POPUP_HOTKEY_FAILED;
        g_popupResult = 0;
    }
}

// POPUP_HOTKEY, once a frame: the buttons held for HOTKEY_HOLD_MS (2 to 4 of them, unchanged) become the hotkey,
// after POPUP_HOTKEY_WARN if they could get in the way while playing; buttons that overlap Rosalina's menu's never
// fill the bar. B pressed and let go on its own cancels. Nothing counts until every button is up after the A that
// opened it. True when the popup needs drawing again.
static bool ChooseHotkey(u32 down, u32 held, u32 up)
{
    u32 keys = held & HOTKEY_BUTTONS;
    u64 now = osGetTime();

    if (!s_hotkeyArmed)
    {
        s_hotkeyArmed = keys == 0;
        return false;
    }
    if ((down & KEY_B) && keys == KEY_B)
        s_hotkeyCancel = true;                      // B alone so far
    if (keys & ~KEY_B)
        s_hotkeyCancel = false;                     // with others: part of a choice
    if ((up & KEY_B) && keys == 0 && s_hotkeyCancel)
    {
        ClosePopup();
        PlayLater(SOUND_BACK);
        return true;
    }

    bool changed = keys != g_hotkeyHeld;
    if (changed)
    {
        g_hotkeyHeld = keys;
        s_hotkeySince = now;
    }
    bool rosalina = Hotkey_Valid(keys) && Ui_HotkeyRosalina(keys);
    if (rosalina != g_hotkeyRosalina)
        changed = true, g_hotkeyRosalina = rosalina;
    int progress = Hotkey_Valid(keys) && !rosalina ? (int)(now - s_hotkeySince) : 0;
    if (progress > HOTKEY_HOLD_MS)
        progress = HOTKEY_HOLD_MS;
    if (progress != g_hotkeyProgress)
        changed = true, g_hotkeyProgress = progress;
    if (progress >= HOTKEY_HOLD_MS)
    {
        if (Hotkey_Warning(keys) != HOTKEY_FINE)
        {
            g_hotkeyChosen = keys;
            g_popup = POPUP_HOTKEY_WARN;            // A uses them anyway, B chooses again
        }
        else
            SaveHotkey(keys);
        return true;
    }
    return changed;
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
    Ui_StoreInit();                 // first: it puts back bought themes that went missing
    bool themeLoaded = Theme_Load();

    // A loading frame, while the files and icons are read.
    for (int i = 0; i < 2; i++)
    {
        Draw_Begin(true, true);
        g_theme->drawLoading();
        Draw_End();
    }
    Games_MoveOldFolders();         // game folders into /achievements/games (no game runs while the app does)
#ifdef BENCH
    // The benchmark leaves the tracker, the updater and Play Coins alone (their popups would stop
    // the script).
    (void)argc;
    (void)argv;
    (void)themeLoaded;
    g_gameCount = Games_Scan(g_games, MAX_GAMES);
#else
    bool wanted = !Tracker_TurnedOff();
    Result startResult = wanted ? Tracker_Start() : 0;
    g_gameCount = Games_Scan(g_games, MAX_GAMES);
    g_trackerRunning = Tracker_Running();
    g_patchedAllowed = Tracker_PatchedAllowed();
    g_hotkey = Tracker_Hotkey();
    g_rosalinaCount = Tracker_RosalinaCombos(g_rosalina);  // the settings say if the buttons clash with it
    Result autostartOff = Autostart_Check();    // turned off since? (a start that failed, a system update)
    g_autostart = Autostart_On();
    g_autostartUsable = Autostart_Usable();
    if (wanted && !g_trackerRunning)
    {
        g_popup = POPUP_START_FAILED;
        g_popupResult = startResult;
    }
    else if (!themeLoaded)
        g_popup = POPUP_THEME_FAILED;   // the theme in theme.txt: modern is used instead
    if (autostartOff != 0 && g_popup == POPUP_NONE)
        g_popup = POPUP_AUTOSTART_OFF, g_popupResult = autostartOff;
    else
        s_autostartOff = autostartOff;

    // Play Coins for achievement points (only when the app opens; the tracker doesn't give them).
    int pointsAll = 0;
    for (int i = 0; i < g_gameCount; i++)
        pointsAll += g_games[i].earned;
    if (Rewards_Update(pointsAll, &g_rewards))
    {
        if (g_popup == POPUP_NONE)
            g_popup = POPUP_COINS;
        else
            s_coinsWaiting = true;
    }
    s_coinsResult = Rewards_Coins(&g_coins);    // while loading: the store shows these (purchases update them)

    // (The Homebrew Launcher passes the .3dsx's path. Store files that aren't here yet are fetched.)
    Update_Start(argc > 0 ? argv[0] : NULL);
    Sound_Init();                   // (not in the benchmark: it stays silent)
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
    bool    settings = false;       // the settings menu (X) is open
    int     settingItem = 0;        // and which setting is highlighted
    bool    restart = false;        // the app was updated: its new version starts once it closes
    int     tabTouch = -1;          // the tab a touch began on, or -1
    bool    tabInside = false;      // and the touch is still on it
    Popup   shownPopup = POPUP_NONE;    // the popup whose sound was played (one up at the start plays too)
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
            SetTabs(settings, sharing, details, game, heroFor == game);
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
        if (g_popup == POPUP_NONE && !sharing && !settings && Update_Poll(&g_newGames, &g_updatedGames, &g_storeFiles, &g_appVersion))
        {
            bool games = g_newGames + g_updatedGames + g_storeFiles > 0;
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
                PlayLater(SOUND_BACK);
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
                ShareSounds();              // saved, or sent to a phone
            }
        }
        else if (g_popup == POPUP_UPDATE)
        {
            if (down & KEY_A)
            {
                // The files are about to change, and the games are read again after.
                Sound_Play(SOUND_SELECT);   // now: the download takes a while
                LoadingDone(true);
                loading = false;
                Result res = Update_Download(ShowDownloadProgress);
                if (R_FAILED(res))
                    g_popup = POPUP_UPDATE_FAILED;
                else
                    ClosePopup();
                g_popupResult = res;

                // Whatever arrived shows up right away: games, and what the store sells (a bought
                // theme that was updated is put in place; a new one is in the settings).
                g_gameCount = Games_Scan(g_games, MAX_GAMES);
                Store_Load();
                Store_Restore();
                Theme_Rescan();
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
                ClosePopup();
                redrawTop = 2;
                PlayLater(SOUND_BACK);
            }
        }
        else if (g_popup == POPUP_APP_UPDATE || g_popup == POPUP_APP_FAILED)
        {
            if (down & KEY_A)                   // update, or try again
            {
                Sound_Play(SOUND_SELECT);       // now: the download takes a while
                LoadingDone(true);
                loading = false;
                InstallAppUpdate();
                redraw = 2;
            }
            else if (down & KEY_B)
            {
                ClosePopup();
                redrawTop = 2;
                PlayLater(SOUND_BACK);
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
        else if (g_popup == POPUP_BUY)
        {
            if (down & KEY_A)
            {
                BuyNow();
                redraw = 2;                 // the list shows it bought
            }
            else if (down & KEY_B)
            {
                ClosePopup();
                redrawTop = 2;
                PlayLater(SOUND_BACK);
            }
        }
        else if (g_popup == POPUP_AUTOSTART_CONFIRM)
        {
            if (down & KEY_A)
            {
                SetAutostart(true);
                redraw = 2;                 // the setting shows On
            }
            else if (down & KEY_B)
            {
                ClosePopup();
                redrawTop = 2;
                PlayLater(SOUND_BACK);
            }
        }
        else if (g_popup == POPUP_HOTKEY)
        {
            if (ChooseHotkey(down, held, up))
            {
                redrawTop = 2;              // the buttons held, the bar
                if (g_popup != POPUP_HOTKEY)
                    redraw = 2;             // and the setting's new value
            }
        }
        else if (g_popup == POPUP_HOTKEY_WARN)
        {
            if (down & KEY_A)               // use them anyway
            {
                SaveHotkey(g_hotkeyChosen);
                redrawTop = 2;
                redraw = 2;                 // the setting's new value
            }
            else if (down & KEY_B)          // choose others
            {
                OpenHotkey();
                redrawTop = 2;
            }
        }
        else if (g_popup != POPUP_NONE)
        {
            if (down & (KEY_A | KEY_B))
            {
                ClosePopup();
                redrawTop = 2;
                PlayLater(SOUND_BACK);
            }
        }
        else if (settings)
        {
            // The settings: Up/Down choose a line, A or a tap on it changes it. Theme opens the list
            // of themes, where A (or a tap) uses the highlighted one at once. B goes back out of the
            // themes, else closes the settings; X closes them.
            int count = Ui_MenuCount(), rows = g_theme->listHeight / g_theme->boxHeight;
            int was = settingItem, wasScroll = g_menuScroll;
            bool change = (down & KEY_A) != 0;
            if (dir & KEY_UP)
                settingItem--;
            if (dir & KEY_DOWN)
                settingItem++;
            if ((down & KEY_TOUCH) || ((held & KEY_TOUCH) && touchActive))
            {
                touchPosition touch;
                hidTouchRead(&touch);
                if (down & KEY_TOUCH)
                {
                    touchActive = true;
                    touchMoved = false;
                    touchY = touch.py;
                }
                if (abs(touch.py - touchY) > TAP_SLOP)
                    touchMoved = true;
            }
            if ((up & KEY_TOUCH) && touchActive)
            {
                touchActive = false;
                int listTop = g_theme->listTop, i = g_menuScroll + (touchY - listTop) / g_theme->boxHeight;
                if (!touchMoved && touchY >= listTop && touchY < listTop + rows * g_theme->boxHeight && i < count)
                    settingItem = i, change = true;
            }
            settingItem = Clamp(settingItem, 0, count - 1);
            if (settingItem != was)
                PlayLater(SOUND_TICK);
            else if (down & (KEY_UP | KEY_DOWN))
                PlayLater(SOUND_BUMP);          // already on the first or last line
            if (change && g_menu == MENU_SETTINGS && settingItem == SETTING_THEME)
            {
                g_menu = MENU_THEMES;
                settingItem = Theme_Current();
                g_menuScroll = 0;
                PlayLater(SOUND_SELECT);
            }
            else if (change && g_menu == MENU_SETTINGS && settingItem == SETTING_SOUNDS)
            {
                g_menu = MENU_SOUNDS;
                settingItem = SoundsLine();
                g_menuScroll = 0;
                PlayLater(SOUND_SELECT);
            }
            else if (change && g_menu == MENU_SOUNDS)
                UseSounds(settingItem);
            else if (change && g_menu == MENU_THEMES)
            {
                if (settingItem != Theme_Current() && !Theme_Use(settingItem))
                {
                    g_popup = POPUP_THEME_FAILED;
                    g_popupResult = 0;
                }
                else
                    PlayLater(SOUND_SELECT);
                g_theme->reset();
                if (game >= g_gameCount)
                {
                    // Past the games the positions differ between themes (empty ones, the store).
                    game = Clamp(game, 0, Ui_RowPositions() - 1);
                    heroFor = -1;
                    heroAt = osGetTime() + HERO_DELAY_MS;
                    g_count = 0;
                    g_selected = 0;
                    scroll = 0;
                }
            }
            else if (change)
                ChangeSetting(settingItem);
            if (down & KEY_X)
            {
                settings = false;
                touchActive = false;
                PlayLater(SOUND_BACK);
            }
            else if ((down & KEY_B) && g_menu != MENU_SETTINGS)
            {
                // Back to the settings, on the line the list came from.
                settingItem = g_menu == MENU_THEMES ? SETTING_THEME : SETTING_SOUNDS;
                g_menu = MENU_SETTINGS;
                g_menuScroll = 0;
                PlayLater(SOUND_BACK);
            }
            else if (down & KEY_B)
            {
                settings = false;
                touchActive = false;
                PlayLater(SOUND_BACK);
            }

            // The highlighted line stays shown (the list, or the theme drawing it, may have changed).
            count = Ui_MenuCount();
            rows = g_theme->listHeight / g_theme->boxHeight;
            if (settingItem < g_menuScroll)
                g_menuScroll = settingItem;
            if (settingItem >= g_menuScroll + rows)
                g_menuScroll = settingItem - rows + 1;
            g_menuScroll = Clamp(g_menuScroll, 0, count > rows ? count - rows : 0);
            if (change || settingItem != was || g_menuScroll != wasScroll || (down & (KEY_B | KEY_X)))
                redraw = 2;
        }
        else
        {
            if (down & KEY_X)
            {
                settings = true;
                settingItem = 0;
                g_menu = MENU_SETTINGS;
                g_menuScroll = 0;
                touchActive = false;
                redraw = 2;
                PlayLater(SOUND_SELECT);
            }

            // Share: only once the selected game's achievements have loaded.
            if ((down & KEY_Y) && game < g_gameCount && heroFor == game)
            {
                Sound_Play(SOUND_SELECT);       // now: the image takes a moment to make
                s_shareStatus[0] = '\0';
                Draw_Begin(true, true);
                DrawTopScreen(game, heroLevel);
                g_theme->drawShareBottom();     // not open yet: the image is being made
                Draw_End();
                sharing = Share_Open(&g_games[game], g_heroReady ? g_hero : NULL, Ui_Trophy(true));
                redraw = 2;
            }

            if (g_gameCount > 0 && g_picking)
            {
                // Choosing a game: Left/Right go along the games (Up/Down a row, if the theme shows
                // them in rows), the highlighted one's stats are shown, A opens it (or the store).
                // A theme that shows the games on the bottom screen says where they are
                // (Theme_PickAt), and the touch screen works like the HOME Menu's: touching a game
                // highlights it at once; touching the highlighted one and letting go on it opens it.
                int tapped = -1;
                bool tapOpens = false;
                if (down & KEY_TOUCH)
                {
                    touchPosition touch;
                    hidTouchRead(&touch);
                    int at = Theme_PickAt(game, touch.px, touch.py);
                    touchActive = at >= 0 && at == game;    // may open it when let go
                    touchMoved = false;
                    if (at >= 0 && at != game)
                        tapped = at;
                }
                else if ((held & KEY_TOUCH) && touchActive)
                {
                    touchPosition touch;
                    hidTouchRead(&touch);
                    if (Theme_PickAt(game, touch.px, touch.py) != game)
                        touchMoved = true;                  // slid off it
                }
                if ((up & KEY_TOUCH) && touchActive)
                {
                    touchActive = false;
                    tapOpens = !touchMoved;
                }
                if (((down & KEY_A) || tapOpens) && (game < g_gameCount || Ui_IsStore(game)))
                {
                    g_picking = false;
                    details = false;
                    bottomFade = 0;
                    touchActive = false;
                    redraw = 2;
                    const Game *g = game < g_gameCount ? &g_games[game] : NULL;
                    PlayLater(g != NULL && g->total > 0 && g->unlocked == g->total ? SOUND_COMPLETE : SOUND_SELECT);
                }
                int step = ((dir & KEY_RIGHT) ? 1 : 0) - ((dir & KEY_LEFT) ? 1 : 0);
                if (g_theme->pickColumns > 1)
                    step += (((dir & KEY_DOWN) ? 1 : 0) - ((dir & KEY_UP) ? 1 : 0)) * g_theme->pickColumns;
                int next = tapped >= 0 && g_picking ? tapped : Clamp(game + step, 0, Ui_RowPositions() - 1);
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
                    PlayLater(SOUND_MOVE);
                }
                else if (step != 0 && (down & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN)))
                    PlayLater(SOUND_BUMP);      // at the first or last (not while held: it would repeat)
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
                    PlayLater(SOUND_TICK);
                }
                else if (down & (KEY_UP | KEY_DOWN))
                    PlayLater(SOUND_BUMP);
                if (down & (KEY_B | KEY_TOUCH))
                {
                    details = false;
                    bottomFade = 0;
                    redrawBottom = 2;
                    PlayLater(SOUND_BACK);
                }
            }
            else if (g_gameCount > 0 && g_theme->gameList && (down & KEY_B))
            {
                // Back to choosing a game.
                g_picking = true;
                touchActive = false;
                bottomFade = 0;
                redraw = 2;
                PlayLater(SOUND_BACK);
            }
            else if (g_gameCount > 0)
            {
                // A shows the selected achievement in full (in the store: buys the selected item).
                if ((down & KEY_A) && g_count > 0 && heroFor == game)
                {
                    if (Ui_IsStore(game))
                    {
                        OpenBuy(g_selected);
                        redrawTop = 2;
                        if (g_popup == POPUP_NONE)
                            PlayLater(SOUND_BUMP);      // already bought
                    }
                    else
                    {
                        details = true;
                        bottomFade = 0;
                        touchActive = false;
                        redrawBottom = 2;
                        PlayLater(SOUND_SELECT);
                    }
                }

                int side = ((dir & KEY_RIGHT) ? 1 : 0) - ((dir & KEY_LEFT) ? 1 : 0);
                int next = Clamp(game + side, 0, Ui_RowPositions() - 1);
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
                    PlayLater(SOUND_MOVE);
                }
                else if (side != 0 && (down & (KEY_LEFT | KEY_RIGHT)))
                    PlayLater(SOUND_BUMP);

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
                    {
                        redrawBottom = 2;
                        PlayLater(SOUND_TICK);
                    }
                    else if (down & (KEY_UP | KEY_DOWN | KEY_L | KEY_R))
                        PlayLater(SOUND_BUMP);
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
                        if (Ui_IsStore(game))
                        {
                            OpenBuy(i);
                            redraw = 2;
                            if (g_popup == POPUP_NONE)
                                PlayLater(SOUND_BUMP);  // already bought
                        }
                        else
                        {
                            details = true;
                            bottomFade = 0;
                            redrawBottom = 2;
                            PlayLater(SOUND_SELECT);
                        }
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

        // This frame's sound: a popup that just came up has its own, else what was pressed.
        if (g_popup != shownPopup)
        {
            int popupSound = PopupSound();
            if (popupSound >= 0)
                s_sound = popupSound;
            shownPopup = g_popup;
        }
        if (s_sound >= 0)
        {
            Sound_Play((SoundId)s_sound);
            s_sound = -1;
        }

        // The old background finishes fading out before the next game is loaded over it.
        if (!loading && g_gameCount > 0 && heroFor != game && osGetTime() >= heroAt && heroLevel == 0)
        {
            if (game < g_gameCount)
            {
                StartLoading(game);
                loading = true;
            }
            else if (Ui_IsStore(game))
            {
                // The store: what's for sale (its Play Coins are read only while the app loads and
                // after it gives some: g_coins), and its own background (made on the spot, so not for
                // a theme that doesn't show it: that takes a while).
                heroFor = game;
                g_count = Store_Count();
                g_heroReady = !(Theme_Flags() & THEME_FLAG_NO_BACKGROUND);
                if (g_heroReady)
                {
                    Ui_StoreBackground(g_hero);
                    Draw_DimHero(g_hero, g_heroDim);
                }
                if (!g_picking)
                {
                    bottomFade = 0;
                    fadeHeld = true;
                }
                redraw = 2;
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
            if (heroLevel != target && (Theme_Flags() & THEME_FLAG_NO_BACKGROUND))
                heroLevel = target;         // the theme doesn't show it: nothing to fade, or redraw
            else if (heroLevel != target)
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
            SetTabs(settings, sharing, details, game, heroFor == game);
            Draw_Begin(topMode, bottomMode);
            if (sharing)
            {
                if (top)
                    g_theme->drawShareTop();
                if (bottom)
                    g_theme->drawShareBottom();
            }
            else if (settings)
            {
                if (top && g_theme->drawSettingsTop == NULL)
                    DrawTopScreen(game, heroLevel);
                else if (top)
                {
                    for (int eye = 0; eye < Draw_Eyes(); eye++)
                    {
                        Draw_Eye(eye);
                        g_theme->drawSettingsTop(settingItem);
                        if (g_popup != POPUP_NONE)
                            g_theme->drawPopup(false);
                    }
                    Draw_Eye(0);
                }
                if (bottom)
                    g_theme->drawSettingsBottom(settingItem);
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
                view.tabPressed = g_tabPressed, view.theme = g_theme;
                if (Ui_IsStore(game))
                    view.coins = g_coins, view.owned = Store_OwnedCount();
                bool listShown = game < g_gameCount || Ui_IsStore(game);
                bool sameView = keptValid && memcmp(&view, &keptView, sizeof(view)) == 0;
                if (bottom && listShown && (sameView || bottomFade < 256))
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
    Sound_Exit();
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
