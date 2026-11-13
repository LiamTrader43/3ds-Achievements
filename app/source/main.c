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
static const Color DARK = { 50, 50, 58 };
static const Color SCROLLBAR = { 110, 110, 120 };
static const Color RED = { 235, 70, 70 };
static const Color BLACK = { 0, 0, 0 };

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
#define TOP_HINT_Y      (SCREEN_HEIGHT - 13)
#define NAME_Y          (TILE_MIDDLE + TILE_BIG / 2 + 8)    // the selected game's name
#define SHARE_HINT_Y    (NAME_Y + LINE_H + 4)               // and "Y: Share" under it

// Bottom screen: the opened game's unlock count and progress bar, then one box
// per achievement.
#define PROGRESS_H      20
#define BOX_H           55
#define BOX_ROWS        ((SCREEN_HEIGHT - PROGRESS_H) / BOX_H)
#define BOX_LEFT        10                          // right of the scrollbar
#define BOX_ICON_X      (BOX_LEFT + 4)
#define BOX_TEXT_X      (BOX_ICON_X + ICON_SIZE + 8)
#define BOX_TEXT_W      (BOTTOM_WIDTH - MARGIN - BOX_TEXT_X)

static Game         s_games[MAX_GAMES];
static int          s_gameCount;

// The opened game's achievements.
static const char  *s_names[MAX_ACHIEVEMENTS];
static const char  *s_descriptions[MAX_ACHIEVEMENTS];
static uint16_t     s_points[MAX_ACHIEVEMENTS];
static bool         s_unlocked[MAX_ACHIEVEMENTS];
static int          s_count;
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

// Copies text cut to maxWidth pixels, ending in "..." if it was longer.
static void Ellipsize(char *out, size_t capacity, const char *text, int maxWidth)
{
    int length = (int)strlen(text);
    if (length < (int)capacity && Draw_TextWidth(text, 1) <= maxWidth)
    {
        memcpy(out, text, length + 1);
        return;
    }
    int keep = Draw_TextFit(text, 1, maxWidth - Draw_TextWidth("...", 1));
    if (keep > (int)capacity - 4)
        keep = (int)capacity - 4;
    while (keep > 0 && text[keep - 1] == ' ')
        keep--;                     // no gap before the dots
    memcpy(out, text, keep);
    memcpy(out + keep, "...", 4);
}

// Draws text word-wrapped to width pixels, at most maxLines lines (the last one ends
// in "..." if the text goes on). Returns the y below the last line.
static int DrawWrapped(gfxScreen_t screen, int x, int y, int width, int lineHeight, Color color,
                       const char *text, int maxLines)
{
    char line[160];

    for (int n = 0; *text != '\0' && n < maxLines; n++)
    {
        while (*text == ' ')
            text++;
        int length = (int)strlen(text);
        int fit = Draw_TextFit(text, 1, width);
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
            Ellipsize(line, sizeof(line), text, width);
        else
        {
            memcpy(line, text, take);
            line[take] = '\0';
        }
        Draw_Text(screen, x, y, 1, color, line, 0);
        y += lineHeight;
        text += take;
    }
    return y;
}

static void DrawProgressBar(gfxScreen_t screen, int x, int y, int w, int h, int unlocked, int total)
{
    Draw_Rect(screen, x, y, w, h, DARK);
    if (total > 0)
        Draw_Rect(screen, x, y, w * unlocked / total, h, unlocked == total ? GOLD : GREEN);
}

static void DrawScrollbar(gfxScreen_t screen, int x, int top, int track, int visible, int scroll, int count)
{
    if (count <= visible)
        return;
    int thumb = track * visible / count;
    if (thumb < 12)
        thumb = 12;
    int y = top + (track - thumb) * scroll / (count - visible);
    Draw_Rect(screen, x, y, 3, thumb, SCROLLBAR);
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

// One button hint ending in a setting's state, "on" in green or "off" in red,
// with room for "off" either way so the hints after it don't move. Returns the next x.
static int DrawSettingHint(int x, const char *label, bool on)
{
    Draw_Text(GFX_TOP, x, TOP_HINT_Y, 1, HINT, label, 0);
    x += Draw_TextWidth(label, 1);
    Draw_Text(GFX_TOP, x, TOP_HINT_Y, 1, on ? GREEN : RED, on ? "on" : "off", 0);
    return x + Draw_TextWidth("off   ", 1);
}

// The top screen's last line, centered: the buttons, with both settings' states.
static void DrawHints(void)
{
    static const char patched[] = "X: Patched games ", tracker[] = "SELECT: Tracker ", exit[] = "START: Exit";
    int width = Draw_TextWidth(patched, 1) + Draw_TextWidth(tracker, 1) + 2 * Draw_TextWidth("off   ", 1) + Draw_TextWidth(exit, 1);

    int x = DrawSettingHint((TOP_WIDTH - width) / 2, patched, s_patchedAllowed);
    x = DrawSettingHint(x, tracker, s_trackerRunning);
    Draw_Text(GFX_TOP, x, TOP_HINT_Y, 1, HINT, exit, 0);
}

static void DrawTop(int selected)
{
    DrawHints();

    if (s_gameCount == 0)
    {
        Draw_Text(GFX_TOP, MARGIN, MARGIN, 1, WHITE, "No games with achievements found.", 0);
        Draw_Text(GFX_TOP, MARGIN, MARGIN + 16, 1, GREY, "Definitions go in /achievements/<TitleID>/definitions.txt", 0);
        return;
    }

    // Slots past the last game are empty grey squares.
    int x = TILE_LEFT - s_camera / 16;
    int selectedX = x;
    for (int i = 0; i < RowPositions() && x < TOP_WIDTH; i++)
    {
        int size = TileSize(i);
        int y = TILE_MIDDLE - size / 2;

        if (i == selected)
            selectedX = x;
        if (x + size + 2 > 0)
        {
            if (i == selected)
                Draw_RoundRect(GFX_TOP, x - 2, y - 2, size + 4, size + 4, TILE_RADIUS(size) + 2, ACCENT);
            if (i < s_gameCount)
                DrawTile(&s_games[i], x, y, size);
            else
                Draw_RoundRect(GFX_TOP, x, y, size, size, TILE_RADIUS(size), DARK);
        }
        x += size + TILE_GAP;
    }

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
        Draw_Text(GFX_TOP, nameX, NAME_Y, 1, WHITE, name, 0);
        Draw_Text(GFX_TOP, nameX, SHARE_HINT_Y, 1, HINT, "Y: Share", 0);
    }
}

