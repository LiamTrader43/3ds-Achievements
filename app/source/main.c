// 3DS Achievements app: starts the background tracker sysmodule when it isn't
// running yet, and shows every game with a definitions file in /achievements/ and
// its achievements, without starting the game. Reads the same files as the
// tracker; changes nothing. SELECT opens the tracker's status and log.
//
// Top screen: a grid of the games' icons. Bottom screen: the highlighted game's
// title, progress and score; after A, its progress bar and achievements, one
// box each (trophy, name, description, points; gold trophy, green name = unlocked).
#include <3ds.h>

#include <stdio.h>
#include <string.h>

#include "achievements.h"
#include "draw.h"
#include "games.h"
#include "tracker.h"
#include "trophy.h"

#define MAX_ACHIEVEMENTS    128     // the tracker's MAX_LINES

// Colours, as in the tracker's menu and banner.
static const Color TITLE = { 30, 144, 255 };        // DodgerBlue
static const Color HINT = { 30, 144, 255 };
static const Color WHITE = { 255, 255, 255 };
static const Color GREEN = { 50, 205, 50 };         // LimeGreen, unlocked
static const Color GOLD = { 255, 196, 40 };
static const Color GREY = { 140, 140, 150 };
static const Color DARK = { 50, 50, 58 };
static const Color SCROLLBAR = { 110, 110, 120 };
static const Color RED = { 235, 70, 70 };

#define MARGIN          8

// Top screen: header, icon grid, hints.
#define HEADER_H        30
#define GRID_COLS       6
#define CELL_W          64
#define CELL_H          62
#define GRID_LEFT       ((TOP_WIDTH - GRID_COLS * CELL_W) / 2)
#define GRID_TOP        36
#define GRID_ROWS       3
#define TOP_HINT_Y      (SCREEN_HEIGHT - 13)

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

static TrackerInfo  s_tracker;
static Result       s_launchResult;
static char         s_dumpMessage[64];

// Copies text cut to maxChars, ending in "..." if it was longer.
static void Ellipsize(char *out, size_t capacity, const char *text, int maxChars)
{
    int length = (int)strlen(text);
    if (maxChars >= (int)capacity)
        maxChars = (int)capacity - 1;
    if (length <= maxChars)
    {
        memcpy(out, text, length + 1);
        return;
    }
    int keep = maxChars > 3 ? maxChars - 3 : 0;
    memcpy(out, text, keep);
    memcpy(out + keep, "...", 4);
}

