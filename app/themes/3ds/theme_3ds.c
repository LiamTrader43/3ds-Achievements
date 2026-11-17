// The "3ds" theme: like the 3DS's own System Settings, in the console's system font. Cream
// screens. Top: a status bar (tracker, clock, battery), an orange header, and the selected game
// in a sunken panel between dotted lines, with yellow arrow tabs at the sides when there are
// more games. Bottom: the achievements as white buttons in a sunken panel, grey tabs along the
// bottom edge. The share screen: the image framed on the top screen, the QR code in a panel.
// The colours were measured from screenshots of System Settings.
//
// An add-on (theme_api.h): built on its own into 3ds.theme, not into the app.
#include "theme.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "share.h"
#include "share_image.h"
#include "ui.h"
#include "update.h"

#include "theme_module.h"           // after every other header

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
static const Color TAB_TOP = { 152, 152, 152 };   // the grey tabs along the bottom
static const Color TAB_BOTTOM = { 100, 100, 100 };
static const Color TAB_PRESSED_TOP = { 112, 112, 112 }; // and while one is touched
static const Color TAB_PRESSED_BOTTOM = { 72, 72, 72 };
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
static const Color COIN_RIM = { 225, 140, 20 };          // a Play Coin before a number
static const Color COIN_FACE = { 252, 208, 96 };
static const Color COIN_FACE_OFF = { 228, 223, 198 };    // (one there aren't enough of)
static const Color OWNED = { 80, 170, 60 };              // an item already bought

#define CREAM_RGB       { 241, 233, 163 }

// Top screen.
#define STATUS_Y        1
#define HEADER_Y        17              // the screen's title, large
#define DOTS1_Y         44
#define TOP_PANEL_X     8
#define TOP_PANEL_Y     50
#define TOP_PANEL_W     384
#define TOP_PANEL_H     154
#define BIG_ICON        96
#define BIG_ICON_X      (TOP_PANEL_X + 16)
#define BIG_ICON_Y      (TOP_PANEL_Y + (TOP_PANEL_H - BIG_ICON) / 2)
#define INFO_X          (BIG_ICON_X + BIG_ICON + 18)
#define INFO_W          (TOP_PANEL_X + TOP_PANEL_W - 14 - INFO_X)
#define DOTS2_Y         210
#define LEGEND_Y        219
// 3D (Draw_Depth: in front of the screen > 0 > behind it): the page (status bar, title, dotted
// lines, legend) at the screen, the sunken panel and what's written on it a little behind, the
// games' icons in front of it, the highlighted one furthest, popups in front of everything.
#define DEPTH_PANEL     -4
#define DEPTH_ICON      -1              // a game in the row that isn't highlighted
#define DEPTH_BIG_ICON  4               // the game's icon once it's open
#define DEPTH_SELECTED  6               // the highlighted game in the row, and its cursor
#define DEPTH_ARROW     2               // the arrow tabs at the sides
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
#define BAR_Y           212                         // the strip along the bottom: the page number,
#define TAB_W           92                          // and a tab in each corner
#define TAB_H           22
#define TAB_Y           (SCREEN_HEIGHT - TAB_H)
#define TAB_GLYPH       13                          // the button's name, in a circle this big
#define COIN_SIZE       11                          // a Play Coin before a number
#define COIN_GAP        4

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
    // Each row from the flat side; the middle row reaches the tip.
    for (int dy = -size; dy <= size; dy++)
    {
        int w = size - (dy < 0 ? -dy : dy) + 1;
        int x = dir < 0 ? cx + size / 2 - w + 1 : cx - size / 2;
        Draw_Rect(screen, x, cy + dy, w, 1, color);
    }
}

// "5/97" as System Settings shows a page: the first number large, both in the muted colour,
// centred on centre.
static void DrawPage(gfxScreen_t screen, int centre, int y, int n, int count)
{
    char big[12], small[12];
    snprintf(big, sizeof(big), "%d", n);
    snprintf(small, sizeof(small), "/%d", count);
    int ws = Draw_TextWidth(small, 1), wb = Draw_TextWidth(big, 2), x = centre - (wb + ws) / 2;
    Draw_Text(screen, x, y, 2, MUTED, big, 0);
    Draw_Text(screen, x + wb, y + 10, 1, MUTED, small, 0);
}

