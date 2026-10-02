// MPC Arcade library app ("Screen B"): full-screen game library, ROM manager, controller setup and settings for MAME
// on the MPC X. Runs while the MPC app is stepped aside (system/session.sh). It never runs MAME itself: it writes
// system/run/launch and exits with code 10; session.sh runs the game and starts the library again afterwards.
//   arcade-ui --root /media/az01-internal/MAME [--select GAME] [--script FILE --shots DIR]
// Exit codes: 0 = back to the MPC, 10 = launch the game in run/launch.
#include "../common/actions.h"
#include "data.h"
#include "gfx.h"
#include "ui.h"
#include <SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VERSION "1.0.0"
enum { S_LIB, S_GAMEOPTS, S_CONTROLS, S_ROMS, S_SETTINGS, S_LEARN, S_ATTRACT, S_INFO };
enum { T_ALL, T_FAV, T_RECENT, T_CATS, T_SEARCH, T_COUNT };
static int W = 1280, H = 800, screen = S_LIB, exitCode = -1;
static int dirty = 1;
static double nowSec (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// ------------------------------------------------------------------ background jobs (a shell command in a thread)
typedef struct { char cmd[2400]; char out[16384]; int status; volatile int done, running; pthread_t th; } Job;
static Job job;
static void* jobThread (void* a)
{
    Job* j = (Job*) a;
    j->status = run_capture (j->cmd, j->out, sizeof j->out);
    j->done = 1;
    return NULL;
}
static int job_start (const char* cmd)
{
    if (job.running) return -1;
    memset (&job, 0, sizeof job);
    snprintf (job.cmd, sizeof job.cmd, "%s", cmd);
    job.running = 1;
    if (pthread_create (&job.th, NULL, jobThread, &job) != 0) { job.running = 0; return -1; }
    return 0;
}
static int job_finished (void)
{
    if (! job.running || ! job.done) return 0;
    pthread_join (job.th, NULL); job.running = 0;
    return 1;
}

// ------------------------------------------------------------------ tasks (verify, benchmark, import, downloads)
enum { TK_NONE, TK_VERIFY, TK_BENCH, TK_IMPORT, TK_ART, TK_FREEROM };
static struct
{
    int kind, n, done, cur, cancel, modal, ok, failed;
    int items[MAXFILES]; char label[96]; char now[160];
    char src[MAXFILES > 600 ? 600 : MAXFILES][300]; char dst[600][300];
} T;
static void task_begin (int kind, const char* label, int modal)
{
    T.kind = kind; T.n = 0; T.done = 0; T.cur = -1; T.cancel = 0; T.modal = modal; T.ok = 0; T.failed = 0;
    snprintf (T.label, sizeof T.label, "%s", label); T.now[0] = 0;
}
static int libWant = 0;                       // the library list needs rebuilding
static char artFor[24];                       // whose picture is loaded
static char q1[700], q2[700];
static void mamePath (char* b, int n) { lib_path (b, n, "%s/mame", L.sys); }
static void task_collect (void)
{
    const int i = T.cur;
    if (i < 0) return;
    if (T.kind == TK_VERIFY) { lib_verify_parse (i, job.out); libWant = 1; }
    else if (T.kind == TK_BENCH)
    {
        const char* sp = strstr (job.out, "Average speed:");
        if (sp) { L.files[i].bench = (int) (atof (sp + 14) + 0.5); ++T.ok; } else ++T.failed;
        user_save_bench();
    }
    else { if (job.status == 0) ++T.ok; else ++T.failed; }
    T.cur = -1; dirty = 1;
}
static void task_finish (void)
{
    const int kind = T.kind;
    T.kind = TK_NONE;
    if (kind == TK_VERIFY) lib_save_verify();
    if (kind == TK_IMPORT || kind == TK_FREEROM || kind == TK_ART) lib_scan();
    char msg[160] = "";
    if (kind == TK_BENCH) snprintf (msg, sizeof msg, "Speed measured for %d game%s%s", T.ok, T.ok == 1 ? "" : "s", T.failed ? " (some couldn't run)" : "");
    else if (kind == TK_IMPORT) snprintf (msg, sizeof msg, "Imported %d file%s%s", T.ok, T.ok == 1 ? "" : "s", T.failed ? " (some failed)" : "");
    else if (kind == TK_ART) snprintf (msg, sizeof msg, "Screenshots: %d downloaded, %d not found", T.ok, T.failed);
    else if (kind == TK_FREEROM) snprintf (msg, sizeof msg, T.failed ? "%d downloaded; some failed (is the MPC online?)" : "Downloaded %d free ROM set(s)", T.ok);
    else if (T.modal) snprintf (msg, sizeof msg, "%s: done", T.label);
    if (msg[0]) ui_toast (msg);
    libWant = 1; dirty = 1;
    artFor[0] = 0;                                                     // pictures may have arrived
    if (kind == TK_IMPORT || kind == TK_FREEROM)                       // check the new arrivals straight away
    {
        task_begin (TK_VERIFY, "Checking new games", 0);
        for (int k = 0; k < L.nfiles; ++k) if (lib_needs_verify (k)) T.items[T.n++] = k;
        if (T.n == 0) T.kind = TK_NONE;
    }
}
static void task_step (void)
{
    if (T.kind == TK_NONE) return;
    if (job.running) { if (! job_finished()) return; task_collect(); }
    if (T.cancel || T.done >= T.n) { task_finish(); return; }
    const int i = T.items[T.done];
    char cmd[2400], mp[400];
    mamePath (mp, sizeof mp);
    switch (T.kind)
    {
        case TK_VERIFY:
            snprintf (T.now, sizeof T.now, "Checking %s", L.files[i].file);
            lib_verify_cmd (i, cmd, sizeof cmd);
            break;
        case TK_BENCH:
        {
            const GameInfo* g = &L.db[L.files[i].db];
            snprintf (T.now, sizeof T.now, "Measuring %s (20-60 s)", g->desc);
            char rp[700]; lib_path (rp, sizeof rp, "%s;%s", L.roms, L.bios);
            snprintf (cmd, sizeof cmd, "%s -rompath %s -cfg_directory /tmp -nvram_directory /tmp -noreadconfig -bench 20 %s 2>&1",
                      shq (q1, sizeof q1, mp), shq (q2, sizeof q2, rp), g->name);
            break;
        }
        case TK_IMPORT:
            snprintf (T.now, sizeof T.now, "Copying %s", strrchr (T.src[T.done], '/') ? strrchr (T.src[T.done], '/') + 1 : T.src[T.done]);
            // a file in MAME/import is on the same drive: move it (no second copy); anything else: copy
            if (strstr (T.src[T.done], "/import/") && strncmp (T.src[T.done], L.root, strlen (L.root)) == 0)
                snprintf (cmd, sizeof cmd, "mv -f %s %s 2>&1", shq (q1, sizeof q1, T.src[T.done]), shq (q2, sizeof q2, T.dst[T.done]));
            else
                snprintf (cmd, sizeof cmd, "cp %s %s.part 2>&1 && mv -f %s.part %s 2>&1", shq (q1, sizeof q1, T.src[T.done]),
                          shq (q2, sizeof q2, T.dst[T.done]), q2, q2);
            break;
        default:
            snprintf (T.now, sizeof T.now, "Downloading %s", T.dst[T.done]);
            snprintf (cmd, sizeof cmd, "%s", T.src[T.done]);
            break;
    }
    T.cur = i; T.done++;
    if (job_start (cmd) != 0) { T.cur = -1; ++T.failed; }
    dirty = 1;
}

// ------------------------------------------------------------------ capture link with padbridge (learn / editor)
static FILE* capF; static long capPos;
static void capture_on (void)
{
    char p[600];
    lib_path (p, sizeof p, "%s/capture.out", L.run); unlink (p);
    FILE* f = fopen (p, "w"); if (f) fclose (f);
    lib_path (p, sizeof p, "%s/capture", L.run);
    f = fopen (p, "w"); if (f) fclose (f);
    capPos = 0; capF = NULL;
}
static void capture_off (void)
{
    char p[600];
    unlink (lib_path (p, sizeof p, "%s/capture", L.run));
    unlink (lib_path (p, sizeof p, "%s/capture.out", L.run));
    if (capF) { fclose (capF); capF = NULL; }
}
// next captured control: kind 'p' press or 't' turn; returns 1 with id filled
static int capture_next (char* kind, char* id, int n)
{
    char p[600], line[400];
    if (capF == NULL) capF = fopen (lib_path (p, sizeof p, "%s/capture.out", L.run), "r");
    if (capF == NULL) return 0;
    clearerr (capF);
    fseek (capF, capPos, SEEK_SET);
    if (fgets (line, sizeof line, capF) == NULL || strchr (line, '\n') == NULL) return 0;
    capPos = ftell (capF);
    line[strcspn (line, "\n")] = 0;
    if (line[1] != ' ') return 0;
    *kind = line[0];
    snprintf (id, (size_t) n, "%s", line + 2);
    return 1;
}
static int padbridge_alive (void)
{
    char p[600], b[32]; lib_path (p, sizeof p, "%s/padbridge.pid", L.run);
    FILE* f = fopen (p, "r"); if (f == NULL) return 0;
    const int ok = fgets (b, sizeof b, f) != NULL; fclose (f);
    return ok && kill (atoi (b), 0) == 0;
}

// ------------------------------------------------------------------ library list
static int tab = T_ALL; static char catFilter[32]; static char query[48]; static int kbFocus, kbOpen;
static Entry* list[MAXFILES]; static int nlist, sel, listFocus = 1, detailFocus;
static const char* titleOf (const Entry* e) { return e->db >= 0 ? L.db[e->db].desc : e->file; }
static int ci_contains (const char* hay, const char* needle)
{
    if (! needle[0]) return 1;
    for (const char* h = hay; *h; ++h)
    {
        const char* a = h; const char* b = needle;
        while (*a && *b && tolower ((unsigned char) *a) == tolower ((unsigned char) *b)) { ++a; ++b; }
        if (! *b) return 1;
    }
    return 0;
}
static int cmpEntries (const void* pa, const void* pb)
{
    const Entry* a = *(Entry* const*) pa; const Entry* b = *(Entry* const*) pb;
    const char* s = set_get ("sort");
    if (tab == T_RECENT) return a->last < b->last ? 1 : (a->last > b->last ? -1 : 0);
    if (! strcmp (s, "year")) { const int c = strcmp (L.db[a->db].year, L.db[b->db].year); if (c) return c; }
    if (! strcmp (s, "manufacturer")) { const int c = strcasecmp (L.db[a->db].manuf, L.db[b->db].manuf); if (c) return c; }
    if (! strcmp (s, "played")) { if (a->plays != b->plays) return b->plays - a->plays; }
    return strcasecmp (titleOf (a), titleOf (b));
}
static void build_list (void)
{
    const char* keepName = nlist > 0 && sel < nlist && list[sel]->db >= 0 ? L.db[list[sel]->db].name : NULL;
    char keep[24] = ""; if (keepName) snprintf (keep, sizeof keep, "%s", keepName);
    nlist = 0;
    const int clones = set_geti ("show_clones"), hideBad = set_geti ("hide_bad");
    for (int i = 0; i < L.nfiles; ++i)
    {
        Entry* e = &L.files[i];
        if (e->db < 0 || L.db[e->db].isbios) continue;
        const GameInfo* g = &L.db[e->db];
        if (! clones && g->parent[0] && lib_has_set (g->parent)) continue;
        if (hideBad && (e->vstatus == V_BAD || e->vstatus == V_NEEDBIOS || e->vstatus == V_NEEDPARENT)) continue;
        if (tab == T_FAV && ! e->fav) continue;
        if (tab == T_RECENT && ! e->plays) continue;
        if (catFilter[0] && strcmp (g->genre, catFilter)) continue;
        if (query[0] && ! ci_contains (g->desc, query) && ! ci_contains (g->name, query) && ! ci_contains (g->manuf, query) && ! ci_contains (g->year, query)) continue;
        list[nlist++] = e;
    }
    qsort (list, (size_t) nlist, sizeof (Entry*), cmpEntries);
    sel = 0;
    for (int i = 0; i < nlist; ++i) if (keep[0] && list[i]->db >= 0 && ! strcmp (L.db[list[i]->db].name, keep)) sel = i;
    libWant = 0;
}
static Entry* selected (void) { return nlist > 0 && sel < nlist ? list[sel] : NULL; }

// screenshot / artwork for a game: newest screenshots/<set>/*.png, screenshots/<set>.png, artwork/snap|titles
static Image* art;
static int findArt (const char* set, char* out, int n)
{
    char d[600];
    DIR* dir = opendir (lib_path (d, sizeof d, "%s/screenshots/%s", L.root, set));
    if (dir)
    {
        struct dirent* de; long best = 0; char bestName[260] = "";
        while ((de = readdir (dir)) != NULL)
        {
            const char* dot = strrchr (de->d_name, '.');
            if (! dot || strcasecmp (dot, ".png")) continue;
            char f[900]; snprintf (f, sizeof f, "%s/%s", d, de->d_name);
            const long t = file_mtime (f);
            if (t >= best) { best = t; snprintf (bestName, sizeof bestName, "%s", de->d_name); }
        }
        closedir (dir);
        if (bestName[0]) { snprintf (out, (size_t) n, "%s/%s", d, bestName); return 1; }
    }
    const char* pats[] = { "%s/screenshots/%s.png", "%s/artwork/snap/%s.png", "%s/artwork/snaps/%s.png", "%s/artwork/titles/%s.png", "%s/artwork/%s.png" };
    for (size_t k = 0; k < sizeof pats / sizeof pats[0]; ++k) { lib_path (out, n, pats[k], L.root, set); if (file_exists (out)) return 1; }
    return 0;
}
static Image* artOf (const Entry* e)
{
    const char* set = e && e->db >= 0 ? L.db[e->db].name : "";
    if (! strcmp (set, artFor)) return art;
    gfx_image_cache_clear();
    img_free (art); art = NULL;
    snprintf (artFor, sizeof artFor, "%s", set);
    char p[900];
    if (set[0] && findArt (set, p, sizeof p)) art = img_load (p);
    else if (set[0] && L.db[e->db].parent[0] && findArt (L.db[e->db].parent, p, sizeof p)) art = img_load (p);
    return art;
}

// ------------------------------------------------------------------ helpers for drawing
static uint32_t statusColor (int v)
{
    switch (v) { case V_OK: case V_BEST: return C_GOOD; case V_UNKNOWN: return C_DIM; case V_ISBIOS: return C_BLUE; default: return C_BAD; }
}
static void fmtDuration (long s, char* b, int n)
{
    if (s < 60) snprintf (b, (size_t) n, "%ld s", s);
    else if (s < 3600) snprintf (b, (size_t) n, "%ld min", s / 60);
    else snprintf (b, (size_t) n, "%ld h %ld min", s / 3600, (s % 3600) / 60);
}
static void fmtAgo (long t, char* b, int n)
{
    const long d = (long) time (NULL) - t;
    if (t <= 0) snprintf (b, (size_t) n, "never");
    else if (d < 3600) snprintf (b, (size_t) n, "just now");
    else if (d < 86400) snprintf (b, (size_t) n, "%ld h ago", d / 3600);
    else snprintf (b, (size_t) n, "%ld days ago", d / 86400);
}
static void splitTitle (const char* desc, char* main, int nm, char* variant, int nv)
{
    const char* paren = strstr (desc, " (");
    if (paren) { snprintf (main, (size_t) nm, "%.*s", (int) (paren - desc), desc); snprintf (variant, (size_t) nv, "%s", paren + 1); }
    else { snprintf (main, (size_t) nm, "%s", desc); variant[0] = 0; }
}
static const char* controlsText (const GameInfo* g, char* b, int n)
{
    const char* c = g->controls;
    const char* kind = strstr (c, "doublejoy") ? "Twin joysticks" : strstr (c, "dial") ? "Dial" : strstr (c, "paddle") ? "Paddle / wheel" :
                       strstr (c, "trackball") ? "Trackball" : strstr (c, "lightgun") ? "Light gun" : strstr (c, "joy4") ? "4-way joystick" :
                       strstr (c, "joy2") ? "2-way joystick" : strstr (c, "joy") ? "8-way joystick" : strstr (c, "stick") ? "Analog stick" : "Buttons";
    snprintf (b, (size_t) n, "%d player%s \xc2\xb7 %s \xc2\xb7 %d button%s%s", g->players, g->players == 1 ? "" : "s", kind, g->buttons,
              g->buttons == 1 ? "" : "s", strstr (c, "pedal") ? " \xc2\xb7 pedal" : "");
    return b;
}
static void topBar (const char* title, int backId)
{
    gfx_fill (0, 0, W, 72, C_PANEL);
    gfx_hline (0, 72, W, C_LINE);
    if (backId)
    {
        if (ui_button (backId, 16, 12, 120, 48, "\xe2\x97\x80  Back", B_GHOST, 0)) { U.key = NK_BACK; snd_play (SND_BACK); }
        gfx_text (F_TITLE, 160, 12, title, C_TEXT);
    }
}
static void hintBar (const char* text)
{
    gfx_fill (0, H - 40, W, 40, C_PANEL);
    gfx_hline (0, H - 40, W, C_LINE);
    gfx_text_fit (F_SMALL, 20, H - 34, W - 360, text, C_DIM);
    if (T.kind != TK_NONE && ! T.modal)
    {
        char b[200];
        snprintf (b, sizeof b, "%s %d/%d", T.label, T.done, T.n);
        const int w = 300, x = W - w - 20;
        gfx_round (x, H - 30, w, 20, 10, C_PANEL3, 255);
        if (T.n) gfx_round (x, H - 30, w * T.done / T.n, 20, 10, C_BLUE, 255);
        gfx_text_c (F_SMALL, x, H - 33, w, 24, b, C_TEXT);
        U.anim = 1;
    }
    else
    {
        char clock[16]; time_t t = time (NULL); struct tm tm; localtime_r (&t, &tm);
        strftime (clock, sizeof clock, "%H:%M", &tm);
        gfx_text (F_SMALL, W - 20 - gfx_text_w (F_SMALL, clock), H - 34, clock, C_DIM);
    }
}

// ------------------------------------------------------------------ menus (MENU key / pads reach everything)
static int pickRow = -1, pickChoice = -1;
static void onPick (int choice, void* ctx) { pickRow = (int) (intptr_t) ctx; pickChoice = choice; dirty = 1; }
static int takePick (int row) { if (pickRow == row) { pickRow = -1; return pickChoice; } return -1; }
enum { ROW_MAINMENU = 5000, ROW_EXIT = 5001, ROW_DELETE = 5002, ROW_TEMPLATE = 5003, ROW_PADACT = 5004, ROW_HWNAME = 5005,
       ROW_FIRSTRUN = 5006, ROW_RESETGAME = 5007, ROW_DELSTATE = 5008, ROW_REVERT = 5009, ROW_SORT = 5010, ROW_CAT = 5011 };
static const char* const kMainMenu[] = { "Play", "Game options", "Pad controls for this game", "Game info", "Add to / remove from favorites",
                                         "ROM manager", "Settings", "Set up the MPC pads", "Exit to the MPC" };
static void openScreen (int s);
static void requestExit (void)
{
    ui_confirm ("Back to the MPC?", "The arcade closes and the MPC app starts again. Your games, favorites and settings stay as they are.",
                "Exit", onPick, (void*) (intptr_t) ROW_EXIT);
}
static void launchSelected (void)
{
    Entry* e = selected();
    if (e == NULL) return;
    if (e->vstatus == V_NEEDBIOS || e->vstatus == V_NEEDPARENT || e->vstatus == V_BAD)
    {
        char b[300]; snprintf (b, sizeof b, "%s.%s%s", vstatus_text (e->vstatus), e->vdetail[0] ? "\n" : "", e->vdetail);
        ui_message ("This game can't start yet", b); snd_play (SND_ERROR); return;
    }
    PadMap pm; char where[96];
    padmap_for_game (&pm, e, where, sizeof where);
    padmap_write_active (&pm, 0);
    if (launch_write (e) != 0) { ui_message ("Couldn't start", "Writing the launch file failed (is the drive full or read-only?)."); return; }
    snd_play (SND_LAUNCH);
    set_set ("last_game", L.db[e->db].name); settings_save();
    exitCode = 10;
}

// ------------------------------------------------------------------ SCREEN: library
enum { ID_TAB = 100, ID_ROMS = 120, ID_SETTINGS, ID_EXIT, ID_ROW = 1000, ID_PLAY = 200, ID_FAV, ID_CTRL, ID_OPTS, ID_INFO, ID_CAT = 300,
       ID_CLEARFILTER = 400, ID_SEARCHBOX, ID_KBDONE, ID_SORT, ID_SETUP };
static const char* kTabNames[T_COUNT] = { "ALL", "\xe2\x98\x85 FAVORITES", "RECENT", "CATEGORIES", "SEARCH" };
static const char* const kSortNames[] = { "Sort: Title", "Sort: Year", "Sort: Manufacturer", "Sort: Most played" };
static const char* const kSortKeys[] = { "title", "year", "manufacturer", "played" };

static void drawDetail (Entry* e, int x, int y, int w, int h)
{
    gfx_fill (x, y, w, h, C_BG);
    if (e == NULL)
    {
        const int empty = L.nfiles == 0;
        gfx_text_c (F_TITLE, x, y + 120, w, 60, empty ? "No games installed yet" : tab == T_FAV ? "No favorites yet" :
                    tab == T_RECENT ? "Nothing played yet" : "No games match", C_TEXT);
        const char* t = empty ?
            "Copy ROM zips (e.g. pacman.zip) to MAME/roms on the MPC's internal drive: by SSH, from a USB stick (ROM manager \xe2\x86\x92 Import USB), or from a computer's browser (ROM manager \xe2\x86\x92 Web upload). Use ROM sets you legally own, or the free sets in ROM manager \xe2\x86\x92 Free ROMs." :
            tab == T_FAV ? "Mark games with \xe2\x98\x86 Favorite (or the B3 pad) and they collect here." :
            tab == T_RECENT ? "Games you play show up here, most recent first." : "Try another tab or clear the search.";
        gfx_text_wrap (F_BODY, x + 60, y + 200, w - 120, 8, t, C_DIM, 6);
        if (empty && ui_button (ID_ROMS + 50, x + (w - 300) / 2, y + 470, 300, 64, "Open the ROM manager", B_PRIMARY, ! listFocus)) openScreen (S_ROMS);
        return;
    }
    const GameInfo* g = &L.db[e->db];
    // artwork
    const int ax = x + 20, ay = y + 16, aw = w - 40, ah = 360;
    gfx_round (ax, ay, aw, ah, 10, C_BLACK, 255);
    Image* im = artOf (e);
    if (im) gfx_image_fit (im, ax + 4, ay + 4, aw - 8, ah - 8, 255);
    else
    {
        gfx_text_c (F_HUGE, ax, ay + 90, aw, 80, g->genre, C_PANEL3);
        gfx_text_c (F_SMALL, ax, ay + 220, aw, 30, "A screenshot is taken automatically the first time you play.", C_FAINT);
    }
    char mainT[100], var[100], b[200];
    splitTitle (g->desc, mainT, sizeof mainT, var, sizeof var);
    int ty = ay + ah + 12;
    gfx_text_fit (F_TITLE, x + 24, ty, w - 48, mainT, C_TEXT); ty += 46;
    snprintf (b, sizeof b, "%s \xc2\xb7 %s \xc2\xb7 %s%s%s", g->manuf, g->year, g->genre, var[0] ? " \xc2\xb7 " : "", var);
    gfx_text_fit (F_SMALL, x + 24, ty, w - 48, b, C_DIM); ty += 28;
    gfx_text_fit (F_SMALL, x + 24, ty, w - 48, controlsText (g, b, sizeof b), C_DIM); ty += 34;
    // status + badges
    int bx = x + 24;
    ui_badge (bx, ty, vstatus_text (e->vstatus), statusColor (e->vstatus)); bx += gfx_text_w (F_SMALL, vstatus_text (e->vstatus)) + 28;
    if (g->status != 'g') { const char* s = g->status == 'i' ? "Imperfect emulation" : "Preliminary emulation"; ui_badge (bx, ty, s, C_WARN); bx += gfx_text_w (F_SMALL, s) + 28; }
    if (e->bench) { snprintf (b, sizeof b, "Speed %d%%", e->bench); ui_badge (bx, ty, b, e->bench >= 100 ? C_GOOD : (e->bench >= 85 ? C_WARN : C_BAD)); bx += gfx_text_w (F_SMALL, b) + 28; }
    char st[700]; lib_path (st, sizeof st, "%s/saves/states/%s/auto.sta", L.root, g->name);
    if (file_exists (st)) { ui_badge (bx, ty, "Resumes where you left off", C_TEAL); }
    ty += 34;
    if (e->vdetail[0] && e->vstatus != V_OK && e->vstatus != V_BEST) { gfx_text_fit (F_SMALL, x + 24, ty, w - 48, e->vdetail, C_BAD); ty += 26; }
    if (e->plays)
    {
        char d[40], a[40]; fmtDuration (e->playSecs, d, sizeof d); fmtAgo (e->last, a, sizeof a);
        snprintf (b, sizeof b, "Played %d time%s \xc2\xb7 %s \xc2\xb7 last %s", e->plays, e->plays == 1 ? "" : "s", d, a);
        gfx_text_fit (F_SMALL, x + 24, ty, w - 48, b, C_DIM);
    }
    // buttons
    const int by = y + h - 84;
    const char* labels[] = { "\xe2\x96\xb6  PLAY", e->fav ? "\xe2\x98\x85 Favorite" : "\xe2\x98\x86 Favorite", "Controls", "Options", "Info" };
    const int widths[] = { 190, 140, 118, 110, 74 };
    int cx = x + 24;
    for (int k = 0; k < 5; ++k)
    {
        if (ui_button (ID_PLAY + k, cx, by, widths[k], 64, labels[k], k == 0 ? B_PRIMARY : B_NORMAL, ! listFocus && detailFocus == k))
        {
            detailFocus = k;
            if (k == 0) launchSelected();
            else if (k == 1) { user_toggle_fav (e); if (tab == T_FAV) libWant = 1; }
            else if (k == 2) openScreen (S_CONTROLS);
            else if (k == 3) openScreen (S_GAMEOPTS);
            else openScreen (S_INFO);
        }
        cx += widths[k] + 10;
    }
}
static int catCounts (const char* names[32], int counts[32])
{
    int n = 0;
    for (int i = 0; i < L.nfiles; ++i)
    {
        const Entry* e = &L.files[i];
        if (e->db < 0 || L.db[e->db].isbios) continue;
        const char* gname = L.db[e->db].genre;
        int k = 0; while (k < n && strcmp (names[k], gname)) ++k;
        if (k == n && n < 32) { names[n] = gname; counts[n] = 0; ++n; }
        if (k < 32) counts[k]++;
    }
    return n;
}
static int catSel;
static void screen_library (void)
{
    if (libWant) build_list();
    // keys
    if (! ui_modal_active())
    {
        if (tab == T_CATS)
        {
            const char* names[32]; int counts[32]; const int n = catCounts (names, counts);
            if (ui_take_key (NK_RIGHT) && catSel < n - 1) { ++catSel; snd_play (SND_TICK); }
            if (ui_take_key (NK_LEFT) && catSel > 0) { --catSel; snd_play (SND_TICK); }
            if (ui_take_key (NK_DOWN) && catSel + 4 < n) { catSel += 4; snd_play (SND_TICK); }
            if (ui_take_key (NK_UP)) { if (catSel >= 4) catSel -= 4; else { tab = T_ALL; libWant = 1; } snd_play (SND_TICK); }
            if (ui_take_key (NK_OK) && n) { snprintf (catFilter, sizeof catFilter, "%s", names[catSel]); tab = T_ALL; libWant = 1; snd_play (SND_OK); }
            if (ui_take_key (NK_BACK)) { tab = T_ALL; libWant = 1; snd_play (SND_BACK); }
        }
        else if (kbOpen)
        {
            if (ui_take_key (NK_BACK) || ui_take_key (NK_MENU)) { kbOpen = 0; snd_play (SND_BACK); }
        }
        else
        {
            if (listFocus)
            {
                if (ui_take_key (NK_DOWN) && sel < nlist - 1) { ++sel; snd_play (SND_TICK); }
                if (ui_take_key (NK_UP) && sel > 0) { --sel; snd_play (SND_TICK); }
                if (ui_take_key (NK_PGDN)) { sel = sel + 8 < nlist ? sel + 8 : (nlist ? nlist - 1 : 0); }
                if (ui_take_key (NK_PGUP)) { sel = sel > 8 ? sel - 8 : 0; }
                if (ui_take_key (NK_RIGHT)) { tab = (tab + 1) % T_COUNT; libWant = 1; if (tab == T_SEARCH) { kbOpen = 1; } snd_play (SND_TICK); }
                if (ui_take_key (NK_LEFT)) { tab = (tab + T_COUNT - 1) % T_COUNT; libWant = 1; if (tab == T_SEARCH) kbOpen = 1; snd_play (SND_TICK); }
                if (ui_take_key (NK_OK) && selected()) launchSelected();
            }
            else
            {
                if (ui_take_key (NK_RIGHT) && detailFocus < 4) { ++detailFocus; snd_play (SND_TICK); }
                if (ui_take_key (NK_LEFT)) { if (detailFocus > 0) --detailFocus; else listFocus = 1; snd_play (SND_TICK); }
                if (ui_take_key (NK_UP) || ui_take_key (NK_DOWN)) listFocus = 1;
                if (ui_take_key (NK_OK) && selected()) U.clicked = ID_PLAY + detailFocus;    // press the focused button
            }
            if (ui_take_key (NK_FAV) && selected()) { user_toggle_fav (selected()); ui_toast (selected()->fav ? "Added to favorites" : "Removed from favorites"); snd_play (SND_OK); if (tab == T_FAV) libWant = 1; }
            if (ui_take_key (NK_MENU)) ui_pick ("Menu", kMainMenu, 9, -1, onPick, (void*) (intptr_t) ROW_MAINMENU);
            if (ui_take_key (NK_BACK))
            {
                if (! listFocus) listFocus = 1;
                else if (query[0] || catFilter[0] || tab != T_ALL) { query[0] = 0; catFilter[0] = 0; tab = T_ALL; libWant = 1; snd_play (SND_BACK); }
                else requestExit();
            }
        }
    }
    if (libWant) build_list();
    // menu picks
    const int mm = takePick (ROW_MAINMENU);
    switch (mm)
    {
        case 0: launchSelected(); break;
        case 1: if (selected()) openScreen (S_GAMEOPTS); break;
        case 2: if (selected()) openScreen (S_CONTROLS); break;
        case 3: if (selected()) openScreen (S_INFO); break;
        case 4: if (selected()) { user_toggle_fav (selected()); libWant = 1; } break;
        case 5: openScreen (S_ROMS); break;
        case 6: openScreen (S_SETTINGS); break;
        case 7: openScreen (S_LEARN); break;
        case 8: requestExit(); break;
        default: break;
    }
    if (takePick (ROW_EXIT) == 1) exitCode = 0;
    const int sp = takePick (ROW_SORT);
    if (sp >= 0) { set_set ("sort", kSortKeys[sp]); settings_save(); libWant = 1; }
    if (takePick (ROW_FIRSTRUN) == 1) openScreen (S_LEARN);

    // ---- draw
    gfx_fill (0, 0, W, H, C_BG);
    gfx_fill (0, 0, W, 72, C_PANEL);
    gfx_hline (0, 72, W, C_LINE);
    gfx_round (16, 16, 40, 40, 8, C_ACCENT, 255);
    gfx_text_c (F_BOLD, 16, 15, 40, 40, "\xe2\x96\xb6", C_TEXT);
    gfx_text (F_TITLE, 68, 12, "ARCADE", C_TEXT);
    int tx = 206;
    for (int t = 0; t < T_COUNT; ++t)
    {
        char lab[48];
        if (t == T_ALL) { int n = 0; for (int i = 0; i < L.nfiles; ++i) if (L.files[i].db >= 0 && ! L.db[L.files[i].db].isbios) ++n; snprintf (lab, sizeof lab, "%s  %d", kTabNames[t], n); }
        else snprintf (lab, sizeof lab, "%s", kTabNames[t]);
        const int tw = gfx_text_w (F_BODY, lab) + 28;
        if (ui_button (ID_TAB + t, tx, 12, tw, 48, lab, tab == t ? B_TAB_ON : B_TAB, 0))
        {
            if (t == T_SEARCH) { kbOpen = 1; tab = T_SEARCH; }
            else { tab = t; kbOpen = 0; if (t != T_SEARCH) query[0] = 0; }
            if (t == T_ALL) catFilter[0] = 0;
            libWant = 1; listFocus = 1;
        }
        tx += tw + 4;
    }
    if (ui_button (ID_ROMS, W - 330, 12, 96, 48, "ROMs", B_GHOST, 0)) openScreen (S_ROMS);
    if (ui_button (ID_SETTINGS, W - 226, 12, 60, 48, "\xe2\x9a\x99", B_GHOST, 0)) openScreen (S_SETTINGS);
    if (ui_button (ID_EXIT, W - 158, 12, 142, 48, "\xe2\x97\x80 MPC", B_GHOST, 0)) requestExit();
    if (libWant) build_list();

    const int top = 73, bottom = H - 40;
    if (tab == T_CATS)
    {
        const char* names[32]; int counts[32]; const int n = catCounts (names, counts);
        gfx_text (F_BOLD, 32, top + 20, "Categories", C_TEXT);
        const int cw = (W - 64 - 3 * 16) / 4, ch = 110;
        for (int i = 0; i < n; ++i)
        {
            const int cx = 32 + (i % 4) * (cw + 16), cy = top + 70 + (i / 4) * (ch + 16);
            char b[64]; snprintf (b, sizeof b, "%d game%s", counts[i], counts[i] == 1 ? "" : "s");
            ui_hit (ID_CAT + i, cx, cy, cw, ch);
            gfx_round (cx, cy, cw, ch, 12, ui_pressed (ID_CAT + i) ? C_PANEL3 : C_PANEL2, 255);
            if (i == catSel) gfx_round_outline (cx - 3, cy - 3, cw + 6, ch + 6, 14, 2, C_ACCENT, 255);
            gfx_fill (cx, cy + 20, 5, ch - 40, C_ACCENT);
            gfx_text (F_BOLD, cx + 24, cy + 22, names[i], C_TEXT);
            gfx_text (F_SMALL, cx + 24, cy + 62, b, C_DIM);
            if (ui_clicked (ID_CAT + i)) { snprintf (catFilter, sizeof catFilter, "%s", names[i]); tab = T_ALL; libWant = 1; snd_play (SND_OK); }
        }
        if (n == 0) gfx_text_c (F_BODY, 0, top + 200, W, 40, "Categories appear once games are installed.", C_DIM);
        hintBar ("\xe2\x97\x80 \xe2\x96\xb6 \xe2\x96\xb2 \xe2\x96\xbc choose  \xc2\xb7  OK open  \xc2\xb7  BACK all games");
        return;
    }
    // list panel
    const int lw = 560;
    gfx_fill (0, top, lw, bottom - top, C_PANEL);
    int listTop = top;
    {
        char hb[64]; snprintf (hb, sizeof hb, "%d game%s", nlist, nlist == 1 ? "" : "s");
        gfx_text (F_SMALL, 20, top + 10, hb, C_DIM);
        const char* sk = set_get ("sort");
        const int si = ! strcmp (sk, "year") ? 1 : ! strcmp (sk, "manufacturer") ? 2 : ! strcmp (sk, "played") ? 3 : 0;
        if (tab != T_RECENT && ui_button (ID_SORT, lw - 214, top + 6, 200, 34, kSortNames[si], B_SMALL, 0))
            ui_pick ("Sort the list by", kSortNames, 4, si, onPick, (void*) (intptr_t) ROW_SORT);
        gfx_hline (0, top + 46, lw, C_PANEL2);
        listTop = top + 47;
    }
    if (tab == T_SEARCH || catFilter[0] || query[0])
    {
        // filter bar
        gfx_fill (0, listTop, lw, 64, C_PANEL2);
        char b[96];
        if (tab == T_SEARCH || query[0]) snprintf (b, sizeof b, "\xe2\x86\x92 %s%s", query, kbOpen ? "_" : "");
        else snprintf (b, sizeof b, "Category: %s", catFilter);
        ui_hit (ID_SEARCHBOX, 0, listTop, lw - 120, 64);
        gfx_text_fit (F_BODY, 20, listTop + 14, lw - 160, b, C_TEXT);
        if (ui_clicked (ID_SEARCHBOX)) { kbOpen = 1; tab = T_SEARCH; }
        if (ui_button (ID_CLEARFILTER, lw - 110, listTop + 10, 96, 44, "\xe2\x9c\x95 Clear", B_SMALL, 0)) { query[0] = 0; catFilter[0] = 0; kbOpen = 0; tab = T_ALL; libWant = 1; }
        listTop += 64;
    }
    const int rowH = 68, viewH = bottom - listTop;
    ui_scroll_area (0, 0, listTop, lw, viewH, nlist * rowH);
    static int lastSel = -1;
    if (sel != lastSel) { ui_scroll_to (0, sel * rowH, rowH, viewH); lastSel = sel; }
    gfx_clip (0, listTop, lw, viewH);
    const int first = (int) U.scroll[0] / rowH;
    for (int i = first; i < nlist && i * rowH - (int) U.scroll[0] < viewH; ++i)
    {
        const Entry* e = list[i];
        const GameInfo* g = &L.db[e->db];
        const int ry = listTop + i * rowH - (int) U.scroll[0];
        ui_hit (ID_ROW + i, 0, ry, lw, rowH);
        const int isSel = i == sel;
        if (isSel) { gfx_fill (0, ry, lw, rowH, listFocus ? C_PANEL3 : C_PANEL2); gfx_fill (0, ry, 5, rowH, C_ACCENT); }
        else if (ui_pressed (ID_ROW + i)) gfx_fill (0, ry, lw, rowH, C_PANEL2);
        char mainT[100], var[100], b[200];
        splitTitle (g->desc, mainT, sizeof mainT, var, sizeof var);
        gfx_text_fit (F_BODY, 22, ry + 8, lw - 110, mainT, C_TEXT);
        snprintf (b, sizeof b, "%s \xc2\xb7 %s%s%s", g->year, g->manuf, var[0] ? " \xc2\xb7 " : "", var);
        gfx_text_fit (F_SMALL, 22, ry + 38, lw - 110, b, C_DIM);
        if (e->fav) gfx_text (F_BODY, lw - 76, ry + 18, "\xe2\x98\x85", C_GOLD);
        gfx_circle (lw - 30, ry + rowH / 2, 7, statusColor (e->vstatus), 255);
        gfx_hline (16, ry + rowH - 1, lw - 32, C_PANEL2);
        if (ui_clicked (ID_ROW + i))
        {
            if (sel == i && listFocus) launchSelected();         // tap again to play
            else { sel = i; listFocus = 1; snd_play (SND_TICK); }
        }
    }
    gfx_unclip();
    if (nlist > 0 && U.scrollMax[0] > 0)                           // scroll indicator
    {
        const int th = viewH * viewH / (nlist * rowH) < 40 ? 40 : viewH * viewH / (nlist * rowH);
        const int ty = listTop + (int) ((viewH - th) * (U.scroll[0] / U.scrollMax[0]));
        gfx_round (lw - 8, ty, 4, th, 2, C_PANEL3, 255);
    }
    drawDetail (selected(), lw, top, W - lw, bottom - top);
    if (kbOpen)
    {
        const int kx = 120, ky = H - 40 - 330, kw = W - 240;
        if (ui_keyboard (kx, ky, kw, 300, query, sizeof query, &kbFocus)) libWant = 1;
        if (ui_button (ID_KBDONE, W - 240 - 0, ky - 70, 120, 52, "Done", B_PRIMARY, 0)) kbOpen = 0;
    }
    char hint[300];
    snprintf (hint, sizeof hint, "%s", kbOpen ? "Type with the keys  \xc2\xb7  BACK closes the keyboard" :
             "\xe2\x96\xb2\xe2\x96\xbc choose  \xc2\xb7  OK play  \xc2\xb7  \xe2\x97\x80\xe2\x96\xb6 tabs  \xc2\xb7  B3 favorite  \xc2\xb7  MENU options  \xc2\xb7  hold EXIT: back");
    hintBar (hint);
}

// ------------------------------------------------------------------ option rows (settings / game options)
typedef struct { const char* label; const char* key; const char* help; const char* const* vals; const char* const* names; int n; int gameScope; } Opt;
static const char* const vProfile[] = { "performance", "balanced", "compatibility", "lightweight" };
static const char* const nProfile[] = { "Performance", "Balanced", "Compatibility", "Lightweight" };
static const char* const vScale[] = { "fit", "pixel", "stretch" };
static const char* const nScale[] = { "Fit (keep shape)", "Pixel-perfect", "Stretch to fill" };
static const char* const vScan[] = { "0", "1", "2", "3" };
static const char* const nScan[] = { "Off", "Light", "Medium", "Strong" };
static const char* const vVert[] = { "fit", "ror", "rol" };
static const char* const nVert[] = { "Upright (pillarbox)", "Rotate right (turn the MPC)", "Rotate left (turn the MPC)" };
static const char* const vOnOff[] = { "0", "1" };
static const char* const nOnOff[] = { "Off", "On" };
static const char* const vVol[] = { "-24", "-18", "-12", "-9", "-6", "-3", "0" };
static const char* const nVol[] = { "-24 dB", "-18 dB", "-12 dB", "-9 dB", "-6 dB", "-3 dB", "0 dB (full)" };
static const char* const vUiVol[] = { "15", "30", "50", "75", "100" };
static const char* const nUiVol[] = { "15 %", "30 %", "50 %", "75 %", "100 %" };
static const char* const vSort[] = { "title", "year", "manufacturer", "played" };
static const char* const nSort[] = { "Title", "Year", "Manufacturer", "Most played" };
static const char* const vDelay[] = { "60", "180", "300", "600" };
static const char* const nDelay[] = { "1 minute", "3 minutes", "5 minutes", "10 minutes" };
static const char* const vRot[] = { "0", "90", "180", "270" };
static const char* const nRot[] = { "Normal", "90\xc2\xb0", "180\xc2\xb0", "270\xc2\xb0" };
static const char* vTpl[NUM_TEMPLATES + 1]; static const char* nTpl[NUM_TEMPLATES + 1];
static char audioVals[12][32], audioNames[12][64]; static const char* vAudio[12]; static const char* nAudio[12]; static int nAudioDev;
static void initOptLists (void)
{
    vTpl[0] = "auto"; nTpl[0] = "Automatic (by game type)";
    for (int i = 0; i < NUM_TEMPLATES; ++i) { vTpl[i + 1] = kTemplates[i].id; nTpl[i + 1] = kTemplates[i].name; }
    // audio outputs from /proc/asound/pcm: "00-00: name : desc : playback 1"
    nAudioDev = 0;
    snprintf (audioVals[0], 32, "default"); snprintf (audioNames[0], 64, "System default"); nAudioDev = 1;
    FILE* f = fopen ("/proc/asound/pcm", "r");
    char line[256];
    while (f && fgets (line, sizeof line, f) && nAudioDev < 12)
    {
        int c, d; char name[80] = "";
        if (! strstr (line, "playback") || sscanf (line, "%d-%d: %79[^:]", &c, &d, name) < 2) continue;
        snprintf (audioVals[nAudioDev], 32, "plughw:%d,%d", c, d);
        snprintf (audioNames[nAudioDev], 64, "%.50s (%d,%d)", name, c, d);
        ++nAudioDev;
    }
    if (f) fclose (f);
    for (int i = 0; i < nAudioDev; ++i) { vAudio[i] = audioVals[i]; nAudio[i] = audioNames[i]; }
}
static int optFocus; static char optGame[24];
static int contentH[16];                        // content height of scroll areas measured on the previous frame
static const char* optValueName (const Opt* o, const char* v, char* buf, int n)
{
    for (int i = 0; i < o->n; ++i) if (! strcmp (o->vals[i], v)) return o->names[i];
    snprintf (buf, (size_t) n, "%s", v);
    return buf;
}
// one row; scope: game options store per game ("default" = use the global setting)
static void optRow (int idx, int x, int* y, int w, const Opt* o, int viewTop, int viewBottom)
{
    const int game = o->gameScope && optGame[0];
    const char* raw = game ? game_get (optGame, o->key) : set_get (o->key);
    char vb[128], shown[160];
    if (game && (! raw[0] || ! strcmp (raw, "default")))
        snprintf (shown, sizeof shown, "Default (%s)", optValueName (o, set_get (o->key), vb, sizeof vb));
    else snprintf (shown, sizeof shown, "%s", optValueName (o, raw, vb, sizeof vb));
    const int rowH = o->help && o->help[0] ? 74 : 56;
    const int visible = *y + rowH > viewTop && *y < viewBottom;
    int act = 0;
    if (visible && ui_toggle_row (6000 + idx, x, *y, w, o->label, shown, o->help, optFocus == idx)) { optFocus = idx; act = 2; }
    if (optFocus == idx) { if (ui_take_key (NK_RIGHT)) act = 1; if (ui_take_key (NK_LEFT)) act = -1; if (ui_take_key (NK_OK)) act = 2; }
    static const char* items[16];
    int cur = -1;
    for (int i = 0; i < o->n; ++i) if (! strcmp (o->vals[i], raw)) cur = i;
    if (act == 2)
    {
        int n = 0;
        if (game) items[n++] = "Default (use Settings)";
        for (int i = 0; i < o->n && n < 16; ++i) items[n++] = o->names[i];
        ui_pick (o->label, items, n, game ? (cur < 0 ? 0 : cur + 1) : cur, onPick, (void*) (intptr_t) (6000 + idx));
    }
    else if (act)
    {
        int ni = cur < 0 ? (act > 0 ? 0 : o->n - 1) : cur + act;
        if (ni < 0) ni = 0; if (ni >= o->n) ni = o->n - 1;
        if (game) game_set (optGame, o->key, o->vals[ni]); else { set_set (o->key, o->vals[ni]); settings_save(); }
        snd_play (SND_TICK);
    }
    const int pk = takePick (6000 + idx);
    if (pk >= 0)
    {
        if (game) game_set (optGame, o->key, pk == 0 ? "default" : o->vals[pk - 1]);
        else { set_set (o->key, o->vals[pk]); settings_save(); }
        if (! strcmp (o->key, "uisounds") || ! strcmp (o->key, "uivolume")) { snd_close(); snd_init (set_geti ("uisounds"), set_geti ("uivolume")); }
        libWant = 1;
    }
    *y += rowH;
}
static void sectionTitle (int x, int* y, const char* t, int viewTop, int viewBottom)
{
    if (*y + 50 > viewTop && *y < viewBottom) gfx_text (F_BOLD, x + 4, *y + 14, t, C_ACCENT);
    *y += 56;
}

// ------------------------------------------------------------------ SCREEN: game options
static const Opt kGameOpts[] = {
    { "Performance mode", "profile", "Performance: speed first \xc2\xb7 Balanced \xc2\xb7 Compatibility: exact timing \xc2\xb7 Lightweight: demanding games", vProfile, nProfile, 4, 1 },
    { "Screen scaling", "scale", "Pixel-perfect = whole-number scaling (needed for scanlines)", vScale, nScale, 3, 1 },
    { "Scanlines (CRT look)", "scanlines", "Horizontal games with Pixel-perfect scaling", vScan, nScan, 4, 1 },
    { "Vertical games", "vertical", "Rotate if you stand the MPC on its side", vVert, nVert, 3, 1 },
    { "Low-resolution mode", "lowres", "Draws at half size and doubles it: faster on heavy games", vOnOff, nOnOff, 2, 1 },
    { "Smooth (bilinear) filter", "smooth", "Softer picture, costs some speed", vOnOff, nOnOff, 2, 1 },
    { "Quick resume", "resume", "Saves on exit and continues next time (games that support save states)", vOnOff, nOnOff, 2, 1 },
    { "Game volume", "volume", NULL, vVol, nVol, 7, 1 },
    { "Pad layout", "template", "Used when no layout has been saved for this game", vTpl, nTpl, NUM_TEMPLATES + 1, 1 },
};
static void screen_gameopts (void)
{
    Entry* e = selected();
    if (e == NULL) { openScreen (S_LIB); return; }
    const GameInfo* g = &L.db[e->db];
    snprintf (optGame, sizeof optGame, "%s", g->name);
    const int nrows = (int) (sizeof kGameOpts / sizeof kGameOpts[0]) + 4;
    if (ui_take_key (NK_DOWN) && optFocus < nrows - 1) { ++optFocus; snd_play (SND_TICK); }
    if (ui_take_key (NK_UP) && optFocus > 0) { --optFocus; snd_play (SND_TICK); }
    gfx_fill (0, 0, W, H, C_BG);
    char title[140]; char mainT[100], var[100];
    splitTitle (g->desc, mainT, sizeof mainT, var, sizeof var);
    snprintf (title, sizeof title, "Options \xc2\xb7 %s", mainT);
    const int x = 140, w = W - 280, top = 80, bottom = H - 40;
    ui_scroll_area (1, 0, top, W, bottom - top, contentH[1]);
    int y = top + 10 - (int) U.scroll[1];
    gfx_clip (0, top, W, bottom - top);
    char b[200];
    if (e->bench)
    {
        snprintf (b, sizeof b, "Measured speed on this MPC: %d%% \xe2\x86\x92 suggested mode: %s", e->bench, suggested_profile (e));
        gfx_text (F_BODY, x, y + 8, b, e->bench >= 100 ? C_GOOD : C_WARN); y += 50;
    }
    else { gfx_text (F_SMALL, x, y + 8, "Tip: run \"Measure speed\" below to get a performance suggestion for this game.", C_DIM); y += 44; }
    for (int i = 0; i < (int) (sizeof kGameOpts / sizeof kGameOpts[0]); ++i)
    {
        if (i == 3 && ! (g->rotate == 90 || g->rotate == 270)) continue;
        if (i == 6 && ! g->savestate) { gfx_text (F_SMALL, x + 18, y + 6, "Quick resume: not available (this driver has no save-state support)", C_FAINT); y += 40; continue; }
        if (optFocus == i) { static int lf = -1; if (lf != i) { ui_scroll_to (1, y + (int) U.scroll[1] - top - 40, 120, bottom - top); lf = i; } }
        optRow (i, x, &y, w, &kGameOpts[i], top, bottom);
    }
    y += 10;
    const int base = (int) (sizeof kGameOpts / sizeof kGameOpts[0]);
    const char* btns[] = { "Edit pad controls", "Measure speed (benchmark)", "Delete quick-resume save", "Reset this game's options" };
    for (int k = 0; k < 4; ++k)
    {
        if (y + 70 > top && y < bottom && ui_button (6500 + k, x, y, 420, 60, btns[k], k == 3 ? B_DANGER : B_NORMAL, optFocus == base + k)) optFocus = base + k;
        else if (! (optFocus == base + k && ui_take_key (NK_OK))) { y += 72; continue; }
        if (k == 0) openScreen (S_CONTROLS);
        else if (k == 1) { task_begin (TK_BENCH, "Measuring speed", 1); T.items[T.n++] = (int) (e - L.files); }
        else if (k == 2) ui_confirm ("Delete the quick-resume save?", "The game will start from the beginning next time.", "Delete", onPick, (void*) (intptr_t) ROW_DELSTATE);
        else ui_confirm ("Reset options?", "All options for this game go back to Default (your Settings).", "Reset", onPick, (void*) (intptr_t) ROW_RESETGAME);
        y += 72;
    }
    gfx_unclip();
    contentH[1] = y + (int) U.scroll[1] - top + 20;
    if (takePick (ROW_DELSTATE) == 1)
    {
        char p[700]; lib_path (p, sizeof p, "%s/saves/states/%s/auto.sta", L.root, g->name);
        ui_toast (unlink (p) == 0 ? "Quick-resume save deleted" : "There was no save to delete");
    }
    if (takePick (ROW_RESETGAME) == 1)
    {
        for (size_t i = 0; i < sizeof kGameOpts / sizeof kGameOpts[0]; ++i) game_set (g->name, kGameOpts[i].key, "default");
        ui_toast ("Options reset");
    }
    topBar (title, 6600);
    hintBar ("\xe2\x96\xb2\xe2\x96\xbc choose  \xc2\xb7  \xe2\x97\x80\xe2\x96\xb6 change  \xc2\xb7  OK list  \xc2\xb7  BACK done");
    if (ui_take_key (NK_BACK)) openScreen (S_LIB);
}

// ------------------------------------------------------------------ SCREEN: pad controls editor
static int editDefault; static PadMap edit; static char editGame[24], editWhere[96], editSel[48] = "PAD1"; static int editDirty;
static double flashUntil; static char flashSrc[48];
static const char* actionsForPick[NUM_ACTIONS]; static int actionsForPickIdx[NUM_ACTIONS];
static uint32_t groupColor (int g)
{
    switch (g) { case G_DIR: return C_BLUE; case G_BUTTON: return C_ACCENT; case G_SYSTEM: return C_GOLD; case G_P2: return C_PURPLE; case G_KNOB: return C_TEAL; case G_EXIT: return C_TEXT; default: return C_PANEL3; }
}
static void editor_load (void)
{
    Entry* e = editDefault ? NULL : selected();
    editGame[0] = 0;
    if (e && e->db >= 0) snprintf (editGame, sizeof editGame, "%s", L.db[e->db].name);
    padmap_for_game (&edit, e, editWhere, sizeof editWhere);
    editDirty = 0;
}
static void pickAction (const char* src)
{
    snprintf (editSel, sizeof editSel, "%s", src);
    static char titleB[96]; snprintf (titleB, sizeof titleB, "%s does\xe2\x80\xa6", src);
    static char descs[NUM_ACTIONS][80];
    int n = 0, cur = -1;
    const int knob = ! strncmp (src, "QLINK", 5) || ! strcmp (src, "HW:Data wheel");
    for (int i = 0; i < NUM_ACTIONS; ++i)
    {
        if (knob != (kActions[i].group == G_KNOB) && i != 0) continue;
        snprintf (descs[n], sizeof descs[n], "%s", kActions[i].desc);
        actionsForPick[n] = descs[n]; actionsForPickIdx[n] = i;
        if (! strcmp (padmap_get (&edit, src), kActions[i].name)) cur = n;
        ++n;
    }
    ui_pick (titleB, actionsForPick, n, cur, onPick, (void*) (intptr_t) ROW_PADACT);
}
static void screen_controls (void)
{
    gfx_fill (0, 0, W, H, C_BG);
    // pads pressed on the MPC select themselves (capture mode)
    char kind, id[300];
    while (capture_next (&kind, id, sizeof id))
    {
        const char* src = layout_src_for (id);
        if (src) { snprintf (editSel, sizeof editSel, "%s", src); snprintf (flashSrc, sizeof flashSrc, "%s", src); flashUntil = U.now + 0.4; }
    }
    const int pk = takePick (ROW_PADACT);
    if (pk >= 0) { padmap_set (&edit, editSel, kActions[actionsForPickIdx[pk]].name); editDirty = 1; }
    const int tp = takePick (ROW_TEMPLATE);
    if (tp >= 0) { padmap_template (&edit, kTemplates[tp].id); editDirty = 1; snprintf (editWhere, sizeof editWhere, "%s layout (not saved yet)", kTemplates[tp].name); }
    if (takePick (ROW_REVERT) == 1) { openScreen (S_LIB); return; }
    // keys: arrows move over the pad grid, OK picks an action
    int padNum = ! strncmp (editSel, "PAD", 3) ? atoi (editSel + 3) : 1;
    if (! ui_modal_active())
    {
        if (ui_take_key (NK_LEFT) && (padNum - 1) % 4 > 0) padNum--;
        if (ui_take_key (NK_RIGHT) && (padNum - 1) % 4 < 3) padNum++;
        if (ui_take_key (NK_UP) && padNum <= 12) padNum += 4;
        if (ui_take_key (NK_DOWN) && padNum > 4) padNum -= 4;
        if (! strncmp (editSel, "PAD", 3)) snprintf (editSel, sizeof editSel, "PAD%d", padNum);
        if (ui_take_key (NK_OK)) pickAction (editSel);
    }
    char title[140];
    snprintf (title, sizeof title, "Pad controls \xc2\xb7 %s", editGame[0] ? L.db[db_find (editGame)].desc : "default layout");
    // pad grid (pad 1 bottom-left, like the MPC)
    const int gx = 40, gy = 100, pw = 136, ph = 112, gap = 12;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
        {
            const int pad = (3 - r) * 4 + c + 1;
            char src[16]; snprintf (src, sizeof src, "PAD%d", pad);
            const int x = gx + c * (pw + gap), y = gy + r * (ph + gap);
            const int ai = actionIndex (padmap_get (&edit, src));
            const Action* a = &kActions[ai < 0 ? 0 : ai];
            const uint32_t col = groupColor (a->group);
            ui_hit (7000 + pad, x, y, pw, ph);
            const int flash = U.now < flashUntil && ! strcmp (flashSrc, src);
            gfx_round (x, y, pw, ph, 10, flash ? C_TEXT : (ai > 0 ? C_PANEL2 : C_PANEL), 255);
            if (ai > 0) gfx_round (x + 6, y + 6, pw - 12, 8, 4, col, 255);
            if (! strcmp (editSel, src)) gfx_round_outline (x - 3, y - 3, pw + 6, ph + 6, 13, 3, C_ACCENT, 255);
            char num[8]; snprintf (num, sizeof num, "%d", pad);
            gfx_text (F_SMALL, x + 10, y + ph - 30, num, layout_has (src) ? C_DIM : C_BAD);
            gfx_text_c (F_BOLD, x, y + 14, pw, ph - 34, ai > 0 ? a->label : "\xe2\x80\x94", ai > 0 ? C_TEXT : C_FAINT);
            if (ui_clicked (7000 + pad)) { snd_play (SND_TICK); pickAction (src); }
        }
    gfx_text (F_SMALL, gx, gy + 4 * (ph + gap) + 4, layout_count() >= 16 ? "Tap a pad here, or hit it on the MPC, to change what it does." :
              "Red pad numbers aren't learned yet: Settings \xe2\x86\x92 Set up the MPC pads.", layout_count() >= 16 ? C_DIM : C_BAD);
    // right column
    const int rx = 660, rw = W - rx - 40;
    int y = 96;
    gfx_text (F_SMALL, rx, y, editWhere, editDirty ? C_GOLD : C_DIM); y += 34;
    if (ui_button (7100, rx, y, rw, 52, "Load a layout: Classic, Fighting, Racing\xe2\x80\xa6", B_NORMAL, 0))
    {
        static const char* names[NUM_TEMPLATES]; static char nb[NUM_TEMPLATES][120];
        for (int i = 0; i < NUM_TEMPLATES; ++i) { snprintf (nb[i], sizeof nb[i], "%s \xe2\x80\x94 %s", kTemplates[i].name, kTemplates[i].desc); names[i] = nb[i]; }
        ui_pick ("Start from a layout", names, NUM_TEMPLATES, -1, onPick, (void*) (intptr_t) ROW_TEMPLATE);
    }
    y += 66;
    // Q-Links + learned hardware buttons
    gfx_text (F_BOLD, rx, y, "Q-Links and buttons", C_TEXT); y += 40;
    int shown = 0;
    for (int q = 1; q <= 4; ++q)
    {
        char src[16]; snprintf (src, sizeof src, "QLINK%d", q);
        if (! layout_has (src) && strcmp (padmap_get (&edit, src), "NONE") == 0) continue;
        const int ai = actionIndex (padmap_get (&edit, src));
        char b[80]; snprintf (b, sizeof b, "Q-Link %d: %s%s", q, ai > 0 ? kActions[ai].desc : "nothing", layout_has (src) ? "" : " (not learned)");
        if (ui_button (7200 + q, rx, y, rw, 44, b, B_SMALL, ! strcmp (editSel, src))) pickAction (src);
        y += 50; ++shown;
    }
    const char *ls, *lid;
    for (int i = 0; layout_get (i, &ls, &lid) && shown < 9; ++i)
    {
        if (strncmp (ls, "HW:", 3)) continue;
        const int ai = actionIndex (padmap_get (&edit, ls));
        char b[100]; snprintf (b, sizeof b, "%s: %s", ls + 3, ai > 0 ? kActions[ai].desc : "nothing");
        if (ui_button (7300 + i, rx, y, rw, 44, b, B_SMALL, ! strcmp (editSel, ls))) pickAction (ls);
        y += 50; ++shown;
    }
    if (shown == 0) { gfx_text_wrap (F_SMALL, rx, y, rw, 3, "Learn Q-Links and buttons (Play, Stop, Rec\xe2\x80\xa6) in Settings \xe2\x86\x92 Set up the MPC pads, then assign them here.", C_DIM, 2); y += 70; }
    // feel
    y += 6;
    char b[80];
    static const int thr[] = { 1, 20, 45, 70 }; static const char* thrN[] = { "Most sensitive", "Normal", "Firm", "Hard hits only" };
    int ti = 0; for (int i = 0; i < 4; ++i) if (edit.threshold >= thr[i]) ti = i;
    snprintf (b, sizeof b, "Pad sensitivity: %s", thrN[ti]);
    if (ui_button (7400, rx, y, rw, 44, b, B_SMALL, 0)) { edit.threshold = thr[(ti + 1) % 4]; editDirty = 1; }
    y += 50;
    static const float holds[] = { 0.8f, 1.5f, 2.5f };
    int hi = 0; for (int i = 0; i < 3; ++i) if (edit.exitHold >= holds[i] - 0.01f) hi = i;
    snprintf (b, sizeof b, "Hold EXIT for %.1f s to leave a game", (double) holds[hi]);
    if (ui_button (7401, rx, y, rw, 44, b, B_SMALL, 0)) { edit.exitHold = holds[(hi + 1) % 3]; editDirty = 1; }
    y += 50;
    static const int turbos[] = { 8, 12, 16 };
    int ui2 = 0; for (int i = 0; i < 3; ++i) if (edit.turbo >= turbos[i]) ui2 = i;
    snprintf (b, sizeof b, "Auto-fire speed: %d per second", turbos[ui2]);
    if (ui_button (7402, rx, y, rw, 44, b, B_SMALL, 0)) { edit.turbo = turbos[(ui2 + 1) % 3]; editDirty = 1; }
    y += 50;
    snprintf (b, sizeof b, "Touch top-right corner 1.5 s to exit: %s", edit.touchExit ? "On" : "Off");
    if (ui_button (7403, rx, y, rw, 44, b, B_SMALL, 0)) { edit.touchExit = ! edit.touchExit; editDirty = 1; }
    y += 50;
    snprintf (b, sizeof b, "Pad lights (experimental): %s", edit.lights ? "On" : "Off");
    if (ui_button (7404, rx, y, rw, 44, b, B_SMALL, 0)) { edit.lights = ! edit.lights; editDirty = 1; }
    // save buttons
    const int by = H - 40 - 76;
    char p[700];
    if (editGame[0] && ui_button (7500, rx, by, (rw - 12) / 2, 60, "Save for this game", B_PRIMARY, 0))
    {
        padmap_save (&edit, lib_path (p, sizeof p, "%s/games/%s.conf", L.prof, editGame));
        snprintf (editWhere, sizeof editWhere, "Saved for %s", editGame); editDirty = 0; ui_toast ("Saved for this game");
    }
    if (ui_button (7501, editGame[0] ? rx + (rw + 12) / 2 : rx, by, editGame[0] ? (rw - 12) / 2 : rw, 60, "Save as my default", B_NORMAL, 0))
    {
        padmap_save (&edit, lib_path (p, sizeof p, "%s/default.conf", L.prof));
        snprintf (editWhere, sizeof editWhere, "Your default layout"); editDirty = 0; ui_toast ("Saved as the default for all games");
    }
    if (editGame[0] && ui_button (7502, gx, by, 300, 60, "Forget this game's layout", B_GHOST, 0))
    {
        unlink (lib_path (p, sizeof p, "%s/games/%s.conf", L.prof, editGame));
        editor_load(); ui_toast ("This game now uses the automatic layout");
    }
    topBar (title, 7600);
    hintBar ("Arrows move  \xc2\xb7  OK change  \xc2\xb7  BACK done  \xc2\xb7  Colours: blue joystick, red buttons, gold coin / start / system, teal knobs");
    if (ui_take_key (NK_BACK))
    {
        if (editDirty) ui_confirm ("Leave without saving?", "Your changes to this pad layout will be lost.", "Leave", onPick, (void*) (intptr_t) ROW_REVERT);
        else openScreen (S_LIB);
    }
}

// ------------------------------------------------------------------ SCREEN: learn the MPC's controls
static int learnStep; static double learnIgnoreUntil; static char learnLast[300], learnPendingId[300];
static const char* learnSrc (int s, char* b, int n)
{
    if (s < 16) snprintf (b, (size_t) n, "PAD%d", s + 1);
    else if (s < 20) snprintf (b, (size_t) n, "QLINK%d", s - 15);
    else b[0] = 0;
    return b;
}
static void screen_learn (void)
{
    gfx_fill (0, 0, W, H, C_BG);
    char kind, id[300], src[24];
    const int hwPick = takePick (ROW_HWNAME);
    if (hwPick >= 0 && learnPendingId[0])
    {
        char s[64]; snprintf (s, sizeof s, "HW:%s", kHwNames[hwPick]);
        layout_set (s, learnPendingId); learnPendingId[0] = 0;
        char b[96]; snprintf (b, sizeof b, "Learned: %s", kHwNames[hwPick]); ui_toast (b);
    }
    while (! ui_modal_active() && capture_next (&kind, id, sizeof id))
    {
        if (U.now < learnIgnoreUntil || ! strcmp (id, learnLast)) continue;
        if (learnStep < 16 && kind != 'p') continue;                       // pads: presses only
        if (learnStep >= 16 && learnStep < 20 && kind != 't') continue;    // Q-Links: turns only
        snprintf (learnLast, sizeof learnLast, "%s", id);
        learnIgnoreUntil = U.now + 0.35;
        if (learnStep < 20)
        {
            layout_set (learnSrc (learnStep, src, sizeof src), id);
            snd_play (SND_OK);
            ++learnStep;
        }
        else
        {
            snprintf (learnPendingId, sizeof learnPendingId, "%s", id);
            ui_pick ("Which control was that?", kHwNames, NUM_HW_NAMES, -1, onPick, (void*) (intptr_t) ROW_HWNAME);
        }
        dirty = 1;
    }
    topBar ("Set up the MPC pads", 7700);
    const int alive = padbridge_alive();
    char b[300];
    if (learnStep < 16)
    {
        snprintf (b, sizeof b, "Hit pad %d", learnStep + 1);
        gfx_text_c (F_HUGE, 0, 100, W, 70, b, C_TEXT);
        gfx_text_c (F_BODY, 0, 170, W, 34, "Pads are numbered like on the MPC: pad 1 bottom-left, pad 16 top-right. Any pad bank works.", C_DIM);
        const int pw = 104, ph = 76, gap = 10, gx = (W - 4 * pw - 3 * gap) / 2, gy = 220;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const int pad = (3 - r) * 4 + c + 1, x = gx + c * (pw + gap), y = gy + r * (ph + gap);
                const int done = pad - 1 < learnStep, now = pad - 1 == learnStep;
                gfx_round (x, y, pw, ph, 10, now ? C_ACCENT : (done ? C_PANEL3 : C_PANEL), 255);
                char n[8]; snprintf (n, sizeof n, done ? "\xe2\x9c\x93" : "%d", pad);
                gfx_text_c (F_BOLD, x, y, pw, ph, n, now ? C_TEXT : (done ? C_GOOD : C_DIM));
                if (now) U.anim = 1;
            }
    }
    else if (learnStep < 20)
    {
        snprintf (b, sizeof b, "Turn Q-Link %d", learnStep - 15);
        gfx_text_c (F_HUGE, 0, 140, W, 70, b, C_TEXT);
        gfx_text_c (F_BODY, 0, 214, W, 34, "Q-Links can steer racing games or work dials and paddles. Skip any you don't need.", C_DIM);
    }
    else
    {
        gfx_text_c (F_HUGE, 0, 120, W, 70, "Press any MPC buttons you want to use", C_TEXT);
        gfx_text_wrap (F_BODY, 200, 210, W - 400, 4, "For example Play for START, Rec for COIN, Stop for EXIT, or the data wheel for scrolling. After each press, pick its name from the list. Tap Done when finished.", C_DIM, 6);
        int y = 340;
        const char *ls, *lid;
        for (int i = 0; layout_get (i, &ls, &lid) && y < H - 160; ++i)
            if (! strncmp (ls, "HW:", 3)) { gfx_text (F_BODY, 260, y, ls + 3, C_GOOD); gfx_text_fit (F_SMALL, 560, y + 4, W - 820, lid, C_FAINT); y += 38; }
    }
    if (learnLast[0]) { snprintf (b, sizeof b, "Last signal: %s", learnLast); gfx_text_c (F_SMALL, 0, H - 150, W, 28, b, C_FAINT); }
    if (! alive) gfx_text_c (F_BODY, 0, H - 190, W, 34, "The pad bridge isn't running, so pads can't be read. See MAME/logs/padbridge.log.", C_BAD);
    if (learnStep < 20 && ui_button (7710, W / 2 - 330, H - 120, 200, 60, "Skip", B_NORMAL, 0)) { ++learnStep; if (learnStep == 16 && 0) {} }
    if (learnStep < 16 && ui_button (7711, W / 2 - 110, H - 120, 220, 60, "Skip the pads", B_NORMAL, 0)) learnStep = 16;
    if (ui_button (7712, W / 2 + 130, H - 120, 200, 60, learnStep >= 20 ? "Done" : "Finish", B_PRIMARY, 0) || (learnStep >= 20 && ui_take_key (NK_OK)))
    {
        set_set ("first_run", "0"); settings_save();
        ui_toast ("Pads set up. Tip: change what each pad does in Controls.");
        openScreen (S_LIB);
    }
    hintBar ("Touch the buttons on screen while learning (pads are being listened to)");
    if (ui_take_key (NK_BACK)) openScreen (S_LIB);
}

