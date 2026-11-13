// 3DS Achievements app: starts the background tracker sysmodule when it isn't
// running yet, and shows every game with a definitions file in /achievements/ and
// its achievements, without starting the game. Reads the same files as the
// tracker. SELECT turns the tracker on or off. Y shares the selected game's progress
// as an image (share.c).
//
// Top screen: a row of the games' icons, the selected one largest on the left. Bottom screen: the highlighted game's
// progress bar and achievements, one box each (trophy, name, description,
// points; gold trophy, green name = unlocked).
#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "draw.h"
#include "games.h"
#include "share.h"
#include "tracker.h"
#include "update.h"
#include "trophy.h"

#define MAX_ACHIEVEMENTS    128     // the tracker's MAX_LINES

// Colours, as in the tracker's menu and banner.
static const Color ACCENT = { 255, 209, 102 };       // the logo's yellow
static const Color HINT = { 255, 209, 102 };
static const Color WHITE = { 255, 255, 255 };
static const Color GREEN = { 50, 205, 50 };         // LimeGreen, unlocked
static const Color GOLD = { 255, 196, 40 };
static const Color GREY = { 140, 140, 150 };
static const Color LIGHT = { 205, 205, 212 };       // longer text, easier to read than GREY
static const Color DARK = { 50, 50, 58 };
static const Color SCROLLBAR = { 110, 110, 120 };
static const Color RED = { 235, 70, 70 };
static const Color SHINE = { 255, 255, 245 };       // the light running around the selected game's frame
static const Color PILL = { 22, 22, 28 };           // behind each button hint
static const Color BG_TOP = { 12, 12, 16 };         // the bottom screen: a faint fade from this
static const Color BG_BOTTOM = { 24, 24, 31 };      // to this

#define MARGIN          8

// Top screen: a row of game tiles across the middle, hints at the bottom.
#define TILE_LEFT       16
#define TILE_GAP        4
#define TILE_MIDDLE     120             // the row is centred on this y
#define ROW_POSITIONS   5               // positions to scroll through, even with fewer games
#define TILE_RADIUS(s)  ((s) / 8)       // corner radius of a tile of size s
#define HERO_DELAY_MS   150             // a game stays selected this long before its background loads
#define HERO_FADE_MS    350
#define HERO_OUT_MS     130             // a game's background fading out as you leave it
#define TILE_BIG        112
#define TILE_SMALL      60
#define TILE_GROW_MS    160             // a tile changing between the two sizes
#define CAMERA_EASE_MS  70              // lower = the row catches up faster
#define NAME_Y          (TILE_MIDDLE + TILE_BIG / 2 + 8)    // the selected game's name
#define PILL_H          21                                  // the button hints along the bottom,
#define PILL_PAD        10                                  // each in a dark pill
#define PILL_GAP        6
#define PILL_Y          (SCREEN_HEIGHT - PILL_H - 4)
#define TILE_DIM        90              // how much darker (of 256) the tiles that aren't selected are
#define SHADOW_BLUR     10              // the selected tile's shadow: how far it fades out,
#define SHADOW_DROP     4               // how much lower than the tile it is,
#define SHADOW_STRENGTH 150             // and how dark (of 256) right under it
#define SLOT_ALPHA      5               // an empty slot's outline (of 16)
#define BOTTOM_FADE_MS  150             // the bottom screen fading in (a list loading, the full view)

// Bottom screen: the opened game's unlock count and progress bar, then one box
// per achievement.
#define PROGRESS_H      20
#define BOX_H           55
#define BOX_ROWS        ((SCREEN_HEIGHT - PROGRESS_H) / BOX_H)
#define BOX_LEFT        10                          // right of the scrollbar
#define BOX_ICON_X      (BOX_LEFT + 4)
#define BOX_TEXT_X      (BOX_ICON_X + ICON_SIZE + 8)
#define BOX_TEXT_W      (BOTTOM_WIDTH - MARGIN - BOX_TEXT_X)
#define LIST_H          (SCREEN_HEIGHT - PROGRESS_H)                // the boxes' part of the screen
#define SELECT_X        (BOX_LEFT - 2)                              // the selected box's outline
#define SELECT_W        (BOTTOM_WIDTH - 4 - SELECT_X)
#define SELECT_LINE     2
#define SELECT_RADIUS   8
#define TAP_SLOP        6                                           // a touch moving less is a tap
#define BAR_H           6                                           // the progress bar, rounded
#define SCROLL_W        4                                           // the scrollbar, rounded

// Bottom screen, one achievement in full (A).
#define DETAIL_MARGIN   10
#define DETAIL_NAME_LINE (LINE_H + 4)                              // the name, the same size as in the game's list
#define DETAIL_LINE     (LINE_H + 3)
#define DETAIL_HINT_Y   (SCREEN_HEIGHT - 16)

