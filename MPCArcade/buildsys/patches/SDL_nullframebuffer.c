/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_DUMMY

#include "../SDL_sysvideo.h"
#include "SDL_nullframebuffer_c.h"

#define DUMMY_SURFACE "_SDL_DummySurface"

/* MPC Arcade patch: when SDL_FBDEV is set (e.g. /dev/fb0), every frame is scaled to fit the Linux framebuffer
   (nearest neighbour, aspect kept, centred). SDL_FBROTATE = 0 / 90 / 180 / 270 rotates it. 16 and 24/32 bpp. */
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
static int fb_fd = -1, fb_failed = 0;
static Uint8 *fb_mem;
static struct fb_var_screeninfo fb_vi;
static struct fb_fix_screeninfo fb_fi;
static int fb_rot;
int SDL_DUMMY_FBOpen(int *w, int *h)
{
    const char *dev = SDL_getenv("SDL_FBDEV");
    if (!dev || fb_failed) return 0;
    if (fb_fd < 0) {
        const char *r = SDL_getenv("SDL_FBROTATE");
        fb_rot = r ? SDL_atoi(r) : 0;
        fb_fd = open(dev, O_RDWR);
        if (fb_fd < 0 || ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_vi) || ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fi)) { fb_failed = 1; return 0; }
        fb_mem = (Uint8 *)mmap(NULL, (size_t)fb_fi.line_length * fb_vi.yres_virtual, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
        if (fb_mem == MAP_FAILED) { fb_mem = NULL; fb_failed = 1; return 0; }
        SDL_memset(fb_mem + (size_t)fb_fi.line_length * fb_vi.yoffset, 0, (size_t)fb_fi.line_length * fb_vi.yres);
    }
    if (w) *w = (fb_rot == 90 || fb_rot == 270) ? (int)fb_vi.yres : (int)fb_vi.xres;
    if (h) *h = (fb_rot == 90 || fb_rot == 270) ? (int)fb_vi.xres : (int)fb_vi.yres;
    return 1;
}
static void fb_blit(const SDL_Surface *s)
{
    const int fw = (int)fb_vi.xres, fh = (int)fb_vi.yres, bpp = (int)fb_vi.bits_per_pixel / 8;
    const int rot = fb_rot == 90 || fb_rot == 270;
    const int sw = rot ? s->h : s->w, sh = rot ? s->w : s->h;          /* source size as seen on the panel */
    int dw = fw, dh = (int)((Sint64)sh * fw / sw);
    if (dh > fh) { dh = fh; dw = (int)((Sint64)sw * fh / sh); }
    const int x0 = (fw - dw) / 2, y0 = (fh - dh) / 2;
    Uint8 *base = fb_mem + (size_t)fb_fi.line_length * fb_vi.yoffset + (size_t)fb_vi.xoffset * bpp;
    for (int y = 0; y < dh; ++y) {
        Uint8 *dst = base + (size_t)(y0 + y) * fb_fi.line_length + (size_t)x0 * bpp;
        const int py = (int)((Sint64)y * sh / dh);
        for (int x = 0; x < dw; ++x) {
            const int px = (int)((Sint64)x * sw / dw);
            int sx, sy;                                                  /* panel (px,py) -> surface (sx,sy) */
            switch (fb_rot) {
            case 90:  sx = py; sy = s->h - 1 - px; break;
            case 180: sx = s->w - 1 - px; sy = s->h - 1 - py; break;
            case 270: sx = s->w - 1 - py; sy = px; break;
            default:  sx = px; sy = py; break;
            }
            const Uint32 c = ((const Uint32 *)((const Uint8 *)s->pixels + (size_t)sy * s->pitch))[sx];
            const Uint8 r = (Uint8)(c >> 16), g = (Uint8)(c >> 8), b = (Uint8)c;
            if (bpp == 2) {
                const Uint16 v = (Uint16)(((r >> (8 - fb_vi.red.length)) << fb_vi.red.offset) | ((g >> (8 - fb_vi.green.length)) << fb_vi.green.offset) | ((b >> (8 - fb_vi.blue.length)) << fb_vi.blue.offset));
                SDL_memcpy(dst, &v, 2);
            } else {
                dst[fb_vi.red.offset / 8] = r; dst[fb_vi.green.offset / 8] = g; dst[fb_vi.blue.offset / 8] = b;
                if (bpp == 4 && fb_vi.transp.length) dst[fb_vi.transp.offset / 8] = 255;
            }
            dst += bpp;
        }
    }
}

int SDL_DUMMY_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    const Uint32 surface_format = SDL_PIXELFORMAT_RGB888;
    int w, h;

    /* Free the old framebuffer surface */
    SDL_DUMMY_DestroyWindowFramebuffer(_this, window);

    /* Create a new one */
    SDL_GetWindowSizeInPixels(window, &w, &h);
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 0, surface_format);
    if (!surface) {
        return -1;
    }

    /* Save the info and return! */
    SDL_SetWindowData(window, DUMMY_SURFACE, surface);
    *format = surface_format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

int SDL_DUMMY_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    static int frame_number;
    SDL_Surface *surface;

    surface = (SDL_Surface *)SDL_GetWindowData(window, DUMMY_SURFACE);
    if (!surface) {
        return SDL_SetError("Couldn't find dummy surface for window");
    }

    /* Send the data to the display */
    if (SDL_DUMMY_FBOpen(NULL, NULL)) fb_blit(surface);
    if (SDL_getenv("SDL_VIDEO_DUMMY_SAVE_FRAMES")) {
        char file[128];
        (void)SDL_snprintf(file, sizeof(file), "SDL_window%" SDL_PRIu32 "-%8.8d.bmp",
                           SDL_GetWindowID(window), ++frame_number);
        SDL_SaveBMP(surface, file);
    }
    return 0;
}

void SDL_DUMMY_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_Surface *surface;

    surface = (SDL_Surface *)SDL_SetWindowData(window, DUMMY_SURFACE, NULL);
    SDL_FreeSurface(surface);
}

#endif /* SDL_VIDEO_DRIVER_DUMMY */

/* vi: set ts=4 sw=4 expandtab: */