// ------------------------------------------------------------------ SCREEN: ROM manager
enum { R_INSTALLED, R_USB, R_WEB, R_FREE, R_SUPPORTED, R_TOOLS, R_COUNT };
static int romTab, romSel;
static const char* kRomTabs[R_COUNT] = { "Installed", "USB import", "Web upload", "Free ROMs", "Supported", "Tools" };
typedef struct { char path[300]; char name[128]; char set[24]; int known, isBios, sel, matched, needed; long long size; } UsbFile;
static UsbFile usb[500]; static int nusb, usbScanned;
static pid_t dropPid; static char dropPin[8];
static void scanUsbDir (const char* dir, int depth)
{
    if (depth > 3 || nusb >= 500) return;
    DIR* d = opendir (dir);
    if (d == NULL) return;
    struct dirent* de;
    while ((de = readdir (d)) != NULL && nusb < 500)
    {
        if (de->d_name[0] == '.') continue;
        char p[600]; snprintf (p, sizeof p, "%s/%s", dir, de->d_name);
        if (! strncmp (p, L.root, strlen (L.root)) && ! strstr (p, "/import")) continue;   // our own folders
        struct stat st;
        if (stat (p, &st) != 0) continue;
        if (S_ISDIR (st.st_mode)) { if (strcasecmp (de->d_name, "System Volume Information")) scanUsbDir (p, depth + 1); continue; }
        const char* dot = strrchr (de->d_name, '.');
        if (! dot || (strcasecmp (dot, ".zip") && strcasecmp (dot, ".7z"))) continue;
        UsbFile* u = &usb[nusb++];
        memset (u, 0, sizeof *u);
        snprintf (u->path, sizeof u->path, "%s", p); snprintf (u->name, sizeof u->name, "%s", de->d_name);
        u->size = st.st_size;
        char set[128]; snprintf (set, sizeof set, "%.*s", (int) (dot - de->d_name), de->d_name);
        for (char* c = set; *c; ++c) *c = (char) tolower ((unsigned char) *c);
        const int di = db_find (set);
        if (di >= 0) { snprintf (u->set, sizeof u->set, "%s", set); u->known = 1; u->isBios = L.db[di].isbios; }
        else if (! strcasecmp (dot, ".zip") && zip_identify (p, u->set, sizeof u->set, &u->matched, &u->needed) && u->matched * 2 >= u->needed)
        { u->known = 2; u->isBios = L.db[db_find (u->set)].isbios; }
        u->sel = u->known && ! lib_has_set (u->set);
    }
    closedir (d);
}
static void scanUsb (void)
{
    nusb = 0;
    char p[600];
    scanUsbDir (lib_path (p, sizeof p, "%s/import", L.root), 0);
    FILE* f = fopen ("/proc/mounts", "r");
    char line[512];
    while (f && fgets (line, sizeof line, f))
    {
        char dev[200], mnt[300];
        if (sscanf (line, "%199s %299s", dev, mnt) != 2) continue;
        if (strncmp (mnt, "/media/", 7) && strncmp (mnt, "/run/media/", 11) && strncmp (mnt, "/mnt/", 5)) continue;
        if (! strncmp (L.root, mnt, strlen (mnt)) && (L.root[strlen (mnt)] == '/' || ! L.root[strlen (mnt)])) continue;   // the drive MAME lives on
        if (strstr (mnt, "az01-internal")) continue;
        scanUsbDir (mnt, 0);
    }
    if (f) fclose (f);
    usbScanned = 1;
}
static void startImport (int all)
{
    task_begin (TK_IMPORT, "Importing", 1);
    for (int i = 0; i < nusb && T.n < 600; ++i)
    {
        UsbFile* u = &usb[i];
        if (! u->known || (! all && ! u->sel)) continue;
        const char* ext = strrchr (u->name, '.');
        snprintf (T.src[T.n], sizeof T.src[T.n], "%s", u->path);
        snprintf (T.dst[T.n], sizeof T.dst[T.n], "%s/%s%s", u->isBios ? L.bios : L.roms, u->set, ext);
        T.items[T.n] = i; T.n++;
    }
    if (T.n == 0) { T.kind = TK_NONE; ui_toast ("Nothing selected to import"); }
}
static void localIPs (char* out, int n)
{
    struct ifaddrs* ifs; out[0] = 0;
    if (getifaddrs (&ifs) != 0) return;
    for (struct ifaddrs* i = ifs; i; i = i->ifa_next)
    {
        if (! i->ifa_addr || i->ifa_addr->sa_family != AF_INET || ! strcmp (i->ifa_name, "lo")) continue;
        char ip[32]; inet_ntop (AF_INET, &((struct sockaddr_in*) i->ifa_addr)->sin_addr, ip, sizeof ip);
        const size_t l = strlen (out);
        snprintf (out + l, (size_t) n - l, "%shttp://%s:8080", l ? "   or   " : "", ip);
    }
    freeifaddrs (ifs);
}
static void dropStop (void) { if (dropPid > 0) { kill (dropPid, SIGTERM); waitpid (dropPid, NULL, 0); dropPid = 0; } }
static void dropStart (void)
{
    if (dropPid > 0) return;
    snprintf (dropPin, sizeof dropPin, "%04d", (int) (time (NULL) * 7919 % 10000));
    char exe[400]; lib_path (exe, sizeof exe, "%s/romdrop", L.sys);
    const pid_t p = fork();
    if (p == 0)
    {
        char log[400]; lib_path (log, sizeof log, "%s/logs/romdrop.log", L.root);
        const int fd = open (log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2 (fd, 1); dup2 (fd, 2); }
        execl (exe, "romdrop", "--root", L.root, "--port", "8080", "--pin", dropPin, (char*) NULL);
        _exit (127);
    }
    dropPid = p > 0 ? p : 0;
}
static int haveDownloader (char* tool, int n)
{
    char out[256];
    if (run_capture ("command -v curl 2>/dev/null", out, sizeof out) == 0 && out[0]) { snprintf (tool, (size_t) n, "curl"); return 1; }
    if (run_capture ("command -v wget 2>/dev/null", out, sizeof out) == 0 && out[0]) { snprintf (tool, (size_t) n, "wget"); return 1; }
    return 0;
}
static void downloadCmd (const char* tool, const char* url, const char* dst, char* cmd, int n)
{
    if (! strcmp (tool, "curl")) snprintf (cmd, (size_t) n, "curl -fsSL --max-time 120 -o %s.part %s && mv -f %s.part %s", shq (q1, sizeof q1, dst), shq (q2, sizeof q2, url), q1, q1);
    else snprintf (cmd, (size_t) n, "wget -q -T 120 -O %s.part %s && mv -f %s.part %s", shq (q1, sizeof q1, dst), shq (q2, sizeof q2, url), q1, q1);
    snprintf (cmd + strlen (cmd), (size_t) n - strlen (cmd), " || { rm -f %s.part; exit 1; }", q1);
}
// sets published for free (non-commercial) at mamedev.org/roms that this build supports
static const char* const kFreeRoms[] = { "alienar", "carpolo", "circus", "crash", "firetrk", "gridlee", "montecar", "polyplay", "ripcord",
                                         "robby", "robotbwl", "sidetrac", "spectar", "sprint1", "sprint2", "superbug", "supertnk", "targ", "teetert" };
