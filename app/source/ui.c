// What the app shows (ui.h), and what every theme uses to draw it.
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "autostart.h"
#include "store_file.h"
#include "store_icon.h"
#include "theme.h"
#include "theme_api.h"
#include "trophy.h"
#include "update.h"

Game        g_games[MAX_GAMES];
int         g_gameCount;
bool        g_picking;

const char *g_names[MAX_ACHIEVEMENTS];
const char *g_descriptions[MAX_ACHIEVEMENTS];
uint16_t    g_points[MAX_ACHIEVEMENTS];
bool        g_unlocked[MAX_ACHIEVEMENTS];
bool        g_missable[MAX_ACHIEVEMENTS];
uint8_t     g_partsDone[MAX_ACHIEVEMENTS];
uint8_t     g_partsTotal[MAX_ACHIEVEMENTS];
uint16_t    g_partFirst[MAX_ACHIEVEMENTS];
const char *g_partNames[MAX_ACHIEVEMENTS];
bool        g_partDone[MAX_ACHIEVEMENTS];
int         g_count;
int         g_selected;
u16         g_gameUnlocked[ICON_SIZE * ICON_SIZE], g_gameLocked[ICON_SIZE * ICON_SIZE];
bool        g_hasGameIcons;
int         g_recent[RECENT_MAX];
int         g_recentCount;

u8          g_hero[HERO_BYTES];
u8          g_heroDim[HERO_BYTES] __attribute__((aligned(4)));
bool        g_heroReady;

Game        g_storeTile;
int         g_coins = -1;
Rewards     g_rewards;
int         g_buyItem;
int         g_paidOwed;

bool        g_trackerRunning;
bool        g_patchedAllowed;
bool        g_autostart;
bool        g_autostartUsable;

Popup       g_popup;
Result      g_popupResult;
int         g_newGames, g_updatedGames;
int         g_storeFiles;
int         g_downloadDone, g_downloadTotal;
const char *g_appVersion;
bool        g_appChanged;
bool        g_trackerNotRestarted;
bool        g_canRestart;

int         g_menu;
int         g_menuScroll;

UiTab       g_tabs[2];
int         g_tabPressed = -1;

static void Line(PopupText *t, const char *text)
{
    if (t->lineCount < POPUP_LINES)
        t->lines[t->lineCount++] = text;
}

