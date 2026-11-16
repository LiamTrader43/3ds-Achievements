// The "3ds" theme: like the 3DS's own System Settings. Cream screens. Top: a status bar
// (tracker, clock, battery), an orange header, and the selected game in a sunken panel between
// dotted lines, with yellow arrow tabs at the sides when there are more games. Bottom: the
// achievements as white buttons in a sunken panel, grey tabs along the bottom edge. The
// colours were measured from screenshots of System Settings.
#include "theme.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "update.h"

static const Color CREAM = { 241, 233, 163 };           // the background
static const Color PANEL = { 217, 211, 159 };           // sunken panels
static const Color DOTS = { 205, 196, 132 };            // the dotted lines
static const Color TITLE = { 99, 89, 63 };              // headings
static const Color TEXT = { 92, 85, 68 };
static const Color SOFT = { 138, 130, 104 };            // descriptions, locked achievements
static const Color MUTED = { 170, 163, 105 };           // labels along the bottom
static const Color ORANGE = { 232, 140, 20 };           // the header, progress
static const Color BADGE = { 230, 150, 20 };            // points of an unlocked achievement
static const Color BADGE_OFF = { 190, 184, 152 };       // and a locked one
static const Color WHITE = { 255, 255, 255 };
static const Color BUTTON_TOP = { 255, 253, 246 };      // a button: white fading to cream,
static const Color BUTTON_BOTTOM = { 220, 215, 193 };
static const Color BUTTON_EDGE = { 160, 152, 108 };     // a darker line under it,
static const Color BUTTON_SHADOW = { 199, 192, 141 };   // and a soft shadow under that
static const Color TAB_TOP = { 152, 152, 152 };         // the grey tabs along the bottom
static const Color TAB_BOTTOM = { 100, 100, 100 };
static const Color GLYPH = { 110, 104, 86 };            // a button's name in a dark circle or pill
static const Color GLYPH_TEXT = { 250, 248, 236 };
static const Color CURSOR = { 50, 175, 235 };           // around the selected achievement
static const Color TRACK = { 196, 188, 132 };           // an empty progress bar
static const Color ARROW = { 248, 198, 48 };            // the arrow tabs at the top screen's sides
static const Color ARROW_EDGE = { 214, 160, 20 };
static const Color PILL = { 250, 249, 240 };            // the status bar's tracker pill
static const Color PILL_EDGE = { 200, 193, 150 };
static const Color ON = { 80, 190, 60 };
static const Color OFF = { 170, 165, 140 };
static const Color BATTERY = { 30, 150, 225 };
static const Color BATTERY_EDGE = { 110, 105, 90 };
static const Color DIALOG_TOP = { 255, 254, 248 };      // a popup
static const Color DIALOG_BOTTOM = { 246, 242, 226 };
static const Color DIALOG_EDGE = { 190, 182, 130 };

#define CREAM_RGB       { 241, 233, 163 }

// Top screen.
#define STATUS_Y        1
#define HEADER_Y        21
#define DOTS1_Y         40
#define TOP_PANEL_X     8
#define TOP_PANEL_Y     46
#define TOP_PANEL_W     384
#define TOP_PANEL_H     140
#define BIG_ICON        96
#define BIG_ICON_X      (TOP_PANEL_X + 16)
#define BIG_ICON_Y      (TOP_PANEL_Y + (TOP_PANEL_H - BIG_ICON) / 2)
#define INFO_X          (BIG_ICON_X + BIG_ICON + 18)
#define INFO_W          (TOP_PANEL_X + TOP_PANEL_W - 14 - INFO_X)
#define DOTS2_Y         192
#define LEGEND_Y        202
#define DEPTH_POPUP     8

// Bottom screen.
#define LIST_TITLE_Y    4
#define LIST_PANEL_X    6
#define LIST_PANEL_Y    22
#define LIST_PANEL_W    (BOTTOM_WIDTH - 2 * LIST_PANEL_X)
#define LIST_PANEL_H    186
#define ITEM_X          12
#define ITEM_W          (BOTTOM_WIDTH - 2 * ITEM_X)
#define ITEM_H          40
#define ITEM_PITCH      44
#define ITEM_RADIUS     9
#define LIST_TOP        27                          // the first item, unscrolled
#define LIST_ROWS       4
#define ITEM_ICON       32
#define ITEM_TEXT_X     (ITEM_X + 8 + ITEM_ICON + 8)
#define BAR_Y           212                         // the tabs along the bottom
#define TAB_W           106

