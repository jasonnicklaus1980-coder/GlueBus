#define _GNU_SOURCE
/*
 * mpc3k_mpcx.c - MPC3000 emulator front end for the Akai MPC X.
 *
 * Runs the MPC3000 v3.10 BIOS/OS (user-supplied EPROM images) on the C core (mpc3k.c) and
 *   - draws the MPC3000 LCD (40 x 8 characters, 6x8 cells, cursor) 5x enlarged on the MPC X display,
 *   - sends MPC X pads, buttons and data wheel to the OS as front-panel messages on SIO channel 2
 *     (90 pp vv pads, 90 kk vv buttons; see ../PANEL_PROTOCOL.md),
 *   - offers touch buttons for soft keys 1-4, MAIN, ENTER, -, + and EXIT.
 *
 * Display, touch and MIDI code is taken unchanged from the MPC4000 front end (mpc4k_mpcx.c),
 * which runs on the MPC X. Audio: the sound chip's stereo mix to outputs 1-2 (ALSA, from the same front end).
 *
 * Test on a computer: --fb-file FILE (raw 32 bpp 800x1280 portrait), --midi-file FILE (raw MIDI bytes).
 */
#define MPC3K_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "mpc3k.c"

#include <errno.h>
#include <strings.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include "mpc3k_kit.c"

/* ------------------------------------------------------------ options */
static const char *opt_rom = "/data/mpc3k/mpc3000_ls_ic13.bin,/data/mpc3k/mpc3000_ms_ic14.bin";
static const char *opt_fb = "/dev/fb0";
static const char *opt_fb_file;
static int opt_fb_w = 800, opt_fb_h = 1280;
static const char *opt_midi[4] = {"/dev/snd/midiC1D0"};
static int opt_n_midi = 1, opt_midi_given;
static const char *opt_touch = "auto";
static int opt_rotate = 90;
static int opt_touch_swap, opt_touch_flipx, opt_touch_flipy;
static int opt_realtime = 1, opt_short_search, opt_learn;
static u64 opt_max;
static u64 opt_input_after;
static struct { u64 at; int kk; } t_ev[64]; static int t_nev, t_next;   /* test: --press KK@STEP */
static const char *opt_buttons, *opt_mpc4k_map, *opt_floppy, *opt_hd;
static u32 lcd_fg = 0x1A2408, lcd_bg = 0x9FB640;      /* dark pixels on a yellow-green panel */

static volatile sig_atomic_t quit;
static void on_signal(int s) { (void)s; quit = 1; }

/* ------------------------------------------------------------ panel messages (SIO channel 2) */
static u8 pq[1024]; static int pq_h, pq_t;
static void panel_byte(u8 b) { if (pq_t - pq_h < (int)sizeof pq) pq[pq_t++ % sizeof pq] = b; }
static void panel_msg(u8 a, u8 b, u8 c) { panel_byte(a); panel_byte(b); panel_byte(c); }
static void button(int idx, int down) { panel_msg(0x90, 0x40 + idx, down ? 0x7F : 0x00); }
static void tap_button(int idx) { button(idx, 1); button(idx, 0); }
static void pad(int p, int vel) { panel_msg(0x90, p & 0x0F, vel); }
static void panel_pump(void) { if (pq_h != pq_t && !te.rx_ready[2]) te_receive(2, pq[pq_h++ % sizeof pq]); }

/* MPC3000 button indexes (kk = 40h + index), identified by test on OS 3.10 (see ../PANEL_PROTOCOL.md).
 * "?" marks a likely but unconfirmed identification. */
enum { B_SOFT1 = 0x06, B_SOFT2 = 0x07, B_SOFT3 = 0x08, B_SOFT4 = 0x09, B_ENTER = 0x1F, B_PLUS = 0x25, B_MINUS = 0x2E,
       B_MAIN = 0x2D };
