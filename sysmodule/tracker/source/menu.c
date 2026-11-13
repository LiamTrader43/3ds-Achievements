#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "overlay.h"
#include "../../../app/source/trophy.h"

// Luma3DS kernel extension (csvc.s).
Result svcCustomBackdoor(void *func, ...);
Result svcControlProcess(Handle process, u32 op, u32 varg2, u32 varg3);

#define PROCESSOP_SCHEDULE_THREADS  5       // lock (1) / unlock (0) all of a process's threads

// Buttons, active low, through Luma's global physical mapping (as Rosalina's
// menu.h). Only read in kernel mode here.
#define HID_PAD             (*(vu32 *)(0x10146000 | (1u << 31)))

#define HOTKEY              (KEY_R | KEY_A | KEY_DLEFT)
#define CLOSE_KEYS          (KEY_B | KEY_SELECT)
#define DIRECTION_KEYS      (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_L | KEY_R)
#define TICK_NS             (16 * 1000 * 1000LL)
#define REDRAW_MS           100     // also catch a swap the game queued before it was paused

// Layout, the same as the app's bottom screen.
#define WIDTH               320
#define HEIGHT              240
#define MARGIN              8
#define PROGRESS_H          20
#define BOX_H               55
#define BOX_ROWS            ((HEIGHT - PROGRESS_H) / BOX_H)
#define BOX_LEFT            10                      // right of the scrollbar
#define BOX_ICON_X          (BOX_LEFT + 4)
#define BOX_TEXT_X          (BOX_ICON_X + OVERLAY_ICON_SIZE + 8)
#define BOX_TEXT_W          (WIDTH - MARGIN - BOX_TEXT_X)

#define LIST_H              (HEIGHT - PROGRESS_H)   // the boxes' part of the screen
#define SELECT_X            (BOX_LEFT - 2)          // the selected box's outline
#define SELECT_W            (WIDTH - 4 - SELECT_X)
#define SELECT_LINE         2
#define SELECT_RADIUS       8
#define TAP_SLOP            6                       // a touch moving less is a tap

// One achievement in full (A), as the app's.
#define DETAIL_MARGIN       10
#define DETAIL_NAME_LINES   3
#define DETAIL_NAME_LINE    (OVERLAY_LINE_H + 4)
#define DETAIL_LINES        10
#define DETAIL_LINE         (OVERLAY_LINE_H + 3)
#define DETAIL_HINT_Y       (HEIGHT - 16)

#define LINE_CHARS          112     // a line of text, 320 px even in narrow characters
#define MAX_ACHIEVEMENTS    128
#define ICON_PIXELS         (OVERLAY_ICON_SIZE * OVERLAY_ICON_SIZE)
#define ICON_FILE_BYTES     (2 * ICON_PIXELS * 2)   // unlocked, then locked

static const Rgb WHITE = { 255, 255, 255 };
static const Rgb GREEN = { 50, 205, 50 };           // LimeGreen, unlocked
static const Rgb GOLD = { 255, 196, 40 };
static const Rgb GREY = { 140, 140, 150 };
static const Rgb LIGHT = { 205, 205, 212 };         // longer text, easier to read than GREY
static const Rgb HINT = { 255, 209, 102 };
static const Rgb DARK = { 50, 50, 58 };
static const Rgb SCROLLBAR = { 110, 110, 120 };

// The game's own achievement icons (/achievements/<TitleID>/trophy.bin), if it has any.
static u16  s_gameIcons[2 * ICON_PIXELS + 1];       // one spare: the file reader ends with a '\0'
static bool s_hasGameIcons;

static void ReadButtonsKernel(u32 *out)
{
    *out = ~HID_PAD & 0xFFF;
}

static u32 Buttons(void)
{
    u32 keys = 0;
    svcCustomBackdoor(ReadButtonsKernel, &keys);
    return keys;
}

bool Menu_HotkeyHeld(void)
{
    return (Buttons() & HOTKEY) == HOTKEY;
}