// One achievement's box: trophy | name, points / description.
static void DrawBox(int i, int y)
{
    char text[24];
    bool unlocked = s_unlocked[i];

    const u16 *icon = s_hasGameIcons ? (unlocked ? s_gameUnlocked : s_gameLocked)
                                     : (unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED);
    Draw_Icon(GFX_BOTTOM, BOX_ICON_X, y + (BOX_H - ICON_SIZE) / 2, icon);

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

    // The boxes first: the one scrolling off the top slides under the header.
    int first = scroll / BOX_H;
    int y = PROGRESS_H - scroll % BOX_H;
    for (int i = first; i < s_count && y < SCREEN_HEIGHT; i++, y += BOX_H)
        DrawBox(i, y);
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, PROGRESS_H, BLACK);

    // Score on the right, total unlocks on the left, the progress bar between them.
    snprintf(text, sizeof(text), "%d/%d pts", g->earned, g->points);
    int right = BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
    Draw_Text(GFX_BOTTOM, right, 5, 1, GOLD, text, 0);
    snprintf(text, sizeof(text), "%d/%d unlocked", g->unlocked, g->total);
    int w = Draw_Text(GFX_BOTTOM, MARGIN, 5, 1, g->unlocked == g->total ? GREEN : WHITE, text, 0);
    DrawProgressBar(GFX_BOTTOM, MARGIN + w + 8, 8, right - 8 - (MARGIN + w + 8), 4, g->unlocked, g->total);

    if (s_count == 0)
    {
        if (loaded)
            Draw_Text(GFX_BOTTOM, MARGIN, PROGRESS_H + 8, 1, WHITE, "No achievements for this game.", 0);
        return;
    }

    // Scrollbar down the left edge.
    int track = SCREEN_HEIGHT - PROGRESS_H;
    Draw_Rect(GFX_BOTTOM, 3, PROGRESS_H, 3, track, DARK);
    if (s_count > BOX_ROWS)
        DrawScrollbar(GFX_BOTTOM, 3, PROGRESS_H, track, BOX_ROWS * BOX_H, scroll, s_count * BOX_H);
    else
        Draw_Rect(GFX_BOTTOM, 3, PROGRESS_H, 3, track, SCROLLBAR);
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
    Draw_Begin(true);
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
        Draw_Begin(true);
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
        u32 down = hidKeysDown(), held = hidKeysHeld();
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
                Draw_Begin(true);
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

            if (s_gameCount > 0)
            {
                int next = Clamp(game + ((dir & KEY_RIGHT) ? 1 : 0) - ((dir & KEY_LEFT) ? 1 : 0), 0, RowPositions() - 1);
                if (next != game)
                {
                    game = next;
                    s_count = 0;                // the list loads once the selection settles
                    heroFor = -1;
                    heroAt = osGetTime() + HERO_DELAY_MS;
                    scroll = 0;
                    redraw = 2;
                }

                int move = 0;
                if (dir & KEY_UP)       move = -1;
                if (dir & KEY_DOWN)     move = 1;
                if (down & KEY_L)       move = -BOX_ROWS;
                if (down & KEY_R)       move = BOX_ROWS;
                int top = scroll;
                if (move != 0)
                    top = ((scroll + BOX_H / 2) / BOX_H + move) * BOX_H;    // lands on a whole box

                // Dragging on the touch screen moves the list with the stylus.
                if (held & KEY_TOUCH)
                {
                    touchPosition touch;
                    hidTouchRead(&touch);
                    if (down & KEY_TOUCH)
                    {
                        touchY = touch.py;
                        touchScroll = scroll;
                    }
                    top = touchScroll + touchY - touch.py;
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
        }

        if (redraw > 0 || redrawBottom > 0)
        {
            bool top = redraw > 0;          // dragging the list leaves the top screen alone

            Draw_Begin(top);
            if (sharing)
            {
                Share_DrawTop();
                Share_DrawBottom();
            }
            else
            {
                if (top && heroLevel > 0)
                    Draw_Hero(s_hero, heroLevel);
                if (top)
                    DrawTop(game);
                if (game < s_gameCount)
                    DrawBoxes(&s_games[game], scroll, heroFor == game);
                if (top && s_popup != POPUP_NONE)
                    DrawPopup();
            }
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
