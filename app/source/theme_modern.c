// The "modern" theme: dark screens. Top: a row of the games' icons, the selected one largest
// on the left in a gold frame with a light running around it, over the game's art. Bottom:
// the selected game's progress bar and achievements, one box each (trophy, name,
// description, points; gold trophy, green name = unlocked).
#include "theme.h"

#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "share.h"
#include "share_image.h"
#include "ui.h"

// Colours, as in the tracker's menu and banner.
static const Color ACCENT = { 255, 209, 102 };       // the logo's yellow
static const Color HINT = { 255, 209, 102 };
static const Color WHITE = { 255, 255, 255 };
static const Color BLACK = { 0, 0, 0 };
static const Color GREEN = { 50, 205, 50 };         // LimeGreen, unlocked
static const Color GOLD = { 255, 196, 40 };
static const Color GREY = { 140, 140, 150 };
static const Color LIGHT = { 205, 205, 212 };       // longer text, easier to read than GREY
static const Color DARK = { 50, 50, 58 };
static const Color SCROLLBAR = { 110, 110, 120 };
static const Color RED = { 235, 70, 70 };
static const Color SHINE = { 255, 255, 245 };       // the light running around the selected game's frame
static const Color PILL = { 22, 22, 28 };           // behind each button hint
#define BG_TOP_RGB      { 12, 12, 16 }  // the bottom screen: a faint fade from this
#define BG_BOTTOM_RGB   { 24, 24, 31 }  // to this
static const Color BG_TOP = BG_TOP_RGB;
static const Color BG_BOTTOM = BG_BOTTOM_RGB;

#define MARGIN          8

// Top screen: a row of game tiles across the middle, hints at the bottom.
#define TILE_LEFT       16
#define TILE_GAP        4
#define TILE_MIDDLE     120             // the row is centred on this y
#define ROW_POSITIONS   5               // positions to scroll through, even with fewer games
#define TILE_RADIUS(s)  ((s) / 8)       // corner radius of a tile of size s
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

// 3D (top screen, with the slider up): how far each layer is in front of the screen (positive)
// or behind it, as the gap between the eyes' images at full slider, in pixels. The other tiles,
// the selected tile's shadow and the hints sit on the screen (0).
#define DEPTH_HERO      -10             // the game's background, behind the screen
_Static_assert(DEPTH_HERO >= -HERO_MARGIN && DEPTH_HERO <= HERO_MARGIN, "hero.bin's margins must cover the background's move");
#define DEPTH_SELECTED  6               // the selected tile, its frame and name come forward
#define DEPTH_POPUP     8               // popups in front of everything

// Bottom screen: the opened game's unlock count and progress bar, then one box
// per achievement.
#define PROGRESS_H      20
#define BOX_H           55
#define BOX_ROWS        (LIST_H / BOX_H)
#define BOX_LEFT        10                          // right of the scrollbar
#define BOX_ICON_X      (BOX_LEFT + 4)
#define BOX_TEXT_X      (BOX_ICON_X + ICON_SIZE + 8)
#define BOX_TEXT_W      (BOTTOM_WIDTH - MARGIN - BOX_TEXT_X)
#define PROMPTS_TOP     212                                         // the button prompts' strip (and tap areas),
#define PROMPT_W        112                                         // this wide from each side
#define LIST_H          (PROMPTS_TOP - PROGRESS_H)                  // the boxes' part of the screen, above the prompts
#define SELECT_X        (BOX_LEFT - 2)                              // the selected box's outline
#define SELECT_W        (BOTTOM_WIDTH - 4 - SELECT_X)
#define SELECT_LINE     2
#define SELECT_RADIUS   8
#define BAR_H           6                                           // the progress bar, rounded
#define SCROLL_W        4                                           // the scrollbar, rounded

// The share screen's QR code.
#define QR_QUIET        4               // light modules around the code, as the standard asks
#define QR_HEIGHT       170             // most pixels the code (with its margin) may take

// Bottom screen, one achievement in full (A).
#define DETAIL_MARGIN   10
#define DETAIL_NAME_LINE (LINE_H + 4)                              // the name, the same size as in the game's list
#define DETAIL_LINE     (LINE_H + 3)
#define PROMPT_Y        (SCREEN_HEIGHT - 16)                        // the button prompts in the bottom corners