// Draws text word-wrapped to width pixels. Returns the y below the last line.
static int DrawWrapped(gfxScreen_t screen, int x, int y, int width, int scale, int lineHeight, Color color,
                       const char *text, int maxLines)
{
    char line[80];
    int  chars = width / (CHAR_W * scale);

    if (chars >= (int)sizeof(line))
        chars = (int)sizeof(line) - 1;
    for (int n = 0; *text != '\0' && n < maxLines; n++)
    {
        while (*text == ' ')
            text++;
        int length = (int)strlen(text);
        int take = length;
        if (length > chars)
        {
            take = chars;
            while (take > 0 && text[take] != ' ')
                take--;
            if (take == 0)
                take = chars;       // one long word
        }
        if (n == maxLines - 1 && take < length)
            Ellipsize(line, sizeof(line), text, chars);
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

static int TextWidth(const char *text, int scale)
{
    return (int)strlen(text) * CHAR_W * scale;
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
static void DrawGameIcon(gfxScreen_t screen, const Game *g, int x, int y)
{
    if (g->hasIcon)
    {
        Draw_Icon(screen, x, y, g->icon);
        return;
    }
    Draw_Rect(screen, x, y, ICON_SIZE, ICON_SIZE, DARK);
    DrawWrapped(screen, x + 3, y + 3, ICON_SIZE - 6, 1, CHAR_H + 4, WHITE, g->title, 3);
}

// Loads game i's achievements into s_* (and refreshes its counts).
static void LoadGame(int i)
{
    Game *g = &s_games[i];

    s_count = 0;
    if (!Achievements_Load(g->titleId))
        return;
    s_count = Achievements_List(s_names, s_unlocked, MAX_ACHIEVEMENTS);
    Achievements_Descriptions(s_descriptions, MAX_ACHIEVEMENTS);
    Achievements_Points(s_points, MAX_ACHIEVEMENTS);
    Achievements_Progress(&g->unlocked, &g->total);
    Achievements_Score(&g->earned, &g->points);
}

static void DrawTop(int selected, int gridScroll, const char *hints)
{
    static const char title[] = "3DS Achievements";

    Draw_Text(GFX_TOP, (TOP_WIDTH - TextWidth(title, 2)) / 2, 5, 2, TITLE, title, 0);
    Draw_Rect(GFX_TOP, 0, HEADER_H - 1, TOP_WIDTH, 1, DARK);
    {
        const char *state = s_tracker.pid != 0 ? "Tracker: on" : "Tracker: off";
        Draw_Text(GFX_TOP, TOP_WIDTH - MARGIN - TextWidth(state, 1), 10, 1, s_tracker.pid != 0 ? GREEN : RED, state, 0);
    }

    if (s_gameCount == 0)
    {
        Draw_Text(GFX_TOP, MARGIN, GRID_TOP + 8, 1, WHITE, "No games with achievements found.", 0);
        Draw_Text(GFX_TOP, MARGIN, GRID_TOP + 24, 1, GREY, "Definitions go in /achievements/<TitleID>.txt", 0);
        Draw_Text(GFX_TOP, MARGIN, TOP_HINT_Y, 1, HINT, "SELECT: Tracker   START: Exit", 0);
        return;
    }

    // The grid: icon, with a progress bar under it; a blue frame on the selection.
    for (int i = gridScroll * GRID_COLS; i < s_gameCount && i < (gridScroll + GRID_ROWS) * GRID_COLS; i++)
    {
        int cell = i - gridScroll * GRID_COLS;
        int x = GRID_LEFT + (cell % GRID_COLS) * CELL_W + (CELL_W - ICON_SIZE) / 2;
        int y = GRID_TOP + (cell / GRID_COLS) * CELL_H + 3;

        if (i == selected)
            Draw_Rect(GFX_TOP, x - 3, y - 3, ICON_SIZE + 6, ICON_SIZE + 6, TITLE);
        DrawGameIcon(GFX_TOP, &s_games[i], x, y);
        DrawProgressBar(GFX_TOP, x, y + ICON_SIZE + 5, ICON_SIZE, 3, s_games[i].unlocked, s_games[i].total);
    }
    int rows = (s_gameCount + GRID_COLS - 1) / GRID_COLS;
    DrawScrollbar(GFX_TOP, TOP_WIDTH - 5, GRID_TOP, GRID_ROWS * CELL_H, GRID_ROWS, gridScroll, rows);

    Draw_Rect(GFX_TOP, 0, TOP_HINT_Y - 5, TOP_WIDTH, 1, DARK);
    Draw_Text(GFX_TOP, MARGIN, TOP_HINT_Y, 1, HINT, hints, 0);
}

// Before a game is opened: the highlighted game's title, progress and score.
static void DrawGameInfo(int selected)
{
    static const char prompt[] = "A: View achievements";
    char text[32];

    if (s_gameCount == 0)
        return;
    const Game *g = &s_games[selected];
    int width = BOTTOM_WIDTH - 2 * MARGIN;
    int y = DrawWrapped(GFX_BOTTOM, MARGIN, MARGIN + 2, width, 2, 24, g->unlocked == g->total ? GREEN : WHITE,
                        g->title, 2);
    snprintf(text, sizeof(text), "%d/%d unlocked", g->unlocked, g->total);
    Draw_Text(GFX_BOTTOM, MARGIN, y, 1, WHITE, text, 0);
    snprintf(text, sizeof(text), "%d/%d pts", g->earned, g->points);
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - MARGIN - TextWidth(text, 1), y, 1, GOLD, text, 0);
    DrawProgressBar(GFX_BOTTOM, MARGIN, y + 14, width, 4, g->unlocked, g->total);

    Draw_Icon(GFX_BOTTOM, (BOTTOM_WIDTH - ICON_SIZE) / 2, 120, TROPHY_UNLOCKED);
    Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - TextWidth(prompt, 1)) / 2, 180, 1, HINT, prompt, 0);
}