void Ui_PopupText(PopupText *t)
{
    memset(t, 0, sizeof(*t));
    t->buttonA = "OK";

    // Short notices, centred.
    switch (g_popup)
    {
        case POPUP_UPDATE:
            t->title = g_newGames + g_updatedGames > 0 ? "New achievements" : "Store update";
            if (g_newGames > 0 && g_updatedGames > 0)
                snprintf(t->lineText, sizeof(t->lineText), "%d new game%s, %d updated", g_newGames,
                         g_newGames == 1 ? "" : "s", g_updatedGames);
            else if (g_newGames > 0)
                snprintf(t->lineText, sizeof(t->lineText), "%d new game%s", g_newGames, g_newGames == 1 ? "" : "s");
            else if (g_updatedGames > 0)
                snprintf(t->lineText, sizeof(t->lineText), "%d game%s updated", g_updatedGames, g_updatedGames == 1 ? "" : "s");
            else
                snprintf(t->lineText, sizeof(t->lineText), "New or updated things to buy");
            Line(t, t->lineText);
            if (g_storeFiles > 0 && g_newGames + g_updatedGames > 0)
                Line(t, "and a store update");
            t->buttonA = "Download", t->buttonB = "Later";
            break;
        case POPUP_DOWNLOADING:
            t->title = "Downloading";
            snprintf(t->lineText, sizeof(t->lineText), "File %d of %d", g_downloadDone + 1, g_downloadTotal);
            Line(t, t->lineText);
            t->progress = true, t->done = g_downloadDone, t->total = g_downloadTotal;
            t->buttonA = NULL;
            break;
        case POPUP_APP_UPDATE:
            t->title = "App update";
            snprintf(t->lineText, sizeof(t->lineText), "Version %s is out. You have %s.",
                     g_appVersion != NULL ? g_appVersion : "?", Update_AppVersion());
            Line(t, t->lineText);
            Line(t, "It updates the app and the tracker.");
            t->buttonA = "Update", t->buttonB = "Later";
            break;
        case POPUP_APP_DOWNLOADING:
            t->title = "Updating";
            if (g_downloadTotal > 0)
                snprintf(t->lineText, sizeof(t->lineText), "Downloading: %d of %d KB", g_downloadDone, g_downloadTotal);
            else
                snprintf(t->lineText, sizeof(t->lineText), "Installing...");
            Line(t, t->lineText);
            t->progress = true;
            t->done = g_downloadTotal > 0 ? g_downloadDone : 1;
            t->total = g_downloadTotal > 0 ? g_downloadTotal : 1;
            t->buttonA = NULL;
            break;
        case POPUP_APP_UPDATED:
            // Nothing to choose: it restarts by itself. Without Luma's 3dsx loader a .3dsx can't
            // open itself again: it only closes.
            snprintf(t->titleText, sizeof(t->titleText), "Updated to %s", g_appVersion != NULL ? g_appVersion : "?");
            t->title = t->titleText;
            t->emphasizeFirst = true;
            Line(t, g_canRestart ? "The app will restart now." : "The app will close now.");
            if (!g_canRestart)
                Line(t, "Open it again to finish updating.");
            if (g_trackerNotRestarted)
                Line(t, "The tracker updates when you restart your 3DS.");
            t->buttonA = NULL;
            break;
        case POPUP_COINS:
        {
            const Rewards *r = &g_rewards;
            t->title = "Play Coins earned";
            t->emphasizeFirst = true;
            if (r->earned > 0)
                snprintf(t->lineText, sizeof(t->lineText), "%d achievement points: %d Play Coins",
                         r->rewards * REWARD_POINTS, r->earned);
            else
                snprintf(t->lineText, sizeof(t->lineText), "%d Play Coins kept from before", r->given);
            Line(t, t->lineText);
            if (R_FAILED(r->result))
                snprintf(t->lineText2, sizeof(t->lineText2), "Couldn't add them yet (0x%08lX). They're kept.", r->result);
            else if (r->owed > 0)
            {
                // At the limit: say what the 3DS has, so it doesn't read as if we gave it all.
                Line(t, "Your 3DS has " PLAY_COINS_TEXT " Play Coins, the most it holds.");
                snprintf(t->lineText2, sizeof(t->lineText2), "%d more are saved until you spend some.", r->owed);
            }
            else
                snprintf(t->lineText2, sizeof(t->lineText2), "You now have %d Play Coins.", r->coins);
            Line(t, t->lineText2);
            break;
        }
        case POPUP_BUY:
        case POPUP_BOUGHT:
        case POPUP_NOT_ENOUGH:
        {
            const StoreItem *item = Store_Item(g_buyItem);
            int price = item != NULL ? item->price : 0;
            if (g_popup == POPUP_NOT_ENOUGH)
            {
                t->title = "Not enough Play Coins";
                snprintf(t->lineText, sizeof(t->lineText), "It costs %d Play Coins, and you have %d.", price, g_coins);
                snprintf(t->lineText2, sizeof(t->lineText2), "Achievements earn %d for every %d points.", REWARD_COINS,
                         REWARD_POINTS);
            }
            else
            {
                Ui_EllipsizeScaled(t->titleText, sizeof(t->titleText), item != NULL ? item->name : "?", 340, 2);
                t->title = t->titleText;
                if (g_popup == POPUP_BUY)
                {
                    snprintf(t->lineText, sizeof(t->lineText), "Buy it for %d Play Coins?", price);
                    snprintf(t->lineText2, sizeof(t->lineText2), "You have %d, and will have %d left.", g_coins,
                             g_coins - price);
                    t->buttonA = "Buy", t->buttonB = "Cancel";
                }
                else
                {
                    t->emphasizeFirst = true;
                    snprintf(t->lineText, sizeof(t->lineText), "Bought for %d Play Coins.", price);
                    if (g_paidOwed > 0)
                        snprintf(t->lineText2, sizeof(t->lineText2), "You have %d left, with %d kept from before.",
                                 g_coins, g_paidOwed);
                    else
                        snprintf(t->lineText2, sizeof(t->lineText2), "You have %d Play Coins left.", g_coins);
                }
            }
            Line(t, t->lineText);
            Line(t, t->lineText2);
            if (g_popup == POPUP_BOUGHT && item != NULL && item->theme != NULL)
                Line(t, "Choose it in the settings (X).");
            break;
        }
        case POPUP_AUTOSTART_ON:
            t->title = "Starts with your 3DS";
            t->emphasizeFirst = true;
            Line(t, "The tracker now starts when you turn on your 3DS.");
            Line(t, "If your 3DS ever doesn't get to the HOME Menu,");
            Line(t, "turn it on again: this setting turns itself off.");
            break;
        case POPUP_AUTOSTART_CONFIRM:
            // Game patching applies every mod in /luma/titles, not just ours: say what that risks.
            t->title = "Before you turn it on";
            t->emphasizeFirst = true;
            // Three lines: a fourth makes the popup reach over the text behind it in Modern and 3DS.
            Line(t, "Luma's \"Enable game patching\" has to be on.");
            Line(t, "It also applies any mods in /luma/titles, so with");
            Line(t, "Patched games on, a mod could unlock achievements.");
            t->buttonA = "Turn on", t->buttonB = "Cancel";
            break;
        case POPUP_AUTOSTART_OFF:
            t->title = "Start with the console is off";
            t->emphasizeFirst = true;
            if (g_popupResult == AUTOSTART_UPDATED)
            {
                Line(t, "A system update changed the module it used.");
                Line(t, "Turn it on again in the settings (X) to set it");
                Line(t, "up for the new version.");
            }
            else
            {
                Line(t, "Your 3DS didn't get to the HOME Menu last time.");
                Line(t, "The tracker started with it, so this was turned");
                Line(t, "off. Turn it on again in the settings (X).");
            }
            break;
        default:
            break;
    }
    if (t->title != NULL)
    {
        t->centred = true;
        return;
    }

    // Problems: the error code, then what to do.
    t->problem = true;
    t->showCode = true;
    t->code = g_popupResult;
    t->emphasizeFirst = true;
    switch (g_popup)
    {
        case POPUP_START_FAILED:
            t->title = "Couldn't start the tracker";
            Line(t, "Check that:");
            Line(t, "- /luma/sysmodules/0004013000AC4102.cxi is on the SD");
            Line(t, "- Luma's config has \"Enable loading external FIRMs");
            Line(t, "  and modules\" on");
            break;
        case POPUP_STOP_FAILED:
            t->title = "Couldn't stop the tracker";
            break;
        case POPUP_THEME_FAILED:
            t->title = "Couldn't load a theme";
            t->showCode = false;
            Line(t, Theme_Error());
            Line(t, "It's skipped until the app is opened again.");
            Line(t, "/achievements/theme.log says more.");
            break;
        case POPUP_BUY_FAILED:
            t->title = "Couldn't buy it";
            Line(t, "No Play Coins were spent.");
            if (g_popupResult == STORE_FILE_MISSING)
            {
                Line(t, "Its file hasn't downloaded yet: connect to");
                Line(t, "the internet and open the app again.");
            }
            else if (g_popupResult == STORE_FILE_DAMAGED)
            {
                Line(t, "Its file in /achievements/store is damaged:");
                Line(t, "open the app online to download it again.");
            }
            else if (g_popupResult == STORE_NO_KEY)
            {
                Line(t, "This copy of the app can't unlock store");
                Line(t, "items: get the app from its GitHub releases.");
            }
            else if (g_popupResult == STORE_FILE_WRITE)
                Line(t, "What it installs couldn't be written to the SD.");
            else
            {
                Line(t, "Your Play Coins or /achievements/purchases.txt");
                Line(t, "couldn't be read or written.");
            }
            break;
        case POPUP_APP_FAILED:
            t->title = "Update failed";
            Line(t, g_appChanged ? "Only part of the update was installed." : "Nothing was changed.");
            Line(t, "Check your internet connection and try again.");
            Line(t, "If you leave it, it's offered again next time");
            Line(t, "you open the app.");
            t->buttonA = "Retry", t->buttonB = "Later";
            break;
        case POPUP_AUTOSTART_FAILED:
            // g_autostart: still on, so it was turning it off that failed.
            t->title = g_autostart ? "Couldn't turn it off" : "Couldn't turn it on";
            if (g_autostart)
            {
                Line(t, "Its file in /luma/titles couldn't be removed.");
                Line(t, "/achievements/autostart.log says which.");
            }
            else if (g_popupResult == AUTOSTART_OLD_LUMA)
                Line(t, "It needs Luma3DS 13.0 or newer.");
            else if (g_popupResult == AUTOSTART_NO_MODULES || g_popupResult == AUTOSTART_NO_PATCHING)
            {
                Line(t, g_popupResult == AUTOSTART_NO_PATCHING ? "Turn on \"Enable game patching\" in Luma's settings"
                                                               : "Turn on \"Enable loading external FIRMs and modules\"");
                Line(t, g_popupResult == AUTOSTART_NO_PATCHING ? "(hold SELECT while turning on your 3DS)," :
                                                                 "in Luma's settings (hold SELECT while turning on");
                Line(t, g_popupResult == AUTOSTART_NO_PATCHING ? "then try again." : "your 3DS), then try again.");
            }
            else if (g_popupResult == AUTOSTART_NO_TRACKER)
                Line(t, "/luma/sysmodules/0004013000AC4102.cxi is missing.");
            else
            {
                Line(t, g_popupResult == AUTOSTART_NO_HOST ? "No system module can start the tracker safely."
                        : g_popupResult == AUTOSTART_NOT_USED ? "Luma didn't use the file, so it was removed."
                        : "Luma's loader or the SD card had a problem.");
                Line(t, "Nothing was changed.");
                Line(t, "/achievements/autostart.log says more.");
            }
            break;
        default:        // POPUP_UPDATE_FAILED
            t->title = "Download failed";
            Line(t, "Your achievements and progress are unchanged.");
            Line(t, "It will offer the download again next time you open");
            Line(t, "the app. Check your internet connection.");
            break;
    }
}