static Game         s_games[MAX_GAMES];
static int          s_gameCount;

// The opened game's achievements.
static const char  *s_names[MAX_ACHIEVEMENTS];
static const char  *s_descriptions[MAX_ACHIEVEMENTS];
static uint16_t     s_points[MAX_ACHIEVEMENTS];
static bool         s_unlocked[MAX_ACHIEVEMENTS];
static int          s_count;
static int          s_selected;             // the achievement Up/Down have selected
static u16          s_gameUnlocked[ICON_SIZE * ICON_SIZE], s_gameLocked[ICON_SIZE * ICON_SIZE];
static bool         s_hasGameIcons;         // this game has its own achievement icons

// The row: how far each position is from small (0) to big (256), and how far the row
// has scrolled, in 1/16ths of a pixel.
static int          s_tileScale[MAX_GAMES + ROW_POSITIONS];
static int          s_camera;

typedef enum
{
    POPUP_NONE, POPUP_START_FAILED, POPUP_STOP_FAILED,
    POPUP_UPDATE, POPUP_DOWNLOADING, POPUP_UPDATE_FAILED
} Popup;

static bool         s_trackerRunning;
static bool         s_patchedAllowed;
static Popup        s_popup;
static Result       s_popupResult;
static int          s_newGames, s_updatedGames;
static int          s_downloadDone, s_downloadTotal;
static u8           s_hero[HERO_BYTES];     // the selected game's background
static bool         s_heroReady;

// Copies text cut to maxWidth pixels at the given text scale, ending in "..." if it was longer.
static void EllipsizeScaled(char *out, size_t capacity, const char *text, int maxWidth, int scale)
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

static void Ellipsize(char *out, size_t capacity, const char *text, int maxWidth)
{
    EllipsizeScaled(out, capacity, text, maxWidth, 1);
}