static const char *const k3_names[0x40] = {
    [0x01] = "16 LEVELS", [0x03] = "7", [0x04] = "8", [0x05] = "9", [0x06] = "SOFT KEY 1", [0x07] = "SOFT KEY 2",
    [0x08] = "SOFT KEY 3", [0x09] = "SOFT KEY 4", [0x0B] = "4", [0x0C] = "5", [0x0D] = "6", [0x0E] = "DISK",
    [0x0F] = "TEMPO/SYNC", [0x10] = "MIXER/EFFECTS", [0x11] = "PROGRAM/SOUNDS", [0x12] = "SONG", [0x14] = "1",
    [0x15] = "2", [0x16] = "3", [0x17] = "SEQ EDIT", [0x18] = "STEP EDIT", [0x19] = "EDIT LOOP", [0x1A] = "MIDI",
    [0x1B] = "COUNT IN", [0x1D] = ".", [0x1E] = "0", [0x1F] = "ENTER", [0x21] = "AUTO PUNCH", [0x22] = "CURSOR UP",
    [0x23] = "TRANSPOSE", [0x24] = "SIMUL SEQ", [0x25] = "+", [0x26] = "ERASE", [0x27] = "TIMING CORRECT",
    [0x2A] = "CURSOR LEFT", [0x2B] = "CURSOR DOWN", [0x2C] = "CURSOR RIGHT", [0x2D] = "MAIN SCREEN?", [0x2E] = "-",
    [0x30] = "STOP", [0x31] = "LOCATE", [0x32] = "STEP >?", [0x33] = "PLAY", [0x34] = "REC?", [0x35] = "OVER DUB?",
    [0x39] = "NOTE VAR ASSIGN",
};
/* MPC4000 button names (as written by the MPC4000 front end's mapping mode) -> MPC3000 index */
static const struct { const char *name; int idx; } from4k[] = {
    {"F1", 0x06}, {"F2", 0x07}, {"F3", 0x08}, {"F4", 0x09}, {"CURSOR UP", 0x22}, {"CURSOR DOWN", 0x2B},
    {"CURSOR LEFT", 0x2A}, {"CURSOR RIGHT", 0x2C}, {"0", 0x1E}, {"1", 0x14}, {"2", 0x15}, {"3", 0x16}, {"4", 0x0B},
    {"5", 0x0C}, {"6", 0x0D}, {"7", 0x03}, {"8", 0x04}, {"9", 0x05}, {"ENTER", 0x1F}, {"MAIN", 0x2D},
    {"PROGRAM", 0x11}, {"MIXER", 0x10}, {"EFFECTS", 0x10}, {"SONG", 0x12}, {"MIDI", 0x1A}, {"SEQ EDIT", 0x17},
    {"STEP EDIT", 0x18}, {"LOAD", 0x0E}, {"SAVE", 0x0E}, {"TIMING CORRECT", 0x27}, {"MASTER TEMPO", 0x0F},
    {"ERASE", 0x26}, {"16 LEVELS", 0x01}, {"REC", 0x34}, {"OVER DUB", 0x35}, {"STOP", 0x30}, {"PLAY", 0x33},
    {"GOTO", 0x31}, {"STEP >", 0x32}, {"BLOCK CURSOR UP", 0x25}, {"BLOCK CURSOR DOWN", 0x2E}, {"-/+", 0x2E},
};
static int button_map[128];                 /* MPC X ch1 note -> MPC3000 index + 1 (0 = none) */
static int name_to_idx(const char *n) {
    for (int i = 0; i < 0x40; i++) if (k3_names[i] && !strcasecmp(k3_names[i], n)) return i;
    unsigned v; if (sscanf(n, "0x%x", &v) == 1 && v < 0x40) return v;
    return -1;
}
static int load_map(const char *path, int from_mpc4k) {
    FILE *f = fopen(path, "r"); if (!f) { perror(path); return 0; }
    char line[160]; int n = 0;
    static const char *const mpc4k_by_number[78] = {
        "","F1","F2","F3","F4","F5","F6","BLOCK CURSOR DOWN","BLOCK CURSOR UP","CURSOR DOWN","CURSOR LEFT","CURSOR RIGHT",
        "CURSOR UP","WINDOW","SHIFT","RECORD","SAMPLE","PROGRAM","MULTI","MIXER","EFFECTS","SAVE","LOAD","GLOBAL","SONG",
        "MISC.","MIDI","MAIN","SEQ EDIT","STEP EDIT","0","1","2","3","4","5","6","7","8","9","ENTER","-/+","< STEP",
        "STEP >","GOTO","<< BAR","BAR >>","REC","OVER DUB","STOP","PLAY","PLAY START","TIMING CORRECT","MASTER TEMPO",
        "UNDO SEQ","TAP TEMPO","BANK A","BANK B","BANK C","BANK D","BANK E","BANK F","NEXT SEQUENCE","TRACK MUTE",
        "QLINK SEQUENCE","PAD ASSIGN","FULL LEVEL","16 LEVELS","ERASE","NOTE REPEAT"};
    while (fgets(line, sizeof line, f)) {
        char *p = line, name[80]; int note;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || !*p) continue;
        if (sscanf(p, "%d %79[^\n#]", &note, name) != 2 || note < 0 || note > 127) continue;
        for (int k = (int)strlen(name) - 1; k >= 0 && (name[k] == ' ' || name[k] == '\r' || name[k] == '\t'); k--) name[k] = 0;
        int idx = -1;
        if (from_mpc4k) {
            const char *nm = name; int num;
            if (sscanf(name, "%d", &num) == 1 && num > 0 && num < 70) nm = mpc4k_by_number[num];
            for (size_t i = 0; i < sizeof from4k / sizeof from4k[0]; i++) if (!strcasecmp(from4k[i].name, nm)) idx = from4k[i].idx;
        } else idx = name_to_idx(name);
        if (idx >= 0) { button_map[note] = idx + 1; n++; }
    }
    fclose(f);
    fprintf(stderr, "buttons: %d mapped from %s%s\n", n, path, from_mpc4k ? " (MPC4000 map, translated)" : "");
    return n;
}

