// MPC Arcade pad bridge: MPC X pads / buttons -> a virtual keyboard that MAME reads.
//
//   padbridge probe [seconds]   log everything the hardware sends (input devices, raw MIDI ports, hidraw)
//   padbridge learn [file]      tap a pad / button for each arcade action; saves the mapping (default pads.conf)
//   padbridge run   [file]      create the virtual keyboard (/dev/uinput) and translate until killed
//
// Sources it reads: /dev/input/event* (key / button events), /dev/snd/midiC*D* (raw MIDI: note on/off and control
// changes), and in probe mode also /dev/hidraw* (raw bytes, shown as hex). Run probe and learn through arcade.sh
// so the MPC app is stopped and not holding the devices.
// The EXIT action only fires (as Esc) when its pad / button is HELD for 1.5 s, so a stray tap never quits a game.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile sig_atomic_t quit;
static void onSignal (int s) { (void) s; quit = 1; }

// ------------------------------------------------------------------ sources
enum { SRC_INPUT, SRC_MIDI, SRC_HID };
#define MAXSRC 48
static struct { int fd, kind; char path[300], name[96]; unsigned char status; int nData; unsigned char data[2]; } src[MAXSRC];
static int nSrc;

static void openDir (const char* dir, const char* prefix, int kind, int withHid)
{
    DIR* d = opendir (dir);
    if (d == NULL) return;
    struct dirent* e;
    while ((e = readdir (d)) != NULL && nSrc < MAXSRC)
    {
        if (strncmp (e->d_name, prefix, strlen (prefix)) != 0) continue;
        if (kind == SRC_HID && ! withHid) continue;
        char path[300]; snprintf (path, sizeof path, "%s/%s", dir, e->d_name);
        const int fd = open (path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) { printf ("  cannot open %s: %s\n", path, strerror (errno)); continue; }
        src[nSrc].fd = fd; src[nSrc].kind = kind; snprintf (src[nSrc].path, sizeof src[nSrc].path, "%s", path);
        src[nSrc].name[0] = 0;
        if (kind == SRC_INPUT) ioctl (fd, EVIOCGNAME (sizeof src[nSrc].name), src[nSrc].name);
        printf ("  %-22s %s %s\n", path, kind == SRC_INPUT ? "input" : kind == SRC_MIDI ? "raw MIDI" : "hidraw", src[nSrc].name);
        ++nSrc;
    }
    closedir (d);
}
static void openSources (int withHid)
{
    printf ("sources:\n");
    openDir ("/dev/input", "event", SRC_INPUT, withHid);
    openDir ("/dev/snd", "midiC", SRC_MIDI, withHid);
    openDir ("/dev", "hidraw", SRC_HID, withHid);
    printf ("  /dev/uinput: %s\n", access ("/dev/uinput", W_OK) == 0 ? "available" : "NOT available");
    fflush (stdout);
}