bool Menu_GameInFront(u32 *active)
{
    static const char *const names[] = { "APT:A", "APT:U", "APT:S" };
    Handle apt = 0;

    *active = 0;
    srvSetBlockingPolicy(true);     // fail rather than wait when a port is full
    for (u32 i = 0; i < 3 && apt == 0; i++)
        if (R_FAILED(srvGetServiceHandle(&apt, names[i])))
            apt = 0;
    srvSetBlockingPolicy(false);
    if (apt == 0)
        return true;

    // GetAppletManInfo(APTPOS_NONE): [5] is the active applet.
    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(0x5, 1, 0);
    cmd[1] = (u32)APTPOS_NONE;
    Result res = svcSendSyncRequest(apt);
    if (R_SUCCEEDED(res))
        res = (Result)cmd[1];
    if (R_SUCCEEDED(res))
        *active = cmd[5];
    svcCloseHandle(apt);

    return R_FAILED(res) || *active == APPID_APPLICATION;
}

// Word-wraps text onto at most maxLines lines of width pixels, the last one ending in
// "..." if it doesn't fit (as the app's DrawWrapped). Returns the number of lines.
static int Wrap(const char *text, int width, char lines[][LINE_CHARS], int maxLines)
{
    int n = 0;

    for (int k = 0; k < maxLines; k++)
        lines[k][0] = '\0';
    for (; n < maxLines && *text != '\0'; n++)
    {
        while (*text == ' ')
            text++;
        int length = (int)strlen(text);
        int fit = Overlay_TextFit(text, width);
        if (fit > LINE_CHARS - 1)
            fit = LINE_CHARS - 1;
        int take = fit;
        if (fit < length)
        {
            while (take > 0 && text[take] != ' ')
                take--;                     // back to the end of a word
            if (take == 0)
                take = fit;                 // one long word
        }
        if (take == 0)
            break;                          // not even one character fits
        if (n == maxLines - 1 && take < length)
            Overlay_Ellipsize(lines[n], LINE_CHARS, text, width);
        else
        {
            memcpy(lines[n], text, take);
            lines[n][take] = '\0';
        }
        text += take;
    }
    return n;
}

// The trophy for achievement i: the game's own, or the app's.
static const u16 *Trophy(bool unlocked)
{
    return s_hasGameIcons ? (unlocked ? s_gameIcons : s_gameIcons + ICON_PIXELS)
                          : (unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED);
}

// Achievement i in full, for names and descriptions too long for its box: its trophy,
// whether it's unlocked and its points, then the whole name and description.
static void DrawDetails(const char **names, const char **descriptions, const u16 *points, const bool *unlocked, int count, int i)
{
    static char         pointsText[12], position[24];
    static char         nameLines[DETAIL_NAME_LINES][LINE_CHARS], lines[DETAIL_LINES][LINE_CHARS];
    static OverlayItem  items[OVERLAY_MAX_ITEMS];
    static const char   other[] = "Up/Down: Previous / Next";
    const int           x = DETAIL_MARGIN, width = WIDTH - 2 * DETAIL_MARGIN, textX = x + OVERLAY_ICON_SIZE + 10;
    bool                got = unlocked[i];
    u32                 n = 0;

    items[n++] = (OverlayItem){ x, DETAIL_MARGIN, 0, 0, WHITE, NULL, Trophy(got), 0, 0 };
    items[n++] = (OverlayItem){ textX, DETAIL_MARGIN + 4, 0, 0, got ? GREEN : GREY, got ? "Unlocked" : "Locked", NULL, 0, 0 };
    snprintf(pointsText, sizeof(pointsText), "%u pts", points[i]);
    items[n++] = (OverlayItem){ textX, DETAIL_MARGIN + 22, 0, 0, got ? GOLD : GREY, pointsText, NULL, 0, 0 };
    snprintf(position, sizeof(position), "%d of %d", i + 1, count);
    items[n++] = (OverlayItem){ WIDTH - DETAIL_MARGIN - Overlay_TextWidth(position), DETAIL_MARGIN + 4, 0, 0, GREY, position, NULL, 0, 0 };

    int y = DETAIL_MARGIN + OVERLAY_ICON_SIZE + 10;
    int nameCount = Wrap(names[i], width, nameLines, DETAIL_NAME_LINES);
    for (int k = 0; k < nameCount; k++, y += DETAIL_NAME_LINE)
        items[n++] = (OverlayItem){ x, y, 0, 0, got ? GREEN : WHITE, nameLines[k], NULL, 0, 0 };

    y += 4;
    int room = (DETAIL_HINT_Y - 4 - y) / DETAIL_LINE;
    if (room > DETAIL_LINES)
        room = DETAIL_LINES;
    int lineCount = Wrap(descriptions[i][0] != '\0' ? descriptions[i] : "No description.", width, lines, room > 0 ? room : 1);
    for (int k = 0; k < lineCount; k++, y += DETAIL_LINE)
        items[n++] = (OverlayItem){ x, y, 0, 0, LIGHT, lines[k], NULL, 0, 0 };

    items[n++] = (OverlayItem){ x, DETAIL_HINT_Y, 0, 0, HINT, "B: Back", NULL, 0, 0 };
    items[n++] = (OverlayItem){ WIDTH - DETAIL_MARGIN - Overlay_TextWidth(other), DETAIL_HINT_Y, 0, 0, HINT, other, NULL, 0, 0 };
    Overlay_DrawScreen(items, n);
}

