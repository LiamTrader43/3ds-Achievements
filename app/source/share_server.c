#include "share_server.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define STACK_SIZE      0x8000
#define PORT_TRIES      10
#define MAX_FILES       4
#define MAX_CLIENTS     4           // phone browsers often open a spare connection or two
#define WAIT_MS         100         // poll in slices this long, so a stop is noticed quickly
#define REQUEST_MS      5000        // a phone gets this long to send its request
#define STALL_MS        15000       // and a download may stall this long
#define DRAIN_MS        1000        // after the answer, how long to wait for the phone to close
#define CHUNK           0x4000
#define REQUEST_SIZE    8192

typedef enum { FREE, READING, SENDING, DRAINING } State;

typedef struct
{
    State       state;
    int         fd;
    char        from[16];               // the phone's address, for the log
    u64         deadline;
    u64         start;
    u32         used;                   // request bytes read
    char        request[REQUEST_SIZE];
    char        header[384];
    u32         headerLength, headerSent;
    const u8   *body;
    u32         bodyLength, bodySent;
    bool        counts;                 // the answer counts in ShareServer_Sent when done
    char        what[48];               // "GET /share.png", for the log
} Client;

static int              s_listen = -1;
static Thread           s_thread;
static volatile bool    s_stop;
static volatile int     s_sent;
static ShareFile        s_files[MAX_FILES];
static int              s_fileCount;
static void           (*s_log)(const char *line);
static Client           s_clients[MAX_CLIENTS];

static void Log(const char *format, ...)
{
    char line[192];
    va_list args;

    if (s_log == NULL)
        return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    s_log(line);
}