// The prompts (g_tabs) as small grey tabs in the bottom corners, both TAB_W x TAB_H, flush with
// the screen's edges with the inner top corner round: the button's name in a white circle and
// what it does, centred. A tab being touched is darker.
static void DrawTabs(void)
{
    const int radius = TAB_H / 2;
    for (int side = TAB_LEFT; side <= TAB_RIGHT; side++)
    {
        const UiTab *t = &g_tabs[side];
        if (t->key == 0)
            continue;
        bool  pressed = g_tabPressed == side;
        Color dark = pressed ? TAB_PRESSED_BOTTOM : TAB_BOTTOM;
        int   x = side == TAB_LEFT ? 0 : BOTTOM_WIDTH - TAB_W;
        Draw_RoundRectGradient(GFX_BOTTOM, side == TAB_LEFT ? x - radius : x, TAB_Y, TAB_W + radius, TAB_H + radius, radius,
                               pressed ? TAB_PRESSED_TOP : TAB_TOP, dark);
        int lw = Draw_TextWidth(t->label, 1), gx = x + (TAB_W - TAB_GLYPH - 4 - lw) / 2;
        int gy = TAB_Y + (TAB_H - TAB_GLYPH) / 2;
        Draw_RoundRect(GFX_BOTTOM, gx, gy, TAB_GLYPH, TAB_GLYPH, TAB_GLYPH / 2, WHITE);
        Draw_Text(GFX_BOTTOM, gx + (TAB_GLYPH - Draw_TextWidth(t->button, 1)) / 2, gy + 1, 1, dark, t->button, 0);
        Draw_Text(GFX_BOTTOM, gx + TAB_GLYPH + 4, gy + 1, 1, WHITE, t->label, 0);
    }
}

// A Play Coin, its top at y.
static void DrawCoin(gfxScreen_t screen, int x, int y, bool bright)
{
    Draw_RoundRect(screen, x, y, COIN_SIZE, COIN_SIZE, COIN_SIZE / 2, bright ? COIN_RIM : BADGE_OFF);
    Draw_RoundRect(screen, x + 2, y + 2, COIN_SIZE - 4, COIN_SIZE - 4, (COIN_SIZE - 4) / 2, bright ? COIN_FACE : COIN_FACE_OFF);
}

// The Play Coins there are, "N Play Coins" after a coin (or that they couldn't be read), and
// how wide that is; draw: false only measures it.
static int DrawCoins(gfxScreen_t screen, int x, int y, Color color, bool draw)
{
    char text[32];
    if (g_coins < 0)
    {
        if (draw)
            Draw_Text(screen, x, y, 1, SOFT, "Couldn't read your Play Coins", 0);
        return Draw_TextWidth("Couldn't read your Play Coins", 1);
    }
    snprintf(text, sizeof(text), "%d Play Coins", g_coins);
    if (draw)
    {
        DrawCoin(screen, x, y + 2, true);
        Draw_Text(screen, x + COIN_SIZE + COIN_GAP, y, 1, color, text, 0);
    }
    return COIN_SIZE + COIN_GAP + Draw_TextWidth(text, 1);
}

static void CentredText(gfxScreen_t screen, int y, int scale, Color color, const char *text)
{
    Draw_Text(screen, (Draw_Width(screen) - Draw_TextWidth(text, scale)) / 2, y, scale, color, text, 0);
}

// The top screen's title under the status bar, and the dotted line under it.
static void DrawHeader(const char *title)
{
    CentredText(GFX_TOP, HEADER_Y, 2, ORANGE, title);
    DrawDots(DOTS1_Y);
}

// The blue cursor around a button, drawn after it (over its shadow, which would hide the cursor's
// bottom edge).
static void DrawCursor(gfxScreen_t screen, int x, int y, int w, int h, int radius)
{
    Draw_RoundRing(screen, x - 3, y - 3, w + 6, h + 6, radius + 3, 3, CURSOR, 16);
}

// Text word-wrapped to width pixels, each line centred on the screen. Returns the y below it.
static int CentredWrapped(gfxScreen_t screen, int y, int width, int lineHeight, Color color, const char *text)
{
    char line[160];
    while (*text != '\0')
    {
        while (*text == ' ')
            text++;
        int length = (int)strlen(text), take = Draw_TextFit(text, 1, width);
        take = take > (int)sizeof(line) - 1 ? (int)sizeof(line) - 1 : take;
        if (take < length)
        {
            int end = take;
            while (end > 0 && text[end] != ' ')
                end--;              // back to the end of a word
            take = end > 0 ? end : take;
        }
        if (take == 0)
            break;
        memcpy(line, text, take);
        line[take] = '\0';
        CentredText(screen, y, 1, color, line);
        y += lineHeight;
        text += take;
    }
    return y;
}

// ---- Status bar: the tracker, the date and time, the battery

static int s_battery = -1;      // 0-5, or -1 before it's read
static bool s_charging;
static time_t s_clockMinute;    // the minute the clock last showed

static void ReadBattery(void)
{
    s_battery = Ui_Battery(&s_charging);
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
    Draw_Depth(DEPTH_ARROW);
    Draw_RoundRect(GFX_TOP, cx - r - 1, cy - r - 1, 2 * r + 2, 2 * r + 2, r + 1, ARROW_EDGE);
    Draw_RoundRect(GFX_TOP, cx - r, cy - r, 2 * r, 2 * r, r, ARROW);
    DrawArrow(GFX_TOP, dir < 0 ? cx + 7 : cx - 8, cy, 5, dir, WHITE);
}