// scroll is in pixels, so a drag can stop between two boxes. The selected box has a
// white outline.
static void Draw(const char **names, const char **descriptions, const u16 *points, const bool *unlocked, int count,
                 int scroll, int selected)
{
    enum { ROWS = BOX_ROWS + 1 };           // a partly scrolled list shows one more
    static const Rgb    BLACK = { 0, 0, 0 };
    static char         progress[24], score[24];
    static char         boxPoints[ROWS][12], boxName[ROWS][LINE_CHARS], boxLines[ROWS][2][LINE_CHARS];
    static OverlayItem  items[OVERLAY_MAX_ITEMS];
    u32                 n = 0;
    int                 done, total, earned, available;

    if (count == 0)
        items[n++] = (OverlayItem){ MARGIN, PROGRESS_H + 8, 0, 0, WHITE, "No achievements for this game.", NULL, 0, 0 };

    // One box per achievement: icon | name, points / description.
    int first = scroll / BOX_H;
    int top = PROGRESS_H - scroll % BOX_H;
    for (int row = 0; row < ROWS && first + row < count && top + row * BOX_H < HEIGHT; row++)
    {
        int  i = first + row, y = top + row * BOX_H;
        bool got = unlocked[i];

        snprintf(boxPoints[row], sizeof(boxPoints[row]), "%u pts", points[i]);
        int pointsX = WIDTH - MARGIN - Overlay_TextWidth(boxPoints[row]);
        Overlay_Ellipsize(boxName[row], LINE_CHARS, names[i], pointsX - 6 - BOX_TEXT_X);
        Wrap(descriptions[i][0] != '\0' ? descriptions[i] : "No description.", BOX_TEXT_W, boxLines[row], 2);

        if (i == selected)
            items[n++] = (OverlayItem){ SELECT_X, y, SELECT_W, BOX_H, WHITE, NULL, NULL, SELECT_LINE, SELECT_RADIUS };
        items[n++] = (OverlayItem){ BOX_ICON_X, y + (BOX_H - OVERLAY_ICON_SIZE) / 2, 0, 0, WHITE, NULL, Trophy(got), 0, 0 };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 6, 0, 0, got ? GREEN : WHITE, boxName[row], NULL, 0, 0 };
        items[n++] = (OverlayItem){ pointsX, y + 6, 0, 0, got ? GOLD : GREY, boxPoints[row], NULL, 0, 0 };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 24, 0, 0, GREY, boxLines[row][0], NULL, 0, 0 };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 24 + OVERLAY_LINE_H + 2, 0, 0, GREY, boxLines[row][1], NULL, 0, 0 };
    }

    // The header goes on last, so a box scrolling off the top slides under it. Unlocks
    // on the left, score on the right, the progress bar between them.
    items[n++] = (OverlayItem){ 0, 0, WIDTH, PROGRESS_H, BLACK, NULL, NULL, 0, 0 };
    Achievements_Progress(&done, &total);
    Achievements_Score(&earned, &available);
    snprintf(progress, sizeof(progress), "%d/%d unlocked", done, total);
    snprintf(score, sizeof(score), "%d/%d pts", earned, available);
    int left = MARGIN + Overlay_TextWidth(progress) + 8;
    int right = WIDTH - MARGIN - Overlay_TextWidth(score);
    int bar = right - 8 - left;

    items[n++] = (OverlayItem){ MARGIN, 5, 0, 0, done == total && total > 0 ? GREEN : WHITE, progress, NULL, 0, 0 };
    items[n++] = (OverlayItem){ right, 5, 0, 0, GOLD, score, NULL, 0, 0 };
    items[n++] = (OverlayItem){ left, 8, bar, 4, DARK, NULL, NULL, 0, 0 };
    if (total > 0 && done > 0)
        items[n++] = (OverlayItem){ left, 8, bar * done / total, 4, done == total ? GOLD : GREEN, NULL, NULL, 0, 0 };

    // The scrollbar down the left edge.
    u32 track = HEIGHT - PROGRESS_H;
    if (count > BOX_ROWS)
    {
        u32 thumb = track * BOX_ROWS / count;
        if (thumb < 12)
            thumb = 12;
        items[n++] = (OverlayItem){ 3, PROGRESS_H, 3, track, DARK, NULL, NULL, 0, 0 };
        items[n++] = (OverlayItem){ 3, PROGRESS_H + (track - thumb) * scroll / ((count - BOX_ROWS) * BOX_H), 3, thumb, SCROLLBAR, NULL, NULL, 0, 0 };
    }
    else
        items[n++] = (OverlayItem){ 3, PROGRESS_H, 3, track, SCROLLBAR, NULL, NULL, 0, 0 };

    Overlay_DrawScreen(items, n);
}