static bool WouldBlock(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

static void Close(Client *c)
{
    close(c->fd);
    c->state = FREE;
}

// Ends a connection gently once the answer is out: no more sending, then read whatever
// the phone still sends until it closes (closing with unread data would reset the
// connection, which can cut off the answer).
static void Finish(Client *c)
{
    shutdown(c->fd, SHUT_WR);
    c->state = DRAINING;
    c->deadline = osGetTime() + DRAIN_MS;
}

static void Answer(Client *c, const char *status, const char *type, const char *disposition,
                   const u8 *body, u32 length, bool headOnly)
{
    int n = snprintf(c->header, sizeof(c->header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n%s%s%s"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, type, (unsigned long)length,
        disposition != NULL ? "Content-Disposition: " : "", disposition != NULL ? disposition : "",
        disposition != NULL ? "\r\n" : "");
    c->headerLength = n < (int)sizeof(c->header) ? (u32)n : sizeof(c->header) - 1;
    c->headerSent = 0;
    c->body = body;
    c->bodyLength = headOnly ? 0 : length;
    c->bodySent = 0;
    c->state = SENDING;
    c->deadline = osGetTime() + STALL_MS;
}

// The request's headers have arrived: decide the answer.
static void Respond(Client *c)
{
    static const char NOT_FOUND[] = "Not found\n", BAD[] = "Bad request\n", ONLY_GET[] = "Only GET\n";
    static const char TEXT[] = "text/plain";
    char method[8], path[160];

    c->counts = false;
    if (sscanf(c->request, "%7s %159s", method, path) != 2)
    {
        snprintf(c->what, sizeof(c->what), "?");
        Answer(c, "400 Bad Request", TEXT, NULL, (const u8 *)BAD, sizeof(BAD) - 1, false);
        return;
    }
    char *query = strchr(path, '?');
    if (query != NULL)
        *query = '\0';
    snprintf(c->what, sizeof(c->what), "%s %.38s", method, path);

    bool head = strcmp(method, "HEAD") == 0;
    if (strcmp(method, "GET") != 0 && !head)
    {
        Answer(c, "405 Method Not Allowed", TEXT, NULL, (const u8 *)ONLY_GET, sizeof(ONLY_GET) - 1, false);
        return;
    }
    for (int i = 0; i < s_fileCount; i++)
    {
        const ShareFile *f = &s_files[i];
        if (strcmp(path, f->path) == 0)
        {
            Answer(c, "200 OK", f->type, f->disposition, f->data, f->size, head);
            c->counts = f->counts && !head;
            return;
        }
    }
    Answer(c, "404 Not Found", TEXT, NULL, (const u8 *)NOT_FOUND, sizeof(NOT_FOUND) - 1, head);
}

static void Read(Client *c)
{
    for (;;)
    {
        ssize_t n = recv(c->fd, c->request + c->used, REQUEST_SIZE - 1 - c->used, 0);
        if (n < 0 && WouldBlock())
            return;
        if (n <= 0)
        {
            Log("%s: closed before asking", c->from);
            Close(c);
            return;
        }
        c->used += (u32)n;
        c->request[c->used] = '\0';
        // Huge headers: the first line is all that's needed, so answer anyway.
        if (strstr(c->request, "\r\n\r\n") != NULL || c->used == REQUEST_SIZE - 1)
        {
            Respond(c);
            return;
        }
    }
}

static void Send(Client *c)
{
    for (;;)
    {
        const u8 *p;
        u32 left;
        if (c->headerSent < c->headerLength)
            p = (const u8 *)c->header + c->headerSent, left = c->headerLength - c->headerSent;
        else if (c->bodySent < c->bodyLength)
            p = c->body + c->bodySent, left = c->bodyLength - c->bodySent;
        else
        {
            if (c->counts)
                s_sent++;
            Log("%s: %s: sent %lu bytes in %lu ms", c->from, c->what, (unsigned long)c->bodyLength,
                (unsigned long)(osGetTime() - c->start));
            Finish(c);
            return;
        }

        ssize_t n = send(c->fd, p, left > CHUNK ? CHUNK : left, 0);
        if (n < 0 && WouldBlock())
            return;
        if (n <= 0)
        {
            Log("%s: %s: stopped after %lu of %lu bytes (errno %d)", c->from, c->what,
                (unsigned long)c->bodySent, (unsigned long)c->bodyLength, errno);
            Close(c);
            return;
        }
        if (c->headerSent < c->headerLength)
            c->headerSent += (u32)n;
        else
            c->bodySent += (u32)n;
        c->deadline = osGetTime() + STALL_MS;
    }
}

static void Drain(Client *c)
{
    char scratch[256];
    for (;;)
    {
        ssize_t n = recv(c->fd, scratch, sizeof(scratch), 0);
        if (n < 0 && WouldBlock())
            return;
        if (n <= 0)
        {
            Close(c);
            return;
        }
    }
}

static void Accept(void)
{
    struct sockaddr_in from;
    socklen_t length = sizeof(from);
    int fd = accept(s_listen, (struct sockaddr *)&from, &length);
    if (fd < 0)
        return;

    Client *c = NULL;
    for (int i = 0; i < MAX_CLIENTS && c == NULL; i++)
        if (s_clients[i].state == FREE)
            c = &s_clients[i];
    if (c == NULL)
    {
        close(fd);              // can't happen: the listener isn't polled while full
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    const u8 *b = (const u8 *)&from.sin_addr.s_addr;
    snprintf(c->from, sizeof(c->from), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    c->fd = fd;
    c->state = READING;
    c->start = osGetTime();
    c->deadline = c->start + REQUEST_MS;
    c->used = 0;
    c->request[0] = '\0';
    c->counts = false;
}

static void ServerThread(void *arg)
{
    struct pollfd fds[MAX_CLIENTS + 1];
    Client *owner[MAX_CLIENTS + 1];

    (void)arg;
    while (!s_stop)
    {
        int n = 0, busy = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            Client *c = &s_clients[i];
            if (c->state == FREE)
                continue;
            busy++;
            fds[n].fd = c->fd;
            fds[n].events = c->state == SENDING ? POLLOUT : POLLIN;
            fds[n].revents = 0;
            owner[n++] = c;
        }
        if (busy < MAX_CLIENTS)
        {
            fds[n].fd = s_listen;
            fds[n].events = POLLIN;
            fds[n].revents = 0;
            owner[n++] = NULL;
        }

        if (poll(fds, n, WAIT_MS) < 0)
            continue;

        for (int i = 0; i < n; i++)
        {
            if (fds[i].revents == 0)
                continue;
            Client *c = owner[i];
            if (c == NULL)
                Accept();
            else if (c->state == READING)
                Read(c);
            else if (c->state == SENDING)
                Send(c);
            else if (c->state == DRAINING)
                Drain(c);
        }

        u64 now = osGetTime();
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            Client *c = &s_clients[i];
            if (c->state == FREE || now < c->deadline)
                continue;
            if (c->state == READING)
                Log("%s: no request", c->from);
            else if (c->state == SENDING)
                Log("%s: %s: stalled after %lu of %lu bytes", c->from, c->what,
                    (unsigned long)c->bodySent, (unsigned long)c->bodyLength);
            Close(c);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (s_clients[i].state != FREE)
            Close(&s_clients[i]);
}

int ShareServer_Start(u32 ip, int port, const ShareFile *files, int count,
                      void (*log)(const char *line))
{
    s_log = log;
    s_fileCount = count < MAX_FILES ? count : MAX_FILES;
    memcpy(s_files, files, s_fileCount * sizeof(ShareFile));
    s_sent = 0;
    s_stop = false;
    for (int i = 0; i < MAX_CLIENTS; i++)
        s_clients[i].state = FREE;

    // A port can stay taken for a while after the last share closed it; try the next ones.
    int got = 0;
    for (int i = 0; i < PORT_TRIES && got == 0; i++)
    {
        s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (s_listen < 0)
        {
            Log("socket failed: errno %d", errno);
            return 0;
        }
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons((u16)(port + i));
        address.sin_addr.s_addr = ip;
        if (bind(s_listen, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(s_listen, MAX_CLIENTS) == 0)
            got = port + i;
        else
        {
            Log("port %d: errno %d", port + i, errno);
            close(s_listen);
            s_listen = -1;
        }
    }
    if (got == 0)
        return 0;

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_thread = threadCreate(ServerThread, NULL, STACK_SIZE, priority + 1, -2, false);
    if (s_thread == NULL)
    {
        Log("thread failed");
        close(s_listen);
        s_listen = -1;
        return 0;
    }
    return got;
}

void ShareServer_Stop(void)
{
    s_stop = true;
    if (s_thread != NULL)
    {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    if (s_listen >= 0)
    {
        close(s_listen);
        s_listen = -1;
    }
}

int ShareServer_Sent(void)
{
    return s_sent;
}
