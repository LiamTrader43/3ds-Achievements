#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "achievements.h"
#include "overlay.h"

// Luma3DS kernel extension (csvc.s).
Result svcCustomBackdoor(void *func, ...);
Result svcControlProcess(Handle process, u32 op, u32 varg2, u32 varg3);

#define PROCESSOP_SCHEDULE_THREADS  5       // lock (1) / unlock (0) all of a process's threads

// Buttons, active low, through Luma's global physical mapping (as Rosalina's
// menu.h). Only read in kernel mode here.
#define HID_PAD             (*(vu32 *)(0x10146000 | (1u << 31)))

#define HOTKEY              (KEY_R | KEY_DDOWN | KEY_SELECT)
#define CLOSE_KEYS          (KEY_B | KEY_SELECT)
#define DIRECTION_KEYS      (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)
#define TICK_NS             (16 * 1000 * 1000LL)
#define REDRAW_MS           100     // also catch a swap the game queued before it was paused

// Layout.
#define WIDTH               320
#define HEIGHT              240
#define TITLE_Y             8
#define SUBTITLE_Y          28
#define LIST_TOP            56
#define ROW_HEIGHT          20
#define VISIBLE_ROWS        8       // leaves room for the hints line
#define HINT_Y              (HEIGHT - 20)
#define TEXT_LEFT           10
#define TEXT_WIDTH          (WIDTH - TEXT_LEFT - 14)
#define LIST_LEFT           (TEXT_LEFT + 16)    // achievements are indented under the game title
#define LIST_WIDTH          (WIDTH - LIST_LEFT - 14)

#define CHAR_W              OVERLAY_FONT_WIDTH
#define LINE_CHARS          (WIDTH / CHAR_W + 1)
#define MAX_ACHIEVEMENTS    128

static const Rgb TITLE_TEXT = { 30, 144, 255 };     // DodgerBlue
static const Rgb HINT_TEXT = { 30, 144, 255 };
static const Rgb HEADER_TEXT = { 255, 255, 255 };
static const Rgb UNLOCKED_TEXT = { 50, 205, 50 };   // LimeGreen
static const Rgb LOCKED_TEXT = { 255, 255, 255 };
static const Rgb SCROLLBAR = { 110, 110, 120 };

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

// Copies text, cut to maxChars (less than LINE_CHARS) with "..." if longer.
static void Ellipsize(char *out, const char *text, u32 maxChars)
{
    size_t length = strlen(text);
    if (length <= maxChars)
    {
        memcpy(out, text, length + 1);
        return;
    }
    u32 keep = maxChars > 3 ? maxChars - 3 : 0;
    memcpy(out, text, keep);
    memcpy(out + keep, "...", 4);
}

static void Draw(const char *gameTitle, const char **names, const bool *unlocked, int count, int scroll)
{
    static char subtitle[LINE_CHARS + 16];
    static char rows[VISIBLE_ROWS][LINE_CHARS];
    OverlayItem items[OVERLAY_MAX_ITEMS];
    u32         n = 0;

    int done = 0;
    for (int i = 0; i < count; i++)
        if (unlocked[i])
            done++;

    char progress[24];
    snprintf(progress, sizeof(progress), ": (%d/%d)", done, count);
    Ellipsize(subtitle, gameTitle, TEXT_WIDTH / CHAR_W - strlen(progress));
    strcat(subtitle, progress);

    static const char title[] = "3DS Achievements", closeHint[] = "B: Close", scrollHint[] = "Up/Down: Scroll";
    items[n++] = (OverlayItem){ (WIDTH - (sizeof(title) - 1) * CHAR_W) / 2, TITLE_Y, 0, 0, TITLE_TEXT, title };
    items[n++] = (OverlayItem){ TEXT_LEFT, SUBTITLE_Y, 0, 0, HEADER_TEXT, subtitle };
    items[n++] = (OverlayItem){ TEXT_LEFT, HINT_Y, 0, 0, HINT_TEXT, closeHint };
    items[n++] = (OverlayItem){ WIDTH - TEXT_LEFT - (sizeof(scrollHint) - 1) * CHAR_W, HINT_Y, 0, 0, HINT_TEXT, scrollHint };

    if (count == 0)
        items[n++] = (OverlayItem){ LIST_LEFT, LIST_TOP, 0, 0, LOCKED_TEXT, "No achievements for this game." };

    for (int row = 0; row < VISIBLE_ROWS && scroll + row < count; row++)
    {
        int i = scroll + row;
        Ellipsize(rows[row], names[i], LIST_WIDTH / CHAR_W);
        items[n++] = (OverlayItem){ LIST_LEFT, LIST_TOP + row * ROW_HEIGHT, 0, 0,
                                    unlocked[i] ? UNLOCKED_TEXT : LOCKED_TEXT, rows[row] };
    }

    if (count > VISIBLE_ROWS)
    {
        u32 track = VISIBLE_ROWS * ROW_HEIGHT;
        u32 thumb = track * VISIBLE_ROWS / count;
        if (thumb < 12)
            thumb = 12;
        u32 thumbY = LIST_TOP + (track - thumb) * scroll / (count - VISIBLE_ROWS);
        items[n++] = (OverlayItem){ WIDTH - 6, thumbY, 3, thumb, SCROLLBAR, NULL };
    }

    Overlay_DrawScreen(items, n);
}

// Up/Down scroll one row, Left/Right one page.
static int Direction(u32 keys)
{
    if (keys & KEY_DUP)     return -1;
    if (keys & KEY_DDOWN)   return 1;
    if (keys & KEY_DLEFT)   return -VISIBLE_ROWS;
    if (keys & KEY_DRIGHT)  return VISIBLE_ROWS;
    return 0;
}

bool Menu_Open(Handle game, const char *gameTitle)
{
    static const char  *names[MAX_ACHIEVEMENTS];
    static bool         unlocked[MAX_ACHIEVEMENTS];

    int count = Achievements_List(names, unlocked, MAX_ACHIEVEMENTS);
    int maxScroll = count > VISIBLE_ROWS ? count - VISIBLE_ROWS : 0;

    if (R_FAILED(svcControlProcess(game, PROCESSOP_SCHEDULE_THREADS, 1, 0)))
        return false;

    int     scroll = 0;
    bool    closeArmed = false;
    bool    redraw = true;
    u32     previous = Buttons();   // the hotkey's own Down isn't a press
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

        if (move != 0)
        {
            int next = scroll + move;
            if (next < 0)
                next = 0;
            if (next > maxScroll)
                next = maxScroll;
            if (next != scroll)
            {
                scroll = next;
                redraw = true;
            }
        }

        u32 buffer = Overlay_Buffer();
        if (redraw || buffer != lastBuffer || now - lastDraw >= REDRAW_MS)
        {
            Draw(gameTitle, names, unlocked, count, scroll);
            redraw = false;
            lastBuffer = buffer;
            lastDraw = now;
        }
        svcSleepThread(TICK_NS);
    }

    // Keeps the buttons that closed the menu from reaching the game.
    while (Buttons() != 0)
        svcSleepThread(TICK_NS);

    svcControlProcess(game, PROCESSOP_SCHEDULE_THREADS, 0, 0);
    return true;
}