// Touch doesn't need ir:rst, and hidExit would shut it down for the game (New 3DS: ZL, ZR, C-Stick).
bool hidShouldUseIrrst(void)
{
    return false;
}

static Result s_touchResult;

Result Menu_TouchResult(void)
{
    return s_touchResult;
}

static void LoadGameIcons(u64 titleId)
{
    char path[64];
    u32  length = 0;

    snprintf(path, sizeof(path), "/achievements/%016llX/trophy.bin", (unsigned long long)titleId);
    s_hasGameIcons = Platform_ReadFile(path, (char *)s_gameIcons, sizeof(s_gameIcons), &length) && length == ICON_FILE_BYTES;
}

// The scroll (in whole boxes) that keeps box `selected` on the screen, starting from scroll.
static int ScrollToShow(int scroll, int selected)
{
    if (selected * BOX_H < scroll)
        return selected * BOX_H;
    if ((selected + 1) * BOX_H > scroll + LIST_H)
        return (selected + 1) * BOX_H - LIST_H;
    return scroll;
}

// Up/Down move the selection one box, Left/Right or L/R one page.
static int Direction(u32 keys)
{
    if (keys & KEY_DUP)     return -1;
    if (keys & KEY_DDOWN)   return 1;
    if (keys & KEY_DLEFT)   return -(int)BOX_ROWS;
    if (keys & KEY_DRIGHT)  return BOX_ROWS;
    if (keys & KEY_L)       return -(int)BOX_ROWS;
    if (keys & KEY_R)       return BOX_ROWS;
    return 0;
}