int Ui_StorePosition(void)
{
    int last = g_theme->minPositions - 1;
    return g_gameCount > last ? g_gameCount : last;
}

int Ui_RowPositions(void)
{
    return g_gameCount > 0 ? Ui_StorePosition() + 1 : g_theme->minPositions;
}

bool Ui_IsStore(int position)
{
    return g_gameCount > 0 && position == Ui_StorePosition();
}

void Ui_StoreInit(void)
{
    memcpy(g_storeTile.icon, STORE_TILE, sizeof(g_storeTile.icon));
    g_storeTile.hasIcon = true;
    snprintf(g_storeTile.title, sizeof(g_storeTile.title), "Store");
    Store_Load();
    Store_Restore();
}

const u16 *Ui_StoreIcon(int i)
{
    const StoreItem *item = Store_Item(i);
    return item != NULL && item->icon != NULL ? item->icon : STORE_ITEM;
}

void Ui_StoreBackground(u8 *pixels)
{
    for (int x = 0; x < HERO_WIDTH; x++)
        for (int row = 0; row < HERO_HEIGHT; row++, pixels += 3)     // row 0 is the bottom of the screen
        {
            float dx = (x - HERO_WIDTH / 2) / 230.0f, dy = (HERO_HEIGHT - 1 - row - 120) / 120.0f;
            float glow = 1.0f / (1.0f + 4.0f * (dx * dx + 2.0f * dy * dy));
            pixels[0] = (u8)(10 + 70 * glow);       // BGR
            pixels[1] = (u8)(18 + 170 * glow);
            pixels[2] = (u8)(26 + 229 * glow);
        }
}