// Draws text word-wrapped to width pixels at the given text scale, at most maxLines lines
// (the last one ends in "..." if the text goes on). Returns the y below the last line.
static int DrawWrappedScaled(gfxScreen_t screen, int x, int y, int width, int lineHeight, int scale,
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
            EllipsizeScaled(line, sizeof(line), text, width, scale);
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

static int DrawWrapped(gfxScreen_t screen, int x, int y, int width, int lineHeight, Color color,
                       const char *text, int maxLines)
{
    return DrawWrappedScaled(screen, x, y, width, lineHeight, 1, color, text, maxLines);
}

// A rounded bar, like the share image's: gold when everything is unlocked, else green.
static void DrawProgressBar(gfxScreen_t screen, int x, int y, int w, int h, int unlocked, int total)
{
    Draw_RoundRect(screen, x, y, w, h, h / 2, DARK);
    if (total > 0 && unlocked > 0)
    {
        int fill = w * unlocked / total;
        if (fill < h)
            fill = h;               // a rounded bar needs at least its height
        Draw_RoundRect(screen, x, y, fill, h, h / 2, unlocked == total ? GOLD : GREEN);
    }
}

static void DrawScrollbar(gfxScreen_t screen, int x, int top, int track, int visible, int scroll, int count)
{
    if (count <= visible)
        return;
    int thumb = track * visible / count;
    if (thumb < 12)
        thumb = 12;
    int y = top + (track - thumb) * scroll / (count - visible);
    Draw_RoundRect(screen, x, y, SCROLL_W, thumb, SCROLL_W / 2, SCROLLBAR);
}

// A game's icon, or its title in a grey box when it isn't installed.
static void DrawTile(const Game *g, int x, int y, int size)
{
    int radius = TILE_RADIUS(size);
    int inset = radius / 2 > 4 ? radius / 2 : 4;    // keeps the title inside the rounded corners

    if (g->hasIcon)
    {
        Draw_IconScaled(GFX_TOP, x, y, size, radius, g->icon);
        return;
    }
    Draw_RoundRect(GFX_TOP, x, y, size, size, radius, DARK);
    DrawWrapped(GFX_TOP, x + inset, y + inset, size - 2 * inset, LINE_H + 4, WHITE, g->title, (size - 2 * inset) / (LINE_H + 4));
}

// Loads game i's achievements into s_* (and refreshes its counts).
static void LoadGame(int i)
{
    Game *g = &s_games[i];

    s_hasGameIcons = Games_LoadTrophies(g->titleId, s_gameUnlocked, s_gameLocked);
    s_count = 0;
    if (!Achievements_Load(g->titleId))
        return;
    s_count = Achievements_List(s_names, s_unlocked, MAX_ACHIEVEMENTS);
    Achievements_Descriptions(s_descriptions, MAX_ACHIEVEMENTS);
    Achievements_Points(s_points, MAX_ACHIEVEMENTS);
    Achievements_Progress(&g->unlocked, &g->total);
    Achievements_Score(&g->earned, &g->points);
}

// Positions to scroll through: every game, and at least ROW_POSITIONS with empty squares.
static int RowPositions(void)
{
    return s_gameCount > ROW_POSITIONS ? s_gameCount : ROW_POSITIONS;
}

static int TileSize(int position)
{
    return TILE_SMALL + (TILE_BIG - TILE_SMALL) * s_tileScale[position] / 256;
}

// Moves value toward target by at most step.
static int Approach(int value, int target, int step)
{
    if (value < target)
        return value + step < target ? value + step : target;
    return value - step > target ? value - step : target;
}

// Eases the tile sizes and the row's scroll toward the selection. True while still moving.
static bool AnimateRow(int selected, int milliseconds)
{
    static int cameraTarget;
    bool moving = false;
    int  step = milliseconds * 256 / TILE_GROW_MS;

    if (step < 1)
        step = 1;
    for (int i = 0; i < RowPositions(); i++)
    {
        int target = i == selected ? 256 : 0;
        if (s_tileScale[i] != target)
        {
            s_tileScale[i] = Approach(s_tileScale[i], target, step);
            moving = true;
        }
    }

    // The row only scrolls when the selection would leave the screen, measured with
    // the tiles at their final sizes.
    int left = selected * (TILE_SMALL + TILE_GAP);
    int right = left + TILE_BIG;
    if (left < cameraTarget)
        cameraTarget = left;
    if (right - cameraTarget > TOP_WIDTH - 2 * TILE_LEFT)
        cameraTarget = right - (TOP_WIDTH - 2 * TILE_LEFT);

    int difference = cameraTarget * 16 - s_camera;
    if (difference != 0)
    {
        int move = difference * milliseconds / CAMERA_EASE_MS;
        if (move == 0)
            move = difference > 0 ? 1 : -1;
        s_camera = Approach(s_camera, cameraTarget * 16, move > 0 ? move : -move);
        moving = true;
    }
    return moving;
}

// A setting's hint ending in its state, "on" in green or "off" in red, centred in a pill
// wide enough for "off" so nothing moves when it changes.
static int SettingWidth(const char *label)
{
    return Draw_TextWidth(label, 1) + Draw_TextWidth("off", 1);
}

static void DrawSetting(int x, int width, const char *label, bool on)
{
    const char *state = on ? "on" : "off";
    x += (width - Draw_TextWidth(label, 1) - Draw_TextWidth(state, 1)) / 2;
    Draw_TextShadow(GFX_TOP, x, PILL_Y + 4, 1, HINT, label, 0);
    Draw_TextShadow(GFX_TOP, x + Draw_TextWidth(label, 1), PILL_Y + 4, 1, on ? GREEN : RED, state, 0);
}

// The top screen's last line: each button hint in its own dark pill, centred. "Y: Share"
// only when a game is selected. (START still exits; it has no hint.)
static void DrawHints(bool canShare)
{
    static const char patched[] = "X: Patched games ", tracker[] = "SELECT: Tracker ", share[] = "Y: Share";
    int w0 = SettingWidth(patched) + 2 * PILL_PAD, w1 = SettingWidth(tracker) + 2 * PILL_PAD;
    int w2 = canShare ? Draw_TextWidth(share, 1) + 2 * PILL_PAD : 0;
    int x = (TOP_WIDTH - (w0 + w1 + PILL_GAP + (canShare ? w2 + PILL_GAP : 0))) / 2;

    Draw_RoundRect(GFX_TOP, x, PILL_Y, w0, PILL_H, PILL_H / 2, PILL);
    DrawSetting(x + PILL_PAD, w0 - 2 * PILL_PAD, patched, s_patchedAllowed);
    x += w0 + PILL_GAP;
    Draw_RoundRect(GFX_TOP, x, PILL_Y, w1, PILL_H, PILL_H / 2, PILL);
    DrawSetting(x + PILL_PAD, w1 - 2 * PILL_PAD, tracker, s_trackerRunning);
    x += w1 + PILL_GAP;
    if (canShare)
    {
        Draw_RoundRect(GFX_TOP, x, PILL_Y, w2, PILL_H, PILL_H / 2, PILL);
        Draw_TextShadow(GFX_TOP, x + PILL_PAD, PILL_Y + 4, 1, HINT, share, 0);
    }
}

static void DrawTop(int selected)
{
    DrawHints(selected < s_gameCount);

    if (s_gameCount == 0)
    {
        Draw_TextShadow(GFX_TOP, MARGIN, MARGIN, 1, WHITE, "No games with achievements found.", 0);
        Draw_TextShadow(GFX_TOP, MARGIN, MARGIN + 16, 1, GREY, "Definitions go in /achievements/<TitleID>/definitions.txt", 0);
        return;
    }

    // Where the selected tile is, so its shadow goes down before any tile.
    int x = TILE_LEFT - s_camera / 16;
    int selectedX = x, selectedY = 0, selectedSize = 0;
    for (int i = 0; i < RowPositions() && x < TOP_WIDTH; i++)
    {
        int size = TileSize(i);
        if (i == selected)
            selectedX = x, selectedY = TILE_MIDDLE - size / 2, selectedSize = size;
        x += size + TILE_GAP;
    }
    if (selectedSize > 0)
        Draw_Shadow(GFX_TOP, selectedX - 2, selectedY - 2, selectedSize + 4, selectedSize + 4,
                    TILE_RADIUS(selectedSize) + 2, SHADOW_BLUR, SHADOW_DROP, SHADOW_STRENGTH);

    // The tiles: the others a little darker, so the selected one stands out. Slots past the
    // last game are faint outlines.
    x = TILE_LEFT - s_camera / 16;
    for (int i = 0; i < RowPositions() && x < TOP_WIDTH; i++)
    {
        int size = TileSize(i), radius = TILE_RADIUS(size);
        int y = TILE_MIDDLE - size / 2;

        if (x + size + 2 > 0)
        {
            if (i < s_gameCount)
            {
                if (i == selected)
                    Draw_RoundRect(GFX_TOP, x - 2, y - 2, size + 4, size + 4, radius + 2, ACCENT);
                DrawTile(&s_games[i], x, y, size);
            }
            else
            {
                if (i == selected)
                    Draw_RoundRing(GFX_TOP, x - 2, y - 2, size + 4, size + 4, radius + 2, 2, ACCENT, 16);
                Draw_RoundRing(GFX_TOP, x, y, size, size, radius, 2, WHITE, SLOT_ALPHA);
            }
            // Dimmed as it shrinks away from being selected, so the change eases.
            if (i < s_gameCount)
                Draw_Darken(GFX_TOP, x, y, size, size, radius, TILE_DIM * (256 - s_tileScale[i]) / 256);
        }
        x += size + TILE_GAP;
    }

    // The light running around the selected frame, once the tiles next to it are drawn (its
    // glow reaches toward them).
    if (selectedSize > 0)
        Draw_FrameShine(GFX_TOP, selectedX - 2, selectedY - 2, selectedSize + 4, selectedSize + 4,
                        TILE_RADIUS(selectedSize) + 2, 2, ACCENT, SHINE);

    // The selected game's name under its tile, kept on the screen.
    if (selected < s_gameCount)
    {
        char name[72];
        Ellipsize(name, sizeof(name), s_games[selected].title, TOP_WIDTH - TILE_LEFT - MARGIN);
        int nameX = selectedX;
        if (nameX + Draw_TextWidth(name, 1) > TOP_WIDTH - MARGIN)
            nameX = TOP_WIDTH - MARGIN - Draw_TextWidth(name, 1);
        if (nameX < TILE_LEFT)
            nameX = TILE_LEFT;
        Draw_TextShadow(GFX_TOP, nameX, NAME_Y, 1, WHITE, name, 0);
    }
}

// One achievement's box: trophy | name, points / description. The selected one has a
// white outline.
static void DrawBox(int i, int y)
{
    char text[24];
    bool unlocked = s_unlocked[i];

    if (i == s_selected)
        Draw_RoundRing(GFX_BOTTOM, SELECT_X, y, SELECT_W, BOX_H, SELECT_RADIUS, SELECT_LINE, WHITE, 16);

    const u16 *icon = s_hasGameIcons ? (unlocked ? s_gameUnlocked : s_gameLocked)
                                     : (unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED);
    Draw_TrophyIcon(GFX_BOTTOM, BOX_ICON_X, y + (BOX_H - ICON_SIZE) / 2, icon);

    snprintf(text, sizeof(text), "%u pts", s_points[i]);
    int pointsX = BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
    Draw_Text(GFX_BOTTOM, pointsX, y + 6, 1, unlocked ? GOLD : GREY, text, 0);

    char name[80];
    Ellipsize(name, sizeof(name), s_names[i], pointsX - 6 - BOX_TEXT_X);
    Draw_Text(GFX_BOTTOM, BOX_TEXT_X, y + 6, 1, unlocked ? GREEN : WHITE, name, 0);

    const char *description = s_descriptions[i][0] != '\0' ? s_descriptions[i] : "No description.";
    DrawWrapped(GFX_BOTTOM, BOX_TEXT_X, y + 24, BOX_TEXT_W, LINE_H + 2, GREY, description, 2);

}

// The furthest the list can scroll, in pixels.
static int MaxScroll(void)
{
    return s_count > BOX_ROWS ? (s_count - BOX_ROWS) * BOX_H : 0;
}

// scroll is in pixels, so a drag can stop between two boxes.
static void DrawBoxes(const Game *g, int scroll, bool loaded)
{
    char text[32];

    // A faint fade behind the list. The boxes first: the one scrolling off the top slides
    // under the header.
    Draw_Gradient(GFX_BOTTOM, PROGRESS_H, SCREEN_HEIGHT, BG_TOP, BG_BOTTOM);
    int first = scroll / BOX_H;
    int y = PROGRESS_H - scroll % BOX_H;
    for (int i = first; i < s_count && y < SCREEN_HEIGHT; i++, y += BOX_H)
        DrawBox(i, y);
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, PROGRESS_H, BG_TOP);

    // Score on the right, total unlocks on the left, the progress bar between them.
    snprintf(text, sizeof(text), "%d/%d pts", g->earned, g->points);
    int right = BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
    Draw_Text(GFX_BOTTOM, right, 5, 1, GOLD, text, 0);
    snprintf(text, sizeof(text), "%d/%d unlocked", g->unlocked, g->total);
    int w = Draw_Text(GFX_BOTTOM, MARGIN, 5, 1, g->unlocked == g->total ? GREEN : WHITE, text, 0);
    DrawProgressBar(GFX_BOTTOM, MARGIN + w + 8, 5 + (LINE_H - BAR_H) / 2 + 1, right - 8 - (MARGIN + w + 8), BAR_H,
                    g->unlocked, g->total);

    if (s_count == 0)
    {
        if (loaded)
            Draw_Text(GFX_BOTTOM, MARGIN, PROGRESS_H + 8, 1, WHITE, "No achievements for this game.", 0);
        return;
    }

    // Scrollbar down the left edge, rounded.
    int track = SCREEN_HEIGHT - PROGRESS_H - 4;
    Draw_RoundRect(GFX_BOTTOM, 3, PROGRESS_H + 2, SCROLL_W, track, SCROLL_W / 2, DARK);
    if (s_count > BOX_ROWS)
        DrawScrollbar(GFX_BOTTOM, 3, PROGRESS_H + 2, track, BOX_ROWS * BOX_H, scroll, s_count * BOX_H);
    else
        Draw_RoundRect(GFX_BOTTOM, 3, PROGRESS_H + 2, SCROLL_W, track, SCROLL_W / 2, SCROLLBAR);
}