/* ------------------------------------------------------------ display (from mpc4k_mpcx.c) */
static uint8_t *fbmem;
static int fb_w, fb_h, fb_stride, fb_bpp;
static size_t fb_len;
static u32 fb_yoff;
static int fb_fd = -1;
#define LW 1280        /* landscape canvas */
#define LH 800
#define LCD_S 5
#define LCD_X 40
#define LCD_Y 40
typedef struct { int x, y, w, h; const char *label; int action; } Btn;   /* action: index, or -1 exit */
static const Btn btns[] = {
    {40, 400, 285, 110, "SOFT 1", B_SOFT1}, {345, 400, 285, 110, "SOFT 2", B_SOFT2},
    {650, 400, 285, 110, "SOFT 3", B_SOFT3}, {955, 400, 285, 110, "SOFT 4", B_SOFT4},
    {40, 600, 220, 110, "MAIN", B_MAIN}, {280, 600, 220, 110, "ENTER", B_ENTER},
    {520, 600, 220, 110, "-", B_MINUS}, {760, 600, 220, 110, "+", B_PLUS}, {1020, 600, 220, 110, "EXIT", -1},
};
static int fb_open(void)
{
    if (opt_fb_file) {
        fb_w = opt_fb_w; fb_h = opt_fb_h; fb_bpp = 32; fb_stride = fb_w * 4;
        fb_len = (size_t)fb_stride * fb_h;
        fb_fd = open(opt_fb_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fb_fd < 0 || ftruncate(fb_fd, (off_t)fb_len)) { perror(opt_fb_file); return -1; }
    } else {
        struct fb_var_screeninfo v;
        struct fb_fix_screeninfo f;
        fb_fd = open(opt_fb, O_RDWR);
        if (fb_fd < 0) { perror(opt_fb); return -1; }
        if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) || ioctl(fb_fd, FBIOGET_FSCREENINFO, &f)) {
            perror("framebuffer ioctl"); return -1;
        }
        fb_w = v.xres; fb_h = v.yres; fb_bpp = v.bits_per_pixel; fb_stride = f.line_length;
        fb_yoff = v.yoffset; fb_len = f.smem_len;
        if (fb_bpp != 32) { fprintf(stderr, "framebuffer is %d bpp; only 32 supported\n", fb_bpp); return -1; }
    }
    fbmem = mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbmem == MAP_FAILED) { perror("mmap framebuffer"); return -1; }
    fprintf(stderr, "display: %dx%d, %d bpp, stride %d, y-offset %u\n", fb_w, fb_h, fb_bpp, fb_stride, fb_yoff);
    return 0;
}


/* Fallback when /dev/fb0 writes do not reach the panel: drive the display
 * with a KMS dumb buffer (kernel DRM interface, no libdrm). UNTESTED on the
 * MPC X: use --diag to compare --display fbdev and --display drm. */
#include <drm/drm.h>
#include <drm/drm_mode.h>

static const char *opt_display = "fbdev";      /* fbdev | drm */
static const char *opt_drm_dev = "/dev/dri/card0";
static int drm_fd = -1;
static u32 drm_fb_id, drm_crtc_id, drm_conn_id;
static struct drm_mode_crtc drm_saved;

static int drm_open(void)
{
    drm_fd = open(opt_drm_dev, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { fprintf(stderr, "drm: %s: %s\n", opt_drm_dev, strerror(errno)); return -1; }
    ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0);
    struct drm_mode_card_res res = {0};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { fprintf(stderr, "drm: getresources: %s\n", strerror(errno)); return -1; }
    u32 conns[32], crtcs[32], encs[32];
    if (res.count_connectors > 32) res.count_connectors = 32;
    if (res.count_crtcs > 32) res.count_crtcs = 32;
    if (res.count_encoders > 32) res.count_encoders = 32;
    res.count_fbs = 0;
    res.connector_id_ptr = (uintptr_t)conns; res.crtc_id_ptr = (uintptr_t)crtcs; res.encoder_id_ptr = (uintptr_t)encs;
    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { fprintf(stderr, "drm: getresources: %s\n", strerror(errno)); return -1; }
    struct drm_mode_modeinfo mode; int found = 0; u32 enc_id = 0;
    for (u32 i = 0; i < res.count_connectors && !found; i++) {
        struct drm_mode_get_connector c = {.connector_id = conns[i]};
        if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
        if (c.connection != 1 || !c.count_modes) continue;
        struct drm_mode_modeinfo *modes = calloc(c.count_modes, sizeof *modes);
        u32 *ce = calloc(c.count_encoders ? c.count_encoders : 1, 4);
        c.modes_ptr = (uintptr_t)modes; c.encoders_ptr = (uintptr_t)ce;
        c.count_props = 0; c.props_ptr = c.prop_values_ptr = 0;
        if (!ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) {
            mode = modes[0];
            for (u32 m = 0; m < c.count_modes; m++) if (modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = modes[m]; break; }
            drm_conn_id = conns[i];
            enc_id = c.encoder_id ? c.encoder_id : (c.count_encoders ? ce[0] : 0);
            found = 1;
        }
        free(modes); free(ce);
    }
    if (!found) { fprintf(stderr, "drm: no connected display\n"); return -1; }
    struct drm_mode_get_encoder e = {.encoder_id = enc_id};
    if (enc_id && !ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &e) && e.crtc_id) drm_crtc_id = e.crtc_id;
    else for (u32 k = 0; k < res.count_crtcs; k++) if (e.possible_crtcs & (1u << k)) { drm_crtc_id = crtcs[k]; break; }
    if (!drm_crtc_id && res.count_crtcs) drm_crtc_id = crtcs[0];
    drm_saved.crtc_id = drm_crtc_id;
    ioctl(drm_fd, DRM_IOCTL_MODE_GETCRTC, &drm_saved);

    struct drm_mode_create_dumb cd = {.width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { fprintf(stderr, "drm: create_dumb: %s\n", strerror(errno)); return -1; }
    struct drm_mode_fb_cmd fb = {.width = mode.hdisplay, .height = mode.vdisplay, .pitch = cd.pitch, .bpp = 32, .depth = 24, .handle = cd.handle};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_ADDFB, &fb)) { fprintf(stderr, "drm: addfb: %s\n", strerror(errno)); return -1; }
    drm_fb_id = fb.fb_id;
    struct drm_mode_map_dumb md = {.handle = cd.handle};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { fprintf(stderr, "drm: map_dumb: %s\n", strerror(errno)); return -1; }
    fbmem = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, (off_t)md.offset);
    if (fbmem == MAP_FAILED) { fprintf(stderr, "drm: mmap: %s\n", strerror(errno)); return -1; }
    memset(fbmem, 0, cd.size);
    struct drm_mode_crtc sc = {.crtc_id = drm_crtc_id, .fb_id = drm_fb_id, .set_connectors_ptr = (uintptr_t)&drm_conn_id,
                               .count_connectors = 1, .mode = mode, .mode_valid = 1};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &sc)) { fprintf(stderr, "drm: setcrtc: %s\n", strerror(errno)); return -1; }
    fb_w = mode.hdisplay; fb_h = mode.vdisplay; fb_stride = cd.pitch; fb_bpp = 32; fb_len = cd.size; fb_yoff = 0;
    fprintf(stderr, "display: DRM %s, %dx%d \"%s\", connector %u, crtc %u\n", opt_drm_dev, fb_w, fb_h, mode.name, drm_conn_id, drm_crtc_id);
    return 0;
}