// The row: how far each position is from small (0) to big (256), and how far the row
// has scrolled, in 1/16ths of a pixel.
static int s_tileScale[MAX_GAMES + ROW_POSITIONS];
static int s_camera;

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

// A game's icon, or its title in a grey box when it isn't installed, darkened by dim (0-256),
// and in a frame (DRAW_FRAME pixels around it) when frame isn't NULL. While it's moving
// (changing size), it's drawn quickly from its largest image.
static void DrawTile(const Game *g, int x, int y, int size, int dim, const Color *frame, bool moving)
{
    int radius = TILE_RADIUS(size);
    int inset = radius / 2 > 4 ? radius / 2 : 4;    // keeps the title inside the rounded corners

    if (g->hasIcon)
    {
        Draw_IconScaled(GFX_TOP, x, y, size, radius, g->icon, dim, frame, moving ? TILE_BIG : 0);
        return;
    }
    if (frame != NULL)
        Draw_RoundRect(GFX_TOP, x - DRAW_FRAME, y - DRAW_FRAME, size + 2 * DRAW_FRAME, size + 2 * DRAW_FRAME,
                       radius + DRAW_FRAME, *frame);
    Draw_RoundRect(GFX_TOP, x, y, size, size, radius, DARK);
    Ui_DrawWrapped(GFX_TOP, x + inset, y + inset, size - 2 * inset, LINE_H + 4, WHITE, g->title, (size - 2 * inset) / (LINE_H + 4));
    Draw_Darken(GFX_TOP, x, y, size, size, radius, dim);
}

// Positions to scroll through: every game, and at least ROW_POSITIONS with empty squares.
static int RowPositions(void)
{
    return g_gameCount > ROW_POSITIONS ? g_gameCount : ROW_POSITIONS;
}

static int TileSize(int position)
{
    return TILE_SMALL + (TILE_BIG - TILE_SMALL) * s_tileScale[position] / 256;
}

static void Reset(void)
{
    memset(s_tileScale, 0, sizeof(s_tileScale));
    s_tileScale[0] = 256;
    s_camera = 0;
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
            s_tileScale[i] = Ui_Approach(s_tileScale[i], target, step);
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
        s_camera = Ui_Approach(s_camera, cameraTarget * 16, move > 0 ? move : -move);
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
    static DrawStamp pills[3];              // the pills as last drawn, for the same hints
    static int       pillsFor = -1;
    int w0 = SettingWidth(patched) + 2 * PILL_PAD, w1 = SettingWidth(tracker) + 2 * PILL_PAD;
    int w2 = canShare ? Draw_TextWidth(share, 1) + 2 * PILL_PAD : 0;
    int x = (TOP_WIDTH - (w0 + w1 + PILL_GAP + (canShare ? w2 + PILL_GAP : 0))) / 2;
    int hints = (g_patchedAllowed ? 1 : 0) | (g_trackerRunning ? 2 : 0) | (canShare ? 4 : 0);

    if (hints == pillsFor && Draw_StampAgain(&pills[0]) && Draw_StampAgain(&pills[1]) && (!canShare || Draw_StampAgain(&pills[2])))
        return;

    Draw_RoundRect(GFX_TOP, x, PILL_Y, w0, PILL_H, PILL_H / 2, PILL);
    DrawSetting(x + PILL_PAD, w0 - 2 * PILL_PAD, patched, g_patchedAllowed);
    Draw_StampKeep(&pills[0], GFX_TOP, x, PILL_Y, w0, PILL_H, PILL_H / 2, PILL);
    x += w0 + PILL_GAP;
    Draw_RoundRect(GFX_TOP, x, PILL_Y, w1, PILL_H, PILL_H / 2, PILL);
    DrawSetting(x + PILL_PAD, w1 - 2 * PILL_PAD, tracker, g_trackerRunning);
    Draw_StampKeep(&pills[1], GFX_TOP, x, PILL_Y, w1, PILL_H, PILL_H / 2, PILL);
    x += w1 + PILL_GAP;
    if (canShare)
    {
        Draw_RoundRect(GFX_TOP, x, PILL_Y, w2, PILL_H, PILL_H / 2, PILL);
        Draw_TextShadow(GFX_TOP, x + PILL_PAD, PILL_Y + 4, 1, HINT, share, 0);
        Draw_StampKeep(&pills[2], GFX_TOP, x, PILL_Y, w2, PILL_H, PILL_H / 2, PILL);
    }
    pillsFor = hints;
}