// ---- Pieces

// Small dots in a row across the top screen, like System Settings' dotted lines.
static void DrawDots(int y)
{
    for (int x = 12; x < TOP_WIDTH - 12; x += 5)
        Draw_Rect(GFX_TOP, x, y, 2, 2, DOTS);
}

// A button: white fading to cream, a darker line and a soft shadow under it.
static void DrawButton(gfxScreen_t screen, int x, int y, int w, int h, int radius)
{
    Draw_RoundRect(screen, x, y + 3, w, h, radius, BUTTON_SHADOW);
    Draw_RoundRect(screen, x, y + 1, w, h, radius, BUTTON_EDGE);
    Draw_RoundRectGradient(screen, x, y, w, h, radius, BUTTON_TOP, BUTTON_BOTTOM);
}

// A button's name ("A", "SELECT") as the 3DS shows it: white in a dark circle, or a pill if
// it's longer. Returns its width.
static int DrawGlyph(gfxScreen_t screen, int x, int y, const char *name, Color fill, Color text)
{
    int tw = Draw_TextWidth(name, 1), w = tw + 8 > 15 ? tw + 8 : 15;
    Draw_RoundRect(screen, x, y, w, 15, 7, fill);
    Draw_Text(screen, x + (w - tw) / 2, y + 1, 1, text, name, 0);
    return w;
}

// A small rounded label in a colour (points, Unlocked).
static int DrawBadge(gfxScreen_t screen, int x, int y, const char *text, Color fill)
{
    int w = Draw_TextWidth(text, 1) + 10;
    Draw_RoundRect(screen, x, y, w, 15, 3, fill);
    Draw_Text(screen, x + 5, y + 1, 1, WHITE, text, 0);
    return w;
}

static void DrawProgress(gfxScreen_t screen, int x, int y, int w, int h, int done, int total)
{
    Draw_RoundRect(screen, x, y, w, h, h / 2, TRACK);
    if (total > 0 && done > 0)
    {
        int fill = w * done / total;
        Draw_RoundRect(screen, x, y, fill > h ? fill : h, h, h / 2, ORANGE);
    }
}

// A triangle pointing left (dir < 0) or right, centred on (cx, cy), size pixels from tip to base.
static void DrawArrow(gfxScreen_t screen, int cx, int cy, int size, int dir, Color color)
{
    for (int dy = -size; dy <= size; dy++)
    {
        int w = size - (dy < 0 ? -dy : dy) + 1;
        int x = dir < 0 ? cx - size / 2 : cx + size / 2 - w + 1;
        Draw_Rect(screen, x, cy + dy, w, 1, color);
    }
}

// "5/97" as System Settings shows a page: the first number large, both in the muted colour,
// right-aligned to right.
static void DrawPage(gfxScreen_t screen, int right, int y, int n, int count)
{
    char big[12], small[12];
    snprintf(big, sizeof(big), "%d", n);
    snprintf(small, sizeof(small), "/%d", count);
    int ws = Draw_TextWidth(small, 1), wb = Draw_TextWidth(big, 2);
    Draw_Text(screen, right - ws - wb, y, 2, MUTED, big, 0);
    Draw_Text(screen, right - ws, y + 10, 1, MUTED, small, 0);
}

// The grey tab at the bottom-left corner: a button's name and what it does there.
static void DrawTab(const char *button, const char *label)
{
    Draw_RoundRectGradient(GFX_BOTTOM, -14, BAR_Y, TAB_W + 14, SCREEN_HEIGHT - BAR_Y + 14, 14, TAB_TOP, TAB_BOTTOM);
    int x = 10 + DrawGlyph(GFX_BOTTOM, 10, BAR_Y + 7, button, WHITE, TAB_BOTTOM);
    Draw_Text(GFX_BOTTOM, x + 6, BAR_Y + 8, 1, WHITE, label, 0);
}

static void CentredText(gfxScreen_t screen, int y, int scale, Color color, const char *text)
{
    Draw_Text(screen, (Draw_Width(screen) - Draw_TextWidth(text, scale)) / 2, y, scale, color, text, 0);
}

// ---- Status bar: the tracker, the date and time, the battery

