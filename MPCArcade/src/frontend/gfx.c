// MPC Arcade library app: software drawing (see gfx.h).
#include "gfx.h"
#include "font_data.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO_FAIL
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../third_party/stb_image.h"
#pragma GCC diagnostic pop

static Canvas* cv;
static int clx0, cly0, clx1, cly1;

void gfx_bind (Canvas* c) { cv = c; gfx_unclip(); }
Canvas* gfx_canvas (void) { return cv; }
void gfx_unclip (void) { clx0 = 0; cly0 = 0; clx1 = cv->w; cly1 = cv->h; }
void gfx_clip (int x, int y, int w, int h)
{
    clx0 = x < 0 ? 0 : x; cly0 = y < 0 ? 0 : y;
    clx1 = x + w > cv->w ? cv->w : x + w; cly1 = y + h > cv->h ? cv->h : y + h;
}
static inline int clipr (int* x, int* y, int* w, int* h)
{
    int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
    if (x0 < clx0) x0 = clx0; if (y0 < cly0) y0 = cly0;
    if (x1 > clx1) x1 = clx1; if (y1 > cly1) y1 = cly1;
    if (x1 <= x0 || y1 <= y0) return 0;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0; return 1;
}
static inline uint32_t mixc (uint32_t d, uint32_t s, int a)
{
    const uint32_t rb = ((((s & 0xff00ff) - (d & 0xff00ff)) * (uint32_t) a) >> 8) + (d & 0xff00ff);
    const uint32_t g = ((((s & 0x00ff00) - (d & 0x00ff00)) * (uint32_t) a) >> 8) + (d & 0x00ff00);
    return (rb & 0xff00ff) | (g & 0x00ff00);
}
// a version safe for "negative" differences (unsigned wrap is fine per channel only when computed separately)
static inline uint32_t blendpx (uint32_t d, uint32_t s, int a)
{
    if (a >= 255) return s;
    if (a <= 0) return d;
    const int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    const int sr = (s >> 16) & 255, sg = (s >> 8) & 255, sb = s & 255;
    return RGB (dr + (((sr - dr) * a) >> 8), dg + (((sg - dg) * a) >> 8), db + (((sb - db) * a) >> 8));
}
void gfx_fill (int x, int y, int w, int h, uint32_t col)
{
    if (! clipr (&x, &y, &w, &h)) return;
    for (int j = 0; j < h; ++j)
    {
        uint32_t* p = cv->px + (size_t) (y + j) * cv->pitch + x;
        for (int i = 0; i < w; ++i) p[i] = col;
    }
}
void gfx_blend (int x, int y, int w, int h, uint32_t col, int alpha)
{
    if (alpha >= 255) { gfx_fill (x, y, w, h, col); return; }
    if (alpha <= 0 || ! clipr (&x, &y, &w, &h)) return;
    for (int j = 0; j < h; ++j)
    {
        uint32_t* p = cv->px + (size_t) (y + j) * cv->pitch + x;
        for (int i = 0; i < w; ++i) p[i] = blendpx (p[i], col, alpha);
    }
}
void gfx_vgrad (int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; ++j) gfx_fill (x, y + j, w, 1, blendpx (top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}
void gfx_hline (int x, int y, int w, uint32_t col) { gfx_fill (x, y, w, 1, col); }

// coverage of pixel (px,py) for a rounded rectangle corner centred at (cx,cy) with radius r
static inline int cornerCov (float px, float py, float cx, float cy, float r)
{
    const float d = sqrtf ((px - cx) * (px - cx) + (py - cy) * (py - cy));
    const float c = r - d + 0.5f;
    return c <= 0 ? 0 : (c >= 1 ? 255 : (int) (c * 255));
}
void gfx_round (int x, int y, int w, int h, int r, uint32_t col, int alpha)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r <= 0) { gfx_blend (x, y, w, h, col, alpha); return; }
    gfx_blend (x, y + r, w, h - 2 * r, col, alpha);
    for (int j = 0; j < r; ++j)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            const int yy = pass ? y + h - 1 - j : y + j;
            const float cyy = pass ? (float) (y + h - r) : (float) (y + r);
            // full-coverage middle span
            gfx_blend (x + r, yy, w - 2 * r, 1, col, alpha);
            for (int i = 0; i < r; ++i)
            {
                const int cl = cornerCov (x + i + 0.5f, yy + 0.5f, (float) (x + r), cyy, (float) r);
                const int cr = cornerCov (x + w - 1 - i + 0.5f, yy + 0.5f, (float) (x + w - r), cyy, (float) r);
                if (cl) gfx_blend (x + i, yy, 1, 1, col, cl * alpha / 255);
                if (cr) gfx_blend (x + w - 1 - i, yy, 1, 1, col, cr * alpha / 255);
            }
        }
    }
}
void gfx_round_outline (int x, int y, int w, int h, int r, int t, uint32_t col, int alpha)
{
    // draw as a ring: outer coverage minus inner coverage, per pixel
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    const int ri = r - t > 0 ? r - t : 0;
    for (int yy = y; yy < y + h; ++yy)
    {
        for (int xx = x; xx < x + w; ++xx)
        {
            const int inEdge = xx < x + t || xx >= x + w - t || yy < y + t || yy >= y + h - t;
            const int nearCorner = (xx < x + r || xx >= x + w - r) && (yy < y + r || yy >= y + h - r);
            if (! nearCorner) { if (inEdge) gfx_blend (xx, yy, 1, 1, col, alpha); continue; }
            const float cx = xx < x + r ? (float) (x + r) : (float) (x + w - r);
            const float cy = yy < y + r ? (float) (y + r) : (float) (y + h - r);
            const int co = cornerCov (xx + 0.5f, yy + 0.5f, cx, cy, (float) r);
            const int ci = ri > 0 ? cornerCov (xx + 0.5f, yy + 0.5f, cx, cy, (float) ri) : 0;
            const int c = co - ci;
            if (c > 0) gfx_blend (xx, yy, 1, 1, col, c * alpha / 255);
        }
    }
}
void gfx_shadow (int x, int y, int w, int h, int r, int spread, int alpha)
{
    for (int k = spread; k > 0; --k) gfx_round (x - k, y - k + spread / 2, w + 2 * k, h + 2 * k, r + k, C_BLACK, alpha / spread);
}
void gfx_circle (int cx, int cy, int r, uint32_t col, int alpha) { gfx_round (cx - r, cy - r, 2 * r, 2 * r, r, col, alpha); }