static void DrawGame(const Game *g, int selected)
{
    char text[64];

    if (g->hasIcon)
    {
        Draw_Shadow(GFX_TOP, BIG_ICON_X - 2, BIG_ICON_Y - 2, BIG_ICON + 4, BIG_ICON + 4, 14, 6, 3, 90, DEPTH_BIG_ICON);
        Draw_Depth(DEPTH_BIG_ICON);
        Draw_IconScaled(GFX_TOP, BIG_ICON_X, BIG_ICON_Y, BIG_ICON, 12, g->icon, 0, &WHITE, 0);
    }
    else
    {
        Draw_Depth(DEPTH_BIG_ICON);
        Draw_RoundRect(GFX_TOP, BIG_ICON_X, BIG_ICON_Y, BIG_ICON, BIG_ICON, 12, TRACK);
        Ui_DrawWrapped(GFX_TOP, BIG_ICON_X + 8, BIG_ICON_Y + 8, BIG_ICON - 16, LINE_H + 4, TEXT, g->title, 4);
    }
    Draw_Depth(DEPTH_PANEL);

    // Its name, large if it fits.
    int y = TOP_PANEL_Y + 30;
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

// The store once it's open: its bag, its name, the Play Coins to spend, what's been bought.
static void DrawStorePanel(void)
{
    char text[48];
    int  count = Store_Count();

    Draw_Shadow(GFX_TOP, BIG_ICON_X - 2, BIG_ICON_Y - 2, BIG_ICON + 4, BIG_ICON + 4, 14, 6, 3, 90, DEPTH_BIG_ICON);
    Draw_Depth(DEPTH_BIG_ICON);
    Draw_IconScaled(GFX_TOP, BIG_ICON_X, BIG_ICON_Y, BIG_ICON, 12, g_storeTile.icon, 0, &WHITE, 0);
    Draw_Depth(DEPTH_PANEL);

    int y = TOP_PANEL_Y + 30;
    Draw_Text(GFX_TOP, INFO_X, y, 2, TITLE, "Store", 0);
    DrawCoins(GFX_TOP, INFO_X, y + 36, TEXT, true);
    if (count > 0)
        snprintf(text, sizeof(text), "%d of %d items bought", Store_OwnedCount(), count);
    else
        snprintf(text, sizeof(text), "Nothing for sale yet");
    Draw_Text(GFX_TOP, INFO_X, y + 56, 1, SOFT, text, 0);
    snprintf(text, sizeof(text), "%d Play Coins for every %d points", REWARD_COINS, REWARD_POINTS);
    Draw_Text(GFX_TOP, INFO_X, y + 74, 1, SOFT, text, 0);
}

// The buttons below the panel (none while the settings are open), and the app's version at the
// right like System Settings'.
static void DrawLegend(bool buttons)
{
    if (buttons)
    {
        int x = 14;
        x += DrawGlyph(GFX_TOP, x, LEGEND_Y, "X", GLYPH, GLYPH_TEXT) + 5;
        Draw_Text(GFX_TOP, x, LEGEND_Y + 1, 1, TEXT, "Settings", 0);
    }
    char version[24];
    snprintf(version, sizeof(version), "Ver. %s", Update_AppVersion());
    Draw_Text(GFX_TOP, TOP_WIDTH - 14 - Draw_TextWidth(version, 1), LEGEND_Y + 1, 1, TEXT, version, 0);
}

// While a game is being chosen: a row of the games' icons and the store's last, ROW_SHOWN at a
// time, the highlighted one larger with a shadow and the blue cursor around it; its name and
// unlocks (the store: the Play Coins there are) under the row.
// The row scrolls with the selection; the arrow tabs show when there are games past either end.
#define ROW_ICON        56
#define ROW_ICON_BIG    72
#define ROW_GAP         14
#define ROW_SHOWN       5
#define ROW_MIDDLE      (TOP_PANEL_Y + 50)      // the icons are centred on this row
#define ROW_CAPTION_Y   (TOP_PANEL_Y + 99)

// A game's icon (or a "?" square) at size in a white frame; the highlighted one with a shadow and
// the blue cursor around it.
static void DrawRowIcon(const Game *g, int x, int y, int size, bool highlighted)
{
    int radius = size / 8, depth = highlighted ? DEPTH_SELECTED : DEPTH_ICON;
    Draw_Depth(DEPTH_PANEL);
    if (highlighted)
        Draw_Shadow(GFX_TOP, x - 2, y - 2, size + 4, size + 4, radius + 2, 6, 3, 90, depth);
    Draw_Depth(depth);
    if (g->hasIcon)
        Draw_IconScaled(GFX_TOP, x, y, size, radius, g->icon, 0, &WHITE, 0);
    else
    {
        Draw_RoundRect(GFX_TOP, x - 2, y - 2, size + 4, size + 4, radius + 2, WHITE);
        Draw_RoundRect(GFX_TOP, x, y, size, size, radius, TRACK);
        Draw_Text(GFX_TOP, x + (size - Draw_TextWidth("?", 1)) / 2, y + size / 2 - 7, 1, WHITE, "?", 0);
    }
    if (highlighted)
        Draw_RoundRing(GFX_TOP, x - 5, y - 5, size + 10, size + 10, radius + 5, 3, CURSOR, 16);
}

// What's at position i of the row: a game, or the store's tile.
static const Game *RowGame(int i)
{
    return i < g_gameCount ? &g_games[i] : &g_storeTile;
}

static void DrawGameRow(int selected)
{
    char text[72];
    int  count = Ui_RowPositions();

    // The first game shown keeps the selection in the middle where it can.
    int first = selected - ROW_SHOWN / 2, last = count - ROW_SHOWN;
    first = first > last ? last : first;
    first = first < 0 ? 0 : first;
    int shown = count - first < ROW_SHOWN ? count - first : ROW_SHOWN, width = 0;
    for (int i = first; i < first + shown; i++)
        width += (i == selected ? ROW_ICON_BIG : ROW_ICON) + (i > first ? ROW_GAP : 0);
    // The others first, the highlighted one over them (in 3D it comes forward, toward them).
    int x = (TOP_WIDTH - width) / 2, selectedX = x;
    for (int i = first; i < first + shown; i++)
    {
        int size = i == selected ? ROW_ICON_BIG : ROW_ICON;
        if (i == selected)
            selectedX = x;
        else
            DrawRowIcon(RowGame(i), x, ROW_MIDDLE - size / 2, size, false);
        x += size + ROW_GAP;
    }
    DrawRowIcon(RowGame(selected), selectedX, ROW_MIDDLE - ROW_ICON_BIG / 2, ROW_ICON_BIG, true);

    Draw_Depth(DEPTH_PANEL);
    if (Ui_IsStore(selected))
    {
        CentredText(GFX_TOP, ROW_CAPTION_Y, 2, TITLE, "Store");
        int w = DrawCoins(GFX_TOP, 0, 0, SOFT, false);
        DrawCoins(GFX_TOP, (TOP_WIDTH - w) / 2, ROW_CAPTION_Y + 25, SOFT, true);
    }
    else
    {
        const Game *g = &g_games[selected];
        Ui_EllipsizeScaled(text, sizeof(text), g->title, TOP_PANEL_W - 24, 2);
        CentredText(GFX_TOP, ROW_CAPTION_Y, 2, TITLE, text);
        snprintf(text, sizeof(text), "%d of %d achievements unlocked", g->unlocked, g->total);
        CentredText(GFX_TOP, ROW_CAPTION_Y + 25, 1, SOFT, text);
    }

    if (first > 0)
        DrawSideArrow(-1);
    if (first + shown < count)
        DrawSideArrow(1);
}

static void DrawTop(int selected, int heroLevel)
{
    (void)heroLevel;
    Draw_Depth(0);
    Draw_Rect(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, CREAM);
    DrawStatusBar();
    DrawHeader("3DS Achievements");
    DrawDots(DOTS2_Y);
    DrawLegend(true);
    Draw_Depth(DEPTH_PANEL);
    Draw_RoundRect(GFX_TOP, TOP_PANEL_X, TOP_PANEL_Y, TOP_PANEL_W, TOP_PANEL_H, 10, PANEL);

    if (g_gameCount == 0)
    {
        CentredText(GFX_TOP, TOP_PANEL_Y + 60, 1, TEXT, "No games with achievements found.");
        CentredText(GFX_TOP, TOP_PANEL_Y + 82, 1, SOFT, "Definitions go in /achievements/<TitleID>/definitions.txt");
        return;
    }
    if (g_picking)
    {
        DrawGameRow(selected);      // several games at once while one is being chosen
        return;
    }
    if (Ui_IsStore(selected))
        DrawStorePanel();
    else
        DrawGame(&g_games[selected], selected);
    if (selected > 0)
        DrawSideArrow(-1);
    if (selected < Ui_RowPositions() - 1)
        DrawSideArrow(1);
}

// ---- Bottom screen

static void DrawItem(int i, int y)
{
    char points[16], text[96];
    bool unlocked = g_unlocked[i];

    DrawButton(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
    if (i == g_selected)
        DrawCursor(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
    Draw_TrophyScaled(GFX_BOTTOM, ITEM_X + 8, y + (ITEM_H - ITEM_ICON) / 2, ITEM_ICON, Ui_Trophy(unlocked));

    snprintf(points, sizeof(points), "%u pts", g_points[i]);
    int bw = Draw_TextWidth(points, 1) + 10, bx = ITEM_X + ITEM_W - 8 - bw;
    DrawBadge(GFX_BOTTOM, bx, y + 5, points, unlocked ? BADGE : BADGE_OFF);

    Ui_Ellipsize(text, sizeof(text), g_names[i], bx - 8 - ITEM_TEXT_X);
    Draw_Text(GFX_BOTTOM, ITEM_TEXT_X, y + 5, 1, unlocked ? TEXT : SOFT, text, 0);
    const char *description = g_descriptions[i][0] != '\0' ? g_descriptions[i] : "No description.";
    Ui_Ellipsize(text, sizeof(text), description, ITEM_X + ITEM_W - 8 - ITEM_TEXT_X);
    Draw_Text(GFX_BOTTOM, ITEM_TEXT_X, y + 22, 1, SOFT, text, 0);
}

static void DrawList(int scroll, bool loaded)
{
    CentredText(GFX_BOTTOM, LIST_TITLE_Y, 1, TITLE, "Achievements");
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);

    if (g_count == 0)
    {
        if (loaded)
            CentredText(GFX_BOTTOM, LIST_PANEL_Y + 80, 1, SOFT, "No achievements for this game.");
    }
    else
    {
        // The items scroll inside the panel, LIST_ROWS of them whole (the cursor around the last
        // one too), nothing of the next one under them.
        Draw_ClipRows(LIST_PANEL_Y + 2, LIST_TOP + LIST_ROWS * ITEM_PITCH - 1);
        int first = scroll / ITEM_PITCH;
        int y = LIST_TOP - scroll % ITEM_PITCH;
        for (int i = first; i < g_count && y < LIST_PANEL_Y + LIST_PANEL_H; i++, y += ITEM_PITCH)
            DrawItem(i, y);
        Draw_ClipRows(0, SCREEN_HEIGHT);
    }

    // Along the bottom: back to the games, which achievement is selected, Share.
    if (g_count > 0)
        DrawPage(GFX_BOTTOM, BOTTOM_WIDTH / 2, BAR_Y, g_selected + 1, g_count);
    DrawTabs();
}

// A stat in the panel: its label on the left, its value on the right, a bar under them.
static void DrawStat(int y, const char *label, int done, int total)
{
    char text[32];
    const int x = LIST_PANEL_X + 12, w = LIST_PANEL_W - 24;
    snprintf(text, sizeof(text), "%d / %d", done, total);
    Draw_Text(GFX_BOTTOM, x, y, 1, TEXT, label, 0);
    Draw_Text(GFX_BOTTOM, x + w - Draw_TextWidth(text, 1), y, 1, TITLE, text, 0);
    DrawProgress(GFX_BOTTOM, x, y + 18, w, 10, done, total);
}

// Every game's achievements added up, along the bottom of the panel.
static void DrawAllGames(void)
{
    char     text[64];
    UiTotals all;
    const int x = LIST_PANEL_X + 12, w = LIST_PANEL_W - 24, y = LIST_PANEL_Y + LIST_PANEL_H - 20;
    Ui_Totals(&all);
    Draw_Text(GFX_BOTTOM, x, y, 1, TEXT, "All games", 0);
    snprintf(text, sizeof(text), "%d / %d (%d of %d complete)", all.unlocked, all.total, all.complete, g_gameCount);
    Draw_Text(GFX_BOTTOM, x + w - Draw_TextWidth(text, 1), y, 1, TITLE, text, 0);
}

// While a game is being chosen: the highlighted game's stats (its unlocks and points, the
// latest unlocks), then every game's added up.
static void DrawStats(int game, bool loaded)
{
    char     text[96];
    const int x = LIST_PANEL_X + 12, w = LIST_PANEL_W - 24;
    const Game *g = &g_games[game];

    CentredText(GFX_BOTTOM, LIST_TITLE_Y, 1, TITLE, "Stats");
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);
    DrawStat(LIST_PANEL_Y + 8, "Achievements", g->unlocked, g->total);
    DrawStat(LIST_PANEL_Y + 42, "Points", g->earned, g->points);

    // The latest unlocks, newest first.
    int y = LIST_PANEL_Y + 78;
    Draw_Text(GFX_BOTTOM, x, y, 1, SOFT, "Recently unlocked", 0);
    if (!loaded)
        Draw_Text(GFX_BOTTOM, x, y + 18, 1, SOFT, "Loading...", 0);
    else if (g_recentCount == 0)
        Draw_Text(GFX_BOTTOM, x, y + 18, 1, SOFT, "None yet.", 0);
    for (int k = 0; loaded && k < g_recentCount; k++)
    {
        int i = g_recent[k], ry = y + 17 + 21 * k;
        Draw_TrophyScaled(GFX_BOTTOM, x, ry, 18, Ui_Trophy(true));
        snprintf(text, sizeof(text), "%u pts", g_points[i]);
        int bw = Draw_TextWidth(text, 1) + 10, bx = x + w - bw;
        DrawBadge(GFX_BOTTOM, bx, ry + 1, text, BADGE);
        char name[80];
        Ui_Ellipsize(name, sizeof(name), g_names[i], bx - 8 - (x + 26));
        Draw_Text(GFX_BOTTOM, x + 26, ry + 2, 1, TEXT, name, 0);
    }

    DrawAllGames();

    DrawPage(GFX_BOTTOM, BOTTOM_WIDTH / 2, BAR_Y, game + 1, g_gameCount);
    DrawTabs();
}

// While the store is highlighted: the Play Coins to spend, what's for sale and bought, how
// coins are earned, then every game's achievements.
static void DrawStoreStats(void)
{
    char text[96];
    const int x = LIST_PANEL_X + 12, w = LIST_PANEL_W - 24;
    int count = Store_Count();

    CentredText(GFX_BOTTOM, LIST_TITLE_Y, 1, TITLE, "Store");
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);
    int y = LIST_PANEL_Y + 10;
    Draw_Text(GFX_BOTTOM, x, y, 1, TEXT, "Play Coins", 0);
    DrawCoins(GFX_BOTTOM, x + w - DrawCoins(GFX_BOTTOM, 0, 0, TITLE, false), y, TITLE, true);
    Draw_Text(GFX_BOTTOM, x, y + 22, 1, TEXT, "For sale", 0);
    if (count > 0)
        snprintf(text, sizeof(text), "%d item%s", count, count == 1 ? "" : "s");
    else
        snprintf(text, sizeof(text), "Nothing yet");
    Draw_Text(GFX_BOTTOM, x + w - Draw_TextWidth(text, 1), y + 22, 1, TITLE, text, 0);
    if (count > 0)
    {
        Draw_Text(GFX_BOTTOM, x, y + 44, 1, TEXT, "Bought", 0);
        snprintf(text, sizeof(text), "%d of %d", Store_OwnedCount(), count);
        Draw_Text(GFX_BOTTOM, x + w - Draw_TextWidth(text, 1), y + 44, 1, TITLE, text, 0);
    }
    snprintf(text, sizeof(text), "Achievements earn %d Play Coins for every %d points, given when you open the app.",
             REWARD_COINS, REWARD_POINTS);
    Ui_DrawWrapped(GFX_BOTTOM, x, y + 74, w, LINE_H + 4, SOFT, text, 3);
    DrawAllGames();
    DrawTabs();
}