static void display_flush(void)        /* some panels need an explicit update */
{
    if (drm_fd < 0) return;
    struct drm_mode_fb_dirty_cmd dc = {.fb_id = drm_fb_id};
    ioctl(drm_fd, DRM_IOCTL_MODE_DIRTYFB, &dc);
}

static void display_close(void)
{
    if (drm_fd < 0) return;
    if (drm_saved.fb_id) {                      /* give the previous picture back */
        drm_saved.set_connectors_ptr = (uintptr_t)&drm_conn_id; drm_saved.count_connectors = 1;
        ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &drm_saved);
    }
    ioctl(drm_fd, DRM_IOCTL_DROP_MASTER, 0);
    close(drm_fd); drm_fd = -1;
}

static inline uint32_t *fb_at(int lx, int ly)
{
    int px, py;
    if (fb_w >= fb_h) { px = lx; py = ly; }                      /* already landscape */
    else if (opt_rotate == 270) { px = ly; py = fb_h - 1 - lx; }
    else { px = fb_w - 1 - ly; py = lx; }
    if (px < 0 || py < 0 || px >= fb_w || py >= fb_h) return NULL;
    return (uint32_t *)(fbmem + (size_t)(py + fb_yoff) * fb_stride) + px;
}
static void fill(int x, int y, int w, int h, uint32_t col)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) { uint32_t *p = fb_at(i, j); if (p) *p = col; }
}
/* 5x7 font, ASCII 32-126. Each glyph: 5 column bytes, bit 0 = top row. */
static const uint8_t font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}};

/* text in landscape coordinates, scale = pixels per font dot */
static int text(int x, int y, int scale, u32 color, const char *str)
{
    for (; *str; str++, x += 6 * scale) {
        int ch = (unsigned char)*str;
        if (ch < 32 || ch > 126) ch = '?';
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (font5x7[ch - 32][cx] >> cy & 1) fill(x + cx * scale, y + cy * scale, scale, scale, color);
    }
    return x;
}
static int text_width(int scale, const char *str) { return (int)strlen(str) * 6 * scale - scale; }

static u8 shown[40 * 8]; static int shown_cur = -1, shown_valid;
static void draw_cell(int c, int r, u8 ch, int cursor) {
    int x0 = LCD_X + c * 6 * LCD_S, y0 = LCD_Y + r * 8 * LCD_S;
    fill(x0, y0, 6 * LCD_S, 8 * LCD_S, lcd_bg);
    if (ch >= 32 && ch <= 126)
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (font5x7[ch - 32][cx] >> cy & 1) fill(x0 + cx * LCD_S, y0 + cy * LCD_S, LCD_S, LCD_S, lcd_fg);
    if (cursor) fill(x0, y0 + 7 * LCD_S, 5 * LCD_S, LCD_S, lcd_fg);   /* HD61830 cursor: line 7 (reg 4 = 7) */
}
static void draw_lcd(int blink_on) {
    int start = lcd_regs[8] | (lcd_regs[9] << 8);
    int cursor_on = (lcd_regs[0] >= 0 && (lcd_regs[0] & 0x08)) && blink_on;
    int cur = cursor_on ? (lcd_cur - start) : -1;
    if (cur < 0 || cur >= 320) cur = -1;
    for (int i = 0; i < 320; i++) {
        u8 ch = lcd_mem[(start + i) & 0xFFFF];
        int was = (i == shown_cur), is = (i == cur);
        if (!shown_valid || ch != shown[i] || was != is) { draw_cell(i % 40, i / 40, ch, is); shown[i] = ch; }
    }
    shown_cur = cur; shown_valid = 1;
}
static void draw_all(void) {
    fill(0, 0, LW, LH, 0x000000);
    fill(LCD_X - 12, LCD_Y - 12, 240 * LCD_S + 24, 64 * LCD_S + 24, lcd_bg);
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) {
        const Btn *b = &btns[i];
        fill(b->x, b->y, b->w, b->h, 0x303030);
        int tw = text_width(4, b->label);
        text(b->x + (b->w - tw) / 2, b->y + (b->h - 28) / 2, 4, 0xE0E0E0, b->label);
    }
    shown_valid = 0;
}

/* ------------------------------------------------------------ touch (from mpc4k_mpcx.c) */
static int touch_fd = -1, t_x, t_y, t_maxx = 799, t_maxy = 1279, t_down;