enum { NFREE = sizeof kFreeRoms / sizeof kFreeRoms[0] };
static char supQuery[32]; static int supKb, supKbFocus;
static void screen_roms (void)
{
    gfx_fill (0, 0, W, H, C_BG);
    if (! ui_modal_active() && ! supKb)
    {
        if (ui_take_key (NK_RIGHT)) { romTab = (romTab + 1) % R_COUNT; romSel = 0; U.scroll[2] = 0; if (romTab == R_USB) usbScanned = 0; }
        if (ui_take_key (NK_LEFT)) { romTab = (romTab + R_COUNT - 1) % R_COUNT; romSel = 0; U.scroll[2] = 0; if (romTab == R_USB) usbScanned = 0; }
    }
    topBar ("ROM manager", 8000);
    int tx = 380;
    for (int t = 0; t < R_COUNT; ++t)
    {
        const int tw = gfx_text_w (F_BODY, kRomTabs[t]) + 30;
        if (ui_button (8010 + t, tx, 12, tw, 48, kRomTabs[t], romTab == t ? B_TAB_ON : B_TAB, 0)) { romTab = t; romSel = 0; U.scroll[2] = 0; if (t == R_USB) usbScanned = 0; }
        tx += tw + 6;
    }
    const int top = 90, bottom = H - 40, x = 40, w = W - 80;
    char b[400], p[700];
    struct statvfs vs;
    if (statvfs (L.root, &vs) == 0)
    {
        snprintf (b, sizeof b, "%s \xc2\xb7 %.1f GB free", L.root, (double) vs.f_bavail * vs.f_frsize / 1e9);
        gfx_text (F_SMALL, W - 40 - gfx_text_w (F_SMALL, b), bottom - 30, b, C_FAINT);
    }
    if (romTab == R_INSTALLED)
    {
        const int rowH = 60, viewH = bottom - top - 40;
        if (ui_take_key (NK_DOWN) && romSel < L.nfiles - 1) { ++romSel; ui_scroll_to (2, romSel * rowH, rowH, viewH); }
        if (ui_take_key (NK_UP) && romSel > 0) { --romSel; ui_scroll_to (2, romSel * rowH, rowH, viewH); }
        if (L.nfiles == 0) gfx_text_wrap (F_BODY, x, top + 20, w, 6, "No ROM files yet. Copy zips to MAME/roms (games) or MAME/bios (BIOS sets like neogeo.zip) by SSH, or use Import USB / Web upload. Then use Tools \xe2\x86\x92 Rescan.", C_DIM, 6);
        ui_scroll_area (2, x, top, w - 420, viewH, L.nfiles * rowH);
        gfx_clip (x, top, w - 420, viewH);
        for (int i = 0; i < L.nfiles; ++i)
        {
            const int ry = top + i * rowH - (int) U.scroll[2];
            if (ry + rowH < top || ry > top + viewH) continue;
            const Entry* e = &L.files[i];
            ui_hit (8100 + i, x, ry, w - 420, rowH);
            gfx_round (x, ry, w - 430, rowH - 6, 8, i == romSel ? C_PANEL3 : C_PANEL, 255);
            gfx_circle (x + 20, ry + rowH / 2 - 3, 7, statusColor (e->vstatus), 255);
            gfx_text_fit (F_BODY, x + 40, ry + 4, 300, e->file, C_TEXT);
            gfx_text_fit (F_SMALL, x + 350, ry + 8, w - 420 - 370, e->db >= 0 ? L.db[e->db].desc : "unknown set", C_DIM);
            snprintf (b, sizeof b, "%.1f MB \xc2\xb7 %s", e->size / 1e6, vstatus_text (e->vstatus));
            gfx_text_fit (F_SMALL, x + 40, ry + 32, w - 480, b, statusColor (e->vstatus));
            if (ui_clicked (8100 + i)) romSel = i;
        }
        gfx_unclip();
        if (romSel < L.nfiles)
        {
            Entry* e = &L.files[romSel];
            const int dx = W - 440, dw = 400;
            gfx_round (dx, top, dw, viewH, 10, C_PANEL, 255);
            gfx_text_fit (F_BOLD, dx + 20, top + 16, dw - 40, e->file, C_TEXT);
            gfx_text_wrap (F_SMALL, dx + 20, top + 60, dw - 40, 3, e->db >= 0 ? L.db[e->db].desc : "This file name doesn't match a ROM set in this MAME build. Import it through Import USB (from MAME/import) to identify it by its contents.", C_DIM, 2);
            gfx_text_wrap (F_SMALL, dx + 20, top + 150, dw - 40, 2, vstatus_text (e->vstatus), statusColor (e->vstatus), 2);
            if (e->vdetail[0]) gfx_text_wrap (F_SMALL, dx + 20, top + 190, dw - 40, 4, e->vdetail, C_DIM, 2);
            snprintf (b, sizeof b, "In MAME/%s \xc2\xb7 %.1f MB", e->dir == 'b' ? "bios" : "roms", e->size / 1e6);
            gfx_text (F_SMALL, dx + 20, top + 300, b, C_FAINT);
            if (e->db >= 0 && ! L.db[e->db].isbios && ui_button (8090, dx + 20, top + viewH - 150, dw - 40, 56, "Check this set again", B_NORMAL, 0))
            { e->vstatus = V_UNKNOWN; task_begin (TK_VERIFY, "Checking", 1); T.items[T.n++] = romSel; }
            if (ui_button (8091, dx + 20, top + viewH - 80, dw - 40, 56, "Delete this file", B_DANGER, 0))
            {
                snprintf (b, sizeof b, "%s will be deleted from the MPC's drive. Its saves and settings are kept.", e->file);
                ui_confirm ("Delete ROM file?", b, "Delete", onPick, (void*) (intptr_t) ROW_DELETE);
            }
            if (takePick (ROW_DELETE) == 1)
            {
                unlink (lib_path (p, sizeof p, "%s/%s", e->dir == 'b' ? L.bios : L.roms, e->file));
                lib_scan(); libWant = 1; if (romSel >= L.nfiles) romSel = L.nfiles ? L.nfiles - 1 : 0; ui_toast ("Deleted");
            }
        }
    }
    else if (romTab == R_USB)
    {
        if (! usbScanned) scanUsb();
        gfx_text_wrap (F_SMALL, x, top, w, 2, "ROM zips found on USB drives and in MAME/import (files there are moved, not copied). Recognised sets are ticked. A zip with the wrong name is identified by its contents.", C_DIM, 2);
        const int rowH = 56, ltop = top + 64, viewH = bottom - ltop - 90;
        if (ui_take_key (NK_DOWN) && romSel < nusb - 1) { ++romSel; ui_scroll_to (2, romSel * rowH, rowH, viewH); }
        if (ui_take_key (NK_UP) && romSel > 0) { --romSel; ui_scroll_to (2, romSel * rowH, rowH, viewH); }
        if (ui_take_key (NK_OK) && romSel < nusb && usb[romSel].known) usb[romSel].sel = ! usb[romSel].sel;
        ui_scroll_area (2, x, ltop, w, viewH, nusb * rowH);
        gfx_clip (x, ltop, w, viewH);
        for (int i = 0; i < nusb; ++i)
        {
            const int ry = ltop + i * rowH - (int) U.scroll[2];
            if (ry + rowH < ltop || ry > ltop + viewH) continue;
            UsbFile* u = &usb[i];
            ui_hit (8200 + i, x, ry, w, rowH);
            gfx_round (x, ry, w, rowH - 6, 8, i == romSel ? C_PANEL3 : C_PANEL, 255);
            gfx_text (F_BOLD, x + 16, ry + 8, u->known ? (u->sel ? "\xe2\x9c\x93" : "\xe2\x97\x8b") : "\xe2\x80\x94", u->sel ? C_GOOD : C_DIM);
            gfx_text_fit (F_BODY, x + 56, ry + 8, 420, u->name, C_TEXT);
            if (u->known == 1) snprintf (b, sizeof b, "%s%s%s", L.db[db_find (u->set)].desc, u->isBios ? " (BIOS)" : "", lib_has_set (u->set) ? " \xc2\xb7 already installed" : "");
            else if (u->known == 2) snprintf (b, sizeof b, "Identified as %s.zip: %s (%d of %d ROMs)", u->set, L.db[db_find (u->set)].desc, u->matched, u->needed);
            else snprintf (b, sizeof b, "Not recognised by this MAME build");
            gfx_text_fit (F_SMALL, x + 500, ry + 12, w - 520, b, u->known ? C_DIM : C_FAINT);
            if (ui_clicked (8200 + i)) { romSel = i; if (u->known) u->sel = ! u->sel; }
        }
        gfx_unclip();
        if (nusb == 0) gfx_text_wrap (F_BODY, x, ltop + 20, w, 4, "No ROM zips found. Plug in a USB drive (FAT32 or exFAT) with your ROM zips, or copy them into MAME/import, then tap Rescan.", C_DIM, 6);
        if (ui_button (8290, x, bottom - 76, 200, 60, "Rescan", B_NORMAL, 0)) usbScanned = 0;
        if (ui_button (8291, x + 216, bottom - 76, 300, 60, "Import ticked files", B_PRIMARY, 0)) startImport (0);
    }
    else if (romTab == R_WEB)
    {
        gfx_text_wrap (F_BODY, x, top, w, 4, "Add games from a phone or computer on the same network: switch the upload page on, open the address in a browser, enter the PIN and drop your ROM zips on the page. The page only runs while it's switched on here.", C_DIM, 6);
        if (dropPid > 0 && waitpid (dropPid, NULL, WNOHANG) == dropPid) { dropPid = 0; ui_toast ("The upload page stopped (see MAME/logs/romdrop.log)"); }
        if (ui_button (8300, x, top + 140, 360, 64, dropPid ? "Switch the upload page off" : "Switch the upload page on", dropPid ? B_DANGER : B_PRIMARY, 0) || ui_take_key (NK_OK))
        { if (dropPid) dropStop(); else dropStart(); }
        if (dropPid)
        {
            char ips[400]; localIPs (ips, sizeof ips);
            gfx_text (F_BODY, x, top + 240, "Open in a browser:", C_DIM);
            gfx_text_fit (F_TITLE, x, top + 276, w, ips[0] ? ips : "(no network connection found)", ips[0] ? C_TEXT : C_BAD);
            snprintf (b, sizeof b, "PIN  %s", dropPin);
            gfx_text (F_HUGE, x, top + 350, b, C_GOLD);
            gfx_text_wrap (F_SMALL, x, top + 440, w, 3, "Uploaded files land in MAME/roms (or MAME/bios if you tick BIOS on the page). They appear in the library when you go back to it; new sets are checked automatically.", C_DIM, 2);
            libWant = 1;
        }
    }
    else if (romTab == R_FREE)
    {
        gfx_text_wrap (F_SMALL, x, top, w, 3, "These games were released by their rights holders for free non-commercial use and are published on mamedev.org. Downloading needs the MPC to be online (and curl or wget on the MPC). Nothing else is ever downloaded.", C_DIM, 2);
        const int rowH = 52, ltop = top + 70, viewH = bottom - ltop - 90;
        ui_scroll_area (2, x, ltop, w, viewH, NFREE * rowH);
        gfx_clip (x, ltop, w, viewH);
        for (int i = 0; i < NFREE; ++i)
        {
            const int di = db_find (kFreeRoms[i]);
            if (di < 0) continue;
            const int ry = ltop + i * rowH - (int) U.scroll[2];
            gfx_round (x, ry, w, rowH - 6, 8, C_PANEL, 255);
            gfx_text_fit (F_BODY, x + 16, ry + 6, 560, L.db[di].desc, C_TEXT);
            snprintf (b, sizeof b, "%s \xc2\xb7 %s \xc2\xb7 %s.zip", L.db[di].year, L.db[di].manuf, kFreeRoms[i]);
            gfx_text_fit (F_SMALL, x + 600, ry + 10, w - 820, b, C_DIM);
            if (lib_has_set (kFreeRoms[i])) gfx_text (F_SMALL, x + w - 150, ry + 10, "\xe2\x9c\x93 installed", C_GOOD);
        }
        gfx_unclip();
        if (ui_button (8400, x, bottom - 76, 420, 60, "Download the ones not installed", B_PRIMARY, 0))
        {
            char tool[16];
            if (! haveDownloader (tool, sizeof tool)) ui_message ("No downloader on this MPC", "Neither curl nor wget is installed. Download the zips on a computer from mamedev.org/roms and add them with Web upload or a USB drive.");
            else
            {
                task_begin (TK_FREEROM, "Downloading free ROMs", 1);
                for (int i = 0; i < NFREE; ++i)
                {
                    if (db_find (kFreeRoms[i]) < 0 || lib_has_set (kFreeRoms[i])) continue;
                    char url[200], dst[400];
                    snprintf (url, sizeof url, "https://www.mamedev.org/roms/%s/%s.zip", kFreeRoms[i], kFreeRoms[i]);
                    snprintf (dst, sizeof dst, "%s/%s.zip", L.roms, kFreeRoms[i]);
                    downloadCmd (tool, url, dst, T.src[T.n], sizeof T.src[T.n]);
                    snprintf (T.dst[T.n], sizeof T.dst[T.n], "%s.zip", kFreeRoms[i]);
                    T.items[T.n] = i; T.n++;
                }
                if (T.n == 0) { T.kind = TK_NONE; ui_toast ("All of them are installed"); }
            }
        }
    }
    else if (romTab == R_SUPPORTED)
    {
        static int sup[MAXGAMES]; int ns = 0;
        for (int i = 0; i < L.ndb; ++i)
        {
            const GameInfo* g = &L.db[i];
            if (g->parent[0] || g->isbios) continue;
            if (supQuery[0] && ! ci_contains (g->desc, supQuery) && ! ci_contains (g->name, supQuery)) continue;
            sup[ns++] = i;
        }
        snprintf (b, sizeof b, "%d games (plus their clones) run in this MAME 0.242 build. The zip name is the set name shown on the right.", ns);
        gfx_text (F_SMALL, x, top, b, C_DIM);
        if (ui_button (8500, W - 300, top - 6, 260, 44, supQuery[0] ? supQuery : "Search\xe2\x80\xa6", B_SMALL, 0)) supKb = ! supKb;
        const int rowH = 44, ltop = top + 44, viewH = bottom - ltop - 10;
        ui_scroll_area (2, x, ltop, w, viewH, ns * rowH);
        gfx_clip (x, ltop, w, viewH);
        for (int k = (int) U.scroll[2] / rowH; k < ns && k * rowH - (int) U.scroll[2] < viewH; ++k)
        {
            const GameInfo* g = &L.db[sup[k]];
            const int ry = ltop + k * rowH - (int) U.scroll[2];
            if (k % 2) gfx_fill (x, ry, w, rowH, C_PANEL);
            gfx_text_fit (F_BODY, x + 12, ry + 4, 640, g->desc, lib_has_set (g->name) ? C_GOOD : C_TEXT);
            snprintf (b, sizeof b, "%s \xc2\xb7 %s", g->year, g->genre);
            gfx_text_fit (F_SMALL, x + 680, ry + 8, 300, b, C_DIM);
            snprintf (b, sizeof b, "%s.zip%s", g->name, g->romof[0] && strcmp (g->romof, g->parent) ? " + " : "");
            if (g->romof[0] && db_find (g->romof) >= 0 && L.db[db_find (g->romof)].isbios) snprintf (b, sizeof b, "%s.zip + %s.zip", g->name, g->romof);
            gfx_text (F_SMALL, x + w - 260, ry + 8, b, C_FAINT);
        }
        gfx_unclip();
        if (supKb)
        {
            if (ui_keyboard (120, H - 40 - 330, W - 240, 300, supQuery, sizeof supQuery, &supKbFocus)) U.scroll[2] = 0;
            if (ui_take_key (NK_BACK)) supKb = 0;
        }
    }
    else if (romTab == R_TOOLS)
    {
        int unchecked = 0, ready = 0;
        for (int i = 0; i < L.nfiles; ++i) { if (lib_needs_verify (i)) ++unchecked; if (L.files[i].vstatus == V_OK || L.files[i].vstatus == V_BEST) ++ready; }
        const char* labels[] = { "Rescan the ROM folders", "Check new / unchecked sets", "Check every set again", "Measure the speed of every ready game",
                                 "Download missing screenshots" };
        const char* helps[] = { "Picks up files added by SSH or USB without restarting",
                                "Runs MAME's ROM check on sets that haven't been checked",
                                "Useful after replacing ROM files",
                                "About 30 s per game. Results pick the best performance mode automatically",
                                "From the libretro-thumbnails project on GitHub (needs internet + curl or wget)" };
        if (ui_take_key (NK_DOWN) && romSel < 4) ++romSel;
        if (ui_take_key (NK_UP) && romSel > 0) --romSel;
        for (int k = 0; k < 5; ++k)
        {
            const int y = top + k * 96;
            int go = ui_button (8600 + k, x, y, 520, 64, labels[k], B_NORMAL, romSel == k);
            if (romSel == k && ui_take_key (NK_OK)) go = 1;
            gfx_text_fit (F_SMALL, x + 540, y + 20, w - 560, helps[k], C_DIM);
            if (! go) continue;
            if (k == 0) { const int n = lib_scan(); libWant = 1; snprintf (b, sizeof b, "Found %d game%s", n, n == 1 ? "" : "s"); ui_toast (b); }
            else if (k == 1 || k == 2)
            {
                task_begin (TK_VERIFY, "Checking ROM sets", 1);
                for (int i = 0; i < L.nfiles; ++i)
                {
                    if (L.files[i].db < 0 || L.db[L.files[i].db].isbios) continue;
                    if (k == 2) L.files[i].vstatus = V_UNKNOWN;
                    if (lib_needs_verify (i)) T.items[T.n++] = i;
                }
                if (T.n == 0) { T.kind = TK_NONE; ui_toast ("Everything is already checked"); }
            }
            else if (k == 3)
            {
                task_begin (TK_BENCH, "Measuring speed", 1);
                for (int i = 0; i < L.nfiles; ++i) if (L.files[i].vstatus == V_OK || L.files[i].vstatus == V_BEST) T.items[T.n++] = i;
                if (T.n == 0) { T.kind = TK_NONE; ui_toast ("No ready games to measure"); }
            }
            else
            {
                char tool[16];
                if (! haveDownloader (tool, sizeof tool)) { ui_message ("No downloader on this MPC", "Neither curl nor wget is installed. Screenshots still appear on their own: the first time you play a game, one is taken automatically."); continue; }
                task_begin (TK_ART, "Downloading screenshots", 1);
                for (int i = 0; i < L.nfiles && T.n < 600; ++i)
                {
                    const Entry* e = &L.files[i];
                    if (e->db < 0 || L.db[e->db].isbios) continue;
                    char path[900]; if (findArt (L.db[e->db].name, path, sizeof path)) continue;
                    // libretro-thumbnails names files after the description, with &*/:`<>?\| replaced by _
                    char nm[128]; snprintf (nm, sizeof nm, "%s", L.db[e->db].desc);
                    for (char* c = nm; *c; ++c) if (strchr ("&*/:`<>?\\|\"", *c)) *c = '_';
                    char url[400], enc[300]; int k2 = 0;
                    for (const unsigned char* c = (const unsigned char*) nm; *c && k2 < 290; ++c)
                    {
                        if (isalnum (*c) || strchr ("-_.~", *c)) enc[k2++] = (char) *c;
                        else k2 += snprintf (enc + k2, sizeof enc - (size_t) k2, "%%%02X", *c);
                    }
                    enc[k2] = 0;
                    snprintf (url, sizeof url, "https://raw.githubusercontent.com/libretro-thumbnails/MAME/master/Named_Snaps/%s.png", enc);
                    char dst[600]; lib_path (dst, sizeof dst, "%s/screenshots/%s.png", L.root, L.db[e->db].name);
                    downloadCmd (tool, url, dst, T.src[T.n], sizeof T.src[T.n]);
                    snprintf (T.dst[T.n], sizeof T.dst[T.n], "%s", L.db[e->db].name);
                    T.items[T.n] = i; T.n++;
                }
                if (T.n == 0) { T.kind = TK_NONE; ui_toast ("Every game already has a picture"); }
            }
        }
        snprintf (b, sizeof b, "%d files \xc2\xb7 %d ready to play \xc2\xb7 %d not checked yet", L.nfiles, ready, unchecked);
        gfx_text (F_BODY, x, top + 5 * 96 + 10, b, C_DIM);
        gfx_text_wrap (F_SMALL, x, top + 5 * 96 + 56, w, 4, "SSH: copy games to MAME/roms and BIOS sets to MAME/bios, e.g.  scp sf2.zip root@<mpc-ip>:/media/az01-internal/MAME/roms/  then come back here and tap Rescan (or just reopen the library).", C_FAINT, 2);
    }
    hintBar ("\xe2\x97\x80\xe2\x96\xb6 tabs  \xc2\xb7  \xe2\x96\xb2\xe2\x96\xbc choose  \xc2\xb7  OK select  \xc2\xb7  BACK library");
    if (! supKb && ui_take_key (NK_BACK)) { dropStop(); openScreen (S_LIB); }
}