// One item for sale, laid out like an achievement's button: icon | name, price / description.
// A bought one says so; the price is grey when there aren't enough Play Coins for it.
static void DrawStoreItem(int i, int y)
{
    const StoreItem *item = Store_Item(i);
    bool owned = Store_Owned(i);
    char text[96];
    int  right = ITEM_X + ITEM_W - 8, priceX;

    DrawButton(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
    if (i == g_selected)
        DrawCursor(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
    Draw_TrophyScaled(GFX_BOTTOM, ITEM_X + 8, y + (ITEM_H - ITEM_ICON) / 2, ITEM_ICON, Ui_StoreIcon(i));

    if (owned)
    {
        priceX = right - (Draw_TextWidth("Owned", 1) + 10);
        DrawBadge(GFX_BOTTOM, priceX, y + 5, "Owned", OWNED);
    }
    else
    {
        bool affordable = g_coins >= item->price;
        snprintf(text, sizeof(text), "%d", item->price);
        priceX = right - Draw_TextWidth(text, 1) - COIN_GAP - COIN_SIZE;
        DrawCoin(GFX_BOTTOM, priceX, y + 7, affordable);
        Draw_Text(GFX_BOTTOM, priceX + COIN_SIZE + COIN_GAP, y + 5, 1, affordable ? TITLE : SOFT, text, 0);
    }

    Ui_Ellipsize(text, sizeof(text), item->name, priceX - 8 - ITEM_TEXT_X);
    Draw_Text(GFX_BOTTOM, ITEM_TEXT_X, y + 5, 1, owned ? SOFT : TEXT, text, 0);
    Ui_Ellipsize(text, sizeof(text), item->description, ITEM_X + ITEM_W - 8 - ITEM_TEXT_X);
    Draw_Text(GFX_BOTTOM, ITEM_TEXT_X, y + 22, 1, SOFT, text, 0);
}

// The store, open: what's for sale (g_count items), scrolled like the achievements, the Play
// Coins at the top right.
static void DrawStoreList(int scroll, bool loaded)
{
    char text[96];

    CentredText(GFX_BOTTOM, LIST_TITLE_Y, 1, TITLE, "Store");
    if (g_coins >= 0)
        DrawCoins(GFX_BOTTOM, BOTTOM_WIDTH - 10 - DrawCoins(GFX_BOTTOM, 0, 0, TEXT, false), LIST_TITLE_Y, TEXT, true);
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);

    if (g_count == 0)
    {
        if (loaded)
        {
            const int w = LIST_PANEL_W - 36;
            CentredText(GFX_BOTTOM, LIST_PANEL_Y + 30, 2, TITLE, "Nothing for sale yet");
            int y = CentredWrapped(GFX_BOTTOM, LIST_PANEL_Y + 72, w, 16, TEXT,
                                   "Things to spend your Play Coins on will show up here in an update.");
            snprintf(text, sizeof(text), "Achievements earn %d for every %d points.", REWARD_COINS, REWARD_POINTS);
            CentredWrapped(GFX_BOTTOM, y + 10, w, 16, SOFT, text);
        }
    }
    else
    {
        Draw_ClipRows(LIST_PANEL_Y + 2, LIST_TOP + LIST_ROWS * ITEM_PITCH - 1);
        int first = scroll / ITEM_PITCH;
        int y = LIST_TOP - scroll % ITEM_PITCH;
        for (int i = first; i < g_count && y < LIST_PANEL_Y + LIST_PANEL_H; i++, y += ITEM_PITCH)
            DrawStoreItem(i, y);
        Draw_ClipRows(0, SCREEN_HEIGHT);
        DrawPage(GFX_BOTTOM, BOTTOM_WIDTH / 2, BAR_Y, g_selected + 1, g_count);
    }
    DrawTabs();
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

    DrawPage(GFX_BOTTOM, BOTTOM_WIDTH / 2, BAR_Y, i + 1, g_count);
    DrawTabs();
}

static void DrawBottom(int game, int scroll, bool details, bool loaded)
{
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, CREAM);
    if (game >= g_gameCount && !Ui_IsStore(game))
        Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);
    else if (g_picking && Ui_IsStore(game))
        DrawStoreStats();
    else if (g_picking)
        DrawStats(game, loaded);
    else if (Ui_IsStore(game))
        DrawStoreList(scroll, loaded);
    else if (details)
        DrawDetails();
    else
        DrawList(scroll, loaded);
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

