// MPC Arcade library app: touch + pad interaction and widgets (immediate mode: draw and handle in one pass).
#pragma once
#include "gfx.h"

enum NavKey { NK_NONE, NK_UP, NK_DOWN, NK_LEFT, NK_RIGHT, NK_OK, NK_BACK, NK_FAV, NK_MENU, NK_PGUP, NK_PGDN, NK_TEXT, NK_BKSP };

typedef struct { int id, x, y, w, h, scroll; } Hit;
typedef struct
{
    int W, H;                       // screen size
    // pointer (touch or mouse)
    int px, py, down, downX, downY, pressed, scrollArea, dragging, clicked, longPress;
    double downT, lastMoveT; float vel;
    // scroll areas: offset in px, kinetic velocity
    float scroll[16]; float kin[16]; int scrollMax[16];
    // keys
    int key; char text[8];
    // hits of the current frame and the previous one
    Hit hits[600], prevHits[600]; int nhits, nprev;
    double now, lastInput;
    int anim;                       // something is moving: redraw at full rate
    int sounds;
} Ui;
extern Ui U;

enum { SND_TICK, SND_OK, SND_BACK, SND_LAUNCH, SND_ERROR };
void snd_init (int on, int volume);
void snd_play (int which);
void snd_close (void);

void ui_begin_frame (void);
void ui_end_frame (void);
void ui_hit (int id, int x, int y, int w, int h);
void ui_scroll_area (int area, int x, int y, int w, int h, int contentH);   // area 0..15
int ui_clicked (int id);            // tap released on this hit
int ui_pressed (int id);            // finger currently down on it
int ui_take_key (int k);            // consume a nav key if it is k
void ui_pointer (int type, int x, int y);                // 0 down 1 move 2 up
void ui_key (int nk, const char* text);
void ui_tick (double dt);           // kinetic scrolling
void ui_scroll_to (int area, int y, int h, int viewH);   // make [y, y+h) visible

// widgets (return 1 when activated)
enum BtnStyle { B_NORMAL, B_PRIMARY, B_GHOST, B_TAB, B_TAB_ON, B_DANGER, B_SMALL };
int ui_button (int id, int x, int y, int w, int h, const char* label, int style, int focused);
void ui_badge (int x, int y, const char* s, uint32_t col);  // small pill
int ui_toggle_row (int id, int x, int y, int w, const char* label, const char* value, const char* help, int focused);
void ui_toast (const char* msg);
void ui_draw_toast (void);

// modal overlays (one at a time); call ui_modal_draw() last in the frame
typedef void (*PickFn) (int choice, void* ctx);
void ui_confirm (const char* title, const char* text, const char* yes, PickFn fn, void* ctx);
void ui_pick (const char* title, const char* const* items, int n, int current, PickFn fn, void* ctx);
void ui_message (const char* title, const char* text);
int ui_modal_active (void);
void ui_modal_draw (void);
void ui_modal_close (void);

// on-screen keyboard (search); returns 1 when text changed
int ui_keyboard (int x, int y, int w, int h, char* buf, int n, int* focus);