// a press / release from any source, identified by a stable text id
typedef struct { char id[400]; int down; } Hit;
// reads what is pending on source i; calls cb for each press / release. In probe mode prints everything.
static void readSource (int i, int probe, void (*cb) (const Hit*))
{
    if (src[i].kind == SRC_INPUT)
    {
        struct input_event ev;
        while (read (src[i].fd, &ev, sizeof ev) == (ssize_t) sizeof ev)
        {
            if (ev.type == EV_SYN || ev.type == EV_MSC) continue;
            if (probe) printf ("  [%s] type %u code %u value %d\n", src[i].name, ev.type, ev.code, ev.value);
            if (ev.type == EV_KEY && ev.value != 2 && cb)
            {
                Hit h; snprintf (h.id, sizeof h.id, "key:%s:%u", src[i].name, ev.code); h.down = ev.value != 0; cb (&h);
            }
        }
        return;
    }
    unsigned char buf[256];
    ssize_t n;
    while ((n = read (src[i].fd, buf, sizeof buf)) > 0)
    {
        if (src[i].kind == SRC_HID || probe)
        {
            printf ("  [%s]", src[i].path);
            for (ssize_t k = 0; k < n; ++k) printf (" %02x", buf[k]);
            printf ("\n");
        }
        if (src[i].kind != SRC_MIDI) continue;
        for (ssize_t k = 0; k < n; ++k)                                    // MIDI parser with running status
        {
            const unsigned char b = buf[k];
            if (b >= 0xf8) continue;                                       // real-time
            if (b & 0x80) { src[i].status = b >= 0xf0 ? 0 : b; src[i].nData = 0; continue; }
            if (src[i].status == 0) continue;                              // sysex / system common data
            src[i].data[src[i].nData++] = b;
            const unsigned char t = src[i].status & 0xf0;
            const int need = (t == 0xc0 || t == 0xd0) ? 1 : 2;
            if (src[i].nData < need) continue;
            src[i].nData = 0;
            if (! cb) continue;
            Hit h;
            if (t == 0x90 || t == 0x80) { snprintf (h.id, sizeof h.id, "midi:%s:note:%u", src[i].path, src[i].data[0]); h.down = t == 0x90 && src[i].data[1] > 0; cb (&h); }
            else if (t == 0xb0) { snprintf (h.id, sizeof h.id, "midi:%s:cc:%u", src[i].path, src[i].data[0]); h.down = src[i].data[1] >= 64; cb (&h); }
        }
    }
}
static void pump (double timeout, int probe, void (*cb) (const Hit*))
{
    struct pollfd p[MAXSRC];
    for (int i = 0; i < nSrc; ++i) { p[i].fd = src[i].fd; p[i].events = POLLIN; }
    if (poll (p, (nfds_t) nSrc, (int) (timeout * 1000)) <= 0) return;
    for (int i = 0; i < nSrc; ++i) if (p[i].revents & POLLIN) readSource (i, probe, cb);
    fflush (stdout);
}

// ------------------------------------------------------------------ actions
static const struct { const char* name; int key; const char* what; } kActions[] = {
    { "UP", KEY_UP, "joystick UP" }, { "DOWN", KEY_DOWN, "joystick DOWN" }, { "LEFT", KEY_LEFT, "joystick LEFT" },
    { "RIGHT", KEY_RIGHT, "joystick RIGHT" }, { "FIRE", KEY_LEFTCTRL, "FIRE (button 1)" }, { "FIRE2", KEY_LEFTALT, "button 2" },
    { "COIN", KEY_5, "insert COIN" }, { "START", KEY_1, "START (1 player)" }, { "MENU", KEY_TAB, "MAME menu" },
    { "SELECT", KEY_ENTER, "menu SELECT (Enter)" }, { "EXIT", KEY_ESC, "EXIT to the MPC (hold 1.5 s)" },
};
enum { NACT = sizeof kActions / sizeof kActions[0], EXIT_ACT = NACT - 1 };
static char mapId[NACT][400];

static int loadMap (const char* file)
{
    FILE* f = fopen (file, "r");
    if (f == NULL) return 0;
    char line[256]; int n = 0;
    while (fgets (line, sizeof line, f))
    {
        char act[32], id[400];
        if (line[0] == '#' || sscanf (line, "%31s %399[^\n]", act, id) != 2) continue;
        for (int a = 0; a < NACT; ++a) if (strcmp (act, kActions[a].name) == 0) { snprintf (mapId[a], sizeof mapId[a], "%s", id); ++n; }
    }
    fclose (f);
    return n;
}

// ------------------------------------------------------------------ learn
static Hit learned; static int gotHit;
static void learnCb (const Hit* h) { if (h->down && ! gotHit) { learned = *h; gotHit = 1; } }
static int learn (const char* file)
{
    openSources (0);
    printf ("\nLEARN: for each action, tap the pad / button you want (15 s each; wait to skip).\n");
    for (int a = 0; a < NACT; ++a)
    {
        printf ("-> %s ... ", kActions[a].what); fflush (stdout);
        gotHit = 0;
        const double t0 = now();
        while (! gotHit && now() - t0 < 15.0 && ! quit) pump (0.2, 0, learnCb);
        if (gotHit)
        {
            int dup = -1;
            for (int b = 0; b < a; ++b) if (strcmp (mapId[b], learned.id) == 0) dup = b;
            if (dup >= 0) { printf ("already used for %s, skipped\n", kActions[dup].name); continue; }
            snprintf (mapId[a], sizeof mapId[a], "%s", learned.id); printf ("%s\n", learned.id);
            const double t1 = now();                                       // let the pad go before the next prompt
            while (now() - t1 < 0.6) pump (0.1, 0, NULL);
        }
        else printf ("skipped\n");
    }
    FILE* f = fopen (file, "w");
    if (f == NULL) { printf ("cannot write %s: %s\n", file, strerror (errno)); return 1; }
    fprintf (f, "# MPC Arcade pad map (padbridge learn). ACTION source-id\n");
    for (int a = 0; a < NACT; ++a) if (mapId[a][0]) fprintf (f, "%s %s\n", kActions[a].name, mapId[a]);
    fclose (f);
    printf ("saved %s\n", file);
    return 0;
}