// ---- The share screen: the image framed on the top screen; the QR code (or why there isn't
// one) in a panel on the bottom, Back and Save to SD as tabs.

#define SHARE_SHOWN     180             // the image's size on the top screen
#define SHARE_Y         (DOTS1_Y + 9)
#define QR_QUIET        4               // light modules around the code, as the standard asks
#define QR_MOST         132             // most pixels the code (with its margin) may take
static const Color GOOD = { 70, 150, 40 };
static const Color BAD = { 200, 60, 40 };
static const Color QR_DARK = { 40, 36, 28 };

// size x size RGB pixels from from x from ones, smoothly (each from the four nearest).
static void ScaleImage(const u8 *rgb, int from, u8 *out, int size)
{
    for (int y = 0; y < size; y++)
    {
        int sy = ((2 * y + 1) * from * 128) / size - 128;
        sy = sy < 0 ? 0 : sy;
        int y0 = sy >> 8, y1 = y0 + 1 < from ? y0 + 1 : y0, wy = sy & 255;
        for (int x = 0; x < size; x++, out += 3)
        {
            int sx = ((2 * x + 1) * from * 128) / size - 128;
            sx = sx < 0 ? 0 : sx;
            int x0 = sx >> 8, x1 = x0 + 1 < from ? x0 + 1 : x0, wx = sx & 255;
            const u8 *a = rgb + (y0 * from + x0) * 3, *b = rgb + (y0 * from + x1) * 3;
            const u8 *c = rgb + (y1 * from + x0) * 3, *d = rgb + (y1 * from + x1) * 3;
            for (int k = 0; k < 3; k++)
            {
                int top = a[k] * (256 - wx) + b[k] * wx, bottom = c[k] * (256 - wx) + d[k] * wx;
                out[k] = (u8)((top * (256 - wy) + bottom * wy) >> 16);
            }
        }
    }
}