static int s_battery = -1;      // 0-5, or -1 before it's read
static bool s_charging;
static u64 s_batteryAt;         // when it was read
static time_t s_clockMinute;    // the minute the clock last showed

static void ReadBattery(void)
{
    static bool ptmu;           // ptm:u stays open until the app closes
    u64 now = osGetTime();
    if (s_battery >= 0 && now - s_batteryAt < 10000)
        return;
    if (!ptmu)
        ptmu = R_SUCCEEDED(ptmuInit());
    u8 level = 0, charging = 0;
    if (ptmu && R_SUCCEEDED(PTMU_GetBatteryLevel(&level)))
    {
        PTMU_GetBatteryChargeState(&charging);
        s_battery = level > 5 ? 5 : level;
        s_charging = charging != 0;
    }
    s_batteryAt = now;
}

static void DrawStatusBar(void)
{
    // The tracker, like the 3DS's wireless pill.
    const char *tracker = g_trackerRunning ? "Tracker on" : "Tracker off";
    int pw = Draw_TextWidth(tracker, 1) + 22;
    Draw_RoundRect(GFX_TOP, 3, STATUS_Y, pw + 2, 17, 8, PILL_EDGE);
    Draw_RoundRect(GFX_TOP, 4, STATUS_Y + 1, pw, 15, 7, PILL);
    Draw_RoundRect(GFX_TOP, 9, STATUS_Y + 5, 7, 7, 3, g_trackerRunning ? ON : OFF);
    Draw_Text(GFX_TOP, 20, STATUS_Y + 2, 1, TEXT, tracker, 0);

    // Battery: an outline filled in fifths, green while charging.
    ReadBattery();
    const int bx = TOP_WIDTH - 26, by = STATUS_Y + 3;
    Draw_RoundRect(GFX_TOP, bx, by, 20, 11, 2, BATTERY_EDGE);
    Draw_Rect(GFX_TOP, bx + 20, by + 3, 2, 5, BATTERY_EDGE);
    Draw_Rect(GFX_TOP, bx + 1, by + 1, 18, 9, CREAM);
    if (s_battery > 0)
        Draw_Rect(GFX_TOP, bx + 2, by + 2, 16 * s_battery / 5, 7, s_charging ? ON : BATTERY);

    // Date and time, as the 3DS writes them.
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);    // the 3DS's clock is already local time
    char clock[32];
    strftime(clock, sizeof(clock), "%m/%d (%a) %H:%M", t);
    Draw_Text(GFX_TOP, bx - 6 - Draw_TextWidth(clock, 1), STATUS_Y + 2, 1, TEXT, clock, 0);
    s_clockMinute = now / 60;
}

// The clock moved on a minute, or the battery changed.
static bool TopChanged(void)
{
    int battery = s_battery;
    bool charging = s_charging;
    ReadBattery();
    return time(NULL) / 60 != s_clockMinute || battery != s_battery || charging != s_charging;
}

// ---- Top screen

// The arrow tab at the left (dir < 0) or right edge: there's another game that way.
static void DrawSideArrow(int dir)
{
    const int cy = TOP_PANEL_Y + TOP_PANEL_H / 2, r = 16;
    int cx = dir < 0 ? -3 : TOP_WIDTH + 2;
    Draw_RoundRect(GFX_TOP, cx - r - 1, cy - r - 1, 2 * r + 2, 2 * r + 2, r + 1, ARROW_EDGE);
    Draw_RoundRect(GFX_TOP, cx - r, cy - r, 2 * r, 2 * r, r, ARROW);
    DrawArrow(GFX_TOP, dir < 0 ? cx + 7 : cx - 8, cy, 5, dir, WHITE);
}