// One achievement's box: trophy | name, points / description.
static void DrawBox(int i, int y)
{
    char text[24];
    bool unlocked = s_unlocked[i];

    Draw_Icon(GFX_BOTTOM, BOX_ICON_X, y + (BOX_H - 1 - ICON_SIZE) / 2, unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED);

    snprintf(text, sizeof(text), "%u pts", s_points[i]);
    int pointsX = BOTTOM_WIDTH - MARGIN - TextWidth(text, 1);
    Draw_Text(GFX_BOTTOM, pointsX, y + 6, 1, unlocked ? GOLD : GREY, text, 0);

    char name[80];
    Ellipsize(name, sizeof(name), s_names[i], (pointsX - 6 - BOX_TEXT_X) / CHAR_W);
    Draw_Text(GFX_BOTTOM, BOX_TEXT_X, y + 6, 1, unlocked ? GREEN : WHITE, name, 0);

    const char *description = s_descriptions[i][0] != '\0' ? s_descriptions[i] : "No description.";
    DrawWrapped(GFX_BOTTOM, BOX_TEXT_X, y + 21, BOX_TEXT_W, 1, 12, GREY, description, 2);

    Draw_Rect(GFX_BOTTOM, BOX_LEFT, y + BOX_H - 1, BOTTOM_WIDTH - BOX_LEFT, 1, DARK);
}

static void DrawBoxes(const Game *g, int scroll)
{
    char text[32];

    // Total unlocks and the progress bar.
    snprintf(text, sizeof(text), "%d/%d unlocked", g->unlocked, g->total);
    int w = Draw_Text(GFX_BOTTOM, MARGIN, 5, 1, g->unlocked == g->total ? GREEN : WHITE, text, 0);
    DrawProgressBar(GFX_BOTTOM, MARGIN + w + 8, 8, BOTTOM_WIDTH - 2 * MARGIN - w - 8, 4, g->unlocked, g->total);
    Draw_Rect(GFX_BOTTOM, 0, PROGRESS_H - 1, BOTTOM_WIDTH, 1, DARK);

    if (s_count == 0)
    {
        Draw_Text(GFX_BOTTOM, MARGIN, PROGRESS_H + 8, 1, WHITE, "No achievements for this game.", 0);
        return;
    }
    for (int row = 0; row < BOX_ROWS && scroll + row < s_count; row++)
        DrawBox(scroll + row, PROGRESS_H + row * BOX_H);

    // Scrollbar down the left edge.
    int track = SCREEN_HEIGHT - PROGRESS_H;
    Draw_Rect(GFX_BOTTOM, 3, PROGRESS_H, 3, track, DARK);
    if (s_count > BOX_ROWS)
        DrawScrollbar(GFX_BOTTOM, 3, PROGRESS_H, track, BOX_ROWS, scroll, s_count);
    else
        Draw_Rect(GFX_BOTTOM, 3, PROGRESS_H, 3, track, SCROLLBAR);
}