static void DrawShareTop(void)
{
    static u8 shown[SHARE_SHOWN * SHARE_SHOWN * 3];
    ShareView v;
    Share_View(&v);

    Draw_Depth(0);
    Draw_Rect(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, CREAM);
    DrawStatusBar();
    DrawHeader("Share");

    const int x = (TOP_WIDTH - SHARE_SHOWN) / 2;
    Draw_Shadow(GFX_TOP, x - 3, SHARE_Y - 3, SHARE_SHOWN + 6, SHARE_SHOWN + 6, 6, 6, 3, 90, 0);
    Draw_RoundRect(GFX_TOP, x - 3, SHARE_Y - 3, SHARE_SHOWN + 6, SHARE_SHOWN + 6, 6, WHITE);
    ScaleImage(v.preview, SHARE_PREVIEW, shown, SHARE_SHOWN);
    Draw_Image(GFX_TOP, x, SHARE_Y, SHARE_SHOWN, SHARE_SHOWN, shown);
}

static void DrawShareBottom(void)
{
    const int top = 6, bottom = LIST_PANEL_Y + LIST_PANEL_H, width = LIST_PANEL_W - 36;
    ShareView v;
    Share_View(&v);
    Color tone = v.tone == SHARE_GOOD ? GOOD : v.tone == SHARE_BAD ? BAD : SOFT;

    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, CREAM);
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, top, LIST_PANEL_W, bottom - top, 10, PANEL);

    switch (v.state)
    {
        case SHARE_OPENING:
            CentredText(GFX_BOTTOM, (top + bottom - LINE_H) / 2, 1, TEXT, "Making the image...");
            return;
        case SHARE_SERVING:
        {
            int total = v.qrSize + 2 * QR_QUIET;
            int module = QR_MOST / total > 4 ? 4 : QR_MOST / total;
            int box = total * module, x = (BOTTOM_WIDTH - box) / 2, y = top + 6;
            Draw_RoundRect(GFX_BOTTOM, x, y, box, box, 6, WHITE);
            for (int my = 0; my < v.qrSize; my++)
                for (int mx = 0; mx < v.qrSize; mx++)
                    if (Share_QrModule(mx, my))
                        Draw_Rect(GFX_BOTTOM, x + (QR_QUIET + mx) * module, y + (QR_QUIET + my) * module, module, module, QR_DARK);
            int ty = y + box + 6;
            CentredText(GFX_BOTTOM, ty, 1, TEXT, "Scan with a phone on the same Wi-Fi");
            CentredText(GFX_BOTTOM, ty + 16, 1, SOFT, v.url);
            if (v.status[0] != '\0')
                CentredText(GFX_BOTTOM, ty + 34, 1, tone, v.status);
            break;
        }
        case SHARE_MAKING:
            CentredText(GFX_BOTTOM, 90, 1, TEXT, "Getting the image ready...");
            if (v.status[0] != '\0')
                CentredText(GFX_BOTTOM, 114, 1, tone, v.status);
            break;
        case SHARE_NO_MEMORY:
            CentredText(GFX_BOTTOM, top + 20, 2, TITLE, "Couldn't make the image");
            CentredWrapped(GFX_BOTTOM, top + 64, width, 16, TEXT, "There wasn't enough memory for it.");
            DrawTabs();
            return;
        default:
        {
            bool failed = v.state == SHARE_SERVER_FAILED;
            CentredText(GFX_BOTTOM, top + 20, 2, TITLE, failed ? "Couldn't start sharing" : "No Wi-Fi connection");
            int y = CentredWrapped(GFX_BOTTOM, top + 62, width, 16, TEXT, failed ?
                                   "See /achievements/share.log for why." :
                                   "Connect the 3DS to Wi-Fi to send the image to your phone. It's sent from the 3DS "
                                   "itself, so the phone must be on the same network.");
            y = CentredWrapped(GFX_BOTTOM, y + 12, width, 16, SOFT, "Press A to save it to the SD card instead.");
            if (v.status[0] != '\0')
                CentredText(GFX_BOTTOM, y + 8, 1, tone, v.status);
            break;
        }
    }
    DrawTabs();
}

