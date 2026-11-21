#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "icons.h"
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
#define MAX_ACHIEVEMENTS    256     // achievements.c's MAX_LINES
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
static const Rgb MISSABLE = { 255, 128, 64 };       // can be missed for good (as in the app)

static const char MISSABLE_TEXT[] = "Missable";
#define DETAIL_PART_LINES   3       // an achievement done in parts: its description gets this many lines
#define MAX_PARTS_SHOWN     24

// The game's own achievement icons (/achievements/<TitleID>/trophy.bin), if it has any.
// Only held while the menu is open.
static u16 *s_gameIcons;
static bool s_hasGameIcons;

// Each achievement's own picture (/achievements/<TitleID>/icons.bin, icons.h), if the game has
// any: where each one's are in the file, which stays open while the menu is, and the last few
// read (a screen shows at most BOX_ROWS + 1, so they're all still here while it's drawn).
#define PICTURE_SLOTS       (BOX_ROWS + 3)
typedef struct
{
    int     achievement;            // -1: empty
    bool    unlocked;
    u32     used;
    u16     pixels[ICON_PIXELS];
} PictureSlot;
static Handle       s_iconsFile;
static IconsPlace  *s_places;       // per achievement in the list; NULL without pictures
static int          s_placeCount;
static PictureSlot *s_pictures;
static u32          s_pictureClock;

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

// The trophy: the game's own, or the app's.
static const u16 *Trophy(bool unlocked)
{
    return s_hasGameIcons ? (unlocked ? s_gameIcons : s_gameIcons + ICON_PIXELS)
                          : (unlocked ? TROPHY_UNLOCKED : TROPHY_LOCKED);
}

// Achievement i's picture: its own from icons.bin (read now if it isn't one of the last few),
// else the trophy. A locked picture the file leaves out is made from the unlocked one.
static const u16 *Picture(int i, bool unlocked)
{
    if (s_places == NULL || i < 0 || i >= s_placeCount || s_places[i].unlocked == 0)
        return Trophy(unlocked);

    PictureSlot *slot = &s_pictures[0];
    for (int k = 0; k < PICTURE_SLOTS; k++)
    {
        PictureSlot *p = &s_pictures[k];
        if (p->achievement == i && p->unlocked == unlocked)
        {
            p->used = ++s_pictureClock;
            return p->pixels;
        }
        if (p->used < slot->used)
            slot = p;
    }

    u32 offset = unlocked || s_places[i].locked == 0 ? s_places[i].unlocked : s_places[i].locked;
    u32 read = 0;
    slot->achievement = -1;
    if (R_FAILED(FSFILE_Read(s_iconsFile, &read, offset, slot->pixels, ICONS_PICTURE_BYTES)) ||
        read != ICONS_PICTURE_BYTES)
    {
        s_places[i].unlocked = 0;           // the trophy from now on
        return Trophy(unlocked);
    }
    if (!unlocked && s_places[i].locked == 0)
        Icons_MakeLocked(slot->pixels, slot->pixels);
    slot->achievement = i;
    slot->unlocked = unlocked;
    slot->used = ++s_pictureClock;
    return slot->pixels;
}