// ------------------------------------------------------------------ run
static int ui = -1; static double exitDownAt = -1;
static void emit (int type, int code, int value)
{
    struct input_event ev; memset (&ev, 0, sizeof ev);
    ev.type = (unsigned short) type; ev.code = (unsigned short) code; ev.value = value;
    if (write (ui, &ev, sizeof ev) < 0) { /* ignore */ }
}
static void key (int code, int down) { emit (EV_KEY, code, down); emit (EV_SYN, SYN_REPORT, 0); }
static void runCb (const Hit* h)
{
    for (int a = 0; a < NACT; ++a)
    {
        if (! mapId[a][0] || strcmp (mapId[a], h->id) != 0) continue;
        if (a == EXIT_ACT) exitDownAt = h->down ? now() : -1;              // Esc only after a 1.5 s hold
        else key (kActions[a].key, h->down);
    }
}
static int run (const char* file)
{
    if (loadMap (file) == 0) { printf ("no mapping in %s: run 'padbridge learn' first\n", file); return 1; }
    ui = open ("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ui < 0) { printf ("cannot open /dev/uinput: %s\n", strerror (errno)); return 1; }
    ioctl (ui, UI_SET_EVBIT, EV_KEY);
    for (int a = 0; a < NACT; ++a) ioctl (ui, UI_SET_KEYBIT, kActions[a].key);
    struct uinput_user_dev dev; memset (&dev, 0, sizeof dev);
    snprintf (dev.name, UINPUT_MAX_NAME_SIZE, "MPC Arcade pads");
    dev.id.bustype = BUS_VIRTUAL; dev.id.vendor = 0x4d50; dev.id.product = 0x4152; dev.id.version = 1;
    if (write (ui, &dev, sizeof dev) != (ssize_t) sizeof dev || ioctl (ui, UI_DEV_CREATE) < 0) { printf ("uinput setup failed: %s\n", strerror (errno)); return 1; }
    openSources (0);
    printf ("running: pads -> virtual keyboard \"MPC Arcade pads\"\n"); fflush (stdout);
    while (! quit)
    {
        pump (0.05, 0, runCb);
        if (exitDownAt > 0 && now() - exitDownAt >= 1.5) { key (KEY_ESC, 1); key (KEY_ESC, 0); exitDownAt = -1; }
    }
    ioctl (ui, UI_DEV_DESTROY); close (ui);
    return 0;
}

int main (int argc, char** argv)
{
    setvbuf (stdout, NULL, _IOLBF, 0);
    signal (SIGTERM, onSignal); signal (SIGINT, onSignal); signal (SIGHUP, onSignal);
    const char* mode = argc > 1 ? argv[1] : "";
    const char* file = argc > 2 ? argv[2] : "/sdcard/mpcarcade/pads.conf";
    if (strcmp (mode, "probe") == 0)
    {
        const double secs = argc > 2 ? atof (argv[2]) : 30.0;
        openSources (1);
        printf ("\nPROBE: tap pads, press buttons, turn knobs for %.0f s\n", secs);
        const double t0 = now();
        while (now() - t0 < secs && ! quit) pump (0.2, 1, NULL);
        printf ("probe done\n");
        return 0;
    }
    if (strcmp (mode, "learn") == 0) return learn (file);
    if (strcmp (mode, "run") == 0) return run (file);
    printf ("usage: padbridge probe [seconds] | learn [file] | run [file]\n");
    return 1;
}