// ---- The settings (X), like System Settings: the highlighted one explained on the top screen,
// a button for each on the bottom with its value in a badge.

static void DrawSettingsTop(int item)
{
    UiSetting set;
    Ui_Setting(item, &set);

    Draw_Depth(0);
    Draw_Rect(GFX_TOP, 0, 0, TOP_WIDTH, SCREEN_HEIGHT, CREAM);
    DrawStatusBar();
    DrawHeader(Ui_MenuTitle());
    DrawDots(DOTS2_Y);
    DrawLegend(false);
    Draw_Depth(DEPTH_PANEL);
    Draw_RoundRect(GFX_TOP, TOP_PANEL_X, TOP_PANEL_Y, TOP_PANEL_W, TOP_PANEL_H, 10, PANEL);

    const int x = TOP_PANEL_X + 18, width = TOP_PANEL_W - 36;
    Draw_Text(GFX_TOP, x, TOP_PANEL_Y + 22, 2, TITLE, set.name, 0);
    int bw = Draw_TextWidth(set.value, 1) + 10;
    if (set.value[0] != '\0')
        DrawBadge(GFX_TOP, x + width - bw, TOP_PANEL_Y + 28, set.value, set.on ? BADGE : BADGE_OFF);
    Ui_DrawWrapped(GFX_TOP, x, TOP_PANEL_Y + 66, width, LINE_H + 5, TEXT, set.description, 4);
}

