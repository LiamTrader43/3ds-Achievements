#include "menu.h"

#include <stdio.h>
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

#define LINE_CHARS          112     // a line of text, 320 px even in narrow characters
#define MAX_ACHIEVEMENTS    128
#define ICON_PIXELS         (OVERLAY_ICON_SIZE * OVERLAY_ICON_SIZE)
#define ICON_FILE_BYTES     (2 * ICON_PIXELS * 2)   // unlocked, then locked

static const Rgb WHITE = { 255, 255, 255 };
static const Rgb GREEN = { 50, 205, 50 };           // LimeGreen, unlocked
static const Rgb GOLD = { 255, 196, 40 };
static const Rgb GREY = { 140, 140, 150 };
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

// Word-wraps a description onto two lines of BOX_TEXT_W pixels, ending in "..." if it
// doesn't fit (as the app's DrawWrapped).
static void WrapDescription(const char *text, char lines[2][LINE_CHARS])
{
    lines[0][0] = lines[1][0] = '\0';
    for (int n = 0; n < 2 && *text != '\0'; n++)
    {
        while (*text == ' ')
            text++;
        int length = (int)strlen(text);
        int fit = Overlay_TextFit(text, BOX_TEXT_W);
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
        if (n == 1 && take < length)
            Overlay_Ellipsize(lines[n], LINE_CHARS, text, BOX_TEXT_W);
        else
        {
            memcpy(lines[n], text, take);
            lines[n][take] = '\0';
        }
        text += take;
    }
}

// scroll is in pixels, so a drag can stop between two boxes.
static void Draw(const char **names, const char **descriptions, const u16 *points, const bool *unlocked, int count, int scroll)
{
    enum { ROWS = BOX_ROWS + 1 };           // a partly scrolled list shows one more
    static const Rgb    BLACK = { 0, 0, 0 };
    static char         progress[24], score[24];
    static char         boxPoints[ROWS][12], boxName[ROWS][LINE_CHARS], boxLines[ROWS][2][LINE_CHARS];
    static OverlayItem  items[OVERLAY_MAX_ITEMS];
    u32                 n = 0;
    int                 done, total, earned, available;

    if (count == 0)
        items[n++] = (OverlayItem){ MARGIN, PROGRESS_H + 8, 0, 0, WHITE, "No achievements for this game.", NULL };

    // One box per achievement: icon | name, points / description.
    int first = scroll / BOX_H;
    int top = PROGRESS_H - scroll % BOX_H;
    for (int row = 0; row < ROWS && first + row < count && top + row * BOX_H < HEIGHT; row++)
    {
        int  i = first + row, y = top + row * BOX_H;
        bool got = unlocked[i];
        const u16 *icon = s_hasGameIcons ? (got ? s_gameIcons : s_gameIcons + ICON_PIXELS)
                                         : (got ? TROPHY_UNLOCKED : TROPHY_LOCKED);

        snprintf(boxPoints[row], sizeof(boxPoints[row]), "%u pts", points[i]);
        int pointsX = WIDTH - MARGIN - Overlay_TextWidth(boxPoints[row]);
        Overlay_Ellipsize(boxName[row], LINE_CHARS, names[i], pointsX - 6 - BOX_TEXT_X);
        WrapDescription(descriptions[i][0] != '\0' ? descriptions[i] : "No description.", boxLines[row]);

        items[n++] = (OverlayItem){ BOX_ICON_X, y + (BOX_H - OVERLAY_ICON_SIZE) / 2, 0, 0, WHITE, NULL, icon };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 6, 0, 0, got ? GREEN : WHITE, boxName[row], NULL };
        items[n++] = (OverlayItem){ pointsX, y + 6, 0, 0, got ? GOLD : GREY, boxPoints[row], NULL };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 24, 0, 0, GREY, boxLines[row][0], NULL };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 24 + OVERLAY_LINE_H + 2, 0, 0, GREY, boxLines[row][1], NULL };
    }

    // The header goes on last, so a box scrolling off the top slides under it. Unlocks
    // on the left, score on the right, the progress bar between them.
    items[n++] = (OverlayItem){ 0, 0, WIDTH, PROGRESS_H, BLACK, NULL, NULL };
    Achievements_Progress(&done, &total);
    Achievements_Score(&earned, &available);
    snprintf(progress, sizeof(progress), "%d/%d unlocked", done, total);
    snprintf(score, sizeof(score), "%d/%d pts", earned, available);
    int left = MARGIN + Overlay_TextWidth(progress) + 8;
    int right = WIDTH - MARGIN - Overlay_TextWidth(score);
    int bar = right - 8 - left;

    items[n++] = (OverlayItem){ MARGIN, 5, 0, 0, done == total && total > 0 ? GREEN : WHITE, progress, NULL };
    items[n++] = (OverlayItem){ right, 5, 0, 0, GOLD, score, NULL };
    items[n++] = (OverlayItem){ left, 8, bar, 4, DARK, NULL, NULL };
    if (total > 0 && done > 0)
        items[n++] = (OverlayItem){ left, 8, bar * done / total, 4, done == total ? GOLD : GREEN, NULL, NULL };

    // The scrollbar down the left edge.
    u32 track = HEIGHT - PROGRESS_H;
    if (count > BOX_ROWS)
    {
        u32 thumb = track * BOX_ROWS / count;
        if (thumb < 12)
            thumb = 12;
        items[n++] = (OverlayItem){ 3, PROGRESS_H, 3, track, DARK, NULL, NULL };
        items[n++] = (OverlayItem){ 3, PROGRESS_H + (track - thumb) * scroll / ((count - BOX_ROWS) * BOX_H), 3, thumb, SCROLLBAR, NULL, NULL };
    }
    else
        items[n++] = (OverlayItem){ 3, PROGRESS_H, 3, track, SCROLLBAR, NULL, NULL };

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

// Up/Down scroll one row, Left/Right or L/R one page.
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
    bool    dragging = false;
    int     dragY = 0, dragScroll = 0;
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

        // The hotkey that opened the menu is still held at first.
        if (!(held & CLOSE_KEYS))
            closeArmed = true;
        if (closeArmed && (pressed & CLOSE_KEYS))
            break;

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

        int next = scroll;
        if (move != 0)
            next = ((scroll + BOX_H / 2) / BOX_H + move) * BOX_H;   // lands on a whole box

        // Dragging on the touch screen moves the list with the stylus.
        if (hasTouch)
        {
            hidScanInput();
            if (hidKeysHeld() & KEY_TOUCH)
            {
                touchPosition touch;
                hidTouchRead(&touch);
                if (!dragging)
                {
                    dragging = true;
                    dragY = touch.py;
                    dragScroll = scroll;
                }
                next = dragScroll + dragY - touch.py;
            }
            else
                dragging = false;
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
            Draw(names, descriptions, points, unlocked, count, scroll);
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