// The scroll (in whole boxes) that keeps the selected box on the screen, starting from scroll.
static int ScrollToShow(int scroll)
{
    if (s_selected * BOX_H < scroll)
        return s_selected * BOX_H;
    if ((s_selected + 1) * BOX_H > scroll + LIST_H)
        return (s_selected + 1) * BOX_H - LIST_H;
    return scroll;
}

// Achievement i in full, for names and descriptions too long for its box: its trophy,
// whether it's unlocked and its points, then the whole name and description (laid out as in
// the game's list, menu.c).
static void DrawDetails(int i)
{
    char text[32];
    bool unlocked = s_unlocked[i];
    const int x = DETAIL_MARGIN, width = BOTTOM_WIDTH - 2 * DETAIL_MARGIN;

    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, PROGRESS_H, BG_TOP);      // the same background as the list
    Draw_Gradient(GFX_BOTTOM, PROGRESS_H, SCREEN_HEIGHT, BG_TOP, BG_BOTTOM);

    const u16 *icon = s_hasGameIcons ? (unlocked ? s_gameUnlocked : s_gameLocked)
                                     : (unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED);
    Draw_TrophyIcon(GFX_BOTTOM, x, DETAIL_MARGIN, icon);
    int textX = x + ICON_SIZE + 10;
    Draw_Text(GFX_BOTTOM, textX, DETAIL_MARGIN + 4, 1, unlocked ? GREEN : GREY, unlocked ? "Unlocked" : "Locked", 0);
    snprintf(text, sizeof(text), "%u pts", s_points[i]);
    Draw_Text(GFX_BOTTOM, textX, DETAIL_MARGIN + 22, 1, unlocked ? GOLD : GREY, text, 0);
    snprintf(text, sizeof(text), "%d of %d", i + 1, s_count);
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - DETAIL_MARGIN - Draw_TextWidth(text, 1), DETAIL_MARGIN + 4, 1, GREY, text, 0);

    int y = DrawWrappedScaled(GFX_BOTTOM, x, DETAIL_MARGIN + ICON_SIZE + 10, width, DETAIL_NAME_LINE, 1,
                              unlocked ? GREEN : WHITE, s_names[i], 3);
    const char *description = s_descriptions[i][0] != '\0' ? s_descriptions[i] : "No description.";
    y += 4;
    DrawWrapped(GFX_BOTTOM, x, y, width, DETAIL_LINE, LIGHT, description, (DETAIL_HINT_Y - 4 - y) / DETAIL_LINE);

    Draw_Text(GFX_BOTTOM, x, DETAIL_HINT_Y, 1, HINT, "B: Back", 0);
    const char *other = "Up/Down: Previous / Next";
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - DETAIL_MARGIN - Draw_TextWidth(other, 1), DETAIL_HINT_Y, 1, HINT, other, 0);
}