bool Menu_Open(Handle game, u64 titleId)
{
    static const char  *names[MAX_ACHIEVEMENTS], *descriptions[MAX_ACHIEVEMENTS];
    static u16          points[MAX_ACHIEVEMENTS];
    static bool         unlocked[MAX_ACHIEVEMENTS];

    int count = Achievements_List(names, unlocked, MAX_ACHIEVEMENTS);
    Achievements_Descriptions(descriptions, MAX_ACHIEVEMENTS);
    Achievements_Points(points, MAX_ACHIEVEMENTS);
    LoadGameIcons(titleId);
    int maxScroll = count > BOX_ROWS ? (count - BOX_ROWS) * BOX_H : 0;

    // Touch comes from the HID service; without it the buttons still work. Not
    // blocking: the service's port can be full.
    srvSetBlockingPolicy(true);
    s_touchResult = hidInit();
    bool hasTouch = R_SUCCEEDED(s_touchResult);
    srvSetBlockingPolicy(false);

    if (R_FAILED(svcControlProcess(game, PROCESSOP_SCHEDULE_THREADS, 1, 0)))
    {
        if (hasTouch)
            hidExit();
        return false;
    }

    int     scroll = 0;             // in pixels
    int     selected = 0;           // the achievement with the white outline
    bool    details = false;        // it's shown in full (A)
    bool    touching = false;       // a touch that began on the list is still down
    bool    touchMoved = false;     // and it has moved: a drag, not a tap
    int     touchY = 0, touchScroll = 0;
    bool    closeArmed = false;
    bool    redraw = true;
    u32     previous = Buttons();   // the hotkey's own Left isn't a press
    u64     repeatAt = 0;           // 0 until a direction is pressed in the menu
    u64     lastDraw = 0;
    u32     lastBuffer = 0;

    while (true)
    {
        u32 held = Buttons();
        u32 pressed = held & ~previous;
        u64 now = osGetTime();
        previous = held;

        // The hotkey that opened the menu is still held at first. In the full view, B goes
        // back to the list; otherwise B or Select closes the menu.
        if (!(held & CLOSE_KEYS))
            closeArmed = true;
        if (closeArmed && (pressed & CLOSE_KEYS))
        {
            if (!(details && (pressed & KEY_B)))
                break;
            details = false;
            redraw = true;
        }

        // A shows the selected achievement in full (the hotkey's own A isn't a press).
        if ((pressed & KEY_A) && !details && count > 0)
        {
            details = true;
            touching = false;
            redraw = true;
        }

        int move = 0;
        if (pressed & DIRECTION_KEYS)
        {
            move = Direction(pressed);
            repeatAt = now + 400;
        }
        else if ((held & DIRECTION_KEYS) && repeatAt != 0 && now >= repeatAt)
        {
            move = Direction(held);
            repeatAt = now + 80;
        }

        // In the list, Up/Down move the selection a box and the others a page, and the list
        // scrolls (by whole boxes) only to keep it on the screen. In the full view, Up/Down
        // show the achievement before or after (the list follows).
        int next = scroll;
        if (move != 0 && count > 0)
        {
            int  was = selected;
            bool page = move == (int)BOX_ROWS || move == -(int)BOX_ROWS;
            if (details)
            {
                if (!page)
                    selected += move;
            }
            else
            {
                int firstShown = (scroll + BOX_H - 1) / BOX_H, lastShown = (scroll + LIST_H) / BOX_H - 1;
                if (page)
                    next = ((scroll + BOX_H / 2) / BOX_H + move) * BOX_H;
                if (!page && selected < firstShown)
                    selected = firstShown;          // dragged off the screen: start from what's shown
                else if (!page && selected > lastShown)
                    selected = lastShown;
                else
                    selected += move;
            }
            selected = selected < 0 ? 0 : selected > count - 1 ? count - 1 : selected;
            next = ScrollToShow(next, selected);
            if (selected != was)
                redraw = true;
        }

        // Dragging on the touch screen moves the list with the stylus; a tap (a touch that
        // hardly moves) on an achievement shows it in full, and a tap there goes back.
        if (hasTouch)
        {
            hidScanInput();
            u32 down = hidKeysDown(), up = hidKeysUp();
            touchPosition touch;
            hidTouchRead(&touch);
            if (down & KEY_TOUCH)
            {
                if (details)
                {
                    details = false;
                    redraw = true;
                }
                else
                {
                    touching = true;
                    touchMoved = false;
                    touchY = touch.py;
                    touchScroll = scroll;
                }
            }
            if (touching && (hidKeysHeld() & KEY_TOUCH))
            {
                if (abs(touch.py - touchY) > TAP_SLOP)
                    touchMoved = true;
                next = touchScroll + touchY - touch.py;
            }
            if (touching && (up & KEY_TOUCH))
            {
                touching = false;
                int i = (scroll + touchY - PROGRESS_H) / BOX_H;
                if (!touchMoved && touchY >= PROGRESS_H && i < count)
                {
                    selected = i;
                    details = true;
                    redraw = true;
                }
            }
        }

        if (next < 0)
            next = 0;
        if (next > maxScroll)
            next = maxScroll;
        if (next != scroll)
        {
            scroll = next;
            redraw = true;
        }

        u32 buffer = Overlay_Buffer();
        if (redraw || buffer != lastBuffer || now - lastDraw >= REDRAW_MS)
        {
            if (details)
                DrawDetails(names, descriptions, points, unlocked, count, selected);
            else
                Draw(names, descriptions, points, unlocked, count, scroll, selected);
            redraw = false;
            lastBuffer = buffer;
            lastDraw = now;
        }
        svcSleepThread(TICK_NS);
    }

    // Keeps the buttons that closed the menu from reaching the game.
    while (Buttons() != 0)
        svcSleepThread(TICK_NS);

    if (hasTouch)
        hidExit();
    svcControlProcess(game, PROCESSOP_SCHEDULE_THREADS, 0, 0);
    return true;
}