const u16 *Ui_Trophy(bool unlocked)
{
    if (g_hasGameIcons)
        return unlocked ? g_gameUnlocked : g_gameLocked;
    return unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED;
}

void Ui_EllipsizeScaled(char *out, size_t capacity, const char *text, int maxWidth, int scale)
{
    int length = (int)strlen(text);
    if (length < (int)capacity && Draw_TextWidth(text, scale) <= maxWidth)
    {
        memcpy(out, text, length + 1);
        return;
    }
    int keep = Draw_TextFit(text, scale, maxWidth - Draw_TextWidth("...", scale));
    if (keep > (int)capacity - 4)
        keep = (int)capacity - 4;
    while (keep > 0 && text[keep - 1] == ' ')
        keep--;                     // no gap before the dots
    memcpy(out, text, keep);
    memcpy(out + keep, "...", 4);
}

void Ui_Ellipsize(char *out, size_t capacity, const char *text, int maxWidth)
{
    Ui_EllipsizeScaled(out, capacity, text, maxWidth, 1);
}

int Ui_DrawWrappedScaled(gfxScreen_t screen, int x, int y, int width, int lineHeight, int scale,
                         Color color, const char *text, int maxLines)
{
    char line[160];

    for (int n = 0; *text != '\0' && n < maxLines; n++)
    {
        while (*text == ' ')
            text++;
        int length = (int)strlen(text);
        int fit = Draw_TextFit(text, scale, width);
        if (fit > (int)sizeof(line) - 1)
            fit = (int)sizeof(line) - 1;
        int take = fit;
        if (fit < length)
        {
            while (take > 0 && text[take] != ' ')
                take--;             // back to the end of a word
            if (take == 0)
                take = fit;         // one long word
        }
        if (take == 0)
            break;                  // not even one character fits
        if (n == maxLines - 1 && take < length)
            Ui_EllipsizeScaled(line, sizeof(line), text, width, scale);
        else
        {
            memcpy(line, text, take);
            line[take] = '\0';
        }
        Draw_Text(screen, x, y, scale, color, line, 0);
        y += lineHeight;
        text += take;
    }
    return y;
}