// Top screen of the tracker page: whether it runs, then what it reports about itself.
static void DrawTrackerStatus(void)
{
    static const char title[] = "Tracker";
    const TrackerStatus *st = &s_tracker.status;
    char text[80];
    int  y = HEADER_H + 6;
    const int line = 12;

    Draw_Text(GFX_TOP, (TOP_WIDTH - TextWidth(title, 2)) / 2, 5, 2, TITLE, title, 0);
    Draw_Rect(GFX_TOP, 0, HEADER_H - 1, TOP_WIDTH, 1, DARK);

    if (s_tracker.pid == 0)
    {
        Draw_Text(GFX_TOP, MARGIN, y, 1, RED, "Not running", 0);
        y += line * 2;
        snprintf(text, sizeof(text), "Launch result: 0x%08lX", s_launchResult);
        Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
        y += line * 2;
        Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, "Check that:", 0);
        Draw_Text(GFX_TOP, MARGIN, y + line, 1, GREY, "- /luma/sysmodules/0004013000AC4102.cxi is on the SD card", 0);
        Draw_Text(GFX_TOP, MARGIN, y + line * 2, 1, GREY, "- Luma's config has \"Enable loading external FIRMs and", 0);
        Draw_Text(GFX_TOP, MARGIN, y + line * 3, 1, GREY, "  modules\" on", 0);
    }
    else
    {
        snprintf(text, sizeof(text), "Running (process %lu)", s_tracker.pid);
        Draw_Text(GFX_TOP, MARGIN, y, 1, GREEN, text, 0);
        y += line + 4;
        if (s_tracker.hasStatus)
        {
            snprintf(text, sizeof(text), "Stage: %lu (%s)", st->stage, Tracker_StageName(st->stage));
            Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
            y += line;
            snprintf(text, sizeof(text), "Heap: %lu KB (%08lX)   Free memory: %lu KB", st->heapSize / 1024, st->heapResult,
                     st->freeMemory / 1024);
            Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
            y += line;
            snprintf(text, sizeof(text), "srv: %08lX   fs: %08lX", st->srvResult, st->fsResult);
            Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
            y += line;
            snprintf(text, sizeof(text), "Last SD open: %08lX", st->lastFileResult);
            Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
            y += line;
            snprintf(text, sizeof(text), "Polls: %lu   Game: %08lX", st->polls, st->gameTitleLow);
            Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
            y += line + 4;
        }
        else
        {
            Draw_Text(GFX_TOP, MARGIN, y, 1, GREY, "Status block not found in its memory.", 0);
            y += line + 4;
        }
        snprintf(text, sizeof(text), "Threads: %d", s_tracker.threadCount);
        Draw_Text(GFX_TOP, MARGIN, y, 1, WHITE, text, 0);
        y += line;
        for (int i = 0; i < s_tracker.threadCount && y < TOP_HINT_Y - 20; i++, y += line)
        {
            const TrackerThread *t = &s_tracker.threads[i];
            snprintf(text, sizeof(text), "  thread %lu: priority %ld, core %ld", t->id, t->priority, t->core);
            Draw_Text(GFX_TOP, MARGIN, y, 1, GREY, text, 0);
        }
    }

    if (s_dumpMessage[0] != '\0')
        Draw_Text(GFX_TOP, MARGIN, TOP_HINT_Y - 18, 1, GOLD, s_dumpMessage, 0);
    Draw_Rect(GFX_TOP, 0, TOP_HINT_Y - 5, TOP_WIDTH, 1, DARK);
    Draw_Text(GFX_TOP, MARGIN, TOP_HINT_Y, 1, HINT,
              s_tracker.pid != 0 ? "Y: Save dump   SELECT: Back   START: Exit" : "A: Start tracker   SELECT: Back   START: Exit", 0);
}

