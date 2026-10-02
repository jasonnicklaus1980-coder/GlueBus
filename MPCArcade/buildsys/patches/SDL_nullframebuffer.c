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

/* MPC Arcade patch: when SDL_FBDEV is set (e.g. /dev/fb0), every frame is copied to the Linux framebuffer
   (scaled nearest-neighbour to fit, aspect kept, centred). 16 and 24/32 bpp. Environment:
     SDL_FBROTATE    0 / 90 / 180 / 270   rotate the picture on the panel
     SDL_FBSCALE     1 (default) or 2     report a half-size display (cheaper rendering), shown 2x
     SDL_FBDOUBLE    1                    tear-free: draw into the hidden half of the framebuffer, then pan
     SDL_FBSCANLINES "period:offset:pct"  darken the last panel row of every `period` rows (starting at `offset`)
                                          by pct %, to look like a CRT when the game is scaled by exactly `period`
   The common case (no rotation, same size, 32 bpp XRGB) is a straight row copy. */
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
static int fb_rot, fb_scale = 1, fb_double, fb_page, fb_sl_period, fb_sl_offset, fb_sl_keep = 256;
static int *fb_xmap, fb_xmap_n, fb_xmap_key[4];
int SDL_DUMMY_FBOpen(int *w, int *h)
{
    const char *dev = SDL_getenv("SDL_FBDEV");
    if (!dev || fb_failed) return 0;
    if (fb_fd < 0) {
        const char *r = SDL_getenv("SDL_FBROTATE"), *sc = SDL_getenv("SDL_FBSCALE"), *db = SDL_getenv("SDL_FBDOUBLE");
        const char *sl = SDL_getenv("SDL_FBSCANLINES");
        fb_rot = r ? SDL_atoi(r) : 0;
        fb_scale = sc && SDL_atoi(sc) == 2 ? 2 : 1;
        if (sl) {
            int period = 0, offset = 0, pct = 0;
            if (SDL_sscanf(sl, "%d:%d:%d", &period, &offset, &pct) == 3 && period >= 2 && pct > 0 && pct <= 100) {
                fb_sl_period = period; fb_sl_offset = offset; fb_sl_keep = 256 - pct * 256 / 100;
            }
        }
        const char *fake = SDL_getenv("SDL_FBTEST");                     /* off-device tests: "WxHxBPP", SDL_FBDEV = a plain file */
        fb_fd = open(dev, fake ? O_RDWR | O_CREAT : O_RDWR, 0644);
        int fwid = 0, fhgt = 0, fbpp = 0;
        if (fb_fd >= 0 && fake && SDL_sscanf(fake, "%dx%dx%d", &fwid, &fhgt, &fbpp) == 3) {
            SDL_zero(fb_vi); SDL_zero(fb_fi);
            fb_vi.xres = fb_vi.xres_virtual = (Uint32)fwid; fb_vi.yres = fb_vi.yres_virtual = (Uint32)fhgt; fb_vi.bits_per_pixel = (Uint32)fbpp;
            if (fbpp == 16) { fb_vi.red.offset = 11; fb_vi.red.length = 5; fb_vi.green.offset = 5; fb_vi.green.length = 6; fb_vi.blue.length = 5; }
            else { fb_vi.red.offset = 16; fb_vi.green.offset = 8; fb_vi.red.length = fb_vi.green.length = fb_vi.blue.length = 8; }
            fb_fi.line_length = (Uint32)(fwid * fbpp / 8);
            if (ftruncate(fb_fd, (off_t)fb_fi.line_length * fhgt)) { fb_failed = 1; return 0; }
        } else
        if (fb_fd < 0 || ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_vi) || ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fi)) { fb_failed = 1; return 0; }
        fb_double = db && SDL_atoi(db) == 1 && fb_vi.yres_virtual >= 2 * fb_vi.yres;
        fb_mem = (Uint8 *)mmap(NULL, (size_t)fb_fi.line_length * fb_vi.yres_virtual, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
        if (fb_mem == MAP_FAILED) { fb_mem = NULL; fb_failed = 1; return 0; }
        SDL_memset(fb_mem, 0, (size_t)fb_fi.line_length * (fb_double ? 2 * fb_vi.yres : fb_vi.yres_virtual));
        if (fb_double) {                                                  /* start on page 0 */
            fb_vi.yoffset = 0;
            if (ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_vi)) fb_double = 0;
        }
    }
    const int pw = (fb_rot == 90 || fb_rot == 270) ? (int)fb_vi.yres : (int)fb_vi.xres;
    const int ph = (fb_rot == 90 || fb_rot == 270) ? (int)fb_vi.xres : (int)fb_vi.yres;
    if (w) *w = pw / fb_scale;
    if (h) *h = ph / fb_scale;
    return 1;
}
static SDL_INLINE Uint32 fb_pack(Uint32 c)
{
    const Uint32 r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return ((r >> (8 - fb_vi.red.length)) << fb_vi.red.offset) | ((g >> (8 - fb_vi.green.length)) << fb_vi.green.offset) |
           ((b >> (8 - fb_vi.blue.length)) << fb_vi.blue.offset) | (fb_vi.transp.length ? (255u >> (8 - fb_vi.transp.length)) << fb_vi.transp.offset : 0);
}
static SDL_INLINE Uint32 fb_dim(Uint32 c)                                /* scanline: scale each channel */
{
    return ((((c & 0xff00ff) * (Uint32)fb_sl_keep) >> 8) & 0xff00ff) | ((((c & 0x00ff00) * (Uint32)fb_sl_keep) >> 8) & 0x00ff00);
}
static void fb_blit(const SDL_Surface *s)
{
    const int fw = (int)fb_vi.xres, fh = (int)fb_vi.yres, bpp = (int)fb_vi.bits_per_pixel / 8;
    const int rot = fb_rot == 90 || fb_rot == 270;
    const int sw = rot ? s->h : s->w, sh = rot ? s->w : s->h;          /* source size as seen on the panel */
    int dw = fw, dh = (int)((Sint64)sh * fw / sw);
    if (dh > fh) { dh = fh; dw = (int)((Sint64)sw * fh / sh); }
    if (sw * 2 == fw && sh * 2 == fh) { dw = fw; dh = fh; }            /* SDL_FBSCALE=2: exact 2x */
    const int x0 = (fw - dw) / 2, y0 = (fh - dh) / 2;
    const int page = fb_double ? 1 - fb_page : 0;
    Uint8 *base = fb_mem + (size_t)fb_fi.line_length * (fb_double ? (size_t)page * fb_vi.yres : fb_vi.yoffset) + (size_t)fb_vi.xoffset * bpp;
    const int xrgb = bpp == 4 && fb_vi.red.offset == 16 && fb_vi.green.offset == 8 && fb_vi.blue.offset == 0 && !fb_vi.transp.length;
    if (fb_xmap_key[0] != dw || fb_xmap_key[1] != sw || fb_xmap_key[2] != fb_rot || fb_xmap_key[3] != s->w) {
        SDL_free(fb_xmap);
        fb_xmap = (int *)SDL_malloc(sizeof(int) * (size_t)(dw > 0 ? dw : 1));
        fb_xmap_n = dw;
        for (int x = 0; x < dw; ++x) fb_xmap[x] = (int)((Sint64)x * sw / dw);
        fb_xmap_key[0] = dw; fb_xmap_key[1] = sw; fb_xmap_key[2] = fb_rot; fb_xmap_key[3] = s->w;
    }
    for (int y = 0; y < dh; ++y) {
        Uint8 *dst = base + (size_t)(y0 + y) * fb_fi.line_length + (size_t)x0 * bpp;
        const int py = (int)((Sint64)y * sh / dh);
        const int dark = fb_sl_period && (y0 + y - fb_sl_offset) % fb_sl_period == fb_sl_period - 1;
        if (!rot && fb_rot != 180) {                                     /* upright: one source row per panel row */
            const Uint32 *src = (const Uint32 *)((const Uint8 *)s->pixels + (size_t)py * s->pitch);
            if (xrgb && dw == sw && !dark) { SDL_memcpy(dst, src, (size_t)dw * 4); continue; }
            if (xrgb) {
                Uint32 *d32 = (Uint32 *)dst;
                if (dark) for (int x = 0; x < dw; ++x) d32[x] = fb_dim(src[fb_xmap[x]]);
                else for (int x = 0; x < dw; ++x) d32[x] = src[fb_xmap[x]];
                continue;
            }
            for (int x = 0; x < dw; ++x) {
                Uint32 c = src[fb_xmap[x]];
                if (dark) c = fb_dim(c);
                const Uint32 v = fb_pack(c);
                if (bpp == 2) { const Uint16 v16 = (Uint16)v; SDL_memcpy(dst, &v16, 2); }
                else if (bpp == 4) SDL_memcpy(dst, &v, 4);
                else { dst[fb_vi.red.offset / 8] = (Uint8)(c >> 16); dst[fb_vi.green.offset / 8] = (Uint8)(c >> 8); dst[fb_vi.blue.offset / 8] = (Uint8)c; }
                dst += bpp;
            }
            continue;
        }
        for (int x = 0; x < dw; ++x) {                                  /* rotated: general path */
            const int px = fb_xmap[x];
            int sx, sy;                                                  /* panel (px,py) -> surface (sx,sy) */
            switch (fb_rot) {
            case 90:  sx = py; sy = s->h - 1 - px; break;
            case 180: sx = s->w - 1 - px; sy = s->h - 1 - py; break;
            case 270: sx = s->w - 1 - py; sy = px; break;
            default:  sx = px; sy = py; break;
            }
            Uint32 c = ((const Uint32 *)((const Uint8 *)s->pixels + (size_t)sy * s->pitch))[sx];
            if (dark) c = fb_dim(c);
            const Uint32 v = fb_pack(c);
            if (bpp == 2) { const Uint16 v16 = (Uint16)v; SDL_memcpy(dst, &v16, 2); }
            else if (bpp == 4) SDL_memcpy(dst, &v, 4);
            else { dst[fb_vi.red.offset / 8] = (Uint8)(c >> 16); dst[fb_vi.green.offset / 8] = (Uint8)(c >> 8); dst[fb_vi.blue.offset / 8] = (Uint8)c; }
            dst += bpp;
        }
    }
    if (fb_double) {
        fb_vi.yoffset = (Uint32)page * fb_vi.yres;
        if (ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_vi) == 0) fb_page = page;
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