static void DrawSettingsBottom(int item)
{
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, CREAM);
    const int count = Ui_MenuCount();
    CentredText(GFX_BOTTOM, LIST_TITLE_Y, 1, TITLE, Ui_MenuTitle());
    Draw_RoundRect(GFX_BOTTOM, LIST_PANEL_X, LIST_PANEL_Y, LIST_PANEL_W, LIST_PANEL_H, 10, PANEL);
    for (int i = g_menuScroll; i < count && i < g_menuScroll + LIST_ROWS; i++)
    {
        UiSetting set;
        Ui_Setting(i, &set);
        int y = LIST_TOP + (i - g_menuScroll) * ITEM_PITCH;
        DrawButton(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
        if (i == item)
            DrawCursor(GFX_BOTTOM, ITEM_X, y, ITEM_W, ITEM_H, ITEM_RADIUS);
        Draw_Text(GFX_BOTTOM, ITEM_X + 14, y + (ITEM_H - LINE_H) / 2, 1, TEXT, set.name, 0);
        int bw = Draw_TextWidth(set.value, 1) + 10;
        if (set.value[0] != '\0')
            DrawBadge(GFX_BOTTOM, ITEM_X + ITEM_W - 10 - bw, y + (ITEM_H - 15) / 2, set.value, set.on ? BADGE : BADGE_OFF);
    }
    if (count > LIST_ROWS)
        DrawPage(GFX_BOTTOM, BOTTOM_WIDTH / 2, BAR_Y, item + 1, count);
    DrawTabs();
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
    .description = "Like the 3DS's own System Settings: cream panels and the system's font.",
    .font = DRAW_FONT_SYSTEM,
    .listTop = LIST_TOP,
    .listHeight = LIST_ROWS * ITEM_PITCH,
    .boxHeight = ITEM_PITCH,
    .tabWidth = TAB_W,
    .tabTop = TAB_Y,
    .minPositions = 1,
    .gameList = true,
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
    .drawShareTop = DrawShareTop,
    .drawShareBottom = DrawShareBottom,
    .drawSettingsTop = DrawSettingsTop,
    .drawSettingsBottom = DrawSettingsBottom,
};

THEME_MODULE_DEFINE(THEME_3DS, "3ds", "3DS", "Like the 3DS's own System Settings: cream panels and the system's font.")