// A message over the top screen, when the tracker couldn't be started or stopped.
static void DrawPopup(void)
{
    static const Color PANEL = { 22, 22, 28 };
    const bool small = s_popup == POPUP_UPDATE || s_popup == POPUP_DOWNLOADING;
    const int w = 376, h = small ? 100 : 150;
    const int x = (TOP_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    const char *title, *button = "A: OK";
    char text[48];

    switch (s_popup)
    {
        case POPUP_START_FAILED:    title = "Couldn't start the tracker"; break;
        case POPUP_STOP_FAILED:     title = "Couldn't stop the tracker"; break;
        case POPUP_UPDATE:          title = "New achievements"; button = "A: Download   B: Later"; break;
        case POPUP_DOWNLOADING:     title = "Downloading"; button = ""; break;
        default:                    title = "Download failed"; break;
    }

    Draw_RoundRect(GFX_TOP, x - 2, y - 2, w + 4, h + 4, 12, small ? ACCENT : RED);
    Draw_RoundRect(GFX_TOP, x, y, w, h, 10, PANEL);
    Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(title, 2)) / 2, y + 12, 2, WHITE, title, 0);

    if (s_popup == POPUP_UPDATE)
    {
        if (s_newGames > 0 && s_updatedGames > 0)
            snprintf(text, sizeof(text), "%d new game%s, %d updated", s_newGames, s_newGames == 1 ? "" : "s", s_updatedGames);
        else if (s_newGames > 0)
            snprintf(text, sizeof(text), "%d new game%s", s_newGames, s_newGames == 1 ? "" : "s");
        else
            snprintf(text, sizeof(text), "%d game%s updated", s_updatedGames, s_updatedGames == 1 ? "" : "s");
        Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(text, 1)) / 2, y + 44, 1, GREY, text, 0);
    }
    else if (s_popup == POPUP_DOWNLOADING)
    {
        snprintf(text, sizeof(text), "File %d of %d", s_downloadDone + 1, s_downloadTotal);
        Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(text, 1)) / 2, y + 44, 1, GREY, text, 0);
        DrawProgressBar(GFX_TOP, x + 24, y + 66, w - 48, 6, s_downloadDone, s_downloadTotal);
    }
    else
    {
        snprintf(text, sizeof(text), "Error 0x%08lX", s_popupResult);
        Draw_Text(GFX_TOP, x + 12, y + 44, 1, GREY, text, 0);
    }

    if (s_popup == POPUP_START_FAILED)
    {
        Draw_Text(GFX_TOP, x + 12, y + 64, 1, WHITE, "Check that:", 0);
        Draw_Text(GFX_TOP, x + 12, y + 78, 1, GREY, "- /luma/sysmodules/0004013000AC4102.cxi is on the SD", 0);
        Draw_Text(GFX_TOP, x + 12, y + 92, 1, GREY, "- Luma's config has \"Enable loading external FIRMs", 0);
        Draw_Text(GFX_TOP, x + 12, y + 106, 1, GREY, "  and modules\" on", 0);
    }
    else if (s_popup == POPUP_UPDATE_FAILED)
    {
        Draw_Text(GFX_TOP, x + 12, y + 64, 1, WHITE, "Your achievements and progress are unchanged.", 0);
        Draw_Text(GFX_TOP, x + 12, y + 78, 1, GREY, "It will offer the download again next time you open", 0);
        Draw_Text(GFX_TOP, x + 12, y + 92, 1, GREY, "the app. Check your internet connection.", 0);
    }
    Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(button, 1)) / 2, y + h - 18, 1, HINT, button, 0);
}