/* Input device numbers change between boots on the MPC X (touchscreen was
 * event0 in one boot, event1 in another; the power button daemon holds the
 * other). Pick the device whose name says touchscreen, or that reports
 * multitouch positions. */
static const char *touch_find(void)
{
    static char path[64];
    for (int pass = 0; pass < 2; pass++)
        for (int n = 0; n < 16; n++) {
            snprintf(path, sizeof path, "/dev/input/event%d", n);
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            char name[128] = "";
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
            unsigned long absbits[(ABS_MAX + 1 + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {0};
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits);
            int mt = (absbits[ABS_MT_POSITION_X / (8 * sizeof(long))] >> (ABS_MT_POSITION_X % (8 * sizeof(long)))) & 1;
            close(fd);
            if (pass == 0 ? (strcasestr(name, "touch") != NULL) : mt) {
                fprintf(stderr, "touch: %s \"%s\"\n", path, name);
                return path;
            }
        }
    return "/dev/input/event0";
}

static void touch_open(void)
{
    if (!strcmp(opt_touch, "auto")) opt_touch = touch_find();
    touch_fd = open(opt_touch, O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) { fprintf(stderr, "touch: %s unavailable (%s)\n", opt_touch, strerror(errno)); return; }
    struct input_absinfo ai;
    if (!ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) && ai.maximum > 0) t_maxx = ai.maximum;
    else if (!ioctl(touch_fd, EVIOCGABS(ABS_X), &ai) && ai.maximum > 0) t_maxx = ai.maximum;
    if (!ioctl(touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) && ai.maximum > 0) t_maxy = ai.maximum;
    else if (!ioctl(touch_fd, EVIOCGABS(ABS_Y), &ai) && ai.maximum > 0) t_maxy = ai.maximum;
}
static void tap(int px, int py)
{
    int lx, ly;
    if (fb_w >= fb_h) { lx = px; ly = py; }
    else if (opt_rotate == 270) { ly = px; lx = fb_h - 1 - py; }
    else { ly = fb_w - 1 - px; lx = py; }
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) {
        const Btn *b = &btns[i];
        if (lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h) {
            if (b->action < 0) quit = 1; else tap_button(b->action);
            return;
        }
    }
    /* the LCD's bottom row holds the soft-key labels: four 10-character keys */
    int c = (lx - LCD_X) / (6 * LCD_S), r = (ly - LCD_Y) / (8 * LCD_S);
    if (c >= 0 && c < 40 && r == 7) tap_button(B_SOFT1 + c / 10);
}
static void touch_poll(void)
{
    if (touch_fd < 0) return;
    struct input_event ev;
    while (read(touch_fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) t_x = ev.value;
            if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) t_y = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value) t_down = 1;
            else if (t_down) {
                t_down = 0;
                int tx = t_x, ty = t_y, mx = t_maxx, my = t_maxy;
                if (opt_touch_swap) { int z = tx; tx = ty; ty = z; z = mx; mx = my; my = z; }
                if (opt_touch_flipx) tx = mx - tx;
                if (opt_touch_flipy) ty = my - ty;
                if (getenv("MPC4K_DEBUG")) fprintf(stderr, "touch raw %d,%d -> panel %d,%d\n", t_x, t_y,
                                                    tx * (fb_w - 1) / mx, ty * (fb_h - 1) / my);
                tap(tx * (fb_w - 1) / mx, ty * (fb_h - 1) / my);
            }
        }
    }
}

/* ------------------------------------------------------------ MPC X controls */

static int midi_fd[4] = {-1, -1, -1, -1};

/* ------------------------------------------------------------ MPC X controls (from mpc4k_mpcx.c) */
static const int pad_notes[16] = {37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53};
static void midi_open(void)
{
    for (int i = 0; i < opt_n_midi; i++) {
        midi_fd[i] = open(opt_midi[i], O_RDONLY | O_NONBLOCK);
        if (midi_fd[i] < 0) fprintf(stderr, "midi: %s unavailable (%s)\n", opt_midi[i], strerror(errno));
    }
}
static void midi_message(u8 st, u8 d1, u8 d2)
{
    int ch = (st & 15) + 1, kind = st >> 4;
    if (opt_learn) fprintf(stderr, "MPC X: ch %d kind %X %d %d%s%s\n", ch, kind, d1, d2,
                           ch == 1 && button_map[d1] ? " -> " : "",
                           ch == 1 && button_map[d1] && k3_names[button_map[d1] - 1] ? k3_names[button_map[d1] - 1] : "");
    if (ch == 10 && (kind == 9 || kind == 8)) {                 /* MPC X pad n (1-16) -> MPC3000 pad A0n */
        /* The panel numbers pads from the top row: panel 0-3 = A13-A16 ... 12-15 = A01-A04 (OS table
         * D66E:0053). pad_notes[] is ordered A01..A16 and matches the OS's default pad notes. Tested:
         * panel pad 0Ch = A01. */
        for (int p = 0; p < 16; p++) if (pad_notes[p] == d1) pad((3 - p / 4) * 4 + p % 4, kind == 9 ? d2 : 0);
        return;
    }
    if (ch == 1 && kind == 0xB && d1 == 100) {                  /* data wheel: one +/- press per detent */
        int delta = d2 >= 64 ? (int)d2 - 128 : d2;
        for (int i = 0; i < (delta < 0 ? -delta : delta) && i < 8; i++) tap_button(delta > 0 ? B_PLUS : B_MINUS);
        return;
    }
    if (ch == 1 && (kind == 9 || kind == 8) && button_map[d1]) button(button_map[d1] - 1, kind == 9 && d2);
}
static void midi_poll(void)
{
    static uint8_t st[4], d[4][2];
    static int n[4];
    for (int i = 0; i < opt_n_midi; i++) {
        uint8_t buf[256];
        ssize_t got;
        while (midi_fd[i] >= 0 && (got = read(midi_fd[i], buf, sizeof buf)) > 0) {
            for (ssize_t k = 0; k < got; k++) {
                uint8_t b = buf[k];
                if (b >= 0xF8) continue;                       /* realtime */
                if (b & 0x80) { st[i] = b; n[i] = 0; continue; }
                if (!st[i] || st[i] >= 0xF0) continue;
                d[i][n[i]++] = b;
                int need = ((st[i] >> 4) == 0xC || (st[i] >> 4) == 0xD) ? 1 : 2;
                if (n[i] == need) { midi_message(st[i], d[i][0], need == 2 ? d[i][1] : 0); n[i] = 0; }
            }
        }
    }
}

