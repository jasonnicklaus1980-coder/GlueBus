// MPC Arcade step 2: hardware test. Run it through scripts/arcade.sh (the MPC app must not own the screen).
//   1. screen:  opens /dev/fb0, reports its geometry, draws colour bars and a moving square for a few seconds
//   2. sound:   plays a 1-second 440 Hz tone through ALSA (libasound loaded at run time; reports if missing)
//   3. input:   lists every /dev/input/event* device and logs what you press / touch for the rest of the 20 s,
//               drawing a dot where the touchscreen reports touches
// Writes nothing except to the screen and stdout (the launcher saves stdout to /sdcard/mpcarcade/hwtest.log).
// Exits on its own after 20 seconds.
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// ------------------------------------------------------------------ framebuffer
static uint8_t* fb; static struct fb_var_screeninfo vi; static struct fb_fix_screeninfo fi; static size_t fbLen;
static void px (int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (fb == NULL || x < 0 || y < 0 || x >= (int) vi.xres || y >= (int) vi.yres) return;
    uint8_t* p = fb + (size_t) (y + vi.yoffset) * fi.line_length + (size_t) (x + vi.xoffset) * (vi.bits_per_pixel / 8);
    if (vi.bits_per_pixel == 16)
    {
        const uint16_t v = (uint16_t) (((r >> (8 - vi.red.length)) << vi.red.offset) | ((g >> (8 - vi.green.length)) << vi.green.offset) | ((b >> (8 - vi.blue.length)) << vi.blue.offset));
        memcpy (p, &v, 2);
    }
    else if (vi.bits_per_pixel >= 24)
    {
        p[vi.red.offset / 8] = r; p[vi.green.offset / 8] = g; p[vi.blue.offset / 8] = b;
        if (vi.bits_per_pixel == 32 && vi.transp.length) p[vi.transp.offset / 8] = 255;
    }
}
static void rect (int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b) { for (int y = y0; y < y0 + h; ++y) for (int x = x0; x < x0 + w; ++x) px (x, y, r, g, b); }
static int screenTest (void)
{
    int fd = open ("/dev/fb0", O_RDWR);
    if (fd < 0) { printf ("screen: cannot open /dev/fb0: %s\n", strerror (errno)); return 0; }
    if (ioctl (fd, FBIOGET_VSCREENINFO, &vi) || ioctl (fd, FBIOGET_FSCREENINFO, &fi)) { printf ("screen: fb ioctl failed: %s\n", strerror (errno)); return 0; }
    printf ("screen: /dev/fb0 \"%s\" %ux%u visible, %ux%u virtual, %u bpp, line %u bytes, offset %u,%u, rgb %u/%u %u/%u %u/%u\n",
            fi.id, vi.xres, vi.yres, vi.xres_virtual, vi.yres_virtual, vi.bits_per_pixel, fi.line_length, vi.xoffset, vi.yoffset,
            vi.red.offset, vi.red.length, vi.green.offset, vi.green.length, vi.blue.offset, vi.blue.length);
    fbLen = (size_t) fi.line_length * vi.yres_virtual;
    fb = mmap (NULL, fbLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { fb = NULL; printf ("screen: mmap failed: %s\n", strerror (errno)); return 0; }
    const uint8_t bars[8][3] = { {255,255,255}, {255,255,0}, {0,255,255}, {0,255,0}, {255,0,255}, {255,0,0}, {0,0,255}, {0,0,0} };
    const int bw = (int) vi.xres / 8;
    for (int i = 0; i < 8; ++i) rect (i * bw, 0, bw, (int) vi.yres * 2 / 3, bars[i][0], bars[i][1], bars[i][2]);
    rect (0, (int) vi.yres * 2 / 3, (int) vi.xres, (int) vi.yres / 3, 20, 20, 40);
    rect (0, 0, 40, 40, 255, 128, 0);                                     // orange square = top-left corner
    const double t0 = now(); int frames = 0, lx = -1;
    while (now() - t0 < 3.0)                                              // moving square: shows the screen refreshes
    {
        const int x = (int) ((now() - t0) / 3.0 * (vi.xres - 60)), y = (int) vi.yres * 2 / 3 + 20;
        if (lx >= 0) rect (lx, y, 60, 60, 20, 20, 40);
        rect (x, y, 60, 60, 255, 220, 60); lx = x; ++frames;
        usleep (16000);
    }
    printf ("screen: drew colour bars (orange square = top-left) and %d animation frames\n", frames);
    return 1;
}

// ------------------------------------------------------------------ sound (ALSA via dlopen: no link-time dependency)
static void soundTest (void)
{
    void* a = dlopen ("libasound.so.2", RTLD_NOW);
    if (a == NULL) { printf ("sound: libasound.so.2 not found (%s)\n", dlerror()); return; }
    int (*open_) (void**, const char*, int, int) = dlsym (a, "snd_pcm_open");
    int (*setp) (void*, int, int, unsigned, unsigned, int, unsigned) = dlsym (a, "snd_pcm_set_params");
    long (*writei) (void*, const void*, unsigned long) = dlsym (a, "snd_pcm_writei");
    int (*drain) (void*) = dlsym (a, "snd_pcm_drain");
    int (*close_) (void*) = dlsym (a, "snd_pcm_close");
    const char* (*err) (int) = dlsym (a, "snd_strerror");
    if (! open_ || ! setp || ! writei || ! drain || ! close_ || ! err) { printf ("sound: libasound symbols missing\n"); return; }
    const char* devs[] = { "default", "hw:0,0", "plughw:0,0" };
    for (int d = 0; d < 3; ++d)
    {
        void* pcm = NULL; int e = open_ (&pcm, devs[d], 0 /* playback */, 0);
        if (e < 0) { printf ("sound: open %s: %s\n", devs[d], err (e)); continue; }
        e = setp (pcm, 2 /* S16_LE */, 3 /* RW_INTERLEAVED */, 2, 48000, 1, 100000);
        if (e < 0) { printf ("sound: params on %s: %s\n", devs[d], err (e)); close_ (pcm); continue; }
        static int16_t buf[48000 * 2];
        for (int i = 0; i < 48000; ++i)
        {
            const double env = i < 2400 ? i / 2400.0 : (i > 45600 ? (48000 - i) / 2400.0 : 1.0);
            const int16_t v = (int16_t) (8000 * env * sin (2 * M_PI * 440 * i / 48000.0));
            buf[2 * i] = buf[2 * i + 1] = v;
        }
        const long w = writei (pcm, buf, 48000);
        drain (pcm); close_ (pcm);
        printf ("sound: played 1 s 440 Hz on \"%s\" (%ld frames written)\n", devs[d], w);
        return;
    }
    printf ("sound: no ALSA device could be opened (the MPC app may still hold it, or audio is routed differently)\n");
}

// ------------------------------------------------------------------ input
#define MAXDEV 24
static int devFd[MAXDEV], nDev; static char devName[MAXDEV][96]; static struct input_absinfo ax[MAXDEV][2];
static void inputTest (double seconds)
{
    DIR* d = opendir ("/dev/input");
    if (d == NULL) { printf ("input: no /dev/input\n"); return; }
    struct dirent* e;
    while ((e = readdir (d)) != NULL && nDev < MAXDEV)
    {
        if (strncmp (e->d_name, "event", 5) != 0) continue;
        char path[300]; snprintf (path, sizeof path, "/dev/input/%s", e->d_name);
        const int fd = open (path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) { printf ("input: %s: %s\n", path, strerror (errno)); continue; }
        devName[nDev][0] = 0; ioctl (fd, EVIOCGNAME (sizeof devName[nDev]), devName[nDev]);
        int xa = ABS_MT_POSITION_X, ya = ABS_MT_POSITION_Y;
        if (ioctl (fd, EVIOCGABS (xa), &ax[nDev][0]) || ax[nDev][0].maximum == 0) { xa = ABS_X; ya = ABS_Y; ioctl (fd, EVIOCGABS (xa), &ax[nDev][0]); }
        ioctl (fd, EVIOCGABS (ya), &ax[nDev][1]);
        printf ("input: %s \"%s\" x %d..%d y %d..%d\n", path, devName[nDev], ax[nDev][0].minimum, ax[nDev][0].maximum, ax[nDev][1].minimum, ax[nDev][1].maximum);
        devFd[nDev++] = fd;
    }
    closedir (d);
    printf ("input: touch the screen, press buttons, hit pads and turn knobs now (%.0f s)\n", seconds);
    fflush (stdout);
    struct pollfd p[MAXDEV];
    for (int i = 0; i < nDev; ++i) { p[i].fd = devFd[i]; p[i].events = POLLIN; }
    const double t0 = now(); int logged = 0, tx[MAXDEV] = {0}, ty[MAXDEV] = {0};
    while (now() - t0 < seconds)
    {
        if (poll (p, (nfds_t) nDev, 100) <= 0) continue;
        for (int i = 0; i < nDev; ++i)
        {
            if (! (p[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read (devFd[i], &ev, sizeof ev) == (ssize_t) sizeof ev)
            {
                if (ev.type == EV_SYN || ev.type == EV_MSC) continue;
                if (logged < 400) { printf ("  [%s] type %u code %u value %d\n", devName[i], ev.type, ev.code, ev.value); ++logged; }
                if (ev.type == EV_ABS && (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)) tx[i] = ev.value;
                if (ev.type == EV_ABS && (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y))
                {
                    ty[i] = ev.value;
                    const int xr = ax[i][0].maximum - ax[i][0].minimum, yr = ax[i][1].maximum - ax[i][1].minimum;
                    if (xr > 0 && yr > 0 && fb != NULL)
                        rect ((int) ((long) (tx[i] - ax[i][0].minimum) * vi.xres / xr) - 4, (int) ((long) (ty[i] - ax[i][1].minimum) * vi.yres / yr) - 4, 9, 9, 255, 255, 255);
                }
            }
        }
        fflush (stdout);
    }
    printf ("input: %d events logged\n", logged);
}

int main (void)
{
    setvbuf (stdout, NULL, _IOLBF, 0);
    printf ("MPC Arcade hardware test\n");
    const int haveScreen = screenTest();
    soundTest();
    inputTest (haveScreen ? 15.0 : 10.0);
    if (fb != NULL) { rect (0, 0, (int) vi.xres, (int) vi.yres, 0, 0, 0); munmap (fb, fbLen); }
    printf ("done\n");
    return 0;
}