// How far forward tile i comes in 3D: the selected one fully, easing along with its size.
static int TileDepth(int i)
{
    return DEPTH_SELECTED * s_tileScale[i] / 256;
}

static void DrawTop(int selected, int heroLevel)
{
    (void)heroLevel;
    Draw_Depth(0);
    BENCH_TIME(BENCH_HINTS, DrawHints(selected < g_gameCount));

    if (g_gameCount == 0)
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
    // A game's tile and frame are solid over it; an empty slot's outline isn't.
    if (selectedSize > 0)
        BENCH_TIME(BENCH_SHADOW, Draw_Shadow(GFX_TOP, selectedX - 2, selectedY - 2, selectedSize + 4, selectedSize + 4,
                                             TILE_RADIUS(selectedSize) + 2, SHADOW_BLUR, SHADOW_DROP, SHADOW_STRENGTH,
                                             selected < g_gameCount ? TileDepth(selected) : DRAW_UNCOVERED));

    // The tiles: the others a little darker, so the selected one stands out. Slots past the
    // last game are faint outlines.
    x = TILE_LEFT - s_camera / 16;
#ifdef BENCH
    u64 tilesStart = svcGetSystemTick();
#endif
    for (int i = 0; i < RowPositions() && x < TOP_WIDTH; i++)
    {
        int size = TileSize(i), radius = TILE_RADIUS(size);
        int y = TILE_MIDDLE - size / 2;

        if (x + size + 2 > 0)
        {
            Draw_Depth(TileDepth(i));
            if (i < g_gameCount)
            {
                // Dimmed as it shrinks away from being selected, so the change eases. The
                // selected one is in a gold frame.
                bool moving = s_tileScale[i] != 0 && s_tileScale[i] != 256;   // growing or shrinking
                DrawTile(&g_games[i], x, y, size, TILE_DIM * (256 - s_tileScale[i]) / 256, i == selected ? &ACCENT : NULL, moving);
            }
            else
            {
                if (i == selected)
                    Draw_RoundRing(GFX_TOP, x - 2, y - 2, size + 4, size + 4, radius + 2, 2, ACCENT, 16);
                Draw_RoundRing(GFX_TOP, x, y, size, size, radius, 2, WHITE, SLOT_ALPHA);
            }
        }
        x += size + TILE_GAP;
    }
#ifdef BENCH
    Bench_Add(BENCH_TILES, svcGetSystemTick() - tilesStart);
#endif

    // The light running around the selected frame, once the tiles next to it are drawn (its
    // glow reaches toward them). It and the name are as far forward as the tile.
    Draw_Depth(selected < RowPositions() ? TileDepth(selected) : 0);
    if (selectedSize > 0)
        BENCH_TIME(BENCH_LIGHT, Draw_FrameShine(GFX_TOP, selectedX - 2, selectedY - 2, selectedSize + 4, selectedSize + 4,
                                                TILE_RADIUS(selectedSize) + 2, 2, ACCENT, SHINE));

    // The selected game's name under its tile, kept on the screen.
    if (selected < g_gameCount)
    {
        char name[72];
        Ui_Ellipsize(name, sizeof(name), g_games[selected].title, TOP_WIDTH - TILE_LEFT - MARGIN);
        int nameX = selectedX;
        if (nameX + Draw_TextWidth(name, 1) > TOP_WIDTH - MARGIN)
            nameX = TOP_WIDTH - MARGIN - Draw_TextWidth(name, 1);
        if (nameX < TILE_LEFT)
            nameX = TILE_LEFT;
        Draw_TextShadow(GFX_TOP, nameX, NAME_Y, 1, WHITE, name, 0);
    }
}