/* (audio code below is taken unchanged from the MPC4000 front end, mpc4k_mpcx.c) */
/* ------------------------------------------------------------ audio out */
/* Raw ALSA kernel interface (no libasound needed). The MPC X interface
 * accepts S32_LE, 8 channels, 44.1/48/88.2/96 kHz (probe, 2026-10-15).
 * Emulator audio (stereo 16-bit) goes to channels 1-2, the rest is silent.
 * UNTESTED on hardware: --audio-test plays a tone to check it. */
#include <sound/asound.h>

static const char *opt_audio_dev = "/dev/snd/pcmC0D0p";
static int opt_audio, opt_audio_test, opt_audio_ch = 8;
static const char *opt_audio_file;          /* test mode: write WAV instead */
static int pcm_fd = -1;
static FILE *audio_file;
#define RING 65536                          /* stereo frames, ~1.5 s */
static int16_t ring[RING * 2];
static u32 ring_r, ring_w;                  /* free-running indices */
static u64 audio_underruns, audio_frames_out;
/* output rate: the emulator makes 44.1 kHz; --audio-rate 96000 resamples on
 * the way out (linear interpolation, state kept across blocks). 44100 = the
 * original path, bit-identical. */
static u32 opt_out_rate = 44100;
static double rs_t, rs_pl, rs_pr;          /* position between the last two source frames */
#define PEND 4096
static int16_t pend[PEND * 2];
static u32 pend_n, pend_off;

/* move source frames from the ring to dst (stereo int16) at opt_out_rate;
 * consumes only the source frames it has used. returns output frames */
static u32 resample_out(int16_t *dst, u32 cap)
{
    u32 n = 0;
    if (opt_out_rate == 44100) {                       /* identical to the original path */
        while (ring_w != ring_r && n < cap) {
            dst[2 * n] = ring[(ring_r % RING) * 2]; dst[2 * n + 1] = ring[(ring_r % RING) * 2 + 1];
            ring_r++; n++;
        }
        return n;
    }
    double step = 44100.0 / opt_out_rate;
    while (ring_w != ring_r) {
        double nl = ring[(ring_r % RING) * 2], nr = ring[(ring_r % RING) * 2 + 1];
        while (rs_t < 1.0 && n < cap) {
            dst[2 * n] = (int16_t)(rs_pl + (nl - rs_pl) * rs_t);
            dst[2 * n + 1] = (int16_t)(rs_pr + (nr - rs_pr) * rs_t);
            n++; rs_t += step;
        }
        if (rs_t < 1.0) break;                         /* block full: continue next time */
        rs_t -= 1.0; rs_pl = nl; rs_pr = nr; ring_r++;
    }
    return n;
}

static void ring_put(int16_t l, int16_t r)
{
    if (ring_w - ring_r >= RING) ring_r++;  /* overflow: drop oldest */
    ring[(ring_w % RING) * 2] = l; ring[(ring_w % RING) * 2 + 1] = r;
    ring_w++;
}

static void mask_set(struct snd_pcm_hw_params *p, int param, unsigned v)
{
    struct snd_mask *m = &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    memset(m, 0, sizeof *m);
    m->bits[v >> 5] = 1u << (v & 31);
}
static void iv_set(struct snd_pcm_hw_params *p, int param, unsigned lo, unsigned hi)
{
    struct snd_interval *i = &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    i->min = lo; i->max = hi; i->openmin = i->openmax = 0; i->integer = 1; i->empty = 0;
}

