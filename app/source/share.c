#include "share.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "qrcodegen.h"
#include "share_image.h"
#include "share_server.h"
#include "update.h"

#define PORT            8080
#define LOG_PATH        "/achievements/share.log"
#define SAVE_FOLDER     "/achievements/share"
#define NETWORK_CHECK_MS 1000           // without Wi-Fi, look again this often
#define ENCODER_STACK   0x8000
#define QR_MAX_VERSION  10
#define QR_QUIET        4               // light modules around the code, as the standard asks
#define QR_HEIGHT       170             // most pixels the code (with its margin) may take

static const Color WHITE = { 255, 255, 255 };
static const Color GREY = { 140, 140, 150 };
static const Color HINT = { 255, 209, 102 };
static const Color GREEN = { 50, 205, 50 };
static const Color RED = { 235, 70, 70 };
static const Color BLACK = { 0, 0, 0 };

// The page the QR code opens: the image and a button to save it. It comes from the 3DS
// over plain http, where phones don't offer their share sheet, so saving is a download
// link, and iPhones (which keep downloads in Files) are told how to put it in Photos.
static const char PAGE[] =
    "<!doctype html>\n"
    "<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>{NAME} - 3DS Achievements</title>\n"
    "<style>\n"
    ":root{color-scheme:dark}\n"
    "body{margin:0;background:#111114;color:#eee;text-align:center;"
    "font:16px/1.4 -apple-system,system-ui,\"Segoe UI\",Roboto,sans-serif}\n"
    "main{max-width:540px;margin:0 auto;padding:16px}\n"
    "img{display:block;width:100%;height:auto;border-radius:14px}\n"
    "a{display:block;margin:16px 0 10px;padding:14px;border-radius:12px;background:#ffd166;"
    "color:#1a1a1a;font-weight:600;text-decoration:none}\n"
    "p{margin:0;color:#9a9aa5;font-size:14px}\n"
    "</style></head><body><main>\n"
    "<img src=\"/share.png\" alt=\"{NAME}: {COUNT} achievements\">\n"
    "<a id=\"save\" href=\"/save.png\" download=\"{FILE}\">Save image</a>\n"
    "<p id=\"hint\">Or touch and hold the image to save it.</p>\n"
    "</main><script>\n"
    "if (/iPhone|iPad|iPod/.test(navigator.userAgent) ||"
    " (navigator.platform === \"MacIntel\" && navigator.maxTouchPoints > 1)) {\n"
    "  document.getElementById(\"save\").textContent = \"Save to Files\";\n"
    "  document.getElementById(\"hint\").textContent ="
    " \"To save it to Photos, touch and hold the image.\";\n"
    "}\n"
    "</script></body></html>\n";

static u8       s_preview[SHARE_PREVIEW * SHARE_PREVIEW * 3];
static u8      *s_png;
static u32      s_pngSize;
static u8      *s_rgb;                  // the drawn image, until it's been made into the PNG
static Thread   s_encoder;              // makes the PNG in the background
static volatile bool s_encoded;         // and has finished
static u64      s_encodeMs;
static bool     s_ready;                // the PNG is made: it can be served and saved
static bool     s_saveWanted;           // A was pressed before it was ready
static char     s_page[sizeof(PAGE) + 1024];
static char     s_name[72];             // the game's name, safe for file names
static char     s_inline[128], s_attachment[128];
static ShareFile s_files[3];
static bool     s_serving;
static bool     s_serverFailed;
static char     s_url[64];
static u8       s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static bool     s_hasQr;
static u64      s_checkAt;
static int      s_shownSent;
static char     s_status[48];           // the latest thing that happened
static Color    s_statusColor;