// Achievement i in full, for names and descriptions too long for its box: its trophy,
// whether it's unlocked and its points, then the whole name and description.
static void DrawDetails(const char **names, const char **descriptions, const u16 *points, const bool *unlocked,
                        const bool *missable, const u8 *partsTotal, int count, int i)
{
    static const char  *partNames[MAX_PARTS_SHOWN + 1];
    static bool         partDone[MAX_PARTS_SHOWN + 1];
    static char         partsHeading[32], partText[MAX_PARTS_SHOWN][LINE_CHARS], measured[24], measuredLine[40];
    static char         pointsText[12], position[24];
    static char         nameLines[DETAIL_NAME_LINES][LINE_CHARS], lines[DETAIL_LINES][LINE_CHARS];
    static OverlayItem  items[OVERLAY_MAX_ITEMS];
    static const char   other[] = "Up/Down: Previous / Next";
    const int           x = DETAIL_MARGIN, width = WIDTH - 2 * DETAIL_MARGIN, textX = x + OVERLAY_ICON_SIZE + 10;
    bool                got = unlocked[i];
    u32                 n = 0;

    items[n++] = (OverlayItem){ x, DETAIL_MARGIN, 0, 0, WHITE, NULL, Picture(i, got), 0, 0 };
    items[n++] = (OverlayItem){ textX, DETAIL_MARGIN + 4, 0, 0, got ? GREEN : GREY, got ? "Unlocked" : "Locked", NULL, 0, 0 };
    snprintf(pointsText, sizeof(pointsText), "%u pts", points[i]);
    items[n++] = (OverlayItem){ textX, DETAIL_MARGIN + 22, 0, 0, got ? GOLD : GREY, pointsText, NULL, 0, 0 };
    snprintf(position, sizeof(position), "%d of %d", i + 1, count);
    items[n++] = (OverlayItem){ WIDTH - DETAIL_MARGIN - Overlay_TextWidth(position), DETAIL_MARGIN + 4, 0, 0, GREY, position, NULL, 0, 0 };
    if (missable[i])
        items[n++] = (OverlayItem){ WIDTH - DETAIL_MARGIN - Overlay_TextWidth(MISSABLE_TEXT), DETAIL_MARGIN + 22, 0, 0,
                                    got ? GREY : MISSABLE, MISSABLE_TEXT, NULL, 0, 0 };

    int y = DETAIL_MARGIN + OVERLAY_ICON_SIZE + 10;
    int nameCount = Wrap(names[i], width, nameLines, DETAIL_NAME_LINES);
    for (int k = 0; k < nameCount; k++, y += DETAIL_NAME_LINE)
        items[n++] = (OverlayItem){ x, y, 0, 0, got ? GREEN : WHITE, nameLines[k], NULL, 0, 0 };

    y += 4;
    int room = (DETAIL_HINT_Y - 4 - y) / DETAIL_LINE;
    if (room > DETAIL_LINES)
        room = DETAIL_LINES;
    if (partsTotal[i] > 0 && room > DETAIL_PART_LINES)
        room = DETAIL_PART_LINES;           // leave the rest for its parts
    bool progressShown = partsTotal[i] == 0 && Achievements_MeasuredTextAt(i, true, measured, sizeof(measured));
    if (progressShown && room > 1)
        room--;                             // a line for its progress
    int lineCount = Wrap(descriptions[i][0] != '\0' ? descriptions[i] : "No description.", width, lines, room > 0 ? room : 1);
    for (int k = 0; k < lineCount; k++, y += DETAIL_LINE)
        items[n++] = (OverlayItem){ x, y, 0, 0, LIGHT, lines[k], NULL, 0, 0 };

    // Measured progress (a number the game keeps, e.g. coins collected).
    if (progressShown)
    {
        snprintf(measuredLine, sizeof(measuredLine), "Progress: %s", measured);
        items[n++] = (OverlayItem){ x, y + 6, 0, 0, WHITE, measuredLine, NULL, 0, 0 };
    }

    // Done in parts: how many, then each part, green when done (two columns when needed).
    int parts = partsTotal[i] > 0 ? Achievements_PartList(i, partNames, partDone, MAX_PARTS_SHOWN + 1) : 0;
    if (parts > 0)
    {
        int done = 0;
        for (int k = 0; k < parts; k++)
            done += partDone[k];
        y += 6;
        snprintf(partsHeading, sizeof(partsHeading), "Parts: %d of %d done", done, partsTotal[i]);
        items[n++] = (OverlayItem){ x, y, 0, 0, WHITE, partsHeading, NULL, 0, 0 };
        y += DETAIL_LINE + 2;
        int rows = (DETAIL_HINT_Y - 4 - y) / DETAIL_LINE;
        int columns = parts > rows ? 2 : 1;
        int shown = parts < rows * columns ? parts : rows * columns;
        int itemsLeft = (int)OVERLAY_MAX_ITEMS - (int)n - 2;    // the hints take 2
        if (shown > itemsLeft)
            shown = itemsLeft;
        if (shown > MAX_PARTS_SHOWN)
            shown = MAX_PARTS_SHOWN;
        int perColumn = (shown + columns - 1) / columns, columnW = width / columns;
        for (int k = 0; k < shown; k++)
        {
            bool more = k == shown - 1 && shown < partsTotal[i];
            if (more)
                snprintf(partText[k], LINE_CHARS, "and %d more", partsTotal[i] - k);
            else
                Overlay_Ellipsize(partText[k], LINE_CHARS, partNames[k], columnW - 8);
            items[n++] = (OverlayItem){ x + (k / perColumn) * columnW, y + (k % perColumn) * DETAIL_LINE, 0, 0,
                                        more ? GREY : partDone[k] ? GREEN : GREY, partText[k], NULL, 0, 0 };
        }
    }

    items[n++] = (OverlayItem){ x, DETAIL_HINT_Y, 0, 0, HINT, "B: Back", NULL, 0, 0 };
    items[n++] = (OverlayItem){ WIDTH - DETAIL_MARGIN - Overlay_TextWidth(other), DETAIL_HINT_Y, 0, 0, HINT, other, NULL, 0, 0 };
    Overlay_DrawScreen(items, n);
}

