// MPC Arcade library app: software drawing on a 32-bit XRGB surface (no GPU, no font library needed on the MPC).
#pragma once
#include <stdint.h>

typedef struct { uint32_t* px; int w, h, pitch; } Canvas;          // pitch in pixels
typedef struct { uint32_t* px; int w, h; } Image;                  // ARGB, straight alpha
enum FontId { F_SMALL, F_BODY, F_BOLD, F_TITLE, F_HUGE, F_COUNT };

#define RGB(r, g, b) ((uint32_t) (((r) << 16) | ((g) << 8) | (b)))
// palette (MPC-style: charcoal panels, white text, MPC red accent)
enum
{
    C_BG = RGB (17, 17, 19), C_PANEL = RGB (30, 31, 35), C_PANEL2 = RGB (42, 43, 49), C_PANEL3 = RGB (56, 58, 66),
    C_LINE = RGB (64, 66, 76), C_TEXT = RGB (236, 236, 240), C_DIM = RGB (150, 152, 164), C_FAINT = RGB (100, 102, 112),
    C_ACCENT = RGB (232, 57, 47), C_ACCENT_DK = RGB (130, 32, 28), C_GOLD = RGB (245, 175, 40), C_GOOD = RGB (62, 207, 106),
    C_WARN = RGB (245, 165, 36), C_BAD = RGB (239, 74, 74), C_BLUE = RGB (56, 140, 230), C_TEAL = RGB (40, 200, 180),
    C_PURPLE = RGB (150, 100, 230), C_BLACK = 0
};

void gfx_bind (Canvas* c);                 // all drawing goes to this canvas
Canvas* gfx_canvas (void);
void gfx_clip (int x, int y, int w, int h); // intersect-free: sets the clip rectangle
void gfx_unclip (void);
void gfx_fill (int x, int y, int w, int h, uint32_t col);
void gfx_blend (int x, int y, int w, int h, uint32_t col, int alpha);      // alpha 0..255
void gfx_vgrad (int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_round (int x, int y, int w, int h, int r, uint32_t col, int alpha);
void gfx_round_outline (int x, int y, int w, int h, int r, int t, uint32_t col, int alpha);
void gfx_shadow (int x, int y, int w, int h, int r, int spread, int alpha);
void gfx_hline (int x, int y, int w, uint32_t col);
void gfx_circle (int cx, int cy, int r, uint32_t col, int alpha);

int gfx_text (int font, int x, int y, const char* s, uint32_t col);        // y = top of the line box; returns width
int gfx_text_w (int font, const char* s);
int gfx_text_h (int font);
void gfx_text_c (int font, int x, int y, int w, int h, const char* s, uint32_t col);  // centred in a box
int gfx_text_fit (int font, int x, int y, int maxw, const char* s, uint32_t col);    // ellipsis if too long
int gfx_text_wrap (int font, int x, int y, int w, int maxLines, const char* s, uint32_t col, int lineGap);

Image* img_load (const char* path);                    // PNG / JPEG / BMP; NULL if missing or broken
Image* img_scaled (const Image* src, int w, int h);    // area-averaged resize
void img_free (Image* im);
void gfx_image (const Image* im, int x, int y, int alpha);
void gfx_image_fit (const Image* im, int x, int y, int w, int h, int alpha);   // keeps aspect, centred, via cache
void gfx_image_cache_clear (void);