// ------------------------------------------------------------------ SCREEN: settings
static const Opt kSettings[] = {
    { "Performance mode", "profile", "Default for every game (each game can override it)", vProfile, nProfile, 4, 0 },
    { "Use measured speed", "autoprofile", "Games with a benchmark use the mode that suits them", vOnOff, nOnOff, 2, 0 },
    { "Screen scaling", "scale", NULL, vScale, nScale, 3, 0 },
    { "Scanlines (CRT look)", "scanlines", "Needs Pixel-perfect scaling; horizontal games", vScan, nScan, 4, 0 },
    { "Vertical games", "vertical", NULL, vVert, nVert, 3, 0 },
    { "Smooth (bilinear) filter", "smooth", NULL, vOnOff, nOnOff, 2, 0 },
    { "Low-resolution mode", "lowres", "Half-size drawing for every game (faster)", vOnOff, nOnOff, 2, 0 },
    { "Tear-free display", "tearfree", "Double-buffered framebuffer; turn off if the picture flickers", vOnOff, nOnOff, 2, 0 },
    { "Panel rotation", "rotate_panel", "Only if the picture is sideways on your MPC", vRot, nRot, 4, 0 },
    { "Game volume", "volume", NULL, vVol, nVol, 7, 0 },
    { "Audio output", "audiodev", "MPC X main outputs = System default", NULL, NULL, 0, 0 },
    { "Quick resume", "resume", "Continue games where you left off", vOnOff, nOnOff, 2, 0 },
    { "Menu sounds", "uisounds", NULL, vOnOff, nOnOff, 2, 0 },
    { "Menu sound volume", "uivolume", NULL, vUiVol, nUiVol, 5, 0 },
    { "Pad layout for new games", "template", "Automatic picks Fighting / Racing / Shoot 'em up\xe2\x80\xa6 from the game", vTpl, nTpl, NUM_TEMPLATES + 1, 0 },
    { "Show clones", "show_clones", "Other versions of a game you also have the parent of", vOnOff, nOnOff, 2, 0 },
    { "Hide sets that can't start", "hide_bad", NULL, vOnOff, nOnOff, 2, 0 },
    { "Sort", "sort", NULL, vSort, nSort, 4, 0 },
    { "Attract mode", "attract", "Shows screenshots after a while without input", vOnOff, nOnOff, 2, 0 },
    { "Attract mode starts after", "attract_delay", NULL, vDelay, nDelay, 4, 0 },
    { "Automatic screenshots", "autosnap", "Takes one picture of each game the first time you play it", vOnOff, nOnOff, 2, 0 },
};
static void screen_settings (void)
{
    gfx_fill (0, 0, W, H, C_BG);
    Opt rows[sizeof kSettings / sizeof kSettings[0]];
    memcpy (rows, kSettings, sizeof rows);
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) if (! strcmp (rows[i].key, "audiodev")) { rows[i].vals = vAudio; rows[i].names = nAudio; rows[i].n = nAudioDev; }
    const int nOpt = (int) (sizeof rows / sizeof rows[0]), nrows = nOpt + 4;
    if (ui_take_key (NK_DOWN) && optFocus < nrows - 1) { ++optFocus; snd_play (SND_TICK); }
    if (ui_take_key (NK_UP) && optFocus > 0) { --optFocus; snd_play (SND_TICK); }
    optGame[0] = 0;
    const int x = 140, w = W - 280, top = 80, bottom = H - 40;
    ui_scroll_area (1, 0, top, W, bottom - top, contentH[1]);
    int y = top + 6 - (int) U.scroll[1];
    gfx_clip (0, top, W, bottom - top);
    const char* sections[] = { "Performance", "Display", "Audio", "Library & controls" };
    const int sectionAt[] = { 0, 2, 9, 14 };
    int focusY = 0;
    for (int i = 0; i < nOpt; ++i)
    {
        for (int s = 0; s < 4; ++s) if (sectionAt[s] == i) sectionTitle (x, &y, sections[s], top, bottom);
        if (optFocus == i) focusY = y;
        optRow (i, x, &y, w, &rows[i], top, bottom);
    }
    sectionTitle (x, &y, "System", top, bottom);
    const char* btns[] = { "Set up the MPC pads (learn)", "Edit my default pad layout", "Help: how MPC Arcade works", "Exit to the MPC" };
    for (int k = 0; k < 4; ++k)
    {
        if (optFocus == nOpt + k) focusY = y;
        int go = ui_button (9000 + k, x, y, 460, 60, btns[k], k == 3 ? B_DANGER : B_NORMAL, optFocus == nOpt + k);
        if (optFocus == nOpt + k && ui_take_key (NK_OK)) go = 1;
        if (go)
        {
            if (k == 0) openScreen (S_LEARN);
            else if (k == 1) { editDefault = 1; openScreen (S_CONTROLS); }
            else if (k == 2) ui_message ("How MPC Arcade works",
                "Opening it from the MPC Arcade plugin steps the MPC app aside so MAME gets the whole machine; leaving brings the MPC app back (save your project first).\n"
                "Pads: hold EXIT to leave a game. MENU (Tab) opens MAME's menu in a game. SAVE / LOAD use state slot 1.\n"
                "Touch: hold the top-right corner 1.5 s to leave a game.\n"
                "Stuck? Hold EXIT for 6 s: everything closes and the MPC app returns. Over SSH: sh /media/az01-internal/MAME/system/arcadectl.sh stop");
            else requestExit();
        }
        y += 72;
    }
    char b[200];
    snprintf (b, sizeof b, "MPC Arcade %s \xc2\xb7 MAME 0.242 \xc2\xb7 %d supported sets \xc2\xb7 %s", VERSION, L.ndb, L.root);
    gfx_text (F_SMALL, x, y + 10, b, C_FAINT); y += 50;
    gfx_unclip();
    contentH[1] = y + (int) U.scroll[1] - top + 20;
    static int lastFocus = -1;
    if (optFocus != lastFocus && lastFocus >= 0) { ui_scroll_to (1, focusY + (int) U.scroll[1] - top - 70, 150, bottom - top); dirty = 1; }
    lastFocus = optFocus;
    if (takePick (ROW_EXIT) == 1) exitCode = 0;
    topBar ("Settings", 9100);
    hintBar ("\xe2\x96\xb2\xe2\x96\xbc choose  \xc2\xb7  \xe2\x97\x80\xe2\x96\xb6 change  \xc2\xb7  OK list  \xc2\xb7  BACK library");
    if (ui_take_key (NK_BACK)) openScreen (S_LIB);
}