// The button prompts (g_tabs) in the bottom corners, "B: Back" style, yellow; white while touched.
static void DrawPrompts(void)
{
    for (int side = TAB_LEFT; side <= TAB_RIGHT; side++)
    {
        const UiTab *t = &g_tabs[side];
        if (t->key == 0)
            continue;
        char text[32];
        snprintf(text, sizeof(text), "%s: %s", t->button, t->label);
        int x = side == TAB_LEFT ? MARGIN : BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
        Draw_Text(GFX_BOTTOM, x, PROMPT_Y, 1, g_tabPressed == side ? WHITE : HINT, text, 0);
    }
}

// One achievement's box: trophy | name, points / description. The selected one has a
// white outline.
static void DrawBox(int i, int y)
{
    char text[24];
    bool unlocked = g_unlocked[i];

    if (i == g_selected)
        Draw_RoundRing(GFX_BOTTOM, SELECT_X, y, SELECT_W, BOX_H, SELECT_RADIUS, SELECT_LINE, WHITE, 16);

    Draw_TrophyIcon(GFX_BOTTOM, BOX_ICON_X, y + (BOX_H - ICON_SIZE) / 2, Ui_Trophy(unlocked));

    snprintf(text, sizeof(text), "%u pts", g_points[i]);
    int pointsX = BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
    Draw_Text(GFX_BOTTOM, pointsX, y + 6, 1, unlocked ? GOLD : GREY, text, 0);

    char name[80];
    Ui_Ellipsize(name, sizeof(name), g_names[i], pointsX - 6 - BOX_TEXT_X);
    Draw_Text(GFX_BOTTOM, BOX_TEXT_X, y + 6, 1, unlocked ? GREEN : WHITE, name, 0);

    const char *description = g_descriptions[i][0] != '\0' ? g_descriptions[i] : "No description.";
    Ui_DrawWrapped(GFX_BOTTOM, BOX_TEXT_X, y + 24, BOX_TEXT_W, LINE_H + 2, GREY, description, 2);
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
    Draw_ClipRows(PROGRESS_H, PROMPTS_TOP);
    for (int i = first; i < g_count && y < PROMPTS_TOP; i++, y += BOX_H)
        DrawBox(i, y);
    Draw_ClipRows(0, SCREEN_HEIGHT);
    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, PROGRESS_H, BG_TOP);

    // Score on the right, total unlocks on the left, the progress bar between them.
    snprintf(text, sizeof(text), "%d/%d pts", g->earned, g->points);
    int right = BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
    Draw_Text(GFX_BOTTOM, right, 5, 1, GOLD, text, 0);
    snprintf(text, sizeof(text), "%d/%d unlocked", g->unlocked, g->total);
    int w = Draw_Text(GFX_BOTTOM, MARGIN, 5, 1, g->unlocked == g->total ? GREEN : WHITE, text, 0);
    DrawProgressBar(GFX_BOTTOM, MARGIN + w + 8, 5 + (LINE_H - BAR_H) / 2 + 1, right - 8 - (MARGIN + w + 8), BAR_H,
                    g->unlocked, g->total);

    DrawPrompts();
    if (g_count == 0)
    {
        if (loaded)
            Draw_Text(GFX_BOTTOM, MARGIN, PROGRESS_H + 8, 1, WHITE, "No achievements for this game.", 0);
        return;
    }

    // Scrollbar down the left edge, rounded.
    int track = LIST_H - 4;
    Draw_RoundRect(GFX_BOTTOM, 3, PROGRESS_H + 2, SCROLL_W, track, SCROLL_W / 2, DARK);
    if (g_count > BOX_ROWS)
        DrawScrollbar(GFX_BOTTOM, 3, PROGRESS_H + 2, track, LIST_H, scroll, g_count * BOX_H);
    else
        Draw_RoundRect(GFX_BOTTOM, 3, PROGRESS_H + 2, SCROLL_W, track, SCROLL_W / 2, SCROLLBAR);
}