static void DrawGame(const Game *g, int selected)
{
    char text[64];

    if (g->hasIcon)
    {
        Draw_Shadow(GFX_TOP, BIG_ICON_X - 2, BIG_ICON_Y - 2, BIG_ICON + 4, BIG_ICON + 4, 14, 6, 3, 90, 0);
        Draw_IconScaled(GFX_TOP, BIG_ICON_X, BIG_ICON_Y, BIG_ICON, 12, g->icon, 0, &WHITE, 0);
    }
    else
    {
        Draw_RoundRect(GFX_TOP, BIG_ICON_X, BIG_ICON_Y, BIG_ICON, BIG_ICON, 12, TRACK);
        Ui_DrawWrapped(GFX_TOP, BIG_ICON_X + 8, BIG_ICON_Y + 8, BIG_ICON - 16, LINE_H + 4, TEXT, g->title, 4);
    }

    // Its name, large if it fits.
    int y = TOP_PANEL_Y + 18;
    if (Draw_TextWidth(g->title, 2) <= INFO_W)
    {
        Draw_Text(GFX_TOP, INFO_X, y, 2, TITLE, g->title, 0);
        y += 36;
    }
    else
        y = Ui_DrawWrapped(GFX_TOP, INFO_X, y + 2, INFO_W, LINE_H + 4, TITLE, g->title, 2) + 10;

    if (g->total > 0 && g->unlocked == g->total)
        snprintf(text, sizeof(text), "All %d achievements unlocked", g->total);
    else
        snprintf(text, sizeof(text), "%d of %d achievements unlocked", g->unlocked, g->total);
    Draw_Text(GFX_TOP, INFO_X, y, 1, SOFT, text, 0);
    DrawProgress(GFX_TOP, INFO_X, y + 20, INFO_W, 10, g->unlocked, g->total);
    snprintf(text, sizeof(text), "%d / %d points", g->earned, g->points);
    Draw_Text(GFX_TOP, INFO_X, y + 38, 1, TEXT, text, 0);

    // Which game this is, like a page number.
    snprintf(text, sizeof(text), "%d/%d", selected + 1, g_gameCount);
    Draw_Text(GFX_TOP, TOP_PANEL_X + TOP_PANEL_W - 10 - Draw_TextWidth(text, 1), TOP_PANEL_Y + 6, 1, MUTED, text, 0);
}

// The buttons below the panel, and the app's version at the right like System Settings'.
static void DrawLegend(void)
{
    int x = 14;
    x += DrawGlyph(GFX_TOP, x, LEGEND_Y, "SELECT", GLYPH, GLYPH_TEXT) + 5;
    x += Draw_Text(GFX_TOP, x, LEGEND_Y + 1, 1, TEXT, "Tracker", 0) + 16;
    x += DrawGlyph(GFX_TOP, x, LEGEND_Y, "X", GLYPH, GLYPH_TEXT) + 5;
    Draw_Text(GFX_TOP, x, LEGEND_Y + 1, 1, TEXT, g_patchedAllowed ? "Patched games: On" : "Patched games: Off", 0);

    char version[24];
    snprintf(version, sizeof(version), "Ver. %s", Update_AppVersion());
    Draw_Text(GFX_TOP, TOP_WIDTH - 14 - Draw_TextWidth(version, 1), LEGEND_Y + 1, 1, TEXT, version, 0);
}

static void DrawTop(int selected, int heroLevel)
{
    (void)heroLevel;
    Draw_Depth(0);
    Draw_Rect(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, CREAM);
    DrawStatusBar();
    CentredText(GFX_TOP, HEADER_Y, 1, ORANGE, "3DS Achievements");
    DrawDots(DOTS1_Y);
    Draw_RoundRect(GFX_TOP, TOP_PANEL_X, TOP_PANEL_Y, TOP_PANEL_W, TOP_PANEL_H, 10, PANEL);
    DrawDots(DOTS2_Y);
    DrawLegend();

    if (g_gameCount == 0)
    {
        CentredText(GFX_TOP, TOP_PANEL_Y + 52, 1, TEXT, "No games with achievements found.");
        CentredText(GFX_TOP, TOP_PANEL_Y + 72, 1, SOFT, "Definitions go in /achievements/<TitleID>/definitions.txt");
        return;
    }
    DrawGame(&g_games[selected], selected);
    if (selected > 0)
        DrawSideArrow(-1);
    if (selected < g_gameCount - 1)
        DrawSideArrow(1);
}

// ---- Bottom screen