static int pcm_open(void)
{
    pcm_fd = open(opt_audio_dev, O_RDWR | O_NONBLOCK);
    if (pcm_fd < 0) { fprintf(stderr, "audio: %s: %s\n", opt_audio_dev, strerror(errno)); return -1; }
    struct snd_pcm_hw_params hp;
    memset(&hp, 0, sizeof hp);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&hp.masks[i], 0xFF, sizeof hp.masks[i]);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
        hp.intervals[i].max = ~0u;
    hp.rmask = ~0u; hp.info = ~0u;
    mask_set(&hp, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    mask_set(&hp, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S32_LE);
    mask_set(&hp, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    iv_set(&hp, SNDRV_PCM_HW_PARAM_CHANNELS, opt_audio_ch, opt_audio_ch);
    iv_set(&hp, SNDRV_PCM_HW_PARAM_RATE, opt_out_rate, opt_out_rate);
    iv_set(&hp, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 256, 256);
    iv_set(&hp, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 2048, 2048);
    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hp)) {
        fprintf(stderr, "audio: hw_params failed (%s); trying the driver's own buffer size\n", strerror(errno));
        iv_set(&hp, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 64, 4096);
        iv_set(&hp, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 256, 16384);
        hp.rmask = ~0u;
        if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hp)) {
            fprintf(stderr, "audio: hw_params failed again (%s); audio off\n", strerror(errno));
            close(pcm_fd); pcm_fd = -1; return -1;
        }
    }
    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE)) {
        fprintf(stderr, "audio: prepare failed (%s); audio off\n", strerror(errno));
        close(pcm_fd); pcm_fd = -1; return -1;
    }
    struct snd_interval *ps = &hp.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    struct snd_interval *bs = &hp.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    fprintf(stderr, "audio: %s, %u Hz, %d ch, S32_LE, period %u, buffer %u frames\n",
            opt_audio_dev, opt_out_rate, opt_audio_ch, ps->min, bs->min);
    return 0;
}

/* push as much of the ring as the device accepts; returns frames written */
static u32 audio_flush(void)
{
    if (audio_file) {                                  /* test mode: raw stereo int16 at the output rate */
        u32 total = 0;
        for (;;) {
            u32 n = resample_out(pend, PEND);
            if (!n) break;
            fwrite(pend, 4, n, audio_file);
            total += n;
        }
        audio_frames_out += total;
        return total;
    }
    if (pcm_fd < 0) { ring_r = ring_w; return 0; }
    u32 total = 0;
    static int32_t buf[512 * 8];
    for (;;) {
        if (pend_off == pend_n) {
            pend_n = resample_out(pend, PEND); pend_off = 0;
            if (!pend_n) break;
        }
        u32 n = pend_n - pend_off; if (n > 512) n = 512;
        for (u32 k = 0; k < n; k++) {
            int32_t *f = &buf[k * opt_audio_ch];
            memset(f, 0, sizeof(int32_t) * opt_audio_ch);
            f[0] = (int32_t)pend[2 * (pend_off + k)] << 16; f[1] = (int32_t)pend[2 * (pend_off + k) + 1] << 16;
        }
        struct snd_xferi x = {.buf = buf, .frames = n};
        if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x)) {
            if (errno == EPIPE) { audio_underruns++; ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE); continue; }
            break;                                     /* EAGAIN: device full; the rest waits in pend */
        }
        pend_off += (u32)x.result; total += (u32)x.result; audio_frames_out += (u64)x.result;
        if ((u32)x.result < n) break;
    }
    return total;
}

static void audio_test_tone(void)                  /* 1 s, 440 Hz, -12 dB on outputs 1-2 */
{
    fprintf(stderr, "audio: test tone (1 s, 440 Hz)\n");
    for (int i = 0; i < 44100; i++) {
        int16_t v = (int16_t)(8192 * sin(2 * 3.14159265358979 * 440 * i / 44100.0));
        ring_put(v, v);
        if (ring_w - ring_r > 4096) { while (ring_w - ring_r > 2048) { if (!audio_flush()) usleep(2000); } }
    }
    for (int k = 0; k < 2000 && ring_w != ring_r; k++) { if (!audio_flush()) usleep(2000); }
}