// Achievement i in full, for names and descriptions too long for its box: its trophy,
// whether it's unlocked and its points, then the whole name and description (laid out as in
// the game's list, menu.c).
static void DrawDetails(int i)
{
    char text[32];
    bool unlocked = g_unlocked[i];
    const int x = DETAIL_MARGIN, width = BOTTOM_WIDTH - 2 * DETAIL_MARGIN;

    Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, PROGRESS_H, BG_TOP);      // the same background as the list
    Draw_Gradient(GFX_BOTTOM, PROGRESS_H, SCREEN_HEIGHT, BG_TOP, BG_BOTTOM);

    Draw_TrophyIcon(GFX_BOTTOM, x, DETAIL_MARGIN, Ui_Trophy(unlocked));
    int textX = x + ICON_SIZE + 10;
    Draw_Text(GFX_BOTTOM, textX, DETAIL_MARGIN + 4, 1, unlocked ? GREEN : GREY, unlocked ? "Unlocked" : "Locked", 0);
    snprintf(text, sizeof(text), "%u pts", g_points[i]);
    Draw_Text(GFX_BOTTOM, textX, DETAIL_MARGIN + 22, 1, unlocked ? GOLD : GREY, text, 0);
    snprintf(text, sizeof(text), "%d of %d", i + 1, g_count);
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - DETAIL_MARGIN - Draw_TextWidth(text, 1), DETAIL_MARGIN + 4, 1, GREY, text, 0);

    int y = Ui_DrawWrappedScaled(GFX_BOTTOM, x, DETAIL_MARGIN + ICON_SIZE + 10, width, DETAIL_NAME_LINE, 1,
                                 unlocked ? GREEN : WHITE, g_names[i], 3);
    const char *description = g_descriptions[i][0] != '\0' ? g_descriptions[i] : "No description.";
    y += 4;
    Ui_DrawWrapped(GFX_BOTTOM, x, y, width, DETAIL_LINE, LIGHT, description, (PROMPT_Y - 4 - y) / DETAIL_LINE);
    DrawPrompts();
}

// A stat: its label on the left, "done/total" on the right in colour value, a bar under them
// filled that far in colour fill.
static void DrawStat(int y, const char *label, int done, int total, Color value, Color fill)
{
    char text[32];
    snprintf(text, sizeof(text), "%d/%d", done, total);
    Draw_Text(GFX_BOTTOM, MARGIN, y, 1, GREY, label, 0);
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1), y, 1, value, text, 0);
    Draw_RoundRect(GFX_BOTTOM, MARGIN, y + 18, BOTTOM_WIDTH - 2 * MARGIN, BAR_H, BAR_H / 2, DARK);
    if (total > 0 && done > 0)
    {
        int width = (BOTTOM_WIDTH - 2 * MARGIN) * done / total;
        Draw_RoundRect(GFX_BOTTOM, MARGIN, y + 18, width > BAR_H ? width : BAR_H, BAR_H, BAR_H / 2, fill);
    }
}

// While a game is being chosen: the highlighted game's stats (its unlocks and points, the
// latest unlocks), then every game's added up.
static void DrawStats(int game, bool loaded)
{
    char     text[64];
    UiTotals all;

    Draw_Gradient(GFX_BOTTOM, 0, SCREEN_HEIGHT, BG_TOP, BG_BOTTOM);
    Ui_Totals(&all);
    if (game < g_gameCount)
    {
        const Game *g = &g_games[game];
        bool complete = g->total > 0 && g->unlocked == g->total;
        Ui_Ellipsize(text, sizeof(text), g->title, BOTTOM_WIDTH - 2 * MARGIN);
        Draw_Text(GFX_BOTTOM, MARGIN, 6, 1, WHITE, text, 0);
        DrawStat(28, "Achievements", g->unlocked, g->total, complete ? GREEN : WHITE, complete ? GOLD : GREEN);
        DrawStat(60, "Points", g->earned, g->points, GOLD, GOLD);

        // The latest unlocks, newest first.
        Draw_Text(GFX_BOTTOM, MARGIN, 94, 1, GREY, "Recently unlocked", 0);
        if (!loaded)
            Draw_Text(GFX_BOTTOM, MARGIN, 114, 1, GREY, "Loading...", 0);
        else if (g_recentCount == 0)
            Draw_Text(GFX_BOTTOM, MARGIN, 114, 1, GREY, "None yet.", 0);
        for (int k = 0; loaded && k < g_recentCount; k++)
        {
            int i = g_recent[k], y = 112 + 23 * k;
            Draw_IconScaled(GFX_BOTTOM, MARGIN, y, 20, 3, Ui_Trophy(true), 0, NULL, 0);
            snprintf(text, sizeof(text), "%u pts", g_points[i]);
            int px = BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1);
            Draw_Text(GFX_BOTTOM, px, y + 3, 1, GOLD, text, 0);
            char name[80];
            Ui_Ellipsize(name, sizeof(name), g_names[i], px - 8 - (MARGIN + 28));
            Draw_Text(GFX_BOTTOM, MARGIN + 28, y + 3, 1, GREEN, name, 0);
        }
    }

    // Every game.
    snprintf(text, sizeof(text), "%d/%d unlocked", all.unlocked, all.total);
    Draw_Text(GFX_BOTTOM, MARGIN, 184, 1, GREY, "All games", 0);
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1), 184, 1, WHITE, text, 0);
    snprintf(text, sizeof(text), "%d/%d pts, %d of %d complete", all.earned, all.points, all.complete, g_gameCount);
    Draw_Text(GFX_BOTTOM, BOTTOM_WIDTH - MARGIN - Draw_TextWidth(text, 1), 200, 1, GREY, text, 0);

    DrawPrompts();
}