static void DrawItem(int i, int y)
{
    char points[16], text[96];
    bool unlocked = g_unlocked[i];

    if (i == g_selected)
        Draw_RoundRing(GFX_BOTTOM, ITEM_X - 3, y - 3, ITEM_W + 6, ITEM_H + 6, ITEM_RADIUS + 3, 3, CURSOR, 16);
    DrawButton(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
    Draw_IconScaled(GFX_BOTTOM, ITEM_X + 8, y + (ITEM_H - ITEM_ICON) / 2, ITEM_ICON, 5, Ui_Trophy(unlocked), 0, NULL, 0);

    snprintf(points, sizeof(points), "%u pts", g_points[i]);
    int bw = Draw_TextWidth(points, 1) + 10, bx = ITEM_X + ITEM_W - 8 - bw;
    DrawBadge(GFX_BOTTOM, bx, y + 5, points, unlocked ? BADGE : BADGE_OFF);

    Ui_Ellipsize(text, sizeof(text), g_names[i], bx - 8 - ITEM_TEXT_X);
    Draw_Text(GFX_BOTTOM, ITEM_TEXT_X, y + 5, 1, unlocked ? TEXT : SOFT, text, 0);
    const char *description = g_descriptions[i][0] != '\0' ? g_descriptions[i] : "No description.";
    Ui_Ellipsize(text, sizeof(text), description, ITEM_X + ITEM_W - 8 - ITEM_TEXT_X);
    Draw_Text(GFX_BOTTOM, ITEM_TEXT_X, y + 22, 1, SOFT, text, 0);
}

static void DrawList(const Game *g, int scroll, bool loaded)
{
    char text[32];

    CentredText(GFX_BOTTOM, LIST_TITLE_Y, 1, TITLE, "Achievements");
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);

    if (g_count == 0)
    {
        if (loaded)
            CentredText(GFX_BOTTOM, LIST_PANEL_Y + 80, 1, SOFT, "No achievements for this game.");
    }
    else
    {
        // The items scroll inside the panel.
        Draw_ClipRows(LIST_PANEL_Y + 2, LIST_PANEL_Y + LIST_PANEL_H - 2);
        int first = scroll / ITEM_PITCH;
        int y = LIST_TOP - scroll % ITEM_PITCH;
        for (int i = first; i < g_count && y < LIST_PANEL_Y + LIST_PANEL_H; i++, y += ITEM_PITCH)
            DrawItem(i, y);
        Draw_ClipRows(0, SCREEN_HEIGHT);
    }

    // Along the bottom: Share, the points, which achievement is selected.
    if (loaded)
        DrawTab("Y", "Share");
    snprintf(text, sizeof(text), "%d/%d pts", g->earned, g->points);
    Draw_Text(GFX_BOTTOM, TAB_W + (BOTTOM_WIDTH - TAB_W - 70 - Draw_TextWidth(text, 1)) / 2, BAR_Y + 12, 1, MUTED, text, 0);
    if (g_count > 0)
        DrawPage(GFX_BOTTOM, BOTTOM_WIDTH - 8, BAR_Y, g_selected + 1, g_count);
}

// The selected achievement in full.
static void DrawDetails(void)
{
    char text[24];
    int i = g_selected;
    bool unlocked = g_unlocked[i];
    const int x = LIST_PANEL_X + 12, width = LIST_PANEL_W - 24;

    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, 6, LIST_PANEL_W, LIST_PANEL_Y + LIST_PANEL_H - 6, 10, PANEL);
    Draw_TrophyIcon(GFX_BOTTOM, x, 16, Ui_Trophy(unlocked));
    int bx = x + ICON_SIZE + 12;
    DrawBadge(GFX_BOTTOM, bx, 20, unlocked ? "Unlocked" : "Locked", unlocked ? BADGE : BADGE_OFF);
    snprintf(text, sizeof(text), "%u points", g_points[i]);
    Draw_Text(GFX_BOTTOM, bx, 42, 1, TEXT, text, 0);

    int y = Ui_DrawWrapped(GFX_BOTTOM, x, 16 + ICON_SIZE + 12, width, LINE_H + 4, TITLE, g_names[i], 3);
    const char *description = g_descriptions[i][0] != '\0' ? g_descriptions[i] : "No description.";
    y += 6;
    Ui_DrawWrapped(GFX_BOTTOM, x, y, width, LINE_H + 3, TEXT, description, (LIST_PANEL_Y + LIST_PANEL_H - 6 - y) / (LINE_H + 3));

    DrawTab("B", "Back");
    DrawPage(GFX_BOTTOM, BOTTOM_WIDTH - 8, BAR_Y, i + 1, g_count);
}

static void DrawBottom(int game, int scroll, bool details, bool loaded)
{
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, CREAM);
    if (game >= g_gameCount)
        Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);
    else if (details)
        DrawDetails();
    else
        DrawList(&g_games[game], scroll, loaded);
}