// ------------------------------------------------------------------ SCREEN: game info (history.xml if present)
static char infoText[6000];
static void loadInfo (const GameInfo* g)
{
    infoText[0] = 0;
    char p[600];
    FILE* f = fopen (lib_path (p, sizeof p, "%s/dats/history.xml", L.root), "r");
    if (f)
    {
        char line[2048], key1[64], key2[64];
        snprintf (key1, sizeof key1, "name=\"%s\"", g->name); snprintf (key2, sizeof key2, "name=\"%s\"", g->parent[0] ? g->parent : g->name);
        int found = 0, inText = 0; size_t n = 0;
        while (fgets (line, sizeof line, f))
        {
            if (! found) { if (strstr (line, "<system") && (strstr (line, key1) || strstr (line, key2))) found = 1; continue; }
            char* s = line;
            if (! inText) { char* t = strstr (line, "<text>"); if (! t) continue; inText = 1; s = t + 6; }
            char* end = strstr (s, "</text>");
            if (end) *end = 0;
            // unescape the common entities
            for (char* c = s; *c && n < sizeof infoText - 2; ++c)
            {
                if (! strncmp (c, "&amp;", 5)) { infoText[n++] = '&'; c += 4; }
                else if (! strncmp (c, "&lt;", 4)) { infoText[n++] = '<'; c += 3; }
                else if (! strncmp (c, "&gt;", 4)) { infoText[n++] = '>'; c += 3; }
                else if (! strncmp (c, "&quot;", 6)) { infoText[n++] = '"'; c += 5; }
                else if (! strncmp (c, "&apos;", 6)) { infoText[n++] = '\''; c += 5; }
                else if (*c != '\r') infoText[n++] = *c;
            }
            infoText[n] = 0;
            if (end) break;
        }
        fclose (f);
    }
    if (! infoText[0])
        snprintf (infoText, sizeof infoText, "No history text for this game. Put MAME's history.xml (from arcade-history.com) in MAME/dats to see the story, tips and trivia of each game here.");
}
static void screen_info (void)
{
    Entry* e = selected();
    if (e == NULL) { openScreen (S_LIB); return; }
    const GameInfo* g = &L.db[e->db];
    gfx_fill (0, 0, W, H, C_BG);
    const int top = 80, bottom = H - 40, x = 60, w = W - 120;
    if (ui_take_key (NK_DOWN)) U.scroll[1] += 120;
    if (ui_take_key (NK_UP)) U.scroll[1] -= 120;
    ui_scroll_area (1, 0, top, W, bottom - top, contentH[1]);
    gfx_clip (0, top, W, bottom - top);
    int y = top + 16 - (int) U.scroll[1];
    char b[300];
    gfx_text_fit (F_TITLE, x, y, w, g->desc, C_TEXT); y += 50;
    snprintf (b, sizeof b, "%s \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 set %s%s%s \xc2\xb7 driver %s", g->manuf, g->year, g->genre, g->name, g->parent[0] ? ", clone of " : "", g->parent, g->source);
    gfx_text_fit (F_SMALL, x, y, w, b, C_DIM); y += 30;
    char c2[120];
    snprintf (b, sizeof b, "%s \xc2\xb7 screen %dx%d%s \xc2\xb7 emulation %s \xc2\xb7 save states %s", controlsText (g, c2, sizeof c2), g->sw, g->sh,
              g->rotate == 90 || g->rotate == 270 ? " (vertical)" : "", g->status == 'g' ? "good" : (g->status == 'i' ? "imperfect" : "preliminary"),
              g->savestate ? "yes" : "no");
    gfx_text_fit (F_SMALL, x, y, w, b, C_DIM); y += 44;
    const int lines = gfx_text_wrap (F_BODY, x, y, w, 120, infoText, C_TEXT, 4);
    y += lines * (gfx_text_h (F_BODY) + 4);
    gfx_unclip();
    contentH[1] = y + (int) U.scroll[1] - top + 40;
    topBar ("Game info", 9200);
    hintBar ("\xe2\x96\xb2\xe2\x96\xbc scroll  \xc2\xb7  BACK library");
    if (ui_take_key (NK_BACK) || ui_take_key (NK_OK)) openScreen (S_LIB);
}