static void DrawBottom(int game, int scroll, bool details, bool loaded)
{
    if (g_picking)
        DrawStats(game, loaded);
    else if (game >= g_gameCount)
        Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, BLACK);
    else if (details)
        DrawDetails(g_selected);
    else
        DrawBoxes(&g_games[game], scroll, loaded);
}

// A message over the top screen: a dark panel, gold-edged for notices, red for problems.
static void DrawPopup(bool alone)
{
    static const Color PANEL = { 22, 22, 28 };
    PopupText t;
    char      text[64], button[48];

    (void)alone;
    Ui_PopupText(&t);
    const int w = 376, h = t.centred && t.emphasizeFirst ? 76 + 16 * (t.lineCount - 1) : t.problem ? 150 : 100;
    const int x = (TOP_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;

    Draw_Depth(DEPTH_POPUP);
    Draw_RoundRect(GFX_TOP, x - 2, y - 2, w + 4, h + 4, 12, t.problem ? RED : ACCENT);
    Draw_RoundRect(GFX_TOP, x, y, w, h, 10, PANEL);
    Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(t.title, 2)) / 2, y + 12, 2, WHITE, t.title, 0);

    if (t.centred)
    {
        int top = t.emphasizeFirst ? 46 : t.lineCount == 2 ? 40 : 44;
        for (int i = 0; i < t.lineCount; i++)
            Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(t.lines[i], 1)) / 2, y + top + 16 * i, 1,
                      i == 0 && t.emphasizeFirst ? WHITE : GREY, t.lines[i], 0);
        if (t.progress)
            DrawProgressBar(GFX_TOP, x + 24, y + 66, w - 48, 6, t.done, t.total);
    }
    else
    {
        snprintf(text, sizeof(text), "Error 0x%08lX", t.code);
        Draw_Text(GFX_TOP, x + 12, y + 44, 1, GREY, text, 0);
        for (int i = 0; i < t.lineCount; i++)
            Draw_Text(GFX_TOP, x + 12, y + 64 + 14 * i, 1, i == 0 ? WHITE : GREY, t.lines[i], 0);
    }

    if (t.buttonA != NULL && t.buttonB != NULL)
        snprintf(button, sizeof(button), "A: %s   B: %s", t.buttonA, t.buttonB);
    else if (t.buttonA != NULL)
        snprintf(button, sizeof(button), "A: %s", t.buttonA);
    else
        button[0] = '\0';
    Draw_Text(GFX_TOP, (TOP_WIDTH - Draw_TextWidth(button, 1)) / 2, y + h - 18, 1, HINT, button, 0);
}

// The share screen's top: the image exactly as it's shared, still (no light running around
// the icon), on black.
static void DrawShareTop(void)
{
    ShareView v;
    Share_View(&v);
    Draw_Image(GFX_TOP, (TOP_WIDTH - SHARE_PREVIEW) / 2, 0, SHARE_PREVIEW, SHARE_PREVIEW, v.preview);
}

static void Centered(int y, Color color, const char *text)
{
    Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth(text, 1)) / 2, y, 1, color, text, 0);
}