// ---- Popups: a dialog box over the dimmed top screen, its choices as buttons

static void DrawDialogButton(int x, int y, int w, const char *button, const char *label)
{
    DrawButton(GFX_TOP, x, y, w, 28, 8);
    int lw = Draw_TextWidth(label, 1), gx = x + (w - 15 - 6 - lw) / 2;
    gx += DrawGlyph(GFX_TOP, gx, y + 6, button, GLYPH, GLYPH_TEXT) + 6;
    Draw_Text(GFX_TOP, gx, y + 7, 1, TEXT, label, 0);
}

static void DrawPopup(bool alone)
{
    PopupText t;
    char      code[32];

    Ui_PopupText(&t);
    if (alone)
    {
        Draw_Rect(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, CREAM);
        Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, CREAM);
    }
    Draw_Depth(0);
    Draw_Darken(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, 0, 100);

    bool buttons = t.buttonA != NULL || t.buttonB != NULL;
    int  lines = t.lineCount + (t.showCode ? 1 : 0);
    const int w = 360, x = (TOP_WIDTH - w) / 2;
    int  h = 14 + 30 + 8 + 16 * lines + (t.progress ? 22 : 0) + (buttons ? 44 : 6) + 6;
    int  y = (SCREEN_HEIGHT - h) / 2;

    Draw_Depth(DEPTH_POPUP);
    Draw_Darken(GFX_TOP, x + 2, y + 4, w, h, 12, 70);
    Draw_RoundRect(GFX_TOP, x - 1, y - 1, w + 2, h + 2, 13, DIALOG_EDGE);
    Draw_RoundRectGradient(GFX_TOP, x, y, w, h, 12, DIALOG_TOP, DIALOG_BOTTOM);
    CentredText(GFX_TOP, y + 12, 2, TITLE, t.title);

    int ly = y + 52;
    if (t.showCode)
    {
        snprintf(code, sizeof(code), "Error 0x%08lX", t.code);
        Draw_Text(GFX_TOP, x + 18, ly, 1, SOFT, code, 0);
        ly += 16;
    }
    for (int i = 0; i < t.lineCount; i++, ly += 16)
    {
        Color color = i == 0 || t.centred ? TEXT : SOFT;
        if (t.centred)
            CentredText(GFX_TOP, ly, 1, color, t.lines[i]);
        else
            Draw_Text(GFX_TOP, x + 18, ly, 1, color, t.lines[i], 0);
    }
    if (t.progress)
        DrawProgress(GFX_TOP, x + 24, ly + 6, w - 48, 10, t.done, t.total);

    // B on the left, A on the right.
    const int bw = 140, by = y + h - 40;
    if (t.buttonA != NULL && t.buttonB != NULL)
    {
        DrawDialogButton(TOP_WIDTH / 2 - 8 - bw, by, bw, "B", t.buttonB);
        DrawDialogButton(TOP_WIDTH / 2 + 8, by, bw, "A", t.buttonA);
    }
    else if (t.buttonA != NULL)
        DrawDialogButton((TOP_WIDTH - bw) / 2, by, bw, "A", t.buttonA);
}

// ---- The rest

static void DrawLoading(void)
{
    Draw_Rect(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, CREAM);
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, CREAM);
    CentredText(GFX_TOP, (SCREEN_HEIGHT - LINE_H) / 2, 1, TITLE, "Loading...");
}

static void Reset(void)
{
}

static bool Animate(int selected, int ms)
{
    (void)selected;
    (void)ms;
    return false;
}

static bool TopCovers(int heroLevel)
{
    (void)heroLevel;
    return true;
}

const Theme THEME_3DS =
{
    .id = "3ds",
    .name = "3DS",
    .listTop = LIST_TOP,
    .listHeight = LIST_ROWS * ITEM_PITCH,
    .boxHeight = ITEM_PITCH,
    .minPositions = 1,
    .fadeTop = CREAM_RGB,
    .fadeBottom = CREAM_RGB,
    .drawLoading = DrawLoading,
    .reset = Reset,
    .animate = Animate,
    .topCovers = TopCovers,
    .drawTopBackground = NULL,
    .drawTop = DrawTop,
    .drawPopup = DrawPopup,
    .drawBottom = DrawBottom,
    .drawIdle = NULL,
    .topChanged = TopChanged,
};