// ------------------------------------------------------------------ SCREEN: attract mode
static int attractIdx = -1; static double attractNext; static Image* attractImg; static char attractTitle[120], attractSub[120];
static void attract_next (void)
{
    img_free (attractImg); attractImg = NULL;
    for (int tries = 0; tries < L.nfiles && ! attractImg; ++tries)
    {
        attractIdx = (attractIdx + 1 + rand() % 3) % (L.nfiles ? L.nfiles : 1);
        const Entry* e = &L.files[attractIdx];
        if (e->db < 0 || L.db[e->db].isbios) continue;
        char p[900];
        if (! findArt (L.db[e->db].name, p, sizeof p)) continue;
        attractImg = img_load (p);
        char var[100]; splitTitle (L.db[e->db].desc, attractTitle, sizeof attractTitle, var, sizeof var);
        snprintf (attractSub, sizeof attractSub, "%s \xc2\xb7 %s", L.db[e->db].manuf, L.db[e->db].year);
    }
    attractNext = U.now + 6.0;
    gfx_image_cache_clear();
}
static void screen_attract (void)
{
    if (U.now >= attractNext) attract_next();
    gfx_fill (0, 0, W, H, C_BLACK);
    if (attractImg) gfx_image_fit (attractImg, 0, 0, W, H - 110, 255);
    gfx_vgrad (0, H - 140, W, 140, C_BLACK, RGB (20, 20, 24));
    gfx_text_fit (F_HUGE, 60, H - 124, W - 520, attractImg ? attractTitle : "MPC ARCADE", C_TEXT);
    gfx_text (F_BODY, 60, H - 56, attractImg ? attractSub : "Add screenshots to see your games here", C_DIM);
    if (((int) (U.now * 1.2)) % 2) gfx_text (F_BOLD, W - 440, H - 84, "TOUCH OR HIT A PAD", C_GOLD);
    U.anim = 1;
    ui_hit (9300, 0, 0, W, H);
    if (ui_clicked (9300) || U.key != NK_NONE) { U.key = NK_NONE; img_free (attractImg); attractImg = NULL; gfx_image_cache_clear(); openScreen (S_LIB); }
}