// ---------------------------------------------------------------- text
static uint32_t nextCp (const char** s)
{
    const unsigned char* p = (const unsigned char*) *s;
    uint32_t c = *p++;
    if (c >= 0xf0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xe0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xc0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char*) p;
    return c;
}
static const Glyph* glyph (const FontData* f, uint32_t cp)
{
    // ASCII and Latin-1 are at fixed positions; symbols searched
    if (cp >= 32 && cp < 127) return &f->g[cp - 32];
    if (cp >= 0xa0 && cp < 0x100) return &f->g[95 + cp - 0xa0];
    for (int i = 95 + 96; i < f->nglyphs; ++i) if (f->g[i].cp == cp) return &f->g[i];
    return &f->g['?' - 32];
}
int gfx_text_h (int font) { return kFonts[font].height; }
int gfx_text_w (int font, const char* s)
{
    const FontData* f = &kFonts[font];
    int w = 0;
    while (*s) w += glyph (f, nextCp (&s))->adv;
    return w;
}
static int drawGlyph (const FontData* f, const Glyph* g, int x, int y, uint32_t col)
{
    for (int j = 0; j < g->h; ++j)
    {
        const int yy = y + g->oy + j;
        if (yy < cly0 || yy >= cly1) continue;
        const uint8_t* a = f->atlas + (size_t) (g->y + j) * f->aw + g->x;
        uint32_t* p = cv->px + (size_t) yy * cv->pitch;
        for (int i = 0; i < g->w; ++i)
        {
            const int xx = x + g->ox + i;
            if (xx < clx0 || xx >= clx1 || a[i] == 0) continue;
            p[xx] = blendpx (p[xx], col, a[i]);
        }
    }
    return g->adv;
}
int gfx_text (int font, int x, int y, const char* s, uint32_t col)
{
    const FontData* f = &kFonts[font];
    const int x0 = x;
    while (*s) x += drawGlyph (f, glyph (f, nextCp (&s)), x, y, col);
    return x - x0;
}
void gfx_text_c (int font, int x, int y, int w, int h, const char* s, uint32_t col)
{
    const int tw = gfx_text_w (font, s);
    gfx_text (font, x + (w - tw) / 2, y + (h - kFonts[font].height) / 2, s, col);
}
int gfx_text_fit (int font, int x, int y, int maxw, const char* s, uint32_t col)
{
    if (gfx_text_w (font, s) <= maxw) return gfx_text (font, x, y, s, col);
    const FontData* f = &kFonts[font];
    const int ell = glyph (f, 0x2026)->adv;
    int w = 0;
    const char* p = s;
    while (*p)
    {
        const char* q = p;
        const int a = glyph (f, nextCp (&q))->adv;
        if (w + a + ell > maxw) break;
        w += a; p = q;
    }
    int cx = x;
    for (const char* q = s; q < p;) cx += drawGlyph (f, glyph (f, nextCp (&q)), cx, y, col);
    cx += drawGlyph (f, glyph (f, 0x2026), cx, y, col);
    return cx - x;
}
int gfx_text_wrap (int font, int x, int y, int w, int maxLines, const char* s, uint32_t col, int lineGap)
{
    const FontData* f = &kFonts[font];
    int lines = 0;
    char line[512];
    while (*s && lines < maxLines)
    {
        // take words while they fit
        int n = 0, lastSpace = -1, lw = 0;
        const char* p = s;
        while (*p && *p != '\n')
        {
            const char* q = p;
            const int a = glyph (f, nextCp (&q))->adv;
            if (lw + a > w && n > 0) break;
            if (*p == ' ') lastSpace = n;
            while (p < q && n < (int) sizeof line - 1) line[n++] = *p++;
            lw += a;
        }
        if (*p && *p != '\n' && lastSpace > 0) { n = lastSpace; p = s + lastSpace + 1; }
        else if (*p == '\n') ++p;
        line[n] = 0;
        if (lines == maxLines - 1 && *p) gfx_text_fit (font, x, y, w, line, col), (void) 0;
        else gfx_text (font, x, y, line, col);
        y += f->height + lineGap; ++lines; s = p;
        while (*s == ' ') ++s;
    }
    return lines;
}

