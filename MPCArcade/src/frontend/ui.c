// MPC Arcade library app: interaction, widgets, modals, UI sounds (see ui.h).
#include "ui.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Ui U;
#define ID_MODAL_BASE 900000

// ------------------------------------------------------------------ sounds (synthesised, optional)
static SDL_AudioDeviceID adev;
static volatile int sndWhich = -1, sndPos; static int sndVol = 50, sndRate = 44100;
static void audioCb (void* u, Uint8* stream, int len)
{
    (void) u;
    Sint16* out = (Sint16*) stream; const int n = len / 4;
    for (int i = 0; i < n; ++i)
    {
        float v = 0;
        const int w = sndWhich;
        if (w >= 0)
        {
            const float t = (float) sndPos / sndRate;
            float f = 0, dur = 0.05f;
            switch (w)
            {
                case SND_TICK: f = 1800; dur = 0.018f; break;
                case SND_OK: f = t < 0.045f ? 880 : 1320; dur = 0.11f; break;
                case SND_BACK: f = t < 0.045f ? 660 : 440; dur = 0.1f; break;
                case SND_LAUNCH: f = t < 0.07f ? 988 : (t < 0.14f ? 1319 : 1976); dur = 0.32f; break;
                default: f = 180; dur = 0.2f; break;
            }
            if (t < dur)
            {
                const float env = 1.f - t / dur;
                v = (fmodf (t * f, 1.f) < 0.5f ? 0.25f : -0.25f) * env * env;
                ++sndPos;
            }
            else sndWhich = -1;
        }
        const Sint16 s = (Sint16) (v * sndVol / 100.f * 32767.f);
        out[2 * i] = out[2 * i + 1] = s;
    }
}
void snd_init (int on, int volume)
{
    U.sounds = on; sndVol = volume;
    if (! on || adev) return;
    if (SDL_InitSubSystem (SDL_INIT_AUDIO) != 0) { U.sounds = 0; return; }
    SDL_AudioSpec want, have; SDL_zero (want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = audioCb;
    adev = SDL_OpenAudioDevice (NULL, 0, &want, &have, 0);
    if (adev == 0) { U.sounds = 0; return; }
    sndRate = have.freq;
    SDL_PauseAudioDevice (adev, 0);
}
void snd_play (int which)
{
    if (! U.sounds || ! adev) return;
    SDL_LockAudioDevice (adev); sndPos = 0; sndWhich = which; SDL_UnlockAudioDevice (adev);
}
void snd_close (void) { if (adev) { SDL_CloseAudioDevice (adev); adev = 0; } }

// ------------------------------------------------------------------ frame + hits
void ui_begin_frame (void) { U.nhits = 0; }
void ui_end_frame (void)
{
    memcpy (U.prevHits, U.hits, sizeof (Hit) * (size_t) U.nhits); U.nprev = U.nhits;
    U.clicked = 0; U.key = NK_NONE; U.text[0] = 0;
}
void ui_hit (int id, int x, int y, int w, int h)
{
    if (U.nhits >= 600) return;
    // when a modal is open only its own hits count
    if (ui_modal_active() && id < ID_MODAL_BASE) return;
    Hit* h0 = &U.hits[U.nhits++]; h0->id = id; h0->x = x; h0->y = y; h0->w = w; h0->h = h; h0->scroll = -1;
}
void ui_scroll_area (int area, int x, int y, int w, int h, int contentH)
{
    U.scrollMax[area] = contentH > h ? contentH - h : 0;
    if (U.scroll[area] > U.scrollMax[area]) U.scroll[area] = (float) U.scrollMax[area];
    if (U.scroll[area] < 0) U.scroll[area] = 0;
    if (U.nhits >= 600 || ui_modal_active()) return;
    Hit* h0 = &U.hits[U.nhits++]; h0->id = -1 - area; h0->x = x; h0->y = y; h0->w = w; h0->h = h; h0->scroll = area;
}
static int hitAt (int x, int y, int wantScroll)
{
    for (int i = U.nprev - 1; i >= 0; --i)
    {
        const Hit* h = &U.prevHits[i];
        if (x < h->x || y < h->y || x >= h->x + h->w || y >= h->y + h->h) continue;
        if (wantScroll) { if (h->scroll >= 0) return h->scroll; continue; }
        if (h->scroll < 0) return h->id;
    }
    return wantScroll ? -1 : 0;
}
void ui_pointer (int type, int x, int y)
{
    U.lastInput = U.now;
    if (type == 0)
    {
        U.down = 1; U.px = U.downX = x; U.py = U.downY = y; U.downT = U.lastMoveT = U.now;
        U.pressed = hitAt (x, y, 0); U.scrollArea = hitAt (x, y, 1); U.dragging = 0; U.vel = 0;
        if (U.scrollArea >= 0) U.kin[U.scrollArea] = 0;
    }
    else if (type == 1 && U.down)
    {
        const int dy = y - U.py;
        if (! U.dragging && U.scrollArea >= 0 && abs (y - U.downY) > 14) { U.dragging = 1; U.pressed = 0; }
        if (U.dragging)
        {
            U.scroll[U.scrollArea] -= (float) dy;
            const double dt = U.now - U.lastMoveT;
            if (dt > 0) U.vel = 0.6f * U.vel + 0.4f * (float) (-dy / dt);
            U.lastMoveT = U.now;
        }
        else if (abs (x - U.downX) > 24 || abs (y - U.downY) > 24) U.pressed = 0;      // slid off: no tap
        U.px = x; U.py = y; U.anim = 1;
    }
    else if (type == 2 && U.down)
    {
        U.down = 0; U.px = x; U.py = y;
        if (U.dragging) { if (U.now - U.lastMoveT < 0.08) U.kin[U.scrollArea] = U.vel; }
        else if (U.pressed && hitAt (x, y, 0) == U.pressed) U.clicked = U.pressed;
        U.pressed = 0; U.dragging = 0; U.anim = 1;
    }
}
void ui_key (int nk, const char* text)
{
    U.lastInput = U.now;
    U.key = nk;
    if (text) snprintf (U.text, sizeof U.text, "%s", text);
}
void ui_tick (double dt)
{
    U.anim = 0;
    for (int a = 0; a < 16; ++a)
    {
        if (fabsf (U.kin[a]) < 20.f) { U.kin[a] = 0; continue; }
        U.scroll[a] += U.kin[a] * (float) dt;
        U.kin[a] *= powf (0.04f, (float) dt);                          // ~ -96 % per second
        if (U.scroll[a] < 0) { U.scroll[a] = 0; U.kin[a] = 0; }
        if (U.scroll[a] > U.scrollMax[a]) { U.scroll[a] = (float) U.scrollMax[a]; U.kin[a] = 0; }
        U.anim = 1;
    }
}
void ui_scroll_to (int area, int y, int h, int viewH)
{
    if (y < U.scroll[area]) U.scroll[area] = (float) y;
    if (y + h > U.scroll[area] + viewH) U.scroll[area] = (float) (y + h - viewH);
    U.kin[area] = 0;
}
int ui_clicked (int id) { if (U.clicked == id && id) { U.clicked = 0; return 1; } return 0; }
int ui_pressed (int id) { return U.down && U.pressed == id && id; }
int ui_take_key (int k) { if (U.key == k) { U.key = NK_NONE; return 1; } return 0; }

// ------------------------------------------------------------------ widgets
int ui_button (int id, int x, int y, int w, int h, const char* label, int style, int focused)
{
    ui_hit (id, x, y, w, h);
    const int down = ui_pressed (id);
    uint32_t bg = C_PANEL3, fg = C_TEXT; int font = F_BODY, r = 8;
    switch (style)
    {
        case B_PRIMARY: bg = down ? C_ACCENT_DK : C_ACCENT; font = F_BOLD; break;
        case B_DANGER: bg = down ? RGB (90, 24, 24) : RGB (120, 34, 34); break;
        case B_GHOST: bg = down ? C_PANEL3 : C_PANEL2; fg = C_TEXT; break;
        case B_TAB: bg = down ? C_PANEL3 : C_PANEL; fg = C_DIM; r = 6; break;
        case B_TAB_ON: bg = C_PANEL3; fg = C_TEXT; r = 6; break;
        case B_SMALL: bg = down ? C_PANEL : C_PANEL3; font = F_SMALL; r = 6; break;
        default: bg = down ? C_PANEL2 : C_PANEL3; break;
    }
    gfx_round (x, y + (down ? 1 : 0), w, h, r, bg, 255);
    if (style == B_TAB_ON) gfx_fill (x + 10, y + h - 4, w - 20, 3, C_ACCENT);
    if (focused) gfx_round_outline (x - 3, y - 3, w + 6, h + 6, r + 3, 2, C_TEXT, 230);
    gfx_text_c (font, x, y + (down ? 1 : 0), w, h, label, fg);
    if (ui_clicked (id)) { snd_play (style == B_PRIMARY ? SND_OK : SND_TICK); return 1; }
    return 0;
}
void ui_badge (int x, int y, const char* s, uint32_t col)
{
    const int w = gfx_text_w (F_SMALL, s) + 16;
    gfx_round (x, y, w, 24, 12, col, 60);
    gfx_round_outline (x, y, w, 24, 12, 1, col, 200);
    gfx_text_c (F_SMALL, x, y - 1, w, 24, s, col);
}
int ui_toggle_row (int id, int x, int y, int w, const char* label, const char* value, const char* help, int focused)
{
    const int h = help && help[0] ? 74 : 56;
    ui_hit (id, x, y, w, h);
    gfx_round (x, y, w, h - 6, 8, ui_pressed (id) ? C_PANEL3 : C_PANEL2, 255);
    if (focused) gfx_round_outline (x - 2, y - 2, w + 4, h - 2, 10, 2, C_ACCENT, 255);
    gfx_text (F_BODY, x + 18, y + 9, label, C_TEXT);
    if (help && help[0]) gfx_text_fit (F_SMALL, x + 18, y + 38, w - 360, help, C_DIM);
    const int vw = gfx_text_w (F_BODY, value);
    gfx_text (F_BODY, x + w - 46 - vw, y + (h - 6 - gfx_text_h (F_BODY)) / 2, value, C_GOLD);
    gfx_text (F_BODY, x + w - 32, y + (h - 6 - gfx_text_h (F_BODY)) / 2, "\xe2\x96\xb6", C_DIM);
    return ui_clicked (id);
}
static char toastMsg[160]; static double toastUntil;
void ui_toast (const char* msg) { snprintf (toastMsg, sizeof toastMsg, "%s", msg); toastUntil = U.now + 3.0; }
void ui_draw_toast (void)
{
    if (U.now > toastUntil || ! toastMsg[0]) return;
    const int w = gfx_text_w (F_BODY, toastMsg) + 48, x = (U.W - w) / 2, y = 84;
    gfx_shadow (x, y, w, 52, 26, 6, 120);
    gfx_round (x, y, w, 52, 26, C_PANEL3, 250);
    gfx_text_c (F_BODY, x, y, w, 52, toastMsg, C_TEXT);
    U.anim = 1;
}

// ------------------------------------------------------------------ modals
enum { M_NONE, M_CONFIRM, M_PICK, M_MSG };
static struct { int kind; char title[96]; char text[1200]; char yes[32]; const char* const* items; int n, cur, focus; PickFn fn; void* ctx; } M;
int ui_modal_active (void) { return M.kind != M_NONE; }
void ui_modal_close (void) { M.kind = M_NONE; }
void ui_confirm (const char* title, const char* text, const char* yes, PickFn fn, void* ctx)
{
    M.kind = M_CONFIRM; snprintf (M.title, sizeof M.title, "%s", title); snprintf (M.text, sizeof M.text, "%s", text);
    snprintf (M.yes, sizeof M.yes, "%s", yes); M.fn = fn; M.ctx = ctx; M.focus = 1;
}
void ui_pick (const char* title, const char* const* items, int n, int current, PickFn fn, void* ctx)
{
    M.kind = M_PICK; snprintf (M.title, sizeof M.title, "%s", title); M.items = items; M.n = n; M.cur = current;
    M.focus = current < 0 ? 0 : current; M.fn = fn; M.ctx = ctx; U.scroll[15] = 0;
    ui_scroll_to (15, M.focus * 62, 62, U.H - 240);
}
void ui_message (const char* title, const char* text)
{
    M.kind = M_MSG; snprintf (M.title, sizeof M.title, "%s", title); snprintf (M.text, sizeof M.text, "%s", text); M.fn = NULL;
}
void ui_modal_draw (void)
{
    if (M.kind == M_NONE) return;
    gfx_blend (0, 0, U.W, U.H, C_BLACK, 170);
    if (M.kind == M_CONFIRM || M.kind == M_MSG)
    {
        const int w = 720, h = M.kind == M_MSG ? 460 : 340, x = (U.W - w) / 2, y = (U.H - h) / 2;
        gfx_shadow (x, y, w, h, 14, 10, 160);
        gfx_round (x, y, w, h, 14, C_PANEL, 255);
        gfx_text (F_TITLE, x + 32, y + 22, M.title, C_TEXT);
        gfx_text_wrap (F_BODY, x + 32, y + 84, w - 64, M.kind == M_MSG ? 9 : 5, M.text, C_DIM, 4);
        if (M.kind == M_MSG)
        {
            if (ui_button (ID_MODAL_BASE + 1, x + w - 212, y + h - 84, 180, 60, "OK", B_PRIMARY, 1) || ui_take_key (NK_OK) || ui_take_key (NK_BACK))
                M.kind = M_NONE;
            return;
        }
        if (ui_take_key (NK_LEFT) || ui_take_key (NK_RIGHT)) M.focus = 1 - M.focus;
        int choice = -1;
        if (ui_button (ID_MODAL_BASE + 1, x + w - 432, y + h - 84, 190, 60, "Cancel", B_GHOST, M.focus == 0)) choice = 0;
        if (ui_button (ID_MODAL_BASE + 2, x + w - 222, y + h - 84, 190, 60, M.yes, B_PRIMARY, M.focus == 1)) choice = 1;
        if (ui_take_key (NK_OK)) choice = M.focus;
        if (ui_take_key (NK_BACK)) choice = 0;
        if (choice >= 0) { PickFn fn = M.fn; void* ctx = M.ctx; M.kind = M_NONE; if (choice == 1 && fn) fn (1, ctx); else snd_play (SND_BACK); }
        return;
    }
    // pick list
    const int maxV = U.H - 240, w = 620, rowH = 62, viewH = M.n * rowH < maxV ? M.n * rowH : maxV, h = viewH + 110, x = (U.W - w) / 2, y = (U.H - h) / 2;
    gfx_shadow (x, y, w, h, 14, 10, 160);
    gfx_round (x, y, w, h, 14, C_PANEL, 255);
    gfx_text (F_BOLD, x + 28, y + 18, M.title, C_TEXT);
    if (ui_take_key (NK_UP) && M.focus > 0) { --M.focus; ui_scroll_to (15, M.focus * rowH, rowH, viewH); snd_play (SND_TICK); }
    if (ui_take_key (NK_DOWN) && M.focus < M.n - 1) { ++M.focus; ui_scroll_to (15, M.focus * rowH, rowH, viewH); snd_play (SND_TICK); }
    // scroll area for the list (modal-owned)
    U.scrollMax[15] = M.n * rowH > viewH ? M.n * rowH - viewH : 0;
    if (U.nhits < 600) { Hit* h0 = &U.hits[U.nhits++]; h0->id = -16; h0->x = x; h0->y = y + 70; h0->w = w; h0->h = viewH; h0->scroll = 15; }
    gfx_clip (x, y + 70, w, viewH);
    int chosen = -1;
    for (int i = 0; i < M.n; ++i)
    {
        const int ry = y + 70 + i * rowH - (int) U.scroll[15];
        if (ry + rowH < y + 70 || ry > y + 70 + viewH) continue;
        const int id = ID_MODAL_BASE + 100 + i;
        if (U.nhits < 600) { Hit* h0 = &U.hits[U.nhits++]; h0->id = id; h0->x = x + 12; h0->y = ry > y + 70 ? ry : y + 70; h0->w = w - 24; h0->h = rowH - 4; h0->scroll = -1; }
        gfx_round (x + 12, ry, w - 24, rowH - 6, 8, i == M.focus ? C_PANEL3 : (ui_pressed (id) ? C_PANEL3 : C_PANEL2), 255);
        if (i == M.cur) gfx_fill (x + 12, ry + 10, 4, rowH - 26, C_ACCENT);
        gfx_text_fit (F_BODY, x + 32, ry + (rowH - 6 - gfx_text_h (F_BODY)) / 2, w - 90, M.items[i], i == M.cur ? C_GOLD : C_TEXT);
        if (ui_clicked (id)) chosen = i;
    }
    gfx_unclip();
    if (ui_take_key (NK_OK)) chosen = M.focus;
    if (ui_take_key (NK_BACK)) { M.kind = M_NONE; snd_play (SND_BACK); return; }
    if (chosen >= 0) { PickFn fn = M.fn; void* ctx = M.ctx; M.kind = M_NONE; snd_play (SND_OK); if (fn) fn (chosen, ctx); }
}

// ------------------------------------------------------------------ on-screen keyboard
int ui_keyboard (int x, int y, int w, int h, char* buf, int n, int* focus)
{
    static const char* rows[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-.&" };
    const int cols = 10, kh = (h - 5 * 8) / 5, kw = (w - 11 * 8) / cols;
    gfx_round (x - 8, y - 8, w + 16, h + 16, 12, C_PANEL, 250);
    int changed = 0;
    const int nkeys = 4 * cols + 3;        // + space, delete, clear
    if (ui_take_key (NK_LEFT) && *focus > 0) --*focus;
    if (ui_take_key (NK_RIGHT) && *focus < nkeys - 1) ++*focus;
    if (ui_take_key (NK_UP) && *focus >= cols) *focus -= cols;
    if (ui_take_key (NK_DOWN) && *focus < nkeys - cols) *focus = *focus + cols < 4 * cols ? *focus + cols : 4 * cols + (*focus % cols) * 3 / cols;
    const int ok = ui_take_key (NK_OK);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int k = r * cols + c, kx = x + 8 + c * (kw + 8), ky = y + 8 + r * (kh + 8);
            char lab[2] = { rows[r][c], 0 };
            if (ui_button (800000 + k, kx, ky, kw, kh, lab, B_NORMAL, *focus == k) || (ok && *focus == k))
            {
                const int len = (int) strlen (buf);
                if (len < n - 1) { buf[len] = (char) (lab[0] >= 'A' && lab[0] <= 'Z' ? lab[0] + 32 : lab[0]); buf[len + 1] = 0; changed = 1; }
            }
        }
    const int by = y + 8 + 4 * (kh + 8), bw = (w - 4 * 8) / 3;
    if (ui_button (800100, x + 8, by, bw, kh, "Space", B_NORMAL, *focus == 40) || (ok && *focus == 40))
    { const int len = (int) strlen (buf); if (len < n - 1 && len > 0) { buf[len] = ' '; buf[len + 1] = 0; changed = 1; } }
    if (ui_button (800101, x + 16 + bw, by, bw, kh, "\xe2\x8c\xab Delete", B_NORMAL, *focus == 41) || (ok && *focus == 41) || ui_take_key (NK_BKSP))
    { const int len = (int) strlen (buf); if (len > 0) { buf[len - 1] = 0; changed = 1; } }
    if (ui_button (800102, x + 24 + 2 * bw, by, bw, kh, "Clear", B_NORMAL, *focus == 42) || (ok && *focus == 42))
    { if (buf[0]) { buf[0] = 0; changed = 1; } }
    if (U.key == NK_TEXT && U.text[0])                             // a USB keyboard works too
    {
        const int len = (int) strlen (buf);
        const char ch = U.text[0];
        if (len < n - 1 && ch >= 32 && ch < 127) { buf[len] = (char) (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch); buf[len + 1] = 0; changed = 1; }
        U.key = NK_NONE;
    }
    return changed;
}