// ------------------------------------------------------------------ screen switching
static void openScreen (int s)
{
    if (screen == S_CONTROLS || screen == S_LEARN) capture_off();
    if (screen == S_CONTROLS && s != S_CONTROLS) editDefault = 0;
    if (screen == S_ROMS && s != S_ROMS) { dropStop(); supKb = 0; }
    if (s == S_CONTROLS) { if (screen != S_GAMEOPTS && screen != S_LIB && screen != S_SETTINGS) {} editor_load(); capture_on(); }
    if (s == S_LEARN) { learnStep = 0; learnLast[0] = 0; learnPendingId[0] = 0; capture_on(); }
    if (s == S_INFO && selected()) loadInfo (&L.db[selected()->db]);
    if (s == S_GAMEOPTS || s == S_SETTINGS) optFocus = 0;
    if (s == S_ROMS) { romSel = 0; U.scroll[2] = 0; }
    if (s == S_LIB)
    {
        libWant = 1;
        PadMap pm; char where[96]; padmap_for_game (&pm, NULL, where, sizeof where); padmap_write_active (&pm, 1);
    }
    U.scroll[1] = 0; contentH[1] = 0;
    screen = s; dirty = 1;
}

// ------------------------------------------------------------------ the list the MPC Arcade plugin shows
static int cmpPlugin (const void* pa, const void* pb)
{
    const Entry* a = *(Entry* const*) pa; const Entry* b = *(Entry* const*) pb;
    if (a->fav != b->fav) return b->fav - a->fav;
    return strcasecmp (titleOf (a), titleOf (b));
}
static void write_plugin_list (void)
{
    static Entry* v[MAXFILES]; int n = 0;
    for (int i = 0; i < L.nfiles; ++i)
    {
        Entry* e = &L.files[i];
        if (e->db < 0 || L.db[e->db].isbios) continue;
        if (e->vstatus != V_OK && e->vstatus != V_BEST && e->vstatus != V_UNKNOWN) continue;
        v[n++] = e;
    }
    qsort (v, (size_t) n, sizeof (Entry*), cmpPlugin);
    char p[600], t[620];
    lib_path (p, sizeof p, "%s/plugin_list.tsv", L.user); snprintf (t, sizeof t, "%s.tmp", p);
    FILE* f = fopen (t, "w");
    if (f == NULL) return;
    fprintf (f, "# MPC Arcade: games shown by the MPC Arcade plugin (written by the library)\n#last %s\n", set_get ("last_game"));
    for (int i = 0; i < n && i < 512; ++i)
    {
        const GameInfo* g = &L.db[v[i]->db];
        char mainT[100], var[100]; splitTitle (g->desc, mainT, sizeof mainT, var, sizeof var);
        fprintf (f, "%s\t%s%s\t%s \xc2\xb7 %s \xc2\xb7 %s\n", g->name, v[i]->fav ? "\xe2\x98\x85 " : "", mainT, g->manuf, g->year, g->genre);
    }
    fclose (f);
    rename (t, p);
}