// The share screen's bottom, on black: the QR code and what to do with it, or why there isn't one.
static void DrawShareBottom(void)
{
    ShareView v;
    Share_View(&v);
    Color tone = v.tone == SHARE_GOOD ? GREEN : v.tone == SHARE_BAD ? RED : GREY;

    if (v.state == SHARE_OPENING)
    {
        Draw_Rect(GFX_BOTTOM, 0, 0, BOTTOM_WIDTH, SCREEN_HEIGHT, BLACK);
        Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth("Making the image...", 1)) / 2,
                  (SCREEN_HEIGHT - LINE_H) / 2, 1, WHITE, "Making the image...", 0);
        return;
    }
    if (v.state == SHARE_SERVING)
    {
        int total = v.qrSize + 2 * QR_QUIET;
        int module = QR_HEIGHT / total > 6 ? 6 : QR_HEIGHT / total;
        int box = total * module, x = (BOTTOM_WIDTH - box) / 2, y = 4;

        Draw_Rect(GFX_BOTTOM, x, y, box, box, WHITE);
        for (int my = 0; my < v.qrSize; my++)
            for (int mx = 0; mx < v.qrSize; mx++)
                if (Share_QrModule(mx, my))
                    Draw_Rect(GFX_BOTTOM, x + (QR_QUIET + mx) * module, y + (QR_QUIET + my) * module, module, module, BLACK);

        int ty = y + box + 5;
        Centered(ty, WHITE, "Scan with a phone on the same Wi-Fi");
        Centered(ty + 15, GREY, v.url);
        if (v.status[0] != '\0')
            Centered(ty + 34, tone, v.status);
    }
    else if (v.state == SHARE_MAKING)
    {
        Centered(100, WHITE, "Getting the image ready...");
        if (v.status[0] != '\0')
            Centered(124, tone, v.status);
    }
    else if (v.state == SHARE_NO_MEMORY)
    {
        const char *title = "Couldn't make the image";
        Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth(title, 2)) / 2, 24, 2, RED, title, 0);
        Centered(70, GREY, "There wasn't enough memory for it.");
        DrawPrompts();
        return;
    }
    else
    {
        bool failed = v.state == SHARE_SERVER_FAILED;
        const char *title = failed ? "Couldn't start sharing" : "No Wi-Fi connection";
        Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth(title, 2)) / 2, 24, 2, failed ? RED : WHITE, title, 0);
        if (failed)
            Centered(70, GREY, "See /achievements/share.log for why.");
        else
        {
            Centered(70, GREY, "Connect the 3DS to Wi-Fi to send the image");
            Centered(85, GREY, "to your phone. It's sent from the 3DS itself,");
            Centered(100, GREY, "so the phone must be on the same network.");
        }
        Centered(136, GREY, "Press A to save it to the SD card instead.");
        if (v.status[0] != '\0')
            Centered(160, tone, v.status);
    }

    DrawPrompts();
}

static void DrawLoading(void)
{
    Draw_Text(GFX_TOP, MARGIN, MARGIN, 1, WHITE, "Loading...", 0);
}

static bool TopCovers(int heroLevel)
{
    return heroLevel > 0;
}

// The game's art behind everything, in both eyes' images.
static void DrawTopBackground(int heroLevel)
{
    if (heroLevel > 0)
        BENCH_TIME(BENCH_HERO, Draw_Hero(g_heroDim, heroLevel, DEPTH_HERO));
}

static bool DrawIdle(void)
{
    return Draw_FrameShineAgain();
}

const Theme THEME_MODERN =
{
    .id = "modern",
    .name = "Modern",
    .font = DRAW_FONT_INTER,
    .listTop = PROGRESS_H,
    .listHeight = LIST_H,
    .boxHeight = BOX_H,
    .tabWidth = PROMPT_W,
    .tabTop = PROMPTS_TOP,
    .minPositions = ROW_POSITIONS,
    .gameList = true,
    .fadeTop = BG_TOP_RGB,
    .fadeBottom = BG_BOTTOM_RGB,
    .drawLoading = DrawLoading,
    .reset = Reset,
    .animate = AnimateRow,
    .topCovers = TopCovers,
    .drawTopBackground = DrawTopBackground,
    .drawTop = DrawTop,
    .drawPopup = DrawPopup,
    .drawBottom = DrawBottom,
    .drawIdle = DrawIdle,
    .drawShareTop = DrawShareTop,
    .drawShareBottom = DrawShareBottom,
};