static void LogLine(const char *line)
{
    FILE *f = fopen(LOG_PATH, "a");
    if (f == NULL)
        return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

static void Log(const char *format, ...)
{
    char line[192];
    va_list args;

    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    LogLine(line);
}

static void Status(Color color, const char *text)
{
    snprintf(s_status, sizeof(s_status), "%s", text);
    s_statusColor = color;
}

// The game's name as a file name: letters, digits and a few safe marks.
static void SafeName(char *out, size_t capacity, const char *title)
{
    size_t n = 0;
    for (; *title != '\0' && n + 1 < capacity; title++)
    {
        char c = *title;
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    strchr(" -_.()'!&+,", c) != NULL;
        if (keep && !(c == ' ' && (n == 0 || out[n - 1] == ' ')))
            out[n++] = c;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.'))
        n--;
    if (n == 0)
        n = (size_t)snprintf(out, capacity, "share");
    out[n] = '\0';
}

// Copies text into out with the characters HTML gives a meaning escaped. Returns the end.
static char *Escape(char *out, const char *end, const char *text)
{
    for (; *text != '\0'; text++)
    {
        const char *s = NULL;
        switch (*text)
        {
            case '&':  s = "&amp;"; break;
            case '<':  s = "&lt;"; break;
            case '>':  s = "&gt;"; break;
            case '"':  s = "&quot;"; break;
            case '\'': s = "&#39;"; break;
        }
        size_t n = s != NULL ? strlen(s) : 1;
        if (out + n >= end)
            break;
        memcpy(out, s != NULL ? s : text, n);
        out += n;
    }
    return out;
}

// PAGE with {NAME}, {COUNT} and {FILE} filled in.
static void BuildPage(const Game *g)
{
    char count[24], file[80];
    const char *end = s_page + sizeof(s_page) - 1;
    char *out = s_page;

    snprintf(count, sizeof(count), "%d of %d", g->unlocked, g->total);
    snprintf(file, sizeof(file), "%s.png", s_name);
    for (const char *p = PAGE; *p != '\0' && out < end;)
    {
        if (strncmp(p, "{NAME}", 6) == 0)
            out = Escape(out, end, g->title), p += 6;
        else if (strncmp(p, "{COUNT}", 7) == 0)
            out = Escape(out, end, count), p += 7;
        else if (strncmp(p, "{FILE}", 6) == 0)
            out = Escape(out, end, file), p += 6;
        else
            *out++ = *p++;
    }
    *out = '\0';
}

// Starts the server once the 3DS has an address on a Wi-Fi network. True if the screen
// should change.
static bool CheckNetwork(void)
{
    if (!s_ready || s_serving || s_serverFailed || !Update_NetworkReady())
        return false;

    u32 ip = (u32)gethostid();
    const u8 *b = (const u8 *)&ip;
    if (ip == 0 || ip == 0xFFFFFFFF || b[0] == 0 || b[0] == 127)
        return false;               // not connected

    int port = ShareServer_Start(ip, PORT, s_files, 3, LogLine);
    if (port == 0)
    {
        s_serverFailed = true;
        Log("server: couldn't start");
        return true;
    }
    s_serving = true;
    snprintf(s_url, sizeof(s_url), "http://%u.%u.%u.%u:%d/", b[0], b[1], b[2], b[3], port);

    static u8 work[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    s_hasQr = qrcodegen_encodeText(s_url, work, s_qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                   QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
    Log("server: %s, QR %s (%d modules)", s_url, s_hasQr ? "made" : "failed", s_hasQr ? qrcodegen_getSize(s_qr) : 0);
    return true;
}

static void EncodeThread(void *arg)
{
    (void)arg;
    u64 start = osGetTime();
    s_png = ShareImage_Png(s_rgb, SHARE_SIZE, SHARE_SIZE, &s_pngSize);
    s_encodeMs = osGetTime() - start;
    s_encoded = true;
}

// Takes the PNG once the encoder has finished (waiting for it if wait), and starts sharing
// it. True if that happened now.
static bool FinishEncoding(bool wait)
{
    if (s_rgb == NULL || (!s_encoded && !wait))
        return false;
    if (s_encoder != NULL)
    {
        threadJoin(s_encoder, U64_MAX);
        threadFree(s_encoder);
        s_encoder = NULL;
    }
    free(s_rgb);
    s_rgb = NULL;

    if (s_png == NULL)
    {
        Log("png: no memory");
        s_saveWanted = false;
        return true;
    }
    Log("png: %lu bytes in %llu ms", (unsigned long)s_pngSize, s_encodeMs);
    s_files[1] = (ShareFile){ "/share.png", "image/png", s_inline, s_png, s_pngSize, true };
    s_files[2] = (ShareFile){ "/save.png", "image/png", s_attachment, s_png, s_pngSize, true };
    s_ready = true;

    if (s_saveWanted)
    {
        s_saveWanted = false;
        Share_Save();
    }
    if (wait)
        return true;                // closing: no server to start
    s_checkAt = osGetTime() + NETWORK_CHECK_MS;
    if (!Update_NetworkReady())
        Log("server: no network service");
    else if (!CheckNetwork())
        Log("server: no Wi-Fi yet");
    return true;
}

bool Share_Open(const Game *g, const u8 *hero, const u16 *trophy)
{
    FILE *f = fopen(LOG_PATH, "w");     // a new log for each share
    if (f != NULL)
        fclose(f);
    Log("share: %s (%016llX), %d/%d", g->title, g->titleId, g->unlocked, g->total);

    u8 *rgb = malloc(SHARE_BYTES);
    if (rgb == NULL)
    {
        Log("image: no memory");
        return false;
    }
    ShareInfo info = { g->title, g->hasIcon ? g->icon : NULL, trophy, hero, g->unlocked, g->total };
    u64 start = osGetTime();
    ShareImage_Draw(&info, rgb);
    ShareImage_Shrink(rgb, s_preview);
    Log("image: drawn in %llu ms", osGetTime() - start);

    // What the phone can ask for: the page, the image to show, the image to download (the
    // two images once the PNG is made).
    SafeName(s_name, sizeof(s_name), g->title);
    BuildPage(g);
    snprintf(s_inline, sizeof(s_inline), "inline; filename=\"%s.png\"", s_name);
    snprintf(s_attachment, sizeof(s_attachment), "attachment; filename=\"%s.png\"", s_name);
    s_files[0] = (ShareFile){ "/", "text/html; charset=utf-8", NULL, (const u8 *)s_page, (u32)strlen(s_page), false };

    s_serving = s_serverFailed = s_hasQr = false;
    s_shownSent = 0;
    s_status[0] = '\0';

    // The PNG (the slow part) is made in the background, so the screen shows right away.
    s_rgb = rgb;
    s_png = NULL;
    s_encoded = s_ready = s_saveWanted = false;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_encoder = threadCreate(EncodeThread, NULL, ENCODER_STACK, priority + 1, -2, false);
    if (s_encoder == NULL)
        EncodeThread(NULL);
    FinishEncoding(false);
    return true;
}

bool Share_Save(void)
{
    char path[160];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    if (!s_ready)
    {
        if (s_rgb != NULL)          // still being made: save as soon as it's ready
        {
            s_saveWanted = true;
            Status(GREY, "Saving...");
        }
        return false;
    }

    mkdir(SAVE_FOLDER, 0777);
    if (t != NULL && t->tm_year >= 100)
        snprintf(path, sizeof(path), SAVE_FOLDER "/%s %04d-%02d-%02d.png", s_name, t->tm_year + 1900,
                 t->tm_mon + 1, t->tm_mday);
    else
        snprintf(path, sizeof(path), SAVE_FOLDER "/%s.png", s_name);

    FILE *f = fopen(path, "wb");
    bool ok = f != NULL && fwrite(s_png, 1, s_pngSize, f) == s_pngSize;
    if (f != NULL && fclose(f) != 0)
        ok = false;
    Log("%s %s", ok ? "saved to" : "couldn't save", path);
    if (ok)
        Status(GREEN, "Saved to the SD card: " SAVE_FOLDER);
    else
        Status(RED, "Couldn't save to the SD card");
    return ok;
}

bool Share_Poll(void)
{
    bool changed = FinishEncoding(false);

    if (s_ready && !s_serving && osGetTime() >= s_checkAt)
    {
        s_checkAt = osGetTime() + NETWORK_CHECK_MS;
        changed |= CheckNetwork();
    }
    if (s_serving && ShareServer_Sent() != s_shownSent)
    {
        s_shownSent = ShareServer_Sent();
        Status(GREEN, "Sent to your phone");
        changed = true;
    }
    return changed;
}

void Share_DrawTop(void)
{
    Draw_Image(GFX_TOP, (TOP_WIDTH - SHARE_PREVIEW) / 2, 0, SHARE_PREVIEW, SHARE_PREVIEW, s_preview);
}

static void Centered(int y, Color color, const char *text)
{
    Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth(text, 1)) / 2, y, 1, color, text, 0);
}

void Share_DrawBottom(void)
{
    const int hintY = SCREEN_HEIGHT - 16;

    if (s_hasQr)
    {
        int size = qrcodegen_getSize(s_qr), total = size + 2 * QR_QUIET;
        int module = QR_HEIGHT / total > 6 ? 6 : QR_HEIGHT / total;
        int box = total * module, x = (BOTTOM_WIDTH - box) / 2, y = 4;

        Draw_Rect(GFX_BOTTOM, x, y, box, box, WHITE);
        for (int my = 0; my < size; my++)
            for (int mx = 0; mx < size; mx++)
                if (qrcodegen_getModule(s_qr, mx, my))
                    Draw_Rect(GFX_BOTTOM, x + (QR_QUIET + mx) * module, y + (QR_QUIET + my) * module, module, module, BLACK);

        int ty = y + box + 5;
        Centered(ty, WHITE, "Scan with a phone on the same Wi-Fi");
        Centered(ty + 15, GREY, s_url);
        if (s_status[0] != '\0')
            Centered(ty + 34, s_statusColor, s_status);
    }
    else if (s_rgb != NULL)
    {
        Centered(100, WHITE, "Getting the image ready...");
        if (s_status[0] != '\0')
            Centered(124, s_statusColor, s_status);
    }
    else if (!s_ready)
    {
        const char *title = "Couldn't make the image";
        Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth(title, 2)) / 2, 24, 2, RED, title, 0);
        Centered(70, GREY, "There wasn't enough memory for it.");
        Draw_Text(GFX_BOTTOM, 8, hintY, 1, HINT, "B: Back", 0);
        return;
    }
    else
    {
        const char *title = s_serverFailed ? "Couldn't start sharing" : "No Wi-Fi connection";
        Draw_Text(GFX_BOTTOM, (BOTTOM_WIDTH - Draw_TextWidth(title, 2)) / 2, 24, 2, s_serverFailed ? RED : WHITE, title, 0);
        if (s_serverFailed)
            Centered(70, GREY, "See /achievements/share.log for why.");
        else
        {
            Centered(70, GREY, "Connect the 3DS to Wi-Fi to send the image");
            Centered(85, GREY, "to your phone. It's sent from the 3DS itself,");
            Centered(100, GREY, "so the phone must be on the same network.");
        }
        Centered(136, GREY, "Press A to save it to the SD card instead.");
        if (s_status[0] != '\0')
            Centered(160, s_statusColor, s_status);
    }

    int x = 8 + Draw_Text(GFX_BOTTOM, 8, hintY, 1, HINT, "A: Save to SD", 0);
    Draw_Text(GFX_BOTTOM, x + 12, hintY, 1, HINT, "B: Back", 0);
}

void Share_Close(void)
{
    FinishEncoding(true);           // the encoder can't be stopped halfway: let it finish
    if (s_serving)
    {
        ShareServer_Stop();
        Log("closed: sent %d time%s", ShareServer_Sent(), ShareServer_Sent() == 1 ? "" : "s");
    }
    s_serving = false;
    free(s_png);
    s_png = NULL;
}