// ------------------------------------------------------------------ the progress window for modal tasks
static void drawTaskModal (void)
{
    if (T.kind == TK_NONE || ! T.modal) return;
    gfx_blend (0, 0, W, H, C_BLACK, 160);
    const int w = 760, h = 260, x = (W - w) / 2, y = (H - h) / 2;
    gfx_round (x, y, w, h, 14, C_PANEL, 255);
    gfx_text (F_TITLE, x + 30, y + 22, T.label, C_TEXT);
    gfx_text_fit (F_BODY, x + 30, y + 84, w - 60, T.now, C_DIM);
    gfx_round (x + 30, y + 130, w - 60, 22, 11, C_PANEL3, 255);
    if (T.n) gfx_round (x + 30, y + 130, (w - 60) * T.done / T.n, 22, 11, C_ACCENT, 255);
    char b[64]; snprintf (b, sizeof b, "%d of %d", T.done, T.n);
    gfx_text (F_SMALL, x + 30, y + 160, b, C_DIM);
    // Cancel is a modal-level hit so it works while a task is shown
    const int bx = x + w - 200, by = y + h - 76;
    if (U.nhits < 600) { Hit* h0 = &U.hits[U.nhits++]; h0->id = 950000; h0->x = bx; h0->y = by; h0->w = 170; h0->h = 56; h0->scroll = -1; }
    gfx_round (bx, by, 170, 56, 8, ui_pressed (950000) ? C_PANEL2 : C_PANEL3, 255);
    gfx_text_c (F_BODY, bx, by, 170, 56, T.cancel ? "Stopping\xe2\x80\xa6" : "Cancel", C_TEXT);
    if (ui_clicked (950000) || ui_take_key (NK_BACK)) T.cancel = 1;
    U.anim = 1;
}

// ------------------------------------------------------------------ test script (off-device screenshots)
static FILE* script; static double scriptWait; static char shotDir[400];
static void scriptStep (SDL_Surface* surf)
{
    if (script == NULL || U.now < scriptWait) return;
    char line[256];
    while (fgets (line, sizeof line, script))
    {
        int a, b2, c, d;
        char s[128];
        if (sscanf (line, "tap %d %d", &a, &b2) == 2) { ui_pointer (0, a, b2); ui_pointer (2, a, b2); dirty = 1; scriptWait = U.now + 0.05; return; }
        if (sscanf (line, "drag %d %d %d %d", &a, &b2, &c, &d) == 4)
        {
            ui_pointer (0, a, b2);
            for (int k = 1; k <= 10; ++k) { U.now += 0.016; ui_pointer (1, a + (c - a) * k / 10, b2 + (d - b2) * k / 10); }
            ui_pointer (2, c, d); dirty = 1; scriptWait = U.now + 0.05; return;
        }
        if (sscanf (line, "key %127s", s) == 1)
        {
            const char* names[] = { "", "up", "down", "left", "right", "ok", "back", "fav", "menu", "pgup", "pgdn" };
            for (int k = 1; k < 11; ++k) if (! strcmp (s, names[k])) ui_key (k, NULL);
            dirty = 1; scriptWait = U.now + 0.05; return;
        }
        if (sscanf (line, "wait %d", &a) == 1) { scriptWait = U.now + a / 1000.0; return; }
        if (sscanf (line, "shot %127s", s) == 1)
        {
            char p[600]; snprintf (p, sizeof p, "%s/%s.bmp", shotDir, s);
            SDL_SaveBMP (surf, p);
            continue;
        }
        if (! strncmp (line, "quit", 4)) { exitCode = 99; return; }
        if (! strncmp (line, "capture ", 8))                               // simulate padbridge: "capture p <id>" / "capture t <id>"
        {
            char p[600]; FILE* f = fopen (lib_path (p, sizeof p, "%s/capture.out", L.run), "a");
            if (f) { fputs (line + 8, f); fclose (f); }
            continue;
        }
    }
    fclose (script); script = NULL;
}

// ------------------------------------------------------------------ main
static int mapKey (SDL_Keycode k)
{
    switch (k)
    {
        case SDLK_UP: return NK_UP;
        case SDLK_DOWN: return NK_DOWN;
        case SDLK_LEFT: return NK_LEFT;
        case SDLK_RIGHT: return NK_RIGHT;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_LCTRL: case SDLK_1: return NK_OK;
        case SDLK_ESCAPE: case SDLK_LALT: case SDLK_BACKSPACE: return NK_BACK;
        case SDLK_SPACE: return NK_FAV;
        case SDLK_TAB: return NK_MENU;
        case SDLK_PAGEUP: return NK_PGUP;
        case SDLK_PAGEDOWN: return NK_PGDN;
        default: return NK_NONE;
    }
}
int main (int argc, char** argv)
{
    const char* root = getenv ("MPCA_ROOT") ? getenv ("MPCA_ROOT") : "/media/az01-internal/MAME";
    const char* selectGame = NULL; const char* launchGame = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (! strcmp (argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (! strcmp (argv[i], "--select") && i + 1 < argc) selectGame = argv[++i];
        else if (! strcmp (argv[i], "--launch") && i + 1 < argc) launchGame = argv[++i];
        else if (! strcmp (argv[i], "--script") && i + 1 < argc) script = fopen (argv[++i], "r");
        else if (! strcmp (argv[i], "--shots") && i + 1 < argc) snprintf (shotDir, sizeof shotDir, "%s", argv[++i]);
    }
    signal (SIGPIPE, SIG_IGN);
    if (lib_init (root) != 0) { fprintf (stderr, "arcade-ui: can't read %s/system/gamedb.tsv\n", root); return 2; }
    user_record_plays();
    lib_scan();
    initOptLists();
    // straight into a game (the plugin's PLAY / RESUME): write the launch file without showing the library
    if (launchGame && launchGame[0])
    {
        Entry* e = lib_entry (launchGame);
        if (e && e->db >= 0 && (e->vstatus == V_OK || e->vstatus == V_BEST || e->vstatus == V_UNKNOWN))
        {
            PadMap pm; char where[96];
            padmap_for_game (&pm, e, where, sizeof where);
            padmap_write_active (&pm, 0);
            if (launch_write (e) == 0) { set_set ("last_game", L.db[e->db].name); settings_save(); return 10; }
        }
        selectGame = launchGame;                                       // can't start it: show it in the library instead
    }
    // a game that failed to start: tell the user why (session.sh leaves the end of MAME's log here)
    char errPath[600]; lib_path (errPath, sizeof errPath, "%s/lasterror", L.run);
    // display settings for our SDL framebuffer driver
    char rot[8]; snprintf (rot, sizeof rot, "%d", set_geti ("rotate_panel"));
    setenv ("SDL_FBROTATE", rot, 1);
    if (set_geti ("tearfree")) setenv ("SDL_FBDOUBLE", "1", 1);
    unsetenv ("SDL_FBSCALE"); unsetenv ("SDL_FBSCANLINES");
    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fprintf (stderr, "arcade-ui: SDL: %s\n", SDL_GetError()); return 2; }
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode (0, &dm) == 0) { W = dm.w; H = dm.h; }
    SDL_Window* win = SDL_CreateWindow ("MPC Arcade", 0, 0, W, H, SDL_WINDOW_FULLSCREEN);
    if (win == NULL) { fprintf (stderr, "arcade-ui: window: %s\n", SDL_GetError()); return 2; }
    SDL_ShowCursor (SDL_DISABLE);
    SDL_Surface* surf = SDL_GetWindowSurface (win);
    if (surf == NULL) { fprintf (stderr, "arcade-ui: surface: %s\n", SDL_GetError()); return 2; }
    W = surf->w; H = surf->h; U.W = W; U.H = H;
    Canvas cv = { (uint32_t*) surf->pixels, surf->w, surf->h, surf->pitch / 4 };
    gfx_bind (&cv);
    snd_init (set_geti ("uisounds"), set_geti ("uivolume"));
    U.now = nowSec(); U.lastInput = U.now;
    openScreen (S_LIB);
    build_list();
    const char* want = selectGame ? selectGame : set_get ("last_game");
    for (int i = 0; i < nlist; ++i) if (want && list[i]->db >= 0 && ! strcmp (L.db[list[i]->db].name, want)) sel = i;
    // check new sets in the background
    task_begin (TK_VERIFY, "Checking new games", 0);
    for (int i = 0; i < L.nfiles; ++i) if (lib_needs_verify (i)) T.items[T.n++] = i;
    if (T.n == 0) T.kind = TK_NONE;
    {
        FILE* ef = fopen (errPath, "r");
        if (ef)
        {
            static char et[1100]; const size_t n = fread (et, 1, sizeof et - 1, ef); et[n] = 0; fclose (ef); unlink (errPath);
            ui_message ("The game stopped with an error", et);
        }
    }
    if (set_geti ("first_run") && layout_count() < 16 && ! script)
        ui_confirm ("Welcome to MPC Arcade", "Let's teach it your MPC's pads (hit each pad once, about 30 seconds). You can also do this later in Settings.", "Set up pads", onPick, (void*) (intptr_t) ROW_FIRSTRUN);

    double last = nowSec();
    while (exitCode < 0)
    {
        SDL_Event ev;
        const int wait = (U.anim || dirty || T.kind != TK_NONE || script) ? 16 : 250;
        if (SDL_WaitEventTimeout (&ev, wait))
        {
            do
            {
                U.now = nowSec();
                switch (ev.type)
                {
                    case SDL_QUIT: exitCode = 0; break;
                    case SDL_KEYDOWN:
                    {
                        const int nk = mapKey (ev.key.keysym.sym);
                        if (nk != NK_NONE) { ui_key (nk, NULL); dirty = 1; }
                        break;
                    }
                    case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
                    {
                        float fx = ev.tfinger.x, fy = ev.tfinger.y;
                        const int r = set_geti ("rotate_panel");
                        if (r == 90) { const float t = fx; fx = 1.f - fy; fy = t; }
                        else if (r == 180) { fx = 1.f - fx; fy = 1.f - fy; }
                        else if (r == 270) { const float t = fx; fx = fy; fy = 1.f - t; }
                        if (ev.tfinger.fingerId != 0 && ev.type != SDL_FINGERUP && U.down && ev.type == SDL_FINGERDOWN) break;   // first finger only
                        ui_pointer (ev.type == SDL_FINGERDOWN ? 0 : (ev.type == SDL_FINGERMOTION ? 1 : 2), (int) (fx * W), (int) (fy * H));
                        dirty = 1;
                        break;
                    }
                    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
                        if (ev.button.which == SDL_TOUCH_MOUSEID || ev.button.button != SDL_BUTTON_LEFT) break;
                        ui_pointer (ev.type == SDL_MOUSEBUTTONDOWN ? 0 : 2, ev.button.x, ev.button.y); dirty = 1; break;
                    case SDL_MOUSEMOTION:
                        if (ev.motion.which == SDL_TOUCH_MOUSEID) break;
                        if (U.down) { ui_pointer (1, ev.motion.x, ev.motion.y); dirty = 1; }
                        break;
                    case SDL_MOUSEWHEEL:
                        ui_key (ev.wheel.y > 0 ? NK_UP : NK_DOWN, NULL); dirty = 1; break;
                    default: break;
                }
            } while (exitCode < 0 && SDL_PollEvent (&ev));
        }
        U.now = nowSec();
        ui_tick (U.now - last);
        last = U.now;
        scriptStep (surf);
        task_step();
        // attract mode
        if (screen == S_LIB && set_geti ("attract") && ! ui_modal_active() && T.kind == TK_NONE && ! kbOpen &&
            U.now - U.lastInput > set_geti ("attract_delay") && L.nfiles > 0) { attractNext = 0; openScreen (S_ATTRACT); }
        // capture screens poll the capture file
        if (screen == S_CONTROLS || screen == S_LEARN) U.anim = 1;
        if (! dirty && ! U.anim) continue;
        dirty = 0;
        const int hadInput = U.key != NK_NONE || U.clicked != 0;
        ui_begin_frame();
        if (T.kind != TK_NONE && T.modal) { if (U.key != NK_BACK) U.key = NK_NONE; if (U.clicked != 950000) U.clicked = 0; }   // input goes to the progress window
        switch (screen)
        {
            case S_LIB: screen_library(); break;
            case S_GAMEOPTS: screen_gameopts(); break;
            case S_CONTROLS: screen_controls(); break;
            case S_ROMS: screen_roms(); break;
            case S_SETTINGS: screen_settings(); break;
            case S_LEARN: screen_learn(); break;
            case S_ATTRACT: screen_attract(); break;
            case S_INFO: screen_info(); break;
        }
        drawTaskModal();
        ui_modal_draw();
        ui_draw_toast();
        ui_end_frame();
        SDL_UpdateWindowSurface (win);
        if (hadInput || libWant) dirty = 1;
        if (! script && screen != S_ATTRACT && (U.anim || U.down)) dirty = 1;
    }
    // leaving: stop helpers, keep everything saved
    capture_off(); dropStop();
    while (job.running) { if (job_finished()) break; SDL_Delay (50); }
    lib_save_verify(); user_save(); settings_save();
    write_plugin_list();
    snd_close();
    SDL_Quit();
    return exitCode == 99 ? 0 : exitCode;
}