// Instead of the list while a plugin the game's definitions block runs in it (main.c
// PluginBlocked): its achievements aren't tracked. plugin = its file name, or "a plugin".
static void DrawBlocked(const char *plugin)
{
    static char         message[128], lines[4][LINE_CHARS];
    static OverlayItem  items[OVERLAY_MAX_ITEMS];
    static const char   title[] = "Achievements aren't loaded";
    static const char   advice[] = "Start the game without it to earn them.";
    const int           width = WIDTH - 2 * DETAIL_MARGIN;
    u32                 n = 0;
    int                 y = 52;

    items[n++] = (OverlayItem){ (WIDTH - OVERLAY_ICON_SIZE) / 2, y, 0, 0, WHITE, NULL, Trophy(false), 0, 0 };
    y += OVERLAY_ICON_SIZE + 14;
    items[n++] = (OverlayItem){ (WIDTH - Overlay_TextWidth(title)) / 2, y, 0, 0, GOLD, title, NULL, 0, 0 };
    y += DETAIL_NAME_LINE + 6;
    snprintf(message, sizeof(message), "This game's achievements can't be earned while %s is running.", plugin);
    int count = Wrap(message, width, lines, 3);
    for (int k = 0; k < count; k++, y += DETAIL_LINE)
        items[n++] = (OverlayItem){ (WIDTH - Overlay_TextWidth(lines[k])) / 2, y, 0, 0, LIGHT, lines[k], NULL, 0, 0 };
    y += 6;
    items[n++] = (OverlayItem){ (WIDTH - Overlay_TextWidth(advice)) / 2, y, 0, 0, LIGHT, advice, NULL, 0, 0 };
    items[n++] = (OverlayItem){ DETAIL_MARGIN, DETAIL_HINT_Y, 0, 0, HINT, "B: Close", NULL, 0, 0 };
    Overlay_DrawScreen(items, n);
}