int Ui_DrawWrapped(gfxScreen_t screen, int x, int y, int width, int lineHeight, Color color,
                   const char *text, int maxLines)
{
    return Ui_DrawWrappedScaled(screen, x, y, width, lineHeight, 1, color, text, maxLines);
}

void Ui_Totals(UiTotals *t)
{
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < g_gameCount; i++)
    {
        const Game *g = &g_games[i];
        t->unlocked += g->unlocked, t->total += g->total;
        t->earned += g->earned, t->points += g->points;
        if (g->total > 0 && g->unlocked == g->total)
            t->complete++;
    }
}

int Ui_MenuCount(void)
{
    return g_menu == MENU_THEMES ? Theme_Count() : SETTING_COUNT;
}

const char *Ui_MenuTitle(void)
{
    return g_menu == MENU_THEMES ? "Theme" : "Settings";
}

void Ui_Setting(int i, UiSetting *s)
{
    if (g_menu == MENU_THEMES)
    {
        // A theme: the one in use says so.
        const ThemeInfo *info = Theme_Info(i);
        s->name = info->name;
        s->on = true;
        s->value = i == Theme_Current() ? "In use" : "";
        s->description = info->description[0] != '\0' ? info->description :
                         Theme_IsAddOn(i) ? "An add-on theme." : "Comes with the app.";
        return;
    }
    switch (i)
    {
        case SETTING_TRACKER:
            s->name = "Tracker";
            s->on = g_trackerRunning;
            s->description = "Watches your games for achievements. Turned off, it stays off until you turn it on again.";
            break;
        case SETTING_AUTOSTART:
            s->name = "Start with the console";
            s->on = g_autostart;
            s->description = g_autostart && !g_autostartUsable ?
                "On, but Luma's \"Enable game patching\" is off: the tracker doesn't start with your 3DS." :
                "Starts the tracker with your 3DS. Needs Luma's game patching, which also applies game mods.";
            break;
        case SETTING_PATCHED:
            s->name = "Patched games";
            s->on = g_patchedAllowed;
            s->description = "Lets games patched through Luma (/luma/titles) earn achievements. Off: they aren't tracked.";
            break;
        default:
        {
            static char text[96];
            s->name = "Theme";
            s->on = true;
            s->value = g_theme->name;
            snprintf(text, sizeof(text), "How the app looks: %d themes to choose from. A shows them.", Theme_Count());
            s->description = text;
            return;
        }
    }
    s->value = s->on ? "On" : "Off";
}

int Ui_Approach(int value, int target, int step)
{
    if (value < target)
        return value + step < target ? value + step : target;
    return value - step > target ? value - step : target;
}

int Ui_Battery(bool *charging)
{
    static bool ptmu;               // ptm:u stays open until the app closes
    static int  level = -1;
    static bool charge;
    static u64  at;
    u64 now = osGetTime();

    if (level < 0 || now - at >= 10000)
    {
        u8 read = 0, state = 0;
        if (!ptmu)
            ptmu = R_SUCCEEDED(ptmuInit());
        if (ptmu && R_SUCCEEDED(PTMU_GetBatteryLevel(&read)))
        {
            PTMU_GetBatteryChargeState(&state);
            level = read > 5 ? 5 : read;
            charge = state != 0;
        }
        at = now;
    }
    if (charging != NULL)
        *charging = charge;
    return level;
}