// Bottom screen of the tracker page: the end of its log, as many lines as fit.
static void DrawTrackerLog(void)
{
    enum { ROWS = 18, ROW_H = 12 };
    const char *text = s_tracker.log;
    const char *end = text + strlen(text);
    const char *p = end;
    char line[80];
    int  y = 5 + 16;

    Draw_Text(GFX_BOTTOM, MARGIN, 5, 1, TITLE, "tracker.log", 0);
    Draw_Rect(GFX_BOTTOM, 0, 18, BOTTOM_WIDTH, 1, DARK);
    if (*text == '\0')
    {
        Draw_Text(GFX_BOTTOM, MARGIN, y, 1, GREY, "No log yet.", 0);
        return;
    }

    // Back up to the start of the last ROWS lines; the very last newline ends a line.
    if (p > text && p[-1] == '\n')
        p--;
    for (int lines = 0; p > text; p--)
        if (p[-1] == '\n' && ++lines == ROWS)
            break;

    while (p < end && y + CHAR_H <= SCREEN_HEIGHT)
    {
        const char *newline = memchr(p, '\n', end - p);
        size_t length = newline != NULL ? (size_t)(newline - p) : (size_t)(end - p);
        if (length > 0 && p[length - 1] == '\r')
            length--;
        if (length >= sizeof(line))
            length = sizeof(line) - 1;
        memcpy(line, p, length);
        line[length] = '\0';
        Draw_Text(GFX_BOTTOM, MARGIN, y, 1, WHITE, line, 0);
        y += ROW_H;
        p = newline != NULL ? newline + 1 : end;
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

int main(void)
{
    Draw_Init();

    // A loading frame, while the files and icons are read.
    for (int i = 0; i < 2; i++)
    {
        Draw_Begin();
        Draw_Text(GFX_TOP, MARGIN, MARGIN, 1, WHITE, "Loading...", 0);
        Draw_End();
    }
    s_launchResult = Tracker_Start();
    s_gameCount = Games_Scan(s_games, MAX_GAMES);
    Tracker_Read(&s_tracker);

    // Straight to the tracker page if it couldn't be started.
    bool    trackerPage = s_tracker.pid == 0;
    u64     nextRead = osGetTime() + 1000;
    bool    open = false;           // the bottom screen shows the selected game's boxes
    int     game = 0, gridScroll = 0;
    int     scroll = 0;             // first box shown
    u64     repeatAt = 0;
    int     redraw = 2;             // both buffers of each screen

    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        u32 dir = Direction(down, held, &repeatAt);

        if (down & KEY_START)
            break;

        if (down & KEY_SELECT)
        {
            trackerPage = !trackerPage;
            s_dumpMessage[0] = '\0';
            nextRead = 0;
            redraw = 2;
        }

        if (trackerPage)
        {
            if ((down & KEY_A) && s_tracker.pid == 0)
            {
                s_launchResult = Tracker_Start();
                nextRead = 0;
            }
            if ((down & KEY_Y) && s_tracker.pid != 0)
            {
                u32 pages = Tracker_Dump(s_tracker.pid);
                if (pages != 0)
                    snprintf(s_dumpMessage, sizeof(s_dumpMessage), "Saved %lu pages to /achievements/tracker_dump.bin", pages);
                else
                    snprintf(s_dumpMessage, sizeof(s_dumpMessage), "Couldn't save tracker_dump.bin");
                redraw = 2;
            }
            if (osGetTime() >= nextRead)
            {
                Tracker_Read(&s_tracker);
                nextRead = osGetTime() + 1000;
                redraw = 2;
            }
        }
        else if (!open && s_gameCount > 0)
        {
            int next = game;
            if (dir & KEY_LEFT)     next--;
            if (dir & KEY_RIGHT)    next++;
            if (dir & KEY_UP)       next -= GRID_COLS;
            if (dir & KEY_DOWN)     next += GRID_COLS;
            next = Clamp(next, 0, s_gameCount - 1);
            if (next != game)
            {
                game = next;
                int row = game / GRID_COLS;
                if (row < gridScroll)
                    gridScroll = row;
                if (row >= gridScroll + GRID_ROWS)
                    gridScroll = row - GRID_ROWS + 1;
                redraw = 2;
            }
            if (down & KEY_A)
            {
                LoadGame(game);     // reads the progress file again, so it's current
                open = true;
                scroll = 0;
                redraw = 2;
            }
        }
        else if (open)
        {
            int move = 0;
            if (dir & KEY_UP)       move = -1;
            if (dir & KEY_DOWN)     move = 1;
            if (dir & KEY_LEFT)     move = -BOX_ROWS;
            if (dir & KEY_RIGHT)    move = BOX_ROWS;
            int next = Clamp(scroll + move, 0, s_count > BOX_ROWS ? s_count - BOX_ROWS : 0);
            if (next != scroll)
            {
                scroll = next;
                redraw = 2;
            }
            if (down & KEY_B)
            {
                open = false;
                redraw = 2;
            }
        }

        if (redraw > 0)
        {
            Draw_Begin();
            if (trackerPage)
            {
                DrawTrackerStatus();
                DrawTrackerLog();
            }
            else if (open)
            {
                DrawTop(game, gridScroll, "B: Back   Up/Down: Scroll   SELECT: Tracker   START: Exit");
                DrawBoxes(&s_games[game], scroll);
            }
            else
            {
                DrawTop(game, gridScroll, "A: Open   D-Pad: Select game   SELECT: Tracker   START: Exit");
                DrawGameInfo(game);
            }
            Draw_End();
            redraw--;
        }
        else
            gspWaitForVBlank();
    }

    Draw_Exit();
    return 0;
}