// scroll is in pixels, so a drag can stop between two boxes. The selected box has a
// white outline.
static void Draw(const char **names, const char **descriptions, const u16 *points, const bool *unlocked,
                 const bool *missable, const u8 *partsDone, const u8 *partsTotal, int count, int scroll, int selected)
{
    enum { ROWS = BOX_ROWS + 1 };           // a partly scrolled list shows one more
    static const Rgb    BLACK = { 0, 0, 0 };
    static char         progress[24], score[24], boxParts[ROWS][24];
    static char         boxPoints[ROWS][12], boxName[ROWS][LINE_CHARS], boxLines[ROWS][2][LINE_CHARS];
    static OverlayItem  items[OVERLAY_MAX_ITEMS];
    u32                 n = 0;
    int                 done, total, earned, available;

    if (count == 0)
        items[n++] = (OverlayItem){ MARGIN, PROGRESS_H + 8, 0, 0, WHITE, "No achievements for this game.", NULL, 0, 0 };

    // One box per achievement: icon | name, "Missable" while it's locked and can be missed,
    // "3/6" while it's locked and done in parts, points / description.
    int first = scroll / BOX_H;
    int top = PROGRESS_H - scroll % BOX_H;
    for (int row = 0; row < ROWS && first + row < count && top + row * BOX_H < HEIGHT; row++)
    {
        int  i = first + row, y = top + row * BOX_H;
        bool got = unlocked[i];

        snprintf(boxPoints[row], sizeof(boxPoints[row]), "%u pts", points[i]);
        int pointsX = WIDTH - MARGIN - Overlay_TextWidth(boxPoints[row]);
        int nameEnd = pointsX;
        bool warn = missable[i] && !got, parts = partsTotal[i] > 0 && !got;
        int  partsX = 0;
        if (parts)
            snprintf(boxParts[row], sizeof(boxParts[row]), "%u/%u", partsDone[i], partsTotal[i]);
        else            // measured progress ("4,200/5,000") goes where parts would
            parts = Achievements_MeasuredTextAt(i, false, boxParts[row], sizeof(boxParts[row]));
        if (parts)
        {
            nameEnd -= Overlay_TextWidth(boxParts[row]) + 8;
            partsX = nameEnd;
        }
        if (warn)
            nameEnd -= Overlay_TextWidth(MISSABLE_TEXT) + 8;
        Overlay_Ellipsize(boxName[row], LINE_CHARS, names[i], nameEnd - 6 - BOX_TEXT_X);
        Wrap(descriptions[i][0] != '\0' ? descriptions[i] : "No description.", BOX_TEXT_W, boxLines[row], 2);

        if (i == selected)
            items[n++] = (OverlayItem){ SELECT_X, y, SELECT_W, BOX_H, WHITE, NULL, NULL, SELECT_LINE, SELECT_RADIUS };
        items[n++] = (OverlayItem){ BOX_ICON_X, y + (BOX_H - OVERLAY_ICON_SIZE) / 2, 0, 0, WHITE, NULL, Picture(i, got), 0, 0 };
        items[n++] = (OverlayItem){ BOX_TEXT_X, y + 6, 0, 0, got ? GREEN : WHITE, boxName[row], NULL, 0, 0 };
        items[n++] = (OverlayItem){ pointsX, y + 6, 0, 0, got ? GOLD : GREY, boxPoints[row], NULL, 0, 0 };
        if (warn)
            items[n++] = (OverlayItem){ nameEnd, y + 6, 0, 0, MISSABLE, MISSABLE_TEXT, NULL, 0, 0 };
        if (parts)
            items[n++] = (OverlayItem){ partsX, y + 6, 0, 0, GOLD, boxParts[row], NULL, 0, 0 };
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

static void FreePictures(void)
{
    if (s_iconsFile != 0)
        FSFILE_Close(s_iconsFile);
    s_iconsFile = 0;
    free(s_places);
    free(s_pictures);
    s_places = NULL;
    s_pictures = NULL;
    s_placeCount = 0;
}

// Finds where the pictures of achievements ids[0..count) are in the game's icons.bin, if it has
// one; they're read as they're shown (Picture).
static void LoadPictures(u64 titleId, const char *const *ids, int count)
{
    char    path[64];
    u8      header[ICONS_HEADER_BYTES];
    u64     size = 0;
    u32     read = 0;

    snprintf(path, sizeof(path), "/achievements/%016llX/icons.bin", (unsigned long long)titleId);
    if (count == 0 || R_FAILED(FSUSER_OpenFileDirectly(&s_iconsFile, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                                       fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0)))
    {
        s_iconsFile = 0;
        return;
    }
    u32 indexBytes = 0;
    if (R_SUCCEEDED(FSFILE_GetSize(s_iconsFile, &size)) && size <= ICONS_FILE_MOST &&
        R_SUCCEEDED(FSFILE_Read(s_iconsFile, &read, 0, header, sizeof(header))) && read == sizeof(header))
        indexBytes = Icons_IndexBytes(header, (u32)size);
    u8 *index = indexBytes != 0 ? malloc(indexBytes) : NULL;
    s_places = index != NULL ? malloc((size_t)count * sizeof(IconsPlace)) : NULL;
    s_pictures = s_places != NULL ? malloc(PICTURE_SLOTS * sizeof(PictureSlot)) : NULL;
    bool found = s_pictures != NULL && R_SUCCEEDED(FSFILE_Read(s_iconsFile, &read, 0, index, indexBytes)) &&
                 read == indexBytes && Icons_Find(index, (u32)size, ids, count, s_places) > 0;
    free(index);
    if (!found)
    {
        FreePictures();
        return;
    }
    s_placeCount = count;
    for (int k = 0; k < PICTURE_SLOTS; k++)
        s_pictures[k] = (PictureSlot){ .achievement = -1 };
}

static void LoadGameIcons(u64 titleId, const char *const *ids, int count)
{
    char path[64];
    u32  length = 0;

    // Room for one byte more than the file should have (a longer file is wrong), and
    // the '\0' the file reader ends with.
    s_gameIcons = malloc(ICON_FILE_BYTES + 2);
    snprintf(path, sizeof(path), "/achievements/%016llX/trophy.bin", (unsigned long long)titleId);
    s_hasGameIcons = s_gameIcons != NULL && Platform_ReadFile(path, (char *)s_gameIcons, ICON_FILE_BYTES + 1, &length)
                     && length == ICON_FILE_BYTES;
    LoadPictures(titleId, ids, count);
}

static void FreeGameIcons(void)
{
    free(s_gameIcons);
    s_gameIcons = NULL;
    s_hasGameIcons = false;
    FreePictures();
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

bool Menu_Open(Handle game, u64 titleId, const char *blockedPlugin)
{
    static const char  *names[MAX_ACHIEVEMENTS], *descriptions[MAX_ACHIEVEMENTS], *ids[MAX_ACHIEVEMENTS];
    static u16          points[MAX_ACHIEVEMENTS];
    static bool         unlocked[MAX_ACHIEVEMENTS], missable[MAX_ACHIEVEMENTS];
    static u8           partsDone[MAX_ACHIEVEMENTS], partsTotal[MAX_ACHIEVEMENTS];

    // Blocked: no list, so nothing to select, open or scroll.
    int count = blockedPlugin != NULL ? 0 : Achievements_List(names, unlocked, MAX_ACHIEVEMENTS);
    Achievements_Descriptions(descriptions, MAX_ACHIEVEMENTS);
    Achievements_Points(points, MAX_ACHIEVEMENTS);
    Achievements_Missable(missable, MAX_ACHIEVEMENTS);
    Achievements_Parts(partsDone, partsTotal, MAX_ACHIEVEMENTS);
    LoadGameIcons(titleId, ids, Achievements_Ids(ids, MAX_ACHIEVEMENTS) == count ? count : 0);
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
        FreeGameIcons();
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
            if (blockedPlugin != NULL)
                DrawBlocked(blockedPlugin);
            else if (details)
                DrawDetails(names, descriptions, points, unlocked, missable, partsTotal, count, selected);
            else
                Draw(names, descriptions, points, unlocked, missable, partsDone, partsTotal, count, scroll, selected);
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
    FreeGameIcons();
    return true;
}
