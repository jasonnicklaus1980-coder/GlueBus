// MPC Arcade pad bridge: MPC X pads, buttons, Q-Links and touchscreen -> a virtual keyboard + mouse that MAME and the
// library read. Runs for the whole arcade session (started by system/session.sh).
//
//   padbridge run --root /media/az01-internal/MAME     the daemon
//   padbridge probe [seconds]                          log everything the hardware sends (diagnostics)
//
// Files (under ROOT): config/pads/layout.conf   learned "SOURCE<TAB>raw id" (PAD1..16, QLINK1..4, HW:<name>)
//                     system/run/active.conf     the map in use: "SOURCE ACTION" + @settings; reloaded when it changes
//                     system/run/capture         while it exists: report presses to capture.out ("p <id>" / "t <id>")
//                                                instead of sending keys (the library's learn wizard and pad editor)
//                     system/run/panic           written when EXIT is held 6 s: session.sh then returns to the MPC
// Raw ids: key:<device>:<code>   rel:<device>:<axis>   midi:<card id>-D<dev>:ch<n>:note:<n> / :cc:<n>
#include "common/actions.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile sig_atomic_t quit;
static void onSignal (int s) { (void) s; quit = 1; }
static char ROOT[400], RUN[460];
static void logf_ (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void logf_ (const char* fmt, ...)
{
    char t[32]; time_t tt = time (NULL); struct tm tm; localtime_r (&tt, &tm); strftime (t, sizeof t, "%H:%M:%S", &tm);
    va_list ap; va_start (ap, fmt); printf ("%s ", t); vprintf (fmt, ap); va_end (ap); printf ("\n"); fflush (stdout);
}

// ------------------------------------------------------------------ sources
enum { SRC_INPUT, SRC_MIDI, SRC_TOUCH };
#define MAXSRC 48
typedef struct
{
    int fd, kind; char path[300], name[128];
    unsigned char status; int nData; unsigned char data[2];       // MIDI parser
    int absX0, absX1, absY0, absY1, tx, ty, touching;              // touchscreen
} Source;
static Source src[MAXSRC]; static int nSrc;

static int isOpen (const char* path) { for (int i = 0; i < nSrc; ++i) if (! strcmp (src[i].path, path)) return 1; return 0; }
static void midiName (const char* devName, char* out, int n)       // midiC1D0 -> "<card id>-D0"
{
    int c = 0, d = 0;
    sscanf (devName, "midiC%dD%d", &c, &d);
    char p[64], id[64] = "";
    snprintf (p, sizeof p, "/proc/asound/card%d/id", c);
    FILE* f = fopen (p, "r");
    if (f) { if (fgets (id, sizeof id, f)) id[strcspn (id, "\n")] = 0; fclose (f); }
    if (! id[0]) snprintf (id, sizeof id, "card%d", c);
    snprintf (out, (size_t) n, "%s-D%d", id, d);
}
#define BITS(n) (((n) + 8 * sizeof (long) - 1) / (8 * sizeof (long)))
static int testBit (const unsigned long* a, int b) { return (a[b / (8 * sizeof (long))] >> (b % (8 * sizeof (long)))) & 1; }
static void openSources (int verbose)
{
    // input devices (not our own virtual ones), touchscreens separately
    DIR* d = opendir ("/dev/input");
    struct dirent* e;
    while (d && (e = readdir (d)) != NULL && nSrc < MAXSRC)
    {
        if (strncmp (e->d_name, "event", 5)) continue;
        char path[300]; snprintf (path, sizeof path, "/dev/input/%s", e->d_name);
        if (isOpen (path)) continue;
        const int fd = open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        Source* s = &src[nSrc]; memset (s, 0, sizeof *s);
        ioctl (fd, EVIOCGNAME (sizeof s->name), s->name);
        if (strstr (s->name, "MPC Arcade")) { close (fd); continue; }
        unsigned long absb[BITS (ABS_MAX + 1)] = { 0 }, keyb[BITS (KEY_MAX + 1)] = { 0 };
        ioctl (fd, EVIOCGBIT (EV_ABS, sizeof absb), absb);
        ioctl (fd, EVIOCGBIT (EV_KEY, sizeof keyb), keyb);
        s->fd = fd; snprintf (s->path, sizeof s->path, "%s", path);
        const int mt = testBit (absb, ABS_MT_POSITION_X), st = testBit (absb, ABS_X) && testBit (keyb, BTN_TOUCH);
        if (mt || st)
        {
            struct input_absinfo ax, ay;
            s->kind = SRC_TOUCH;
            if (ioctl (fd, EVIOCGABS (mt ? ABS_MT_POSITION_X : ABS_X), &ax) == 0) { s->absX0 = ax.minimum; s->absX1 = ax.maximum; }
            if (ioctl (fd, EVIOCGABS (mt ? ABS_MT_POSITION_Y : ABS_Y), &ay) == 0) { s->absY0 = ay.minimum; s->absY1 = ay.maximum; }
        }
        else s->kind = SRC_INPUT;
        if (verbose) logf_ ("source %s: %s (%s)", path, s->name, s->kind == SRC_TOUCH ? "touchscreen" : "input");
        ++nSrc;
    }
    if (d) closedir (d);
    d = opendir ("/dev/snd");
    while (d && (e = readdir (d)) != NULL && nSrc < MAXSRC)
    {
        if (strncmp (e->d_name, "midiC", 5)) continue;
        char path[300]; snprintf (path, sizeof path, "/dev/snd/%s", e->d_name);
        if (isOpen (path)) continue;
        int fd = open (path, O_RDWR | O_NONBLOCK | O_CLOEXEC);      // RDWR: pad lights (experimental)
        if (fd < 0) fd = open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { if (verbose) logf_ ("can't open %s: %s", path, strerror (errno)); continue; }
        Source* s = &src[nSrc]; memset (s, 0, sizeof *s);
        s->fd = fd; s->kind = SRC_MIDI; snprintf (s->path, sizeof s->path, "%s", path);
        midiName (e->d_name, s->name, sizeof s->name);
        if (verbose) logf_ ("source %s: MIDI %s", path, s->name);
        ++nSrc;
    }
    if (d) closedir (d);
}
static void closeSource (int i) { close (src[i].fd); src[i] = src[--nSrc]; }

// ------------------------------------------------------------------ the map
typedef struct { char id[200]; int act; int down; double t0; int vel; double nextTurbo; int turboOn; int fired; int lastVal; } Bind;
static Bind binds[96]; static int nBinds;
static int threshold = 1, turboHz = 12, lights, touchExit = 1, uiMode;
static float exitHold = 1.5f;
static long activeMtime, layoutMtime;
static char layoutSrc[96][48], layoutId[96][200]; static int nLayout;
static long mtimeOf (const char* p) { struct stat st; return stat (p, &st) == 0 ? (long) st.st_mtime * 1000 + st.st_mtim.tv_nsec / 1000000 : 0; }
static void loadLayout (void)
{
    char p[500], line[400];
    nLayout = 0;
    snprintf (p, sizeof p, "%s/config/pads/layout.conf", ROOT);
    FILE* f = fopen (p, "r");
    while (f && fgets (line, sizeof line, f) && nLayout < 96)
    {
        line[strcspn (line, "\n")] = 0;
        char* tab = strchr (line, '\t');
        if (line[0] == '#' || ! tab) continue;
        *tab = 0;
        snprintf (layoutSrc[nLayout], 48, "%s", line); snprintf (layoutId[nLayout], 200, "%s", tab + 1); ++nLayout;
    }
    if (f) fclose (f);
    layoutMtime = mtimeOf (p);
}
static void sendLights (void);
static void loadActive (void)
{
    char p[500], line[300];
    snprintf (p, sizeof p, "%s/active.conf", RUN);
    FILE* f = fopen (p, "r");
    nBinds = 0; threshold = 1; turboHz = 12; lights = 0; touchExit = 1; exitHold = 1.5f; uiMode = 0;
    while (f && fgets (line, sizeof line, f))
    {
        line[strcspn (line, "\n")] = 0;
        if (line[0] == '#' || ! line[0]) continue;
        if (line[0] == '@')
        {
            char k[32], v[32];
            if (sscanf (line + 1, "%31s %31s", k, v) != 2) continue;
            if (! strcmp (k, "threshold")) threshold = atoi (v);
            else if (! strcmp (k, "exit_hold")) exitHold = (float) atof (v);
            else if (! strcmp (k, "turbo_hz")) turboHz = atoi (v) > 0 ? atoi (v) : 12;
            else if (! strcmp (k, "lights")) lights = atoi (v);
            else if (! strcmp (k, "touch_exit")) touchExit = atoi (v);
            else if (! strcmp (k, "mode")) uiMode = ! strcmp (v, "ui");
            continue;
        }
        char* sp = strrchr (line, ' ');
        if (! sp) continue;
        *sp = 0;
        const int a = actionIndex (sp + 1);
        if (a <= 0) continue;
        const char* id = NULL;                                         // SOURCE -> raw id via the layout; raw ids allowed too
        for (int i = 0; i < nLayout; ++i) if (! strcmp (layoutSrc[i], line)) id = layoutId[i];
        if (! id && strchr (line, ':') && strncmp (line, "HW:", 3)) id = line;
        if (! id || nBinds >= 96) continue;
        Bind* b = &binds[nBinds++]; memset (b, 0, sizeof *b);
        snprintf (b->id, sizeof b->id, "%s", id); b->act = a; b->lastVal = -1;
    }
    if (f) fclose (f);
    activeMtime = mtimeOf (p);
    logf_ ("map loaded: %d bindings, %s mode, threshold %d, exit hold %.1f s", nBinds, uiMode ? "library" : "game", threshold, (double) exitHold);
    if (lights) sendLights();
}

// ------------------------------------------------------------------ output: virtual keyboard + mouse
static int kbd = -1, mouse = -1;
static int mkDevice (const char* name, int isMouse)
{
    const int fd = open ("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    ioctl (fd, UI_SET_EVBIT, EV_KEY);
    if (isMouse)
    {
        ioctl (fd, UI_SET_EVBIT, EV_REL); ioctl (fd, UI_SET_RELBIT, REL_X); ioctl (fd, UI_SET_RELBIT, REL_Y);
        ioctl (fd, UI_SET_KEYBIT, BTN_LEFT); ioctl (fd, UI_SET_KEYBIT, BTN_RIGHT);
    }
    else
    {
        for (int k = 1; k < 128; ++k) ioctl (fd, UI_SET_KEYBIT, k);     // all ordinary keys: MAME's defaults use many
        ioctl (fd, UI_SET_KEYBIT, KEY_F12); ioctl (fd, UI_SET_KEYBIT, KEY_F7);
    }
    struct uinput_user_dev dev; memset (&dev, 0, sizeof dev);
    snprintf (dev.name, UINPUT_MAX_NAME_SIZE, "%s", name);
    dev.id.bustype = BUS_VIRTUAL; dev.id.vendor = 0x4d50; dev.id.product = isMouse ? 0x4d53 : 0x4b42; dev.id.version = 1;
    if (write (fd, &dev, sizeof dev) != (ssize_t) sizeof dev || ioctl (fd, UI_DEV_CREATE) < 0) { close (fd); return -1; }
    return fd;
}
static FILE* keylog;                                                   // tests: PADBRIDGE_KEYLOG=file logs every event sent
static void emit (int fd, int type, int code, int value)
{
    if (keylog && type != EV_SYN) { fprintf (keylog, "%.3f %s %d %d\n", now(), type == EV_KEY ? "key" : "rel", code, value); fflush (keylog); }
    if (fd < 0) return;
    struct input_event ev; memset (&ev, 0, sizeof ev);
    ev.type = (unsigned short) type; ev.code = (unsigned short) code; ev.value = value;
    if (write (fd, &ev, sizeof ev) < 0) { /* the reader is gone or slow: drop */ }
}
static void syn (int fd) { emit (fd, EV_SYN, SYN_REPORT, 0); }
static void key (int code, int down) { emit (kbd, EV_KEY, code, down); syn (kbd); }
// timed key sequence queue (save / load state: F7 then the slot key)
typedef struct { double at; int code, down; } Pending;
static Pending pend[64]; static int nPend;
static void later (double dt, int code, int down) { if (nPend < 64) { pend[nPend].at = now() + dt; pend[nPend].code = code; pend[nPend].down = down; ++nPend; } }
static void tap (int code) { key (code, 1); later (0.04, code, 0); }
static void runPending (void)
{
    const double t = now();
    for (int i = 0; i < nPend;)
        if (pend[i].at <= t) { key (pend[i].code, pend[i].down); pend[i] = pend[--nPend]; } else ++i;
}

// ------------------------------------------------------------------ capture (learn wizard / editor)
static int capturing;
static double capLast[64]; static char capLastId[64][200]; static int nCapLast;
static void captureWrite (char kind, const char* id)
{
    // knobs send a stream: report each control at most every 0.3 s
    if (kind == 't')
    {
        const double t = now();
        for (int i = 0; i < nCapLast; ++i) if (! strcmp (capLastId[i], id)) { if (t - capLast[i] < 0.3) return; capLast[i] = t; goto write; }
        if (nCapLast < 64) { snprintf (capLastId[nCapLast], 200, "%s", id); capLast[nCapLast++] = t; }
    }
write:;
    char p[500]; snprintf (p, sizeof p, "%s/capture.out", RUN);
    FILE* f = fopen (p, "a");
    if (f) { fprintf (f, "%c %s\n", kind, id); fclose (f); }
}

// ------------------------------------------------------------------ actions
static void panic (void)
{
    char p[500]; snprintf (p, sizeof p, "%s/panic", RUN);
    FILE* f = fopen (p, "w"); if (f) fclose (f);
    logf_ ("PANIC: EXIT held 6 s - closing MAME and the library");
    if (system ("pkill -TERM -x mame; pkill -TERM -x arcade-ui") != 0) { /* nothing running */ }
}
static void press (Bind* b, int down, int vel)
{
    const Action* a = &kActions[b->act];
    if (down && vel > 0 && vel < threshold) return;                 // too soft: ignore (sensitivity)
    if (down == b->down) return;
    b->down = down;
    if (down) { b->t0 = now(); b->fired = 0; b->turboOn = 1; b->nextTurbo = b->t0 + 0.5 / turboHz; }
    switch (a->group)
    {
        case G_EXIT:
            if (uiMode) { if (down) tap (KEY_ESC); }                    // library: a tap is "back"
            break;                                                      // game: handled by the hold timer
        case G_KNOB: break;
        default:
            if (a->key2)                                                // save / load state: modifier + F7, then slot 1
            {
                if (! down) break;
                if (a->mod) key (a->mod, 1);
                key (a->key, 1); key (a->key, 0);
                if (a->mod) key (a->mod, 0);
                later (0.12, a->key2, 1); later (0.17, a->key2, 0);
                break;
            }
            key (a->key, down);
    }
}
static void turn (Bind* b, int delta)
{
    const Action* a = &kActions[b->act];
    if (delta == 0) return;
    if (a->group != G_KNOB)                                            // a knob bound to a key action: a tap per detent
    {
        if (a->key) tap (a->key);
        return;
    }
    if (! strcmp (a->name, "SCROLL") || (uiMode && (! strcmp (a->name, "KNOB_X") || ! strcmp (a->name, "KNOB_Y"))))
    {
        const int n = delta > 0 ? (delta > 4 ? 4 : delta) : (delta < -4 ? 4 : -delta);
        for (int i = 0; i < n; ++i) { later (i * 0.03, delta > 0 ? KEY_DOWN : KEY_UP, 1); later (i * 0.03 + 0.015, delta > 0 ? KEY_DOWN : KEY_UP, 0); }
        return;
    }
    emit (mouse, EV_REL, ! strcmp (a->name, "KNOB_Y") ? REL_Y : REL_X, delta * 6);
    syn (mouse);
}
static void hit (const char* id, int down, int vel)
{
    if (capturing) { if (down) captureWrite ('p', id); return; }
    for (int i = 0; i < nBinds; ++i) if (! strcmp (binds[i].id, id)) press (&binds[i], down, vel);
}
static void knob (const char* id, int value, int relative)
{
    if (capturing) { captureWrite ('t', id); return; }
    for (int i = 0; i < nBinds; ++i)
    {
        Bind* b = &binds[i];
        if (strcmp (b->id, id)) continue;
        int delta;
        if (relative) delta = value;
        else if (value >= 1 && value <= 15) delta = value;              // MIDI relative encoders: 1..15 up, 113..127 down
        else if (value >= 113 && value <= 127) delta = value - 128;
        else { delta = b->lastVal < 0 ? 0 : value - b->lastVal; }       // absolute knob
        b->lastVal = value;
        turn (b, delta);
    }
}
static void timers (void)
{
    const double t = now();
    for (int i = 0; i < nBinds; ++i)
    {
        Bind* b = &binds[i];
        if (! b->down) continue;
        const Action* a = &kActions[b->act];
        if (a->group == G_EXIT)
        {
            if (! uiMode && ! b->fired && t - b->t0 >= exitHold) { tap (KEY_ESC); b->fired = 1; logf_ ("EXIT held: leaving the game"); }
            if (t - b->t0 >= 6.0 && b->fired < 2) { b->fired = 2; panic(); }
        }
        if (a->turbo && t >= b->nextTurbo)
        {
            b->turboOn = ! b->turboOn;
            key (a->key, b->turboOn);
            b->nextTurbo = t + 0.5 / turboHz;
        }
    }
}

// ------------------------------------------------------------------ touchscreen gestures
static double cornerSince = -1; static int cornerFired;
static void touchUpdate (Source* s)
{
    if (! touchExit || capturing || uiMode) { cornerSince = -1; return; }
    const int w = s->absX1 - s->absX0, h = s->absY1 - s->absY0;
    const int inCorner = s->touching && w > 0 && h > 0 && s->tx - s->absX0 > w * 88 / 100 && s->ty - s->absY0 < h * 14 / 100;
    if (! inCorner) { cornerSince = -1; cornerFired = 0; return; }
    if (cornerSince < 0) cornerSince = now();
    if (! cornerFired && now() - cornerSince >= 1.5) { tap (KEY_ESC); cornerFired = 1; logf_ ("touch corner held: leaving the game"); }
    if (cornerFired == 1 && now() - cornerSince >= 6.0) { cornerFired = 2; panic(); }
}

// ------------------------------------------------------------------ pad lights (EXPERIMENTAL, off by default)
// Akai MPC pad colour SysEx as documented by the community (TKGL MPC mapper): F0 47 7F <device> 65 00 04 <pad> R G B F7,
// device 0x3A = MPC X. Not verified on hardware here: if the pads stay dark nothing is lost.
static void sendLights (void)
{
    for (int p = 1; p <= 16; ++p)
    {
        char srcName[16]; snprintf (srcName, sizeof srcName, "PAD%d", p);
        const char* id = NULL;
        for (int i = 0; i < nLayout; ++i) if (! strcmp (layoutSrc[i], srcName)) id = layoutId[i];
        if (! id || strncmp (id, "midi:", 5)) continue;
        int group = G_NONE;
        for (int i = 0; i < nBinds; ++i) if (! strcmp (binds[i].id, id)) group = kActions[binds[i].act].group;
        static const unsigned char col[][3] = { { 0, 0, 0 }, { 0, 40, 127 }, { 127, 10, 0 }, { 127, 90, 0 }, { 80, 0, 127 }, { 0, 110, 100 }, { 127, 127, 127 } };
        const char* card = id + 5;
        for (int s = 0; s < nSrc; ++s)
        {
            if (src[s].kind != SRC_MIDI || strncmp (card, src[s].name, strlen (src[s].name))) continue;
            const unsigned char msg[] = { 0xF0, 0x47, 0x7F, 0x3A, 0x65, 0x00, 0x04, (unsigned char) (p - 1), col[group][0], col[group][1], col[group][2], 0xF7 };
            if (write (src[s].fd, msg, sizeof msg) < 0) { /* read-only port */ }
        }
    }
}

// ------------------------------------------------------------------ reading
static void readSource (int i, int probe)
{
    Source* s = &src[i];
    if (s->kind != SRC_MIDI)
    {
        struct input_event ev;
        ssize_t r;
        while ((r = read (s->fd, &ev, sizeof ev)) == (ssize_t) sizeof ev)
        {
            if (probe && ev.type != EV_SYN && ev.type != EV_MSC) logf_ ("[%s] type %u code %u value %d", s->name, ev.type, ev.code, ev.value);
            char id[300];
            if (s->kind == SRC_TOUCH)
            {
                if (ev.type == EV_ABS && (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)) s->tx = ev.value;
                if (ev.type == EV_ABS && (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)) s->ty = ev.value;
                if (ev.type == EV_KEY && ev.code == BTN_TOUCH) s->touching = ev.value != 0;
                if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) s->touching = ev.value >= 0;
                if (ev.type == EV_SYN) touchUpdate (s);
                continue;
            }
            if (ev.type == EV_KEY && ev.value != 2) { snprintf (id, sizeof id, "key:%s:%u", s->name, ev.code); hit (id, ev.value != 0, 0); }
            else if (ev.type == EV_REL) { snprintf (id, sizeof id, "rel:%s:%u", s->name, ev.code); knob (id, ev.value, 1); }
        }
        if (r < 0 && errno == ENODEV) { logf_ ("source gone: %s", s->path); closeSource (i); }
        return;
    }
    unsigned char buf[512];
    ssize_t n;
    while ((n = read (s->fd, buf, sizeof buf)) > 0)
    {
        if (probe)
        {
            char hex[1600]; int k = 0;
            for (ssize_t j = 0; j < n && k < (int) sizeof hex - 4; ++j) k += snprintf (hex + k, sizeof hex - (size_t) k, " %02x", buf[j]);
            logf_ ("[MIDI %s]%s", s->name, hex);
        }
        for (ssize_t j = 0; j < n; ++j)                                 // MIDI parser with running status
        {
            const unsigned char b = buf[j];
            if (b >= 0xf8) continue;
            if (b & 0x80) { s->status = b >= 0xf0 ? 0 : b; s->nData = 0; continue; }
            if (s->status == 0) continue;
            s->data[s->nData++] = b;
            const unsigned char t = s->status & 0xf0, ch = (unsigned char) ((s->status & 0x0f) + 1);
            const int need = (t == 0xc0 || t == 0xd0) ? 1 : 2;
            if (s->nData < need) continue;
            s->nData = 0;
            char id[300];
            if (t == 0x90 || t == 0x80)
            {
                snprintf (id, sizeof id, "midi:%s:ch%u:note:%u", s->name, ch, s->data[0]);
                hit (id, t == 0x90 && s->data[1] > 0, s->data[1]);
            }
            else if (t == 0xb0)
            {
                snprintf (id, sizeof id, "midi:%s:ch%u:cc:%u", s->name, ch, s->data[0]);
                knob (id, s->data[1], 0);
            }
        }
    }
    if (n < 0 && errno == ENODEV) { logf_ ("source gone: %s", s->path); closeSource (i); }
}
static void pumpAll (int timeoutMs, int probe)
{
    struct pollfd p[MAXSRC];
    for (int i = 0; i < nSrc; ++i) { p[i].fd = src[i].fd; p[i].events = POLLIN; p[i].revents = 0; }
    if (poll (p, (nfds_t) nSrc, timeoutMs) <= 0) return;
    for (int i = nSrc - 1; i >= 0; --i) if (p[i].revents & (POLLIN | POLLERR | POLLHUP)) readSource (i, probe);
}

int main (int argc, char** argv)
{
    setvbuf (stdout, NULL, _IOLBF, 0);
    signal (SIGTERM, onSignal); signal (SIGINT, onSignal); signal (SIGHUP, onSignal); signal (SIGPIPE, SIG_IGN);
    const char* mode = argc > 1 ? argv[1] : "";
    if (! strcmp (mode, "probe"))
    {
        const double secs = argc > 2 ? atof (argv[2]) : 30.0;
        openSources (1);
        printf ("uinput: %s\nPROBE: tap pads, press buttons, turn knobs for %.0f s\n", access ("/dev/uinput", W_OK) == 0 ? "available" : "NOT available", secs);
        const double t0 = now();
        while (now() - t0 < secs && ! quit) pumpAll (200, 1);
        printf ("probe done\n");
        return 0;
    }
    if (strcmp (mode, "run") || argc < 4 || strcmp (argv[2], "--root"))
    {
        printf ("usage: padbridge run --root DIR  |  padbridge probe [seconds]\n");
        return 1;
    }
    snprintf (ROOT, sizeof ROOT, "%s", argv[3]);
    snprintf (RUN, sizeof RUN, "%s/system/run", ROOT);
    char p[600];
    snprintf (p, sizeof p, "%s/padbridge.pid", RUN);
    FILE* f = fopen (p, "w"); if (f) { fprintf (f, "%d\n", (int) getpid()); fclose (f); }
    if (getenv ("PADBRIDGE_KEYLOG")) keylog = fopen (getenv ("PADBRIDGE_KEYLOG"), "w");
    kbd = mkDevice ("MPC Arcade pads", 0);
    mouse = mkDevice ("MPC Arcade knobs", 1);
    if (kbd < 0) logf_ ("WARNING: /dev/uinput unavailable (%s): pads can be learned but can't control games", strerror (errno));
    openSources (1);
    loadLayout();
    loadActive();
    char capPath[600], actPath[600], layPath[600];
    snprintf (capPath, sizeof capPath, "%s/capture", RUN);
    snprintf (actPath, sizeof actPath, "%s/active.conf", RUN);
    snprintf (layPath, sizeof layPath, "%s/config/pads/layout.conf", ROOT);
    double lastCheck = 0, lastScan = now();
    while (! quit)
    {
        pumpAll (nPend || capturing ? 10 : 30, 0);
        runPending();
        timers();
        const double t = now();
        if (t - lastCheck > 0.25)
        {
            lastCheck = t;
            const int cap = access (capPath, F_OK) == 0;
            if (cap != capturing)
            {
                capturing = cap;
                for (int i = 0; i < nBinds; ++i) if (binds[i].down) press (&binds[i], 0, 0);   // release everything
                logf_ ("capture mode %s", cap ? "on" : "off");
            }
            if (mtimeOf (layPath) != layoutMtime) { loadLayout(); loadActive(); }
            else if (mtimeOf (actPath) != activeMtime) loadActive();
        }
        if (t - lastScan > 3.0) { lastScan = t; openSources (1); }      // hot-plug (USB keyboard, controllers)
    }
    for (int i = 0; i < nBinds; ++i) if (binds[i].down && kActions[binds[i].act].key) key (kActions[binds[i].act].key, 0);
    if (kbd >= 0) { ioctl (kbd, UI_DEV_DESTROY); close (kbd); }
    if (mouse >= 0) { ioctl (mouse, UI_DEV_DESTROY); close (mouse); }
    unlink (p);
    logf_ ("stopped");
    return 0;
}
