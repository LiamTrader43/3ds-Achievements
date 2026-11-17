// What the app shows (ui.h), and what every theme uses to draw it.
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "theme.h"
#include "trophy.h"
#include "update.h"

Game        g_games[MAX_GAMES];
int         g_gameCount;
bool        g_picking;

const char *g_names[MAX_ACHIEVEMENTS];
const char *g_descriptions[MAX_ACHIEVEMENTS];
uint16_t    g_points[MAX_ACHIEVEMENTS];
bool        g_unlocked[MAX_ACHIEVEMENTS];
int         g_count;
int         g_selected;
u16         g_gameUnlocked[ICON_SIZE * ICON_SIZE], g_gameLocked[ICON_SIZE * ICON_SIZE];
bool        g_hasGameIcons;
int         g_recent[RECENT_MAX];
int         g_recentCount;

u8          g_hero[HERO_BYTES];
u8          g_heroDim[HERO_BYTES] __attribute__((aligned(4)));
bool        g_heroReady;

bool        g_trackerRunning;
bool        g_patchedAllowed;

Popup       g_popup;
Result      g_popupResult;
int         g_newGames, g_updatedGames;
int         g_downloadDone, g_downloadTotal;
const char *g_appVersion;
bool        g_appChanged;
bool        g_trackerNotRestarted;
bool        g_canRestart;

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
            t->title = "New achievements";
            if (g_newGames > 0 && g_updatedGames > 0)
                snprintf(t->lineText, sizeof(t->lineText), "%d new game%s, %d updated", g_newGames,
                         g_newGames == 1 ? "" : "s", g_updatedGames);
            else if (g_newGames > 0)
                snprintf(t->lineText, sizeof(t->lineText), "%d new game%s", g_newGames, g_newGames == 1 ? "" : "s");
            else
                snprintf(t->lineText, sizeof(t->lineText), "%d game%s updated", g_updatedGames, g_updatedGames == 1 ? "" : "s");
            Line(t, t->lineText);
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
        case POPUP_APP_FAILED:
            t->title = "Update failed";
            Line(t, g_appChanged ? "Only part of the update was installed." : "Nothing was changed.");
            Line(t, "Check your internet connection and try again.");
            Line(t, "If you leave it, it's offered again next time");
            Line(t, "you open the app.");
            t->buttonA = "Retry", t->buttonB = "Later";
            break;
        default:        // POPUP_UPDATE_FAILED
            t->title = "Download failed";
            Line(t, "Your achievements and progress are unchanged.");
            Line(t, "It will offer the download again next time you open");
            Line(t, "the app. Check your internet connection.");
            break;
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

void Ui_Setting(int i, UiSetting *s)
{
    switch (i)
    {
        case SETTING_TRACKER:
            s->name = "Tracker";
            s->on = g_trackerRunning;
            s->description = "Watches your games for achievements. Turned off, it stays off until you turn it on again.";
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
            snprintf(text, sizeof(text), "How the app looks: %d themes to choose from. A switches to the next one.",
                     Theme_Count());
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