// ---------------------------------------------------------------- images
Image* img_load (const char* path)
{
    int w, h, n;
    unsigned char* d = stbi_load (path, &w, &h, &n, 4);
    if (d == NULL) return NULL;
    Image* im = (Image*) malloc (sizeof (Image));
    im->w = w; im->h = h;
    im->px = (uint32_t*) d;                                   // reuse the buffer: RGBA bytes -> ARGB words in place
    for (int i = 0; i < w * h; ++i)
    {
        const unsigned char* q = d + (size_t) i * 4;
        im->px[i] = ((uint32_t) q[3] << 24) | ((uint32_t) q[0] << 16) | ((uint32_t) q[1] << 8) | q[2];
    }
    return im;
}
void img_free (Image* im) { if (im) { free (im->px); free (im); } }
Image* img_scaled (const Image* src, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    Image* im = (Image*) malloc (sizeof (Image));
    im->w = w; im->h = h; im->px = (uint32_t*) malloc ((size_t) w * h * 4);
    for (int y = 0; y < h; ++y)
    {
        const int sy0 = y * src->h / h, sy1t = (y + 1) * src->h / h, sy1 = sy1t > sy0 ? sy1t : sy0 + 1;
        for (int x = 0; x < w; ++x)
        {
            const int sx0 = x * src->w / w, sx1t = (x + 1) * src->w / w, sx1 = sx1t > sx0 ? sx1t : sx0 + 1;
            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < src->h; ++sy)
                for (int sx = sx0; sx < sx1 && sx < src->w; ++sx)
                {
                    const uint32_t c = src->px[(size_t) sy * src->w + sx];
                    a += c >> 24; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; ++n;
                }
            if (n == 0) n = 1;
            im->px[(size_t) y * w + x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    return im;
}
void gfx_image (const Image* im, int x, int y, int alpha)
{
    for (int j = 0; j < im->h; ++j)
    {
        const int yy = y + j;
        if (yy < cly0 || yy >= cly1) continue;
        uint32_t* p = cv->px + (size_t) yy * cv->pitch;
        const uint32_t* s = im->px + (size_t) j * im->w;
        for (int i = 0; i < im->w; ++i)
        {
            const int xx = x + i;
            if (xx < clx0 || xx >= clx1) continue;
            const int a = (int) (s[i] >> 24) * alpha / 255;
            if (a) p[xx] = blendpx (p[xx], s[i] & 0xffffff, a);
        }
    }
}
// small cache of resized images (key: source pointer + size)
typedef struct { const Image* src; int w, h; Image* im; unsigned age; } CacheEnt;
static CacheEnt cache[12];
static unsigned cacheClock;
void gfx_image_cache_clear (void)
{
    for (int i = 0; i < 12; ++i) { img_free (cache[i].im); memset (&cache[i], 0, sizeof cache[i]); }
}
void gfx_image_fit (const Image* im, int x, int y, int w, int h, int alpha)
{
    if (im == NULL || im->w <= 0 || im->h <= 0) return;
    int dw = w, dh = (int) ((long long) im->h * w / im->w);
    if (dh > h) { dh = h; dw = (int) ((long long) im->w * h / im->h); }
    Image* sc = NULL;
    int oldest = 0;
    for (int i = 0; i < 12; ++i)
    {
        if (cache[i].src == im && cache[i].w == dw && cache[i].h == dh) { sc = cache[i].im; cache[i].age = ++cacheClock; break; }
        if (cache[i].age < cache[oldest].age) oldest = i;
    }
    if (sc == NULL)
    {
        // pixel-art friendly: integer nearest-neighbour upscale when growing, area-average when shrinking
        if (dw >= im->w)
        {
            sc = (Image*) malloc (sizeof (Image)); sc->w = dw; sc->h = dh; sc->px = (uint32_t*) malloc ((size_t) dw * dh * 4);
            for (int j = 0; j < dh; ++j) for (int i = 0; i < dw; ++i)
                sc->px[(size_t) j * dw + i] = im->px[(size_t) (j * im->h / dh) * im->w + i * im->w / dw];
        }
        else sc = img_scaled (im, dw, dh);
        img_free (cache[oldest].im);
        cache[oldest].src = im; cache[oldest].w = dw; cache[oldest].h = dh; cache[oldest].im = sc; cache[oldest].age = ++cacheClock;
    }
    gfx_image (sc, x + (w - dw) / 2, y + (h - dh) / 2, alpha);
}