// Shown while files download; nothing else on the screen is changing.
static void ShowDownloadProgress(int done, int total)
{
    s_popup = POPUP_DOWNLOADING;
    s_downloadDone = done;
    s_downloadTotal = total;
    Draw_Begin(true, true);
    DrawPopup();
    Draw_End();
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

int main(void)
{
    Draw_Init();

    // A loading frame, while the files and icons are read.
    for (int i = 0; i < 2; i++)
    {
        Draw_Begin(true, true);
        Draw_Text(GFX_TOP, MARGIN, MARGIN, 1, WHITE, "Loading...", 0);
        Draw_End();
    }
    bool wanted = !Tracker_TurnedOff();
    Result startResult = wanted ? Tracker_Start() : 0;
    s_gameCount = Games_Scan(s_games, MAX_GAMES);
    s_trackerRunning = Tracker_Running();
    s_patchedAllowed = Tracker_PatchedAllowed();
    if (wanted && !s_trackerRunning)
    {
        s_popup = POPUP_START_FAILED;
        s_popupResult = startResult;
    }

    Update_Start();

    s_tileScale[0] = 256;
    int     game = 0;
    int     scroll = 0;             // how far the list is scrolled, in pixels
    int     touchY = 0;             // where the current drag started
    int     touchScroll = 0;        // and how far the list was scrolled then
    bool    touchActive = false;    // a touch that began on the list is still down
    bool    touchMoved = false;     // and it has moved: a drag, not a tap
    bool    details = false;        // the selected achievement is shown in full
    int     bottomFade = 256;       // the bottom screen fading in (0-256): a list loading, the full view
    int     redrawBottom = 0;       // like redraw, for changes to the list alone
    u64     repeatAt = 0;
    int     redraw = 2;             // both buffers of each screen
    int     heroFor = -1;           // the game s_hero was last loaded for
    u64     heroAt = 0;             // when to load the selected game's background
    int     heroLevel = 0;          // how far the background has faded in, 0-256
    bool    sharing = false;        // the share screen is open
    u64     lastFrame = osGetTime();

    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
        u32 dir = Direction(down, held, &repeatAt);

        if (down & KEY_START)
            break;

        if (s_popup == POPUP_NONE && !sharing && Update_Poll(&s_newGames, &s_updatedGames))
        {
            s_popup = POPUP_UPDATE;
            redraw = 2;
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
        else if (s_popup == POPUP_UPDATE)
        {
            if (down & KEY_A)
            {
                Result res = Update_Download(ShowDownloadProgress);
                s_popup = R_FAILED(res) ? POPUP_UPDATE_FAILED : POPUP_NONE;
                s_popupResult = res;

                // Whatever arrived shows up right away.
                s_gameCount = Games_Scan(s_games, MAX_GAMES);
                game = 0;
                memset(s_tileScale, 0, sizeof(s_tileScale));
                s_tileScale[0] = 256;
                s_camera = 0;
                s_count = 0;
                s_heroReady = false;
                heroFor = -1;
                heroAt = 0;
                scroll = 0;
                s_selected = 0;
                details = false;
                redraw = 2;
            }
            else if (down & KEY_B)
            {
                s_popup = POPUP_NONE;
                redraw = 2;
            }
        }
        else if (s_popup != POPUP_NONE)
        {
            if (down & (KEY_A | KEY_B))
            {
                s_popup = POPUP_NONE;
                redraw = 2;
            }
        }
        else
        {
            if (down & KEY_SELECT)
            {
                bool on = !s_trackerRunning;
                Result res = Tracker_SetEnabled(on);
                s_trackerRunning = Tracker_Running();
                if (s_trackerRunning != on)
                {
                    s_popup = on ? POPUP_START_FAILED : POPUP_STOP_FAILED;
                    s_popupResult = res;
                }
                redraw = 2;
            }

            if (down & KEY_X)
            {
                Tracker_SetPatchedAllowed(!s_patchedAllowed);
                s_patchedAllowed = Tracker_PatchedAllowed();    // what was actually saved
                redraw = 2;
            }

            // Share: only once the selected game's achievements have loaded.
            if ((down & KEY_Y) && game < s_gameCount && heroFor == game)
            {
                Draw_Begin(true, true);
                if (heroLevel > 0)
                    Draw_Hero(s_hero, heroLevel);
                DrawTop(game);
                Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth("Making the image...", 1)) / 2,
                          (SCREEN_HEIGHT - LINE_H) / 2, 1, WHITE, "Making the image...", 0);
                Draw_End();
                const u16 *trophy = s_hasGameIcons ? s_gameUnlocked : TROPHY_UNLOCKED;
                sharing = Share_Open(&s_games[game], s_heroReady ? s_hero : NULL, trophy);
                redraw = 2;
            }

            if (s_gameCount > 0 && details)
            {
                // One achievement in full: B or a tap goes back to the list, Up/Down show the
                // one before or after (the list follows, so it's on screen when you go back).
                int was = s_selected;
                if (dir & KEY_UP)
                    s_selected--;
                if (dir & KEY_DOWN)
                    s_selected++;
                s_selected = Clamp(s_selected, 0, s_count - 1);
                if (s_selected != was)
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
            else if (s_gameCount > 0)
            {
                // A shows the selected achievement in full.
                if ((down & KEY_A) && s_count > 0 && heroFor == game)
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
                    s_count = 0;                // the list loads once the selection settles
                    heroFor = -1;
                    heroAt = osGetTime() + HERO_DELAY_MS;
                    scroll = 0;
                    s_selected = 0;
                    redraw = 2;
                }

                // Up/Down move the selection one box, L/R a page. The list scrolls (by whole
                // boxes) only to keep the selected box on the screen.
                int move = 0;
                if (dir & KEY_UP)       move = -1;
                if (dir & KEY_DOWN)     move = 1;
                if (down & KEY_L)       move = -BOX_ROWS;
                if (down & KEY_R)       move = BOX_ROWS;
                int top = scroll;
                if (move != 0 && s_count > 0)
                {
                    int was = s_selected;
                    int firstShown = (scroll + BOX_H - 1) / BOX_H, lastShown = (scroll + LIST_H) / BOX_H - 1;
                    bool page = move == BOX_ROWS || move == -BOX_ROWS;
                    if (page)
                        top = ((scroll + BOX_H / 2) / BOX_H + move) * BOX_H;
                    if (!page && s_selected < firstShown)
                        s_selected = firstShown;        // dragged off the screen: start from what's shown
                    else if (!page && s_selected > lastShown)
                        s_selected = lastShown;
                    else
                        s_selected += move;
                    s_selected = Clamp(s_selected, 0, s_count - 1);
                    top = ScrollToShow(top);
                    if (s_selected != was)
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
                    int i = (scroll + touchY - PROGRESS_H) / BOX_H;
                    if (!touchMoved && touchY >= PROGRESS_H && i < s_count && heroFor == game)
                    {
                        s_selected = i;
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
        if (s_gameCount > 0 && heroFor != game && osGetTime() >= heroAt && heroLevel == 0)
        {
            heroFor = game;
            s_heroReady = false;
            if (game < s_gameCount)         // the empty positions have nothing to load
            {
                LoadGame(game);             // reads the progress file again, so it's current
                s_heroReady = Games_LoadHero(s_games[game].titleId, s_hero);
                bottomFade = 0;             // its list fades in
            }
            redraw = 2;
        }

        // Everything that eases: the row and the background.
        {
            u64 now = osGetTime();
            int milliseconds = (int)(now - lastFrame);
            lastFrame = now;
            if (milliseconds > 100)
                milliseconds = 100;         // after a pause, don't jump

            if (s_gameCount > 0 && AnimateRow(game, milliseconds))
                redraw = 2;

            int target = s_heroReady && heroFor == game ? 256 : 0;
            if (heroLevel != target)
            {
                int step = milliseconds * 256 / (target != 0 ? HERO_FADE_MS : HERO_OUT_MS);
                heroLevel = Approach(heroLevel, target, step > 0 ? step : 1);
                redraw = 2;
            }

            if (bottomFade < 256)
            {
                int step = milliseconds * 256 / BOTTOM_FADE_MS;
                bottomFade = Approach(bottomFade, 256, step > 0 ? step : 1);
                redrawBottom = 2;
            }
        }

        // The light around the selected game's frame moves all the time. In a frame where
        // nothing else changed, only it is redrawn.
        // The share screen stays still: it shows the image as it's shared.
        bool shine = !sharing && s_gameCount > 0 && s_popup == POPUP_NONE;
        if (redraw > 0 || redrawBottom > 0 || shine)
        {
            bool top = redraw > 0;
            bool bottom = redraw > 0 || redrawBottom > 0;   // dragging the list leaves the top screen alone

            Draw_Begin(top, bottom);
            if (sharing)
            {
                if (top)
                    Share_DrawTop();
                if (bottom)
                    Share_DrawBottom();
            }
            else
            {
                if (top && heroLevel > 0)
                    Draw_Hero(s_hero, heroLevel);
                if (top)
                    DrawTop(game);
                if (bottom && game < s_gameCount && details && s_count > 0)
                    DrawDetails(s_selected);
                else if (bottom && game < s_gameCount)
                    DrawBoxes(&s_games[game], scroll, heroFor == game);
                if (bottom && game < s_gameCount && bottomFade < 256)
                    Draw_FadeToGradient(GFX_BOTTOM, 0, SCREEN_HEIGHT, BG_TOP, BG_BOTTOM, bottomFade);
                if (top && s_popup != POPUP_NONE)
                    DrawPopup();
            }
            if (!top && shine)
                Draw_FrameShineAgain();
            Draw_End();
            if (redraw > 0)
                redraw--;
            if (redrawBottom > 0)
                redrawBottom--;
        }
        else
            gspWaitForVBlank();
    }

    if (sharing)
        Share_Close();
    Update_Stop();
    Draw_Exit();
    return 0;
}