/* ------------------------------------------------------------ main loop */
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
    if ((argc == 4 || argc == 6) && !strcmp(argv[1], "--make-kit")) {
        if (argc == 6 && !strcmp(argv[4], "--part")) kit_part = (argv[5][0] & 0xDF) - 'A';
        return make_kit_main(argv[2], argv[3]) ? 1 : 0; }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--rom") && v) { opt_rom = v; i++; }
        else if (!strcmp(a, "--fb") && v) { opt_fb = v; i++; }
        else if (!strcmp(a, "--fb-file") && v) { opt_fb_file = v; i++; }
        else if (!strcmp(a, "--display") && v) { opt_display = v; i++; }
        else if (!strcmp(a, "--midi") && v) { if (!opt_midi_given) opt_n_midi = 0; opt_midi_given = 1;
                                              if (opt_n_midi < 4) opt_midi[opt_n_midi++] = v; i++; }
        else if (!strcmp(a, "--midi-file") && v) { opt_midi[0] = v; opt_n_midi = 1; opt_midi_given = 1; i++; }
        else if (!strcmp(a, "--touch") && v) { opt_touch = v; i++; }
        else if (!strcmp(a, "--rotate") && v) { opt_rotate = atoi(v); i++; }
        else if (!strcmp(a, "--touch-swap")) opt_touch_swap = 1;
        else if (!strcmp(a, "--touch-flipx")) opt_touch_flipx = 1;
        else if (!strcmp(a, "--touch-flipy")) opt_touch_flipy = 1;
        else if (!strcmp(a, "--buttons") && v) { opt_buttons = v; i++; }
        else if (!strcmp(a, "--floppy") && v) { opt_floppy = v; i++; }
        else if (!strcmp(a, "--hd") && v) { opt_hd = v; i++; }
        else if (!strcmp(a, "--press") && v && t_nev < 60) { unsigned kk; unsigned long long at;
            if (sscanf(v, "%x@%llu", &kk, &at) == 2) { t_ev[t_nev].at = at; t_ev[t_nev].kk = kk; t_nev++; } i++; }
        else if (!strcmp(a, "--midi-at") && v && t_nev < 60) { unsigned nt; unsigned long long at;   /* test: MPC X pad note on ch10 */
            if (sscanf(v, "%u@%llu", &nt, &at) == 2) { t_ev[t_nev].at = at; t_ev[t_nev].kk = 0x1000 | nt; t_nev++; } i++; }
        else if (!strcmp(a, "--no-audio")) opt_audio = -1;
        else if (!strcmp(a, "--audio-test")) opt_audio_test = 1;
        else if (!strcmp(a, "--audio-dev") && v) { opt_audio_dev = v; i++; }
        else if (!strcmp(a, "--audio-rate") && v) { opt_out_rate = (u32)atoi(v); i++; }
        else if (!strcmp(a, "--audio-file") && v) { opt_audio_file = v; i++; }
        else if (!strcmp(a, "--mpc4k-map") && v) { opt_mpc4k_map = v; i++; }
        else if (!strcmp(a, "--learn")) opt_learn = 1;
        else if (!strcmp(a, "--short-search")) opt_short_search = 1;
        else if (!strcmp(a, "--no-realtime")) opt_realtime = 0;
        else if (!strcmp(a, "--max") && v) { opt_max = strtoull(v, 0, 0); i++; }
        else if (!strcmp(a, "--input-after") && v) { opt_input_after = strtoull(v, 0, 0); i++; }
        else if (!strcmp(a, "--colors") && v) { unsigned fg, bg; if (sscanf(v, "%x,%x", &fg, &bg) == 2) { lcd_fg = fg; lcd_bg = bg; } i++; }
        else { fprintf(stderr,
            "usage: %s [--rom LS.bin,MS.bin | IMAGE.bin] [--display fbdev|drm] [--rotate 90|270]\n"
            "          [--no-audio] [--audio-test] [--audio-rate 44100|96000] [--audio-dev DEV]\n"
            "          [--floppy IMAGE.img] [--mpc4k-map FILE] [--buttons FILE] [--learn] [--short-search] [--colors FG,BG]\n"
            "       %s --make-kit KIT.xpm|WAV_FOLDER OUT.img [--part A-Z]  (floppy image, or add to a formatted SCSI disk image)\n"
            "          [--hd DISK.img]  (SCSI hard disk, ID 0)\n"
            "          test: [--fb-file F] [--midi-file F] [--no-realtime] [--max N] [--input-after N]\n", argv[0], argv[0]);
            return 1; }
    }
    if (mpc3k_load_rom(opt_rom)) return 1;
    mpc3k_reset();
    if (opt_floppy && mpc3k_load_floppy(opt_floppy)) return 1;
    if (opt_hd && mpc3k_attach_hd(opt_hd)) return 1;
    if (opt_mpc4k_map) load_map(opt_mpc4k_map, 1);
    if (opt_buttons) load_map(opt_buttons, 0);
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal); signal(SIGHUP, on_signal);
    if (!strcmp(opt_display, "drm") && !opt_fb_file) { if (drm_open()) return 1; }
    else if (fb_open()) return 1;
    draw_all();
    if (!opt_fb_file) touch_open();
    midi_open();
    if (opt_audio_file) { audio_file = fopen(opt_audio_file, "wb"); if (!audio_file) perror(opt_audio_file); }
    else if (opt_audio >= 0 && pcm_open() == 0 && opt_audio_test) audio_test_tone();
    audio_sink = ring_put;                                    /* sound chip stereo mix -> ring -> ALSA outputs 1-2 */

    double t0 = now_s(), last_draw = 0; int patched = 0;
    double m0 = machine_seconds();
    while (!quit && !stop_msg && (!opt_max || steps < opt_max)) {
        while (t_next < t_nev && steps >= t_ev[t_next].at) {   /* test events */
            int kk = t_ev[t_next].kk;
            if (kk & 0x1000) { midi_message(0x99, kk & 0x7F, 100); midi_message(0x89, kk & 0x7F, 0); }
            else tap_button(kk - 0x40);
            t_next++; }
        for (int k = 0; k < 20000 && !stop_msg; k++) {         /* about 5 ms of machine time */
            if (opt_short_search && !patched && S[1] == 0xD5CC && IP == 0x0644) { wr8(0x8010 * 16 + 0x834A, 1); patched = 1; }
            panel_pump();
            if (skip_idle()) break;
            step();
        }
        if (steps >= opt_input_after) { midi_poll(); touch_poll(); }
        audio_flush();
        double t = now_s();
        if (opt_realtime) {                                       /* hold machine time (instructions / 4 M) to wall time */
            double ahead = (machine_seconds() - m0) - (t - t0);
            if (ahead > 0.002) usleep((useconds_t)(ahead * 1e6));
        }
        if (t - last_draw > 1.0 / 30) {
            draw_lcd(((u64)(t * 2)) & 1); display_flush(); last_draw = t;
        }
    }
    draw_lcd(1); display_flush();
    if (stop_msg) fprintf(stderr, "emulator stopped: %s at %05X\n", stop_msg, stop_addr);
    fprintf(stderr, "steps %llu, machine time %.1f s\n", (unsigned long long)steps, machine_seconds());
    audio_flush();
    if (audio_file) { fclose(audio_file); fprintf(stderr, "audio: %llu frames written to %s\n", (unsigned long long)audio_frames_out, opt_audio_file); }
    if (pcm_fd >= 0) fprintf(stderr, "audio: %llu frames out, %llu underruns\n", (unsigned long long)audio_frames_out, (unsigned long long)audio_underruns);
    display_close();
    return 0;
}
