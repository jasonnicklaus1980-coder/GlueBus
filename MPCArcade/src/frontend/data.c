// MPC Arcade library app: game database, installed ROMs, user data, settings and MAME launch (see data.h).
#include "data.h"
#include "../common/actions.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

Lib L;

// ------------------------------------------------------------------ helpers
const char* lib_path (char* buf, int n, const char* fmt, ...)
{
    va_list ap; va_start (ap, fmt); vsnprintf (buf, (size_t) n, fmt, ap); va_end (ap);
    return buf;
}
long file_mtime (const char* p) { struct stat st; return stat (p, &st) == 0 ? (long) st.st_mtime : 0; }
int file_exists (const char* p) { struct stat st; return stat (p, &st) == 0; }
int mkdir_p (const char* path)
{
    char b[512]; snprintf (b, sizeof b, "%s", path);
    for (char* p = b + 1; *p; ++p) if (*p == '/') { *p = 0; mkdir (b, 0755); *p = '/'; }
    return mkdir (b, 0755) == 0 || errno == EEXIST ? 0 : -1;
}
const char* shq (char* buf, int n, const char* s)
{
    int k = 0;
    if (k < n - 1) buf[k++] = '\'';
    for (; *s && k < n - 5; ++s)
    {
        if (*s == '\'') { memcpy (buf + k, "'\\''", 4); k += 4; }
        else buf[k++] = *s;
    }
    if (k < n - 1) buf[k++] = '\'';
    buf[k] = 0;
    return buf;
}
int run_capture (const char* cmd, char* out, int n)
{
    FILE* p = popen (cmd, "r");
    if (p == NULL) { if (n) out[0] = 0; return -1; }
    int k = 0, c;
    while ((c = fgetc (p)) != EOF) if (k < n - 1) out[k++] = (char) c;
    if (n) out[k] = 0;
    const int st = pclose (p);
    return WIFEXITED (st) ? WEXITSTATUS (st) : -1;
}
static void chomp (char* s) { size_t n = strlen (s); while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0; }
static void copyField (char* dst, size_t n, const char* src) { snprintf (dst, n, "%s", src ? src : ""); }

// ------------------------------------------------------------------ settings
typedef struct { char key[32]; char val[96]; } KV;
static KV gset[96]; static int ngset;
static const char* const kDefaults[][2] = {
    { "profile", "balanced" }, { "scale", "fit" }, { "scanlines", "0" }, { "vertical", "fit" }, { "smooth", "0" },
    { "lowres", "0" }, { "tearfree", "0" }, { "volume", "0" }, { "uisounds", "1" }, { "uivolume", "50" },
    { "audiodev", "default" }, { "resume", "1" }, { "autoprofile", "1" }, { "attract", "1" }, { "attract_delay", "180" },
    { "show_clones", "1" }, { "sort", "title" }, { "hide_bad", "0" }, { "autosnap", "1" }, { "rotate_panel", "0" },
    { "first_run", "1" }, { "template", "auto" },
};
static KV* findKV (KV* a, int n, const char* k) { for (int i = 0; i < n; ++i) if (strcmp (a[i].key, k) == 0) return &a[i]; return NULL; }
static int loadKV (const char* file, KV* a, int max)
{
    FILE* f = fopen (file, "r");
    if (f == NULL) return 0;
    char line[256]; int n = 0;
    while (fgets (line, sizeof line, f) && n < max)
    {
        chomp (line);
        char* eq = strchr (line, '=');
        if (line[0] == '#' || eq == NULL) continue;
        *eq = 0;
        copyField (a[n].key, sizeof a[n].key, line); copyField (a[n].val, sizeof a[n].val, eq + 1); ++n;
    }
    fclose (f);
    return n;
}
static void saveKV (const char* file, const KV* a, int n)
{
    char tmp[600]; snprintf (tmp, sizeof tmp, "%s.tmp", file);
    FILE* f = fopen (tmp, "w");
    if (f == NULL) return;
    for (int i = 0; i < n; ++i) if (a[i].val[0]) fprintf (f, "%s=%s\n", a[i].key, a[i].val);
    fclose (f);
    rename (tmp, file);
}
const char* set_get (const char* key)
{
    KV* kv = findKV (gset, ngset, key);
    if (kv) return kv->val;
    for (size_t i = 0; i < sizeof kDefaults / sizeof kDefaults[0]; ++i) if (strcmp (kDefaults[i][0], key) == 0) return kDefaults[i][1];
    return "";
}
int set_geti (const char* key) { return atoi (set_get (key)); }
void set_set (const char* key, const char* val)
{
    KV* kv = findKV (gset, ngset, key);
    if (kv == NULL && ngset < 96) { kv = &gset[ngset++]; copyField (kv->key, sizeof kv->key, key); }
    if (kv) copyField (kv->val, sizeof kv->val, val);
}
void set_seti (const char* key, int v) { char b[24]; snprintf (b, sizeof b, "%d", v); set_set (key, b); }
void settings_save (void) { char p[400]; saveKV (lib_path (p, sizeof p, "%s/settings.conf", L.user), gset, ngset); }

static char gameKVName[24]; static KV gameKV[32]; static int ngameKV;
static void loadGameKV (const char* game)
{
    if (strcmp (gameKVName, game) == 0) return;
    char p[400];
    copyField (gameKVName, sizeof gameKVName, game);
    ngameKV = loadKV (lib_path (p, sizeof p, "%s/games/%s.conf", L.user, game), gameKV, 32);
}
const char* game_get (const char* game, const char* key)
{
    loadGameKV (game);
    KV* kv = findKV (gameKV, ngameKV, key);
    return kv ? kv->val : "";
}
void game_set (const char* game, const char* key, const char* val)
{
    loadGameKV (game);
    KV* kv = findKV (gameKV, ngameKV, key);
    if (kv == NULL && ngameKV < 32) { kv = &gameKV[ngameKV++]; copyField (kv->key, sizeof kv->key, key); }
    if (kv) copyField (kv->val, sizeof kv->val, val);
    char p[400], d[400];
    mkdir_p (lib_path (d, sizeof d, "%s/games", L.user));
    saveKV (lib_path (p, sizeof p, "%s/games/%s.conf", L.user, game), gameKV, ngameKV);
}
const char* eff_get (const char* game, const char* key)
{
    const char* v = game ? game_get (game, key) : "";
    return v[0] && strcmp (v, "default") != 0 ? v : set_get (key);
}

// ------------------------------------------------------------------ database
static int cmpName (const void* a, const void* b) { return strcmp (((const GameInfo*) a)->name, ((const GameInfo*) b)->name); }
int db_find (const char* name)
{
    int lo = 0, hi = L.ndb - 1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2, c = strcmp (L.db[mid].name, name);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}
static int loadDb (const char* file)
{
    FILE* f = fopen (file, "r");
    if (f == NULL) return -1;
    L.db = (GameInfo*) calloc (MAXGAMES, sizeof (GameInfo));
    char line[1024];
    while (fgets (line, sizeof line, f) && L.ndb < MAXGAMES)
    {
        chomp (line);
        char* col[17]; int n = 0; char* p = line;
        while (n < 17) { col[n++] = p; char* t = strchr (p, '\t'); if (t == NULL) break; *t = 0; p = t + 1; }
        if (n < 17) continue;
        GameInfo* g = &L.db[L.ndb++];
        copyField (g->name, sizeof g->name, col[0]); copyField (g->parent, sizeof g->parent, col[1]);
        copyField (g->romof, sizeof g->romof, col[2]); copyField (g->desc, sizeof g->desc, col[3]);
        copyField (g->year, sizeof g->year, col[4]); copyField (g->manuf, sizeof g->manuf, col[5]);
        copyField (g->source, sizeof g->source, col[6]); copyField (g->genre, sizeof g->genre, col[7]);
        g->rotate = (short) atoi (col[8]); g->sw = (short) atoi (col[9]); g->sh = (short) atoi (col[10]);
        g->status = col[11][0]; g->savestate = (char) atoi (col[12]); g->players = (char) atoi (col[13]);
        g->buttons = (char) atoi (col[14]); copyField (g->controls, sizeof g->controls, col[15]); g->isbios = (char) atoi (col[16]);
    }
    fclose (f);
    qsort (L.db, (size_t) L.ndb, sizeof (GameInfo), cmpName);
    return 0;
}
// optional catver.ini in MAME/dats/: "[Category]" section, "name=Genre / Subgenre"
static void loadCatver (void)
{
    char p[400];
    FILE* f = fopen (lib_path (p, sizeof p, "%s/dats/catver.ini", L.root), "r");
    if (f == NULL) return;
    char line[256]; int inCat = 0;
    while (fgets (line, sizeof line, f))
    {
        chomp (line);
        if (line[0] == '[') { inCat = strncmp (line, "[Category]", 10) == 0; continue; }
        char* eq = strchr (line, '=');
        if (! inCat || eq == NULL) continue;
        *eq = 0;
        const int i = db_find (line);
        if (i < 0) continue;
        char* g = eq + 1; char* slash = strstr (g, " / ");
        if (slash) *slash = 0;
        if (strstr (g, "Mature") == NULL) copyField (L.db[i].genre, sizeof L.db[i].genre, g);
    }
    fclose (f);
}

int lib_init (const char* root)
{
    memset (&L, 0, sizeof L);
    copyField (L.root, sizeof L.root, root);
    lib_path (L.sys, sizeof L.sys, "%s/system", root);
    lib_path (L.roms, sizeof L.roms, "%s/roms", root);
    lib_path (L.bios, sizeof L.bios, "%s/bios", root);
    lib_path (L.run, sizeof L.run, "%s/system/run", root);
    lib_path (L.user, sizeof L.user, "%s/config/library", root);
    lib_path (L.prof, sizeof L.prof, "%s/config/pads", root);
    const char* dirs[] = { "roms", "bios", "artwork", "screenshots", "config", "config/library", "config/library/games",
                           "config/pads", "config/pads/games", "config/mame", "saves", "saves/states", "saves/nvram",
                           "saves/hiscore", "dats", "logs", "system/run", "samples", "import" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; ++i) { char p[400]; mkdir_p (lib_path (p, sizeof p, "%s/%s", root, dirs[i])); }
    char p[400];
    if (loadDb (lib_path (p, sizeof p, "%s/gamedb.tsv", L.sys)) != 0) return -1;
    loadCatver();
    ngset = loadKV (lib_path (p, sizeof p, "%s/settings.conf", L.user), gset, 96);
    return 0;
}

// ------------------------------------------------------------------ scan + verify cache
static int isRomFile (const char* n, char* setOut, int sn)
{
    const char* dot = strrchr (n, '.');
    if (n[0] == '.') return 0;
    if (dot && (strcasecmp (dot, ".zip") == 0 || strcasecmp (dot, ".7z") == 0))
    {
        const int len = (int) (dot - n);
        snprintf (setOut, (size_t) sn, "%.*s", len, n);
        for (char* c = setOut; *c; ++c) *c = (char) tolower ((unsigned char) *c);
        return 1;
    }
    return 0;
}
typedef struct { char file[128]; char dir; long long size; long mtime; int v; char detail[120]; } VCache;
static VCache vc[MAXFILES]; static int nvc;
static void loadVerifyCache (void)
{
    char p[400]; nvc = 0;
    FILE* f = fopen (lib_path (p, sizeof p, "%s/verify.tsv", L.user), "r");
    if (f == NULL) return;
    char line[512];
    while (fgets (line, sizeof line, f) && nvc < MAXFILES)
    {
        chomp (line);
        VCache* c = &vc[nvc];
        char* col[6]; int n = 0; char* q = line;
        while (n < 6) { col[n++] = q; char* t = strchr (q, '\t'); if (t == NULL) break; *t = 0; q = t + 1; }
        if (n < 6) continue;
        copyField (c->file, sizeof c->file, col[0]); c->dir = col[1][0]; c->size = atoll (col[2]); c->mtime = atol (col[3]);
        c->v = atoi (col[4]); copyField (c->detail, sizeof c->detail, col[5]); ++nvc;
    }
    fclose (f);
}
void lib_save_verify (void)
{
    char p[400], t[420];
    lib_path (p, sizeof p, "%s/verify.tsv", L.user); snprintf (t, sizeof t, "%s.tmp", p);
    FILE* f = fopen (t, "w");
    if (f == NULL) return;
    for (int i = 0; i < L.nfiles; ++i)
    {
        const Entry* e = &L.files[i];
        fprintf (f, "%s\t%c\t%lld\t%ld\t%d\t%s\n", e->file, e->dir, e->size, e->mtime, e->vstatus, e->vdetail);
    }
    fclose (f);
    rename (t, p);
}
static void scanDir (const char* dir, char tag)
{
    DIR* d = opendir (dir);
    if (d == NULL) return;
    struct dirent* de;
    while ((de = readdir (d)) != NULL && L.nfiles < MAXFILES)
    {
        char set[128], path[600];
        if (! isRomFile (de->d_name, set, sizeof set)) continue;
        struct stat st;
        if (stat (lib_path (path, sizeof path, "%s/%s", dir, de->d_name), &st) != 0 || ! S_ISREG (st.st_mode)) continue;
        if (lib_entry (set) != NULL) continue;                       // same set in roms/ and bios/: first wins
        Entry* e = &L.files[L.nfiles++];
        memset (e, 0, sizeof *e);
        copyField (e->file, sizeof e->file, de->d_name);
        e->dir = tag; e->size = (long long) st.st_size; e->mtime = (long) st.st_mtime;
        e->db = db_find (set);
        e->vstatus = e->db < 0 ? V_UNSUPPORTED : (L.db[e->db].isbios ? V_ISBIOS : V_UNKNOWN);
        for (int k = 0; k < nvc; ++k)
            if (strcmp (vc[k].file, e->file) == 0 && vc[k].dir == tag && vc[k].size == e->size && vc[k].mtime == e->mtime && e->db >= 0 && ! L.db[e->db].isbios)
            { e->vstatus = vc[k].v; copyField (e->vdetail, sizeof e->vdetail, vc[k].detail); }
    }
    closedir (d);
}
int lib_scan (void)
{
    loadVerifyCache();
    L.nfiles = 0;
    scanDir (L.roms, 'r');
    scanDir (L.bios, 'b');
    user_load();
    int games = 0;
    for (int i = 0; i < L.nfiles; ++i) if (L.files[i].db >= 0 && ! L.db[L.files[i].db].isbios) ++games;
    return games;
}
Entry* lib_entry (const char* name)
{
    for (int i = 0; i < L.nfiles; ++i)
    {
        char set[128];
        isRomFile (L.files[i].file, set, sizeof set);
        if (strcmp (set, name) == 0) return &L.files[i];
    }
    return NULL;
}
int lib_has_set (const char* name) { return lib_entry (name) != NULL; }
int lib_needs_verify (int i)
{
    const Entry* e = &L.files[i];
    return e->db >= 0 && ! L.db[e->db].isbios && e->vstatus == V_UNKNOWN;
}
const char* vstatus_text (int v)
{
    switch (v)
    {
        case V_OK: return "Ready";
        case V_BEST: return "Ready (best available dump)";
        case V_BAD: return "ROM set incomplete or wrong version";
        case V_NEEDBIOS: return "Missing BIOS";
        case V_NEEDPARENT: return "Missing parent set";
        case V_UNSUPPORTED: return "Not a set this MAME knows";
        case V_ISBIOS: return "BIOS / system files";
        default: return "Not checked yet";
    }
}
void lib_verify_cmd (int i, char* cmd, int n)
{
    const GameInfo* g = &L.db[L.files[i].db];
    char q1[400], q2[700], rp[700];
    lib_path (rp, sizeof rp, "%s;%s", L.roms, L.bios);
    snprintf (cmd, (size_t) n, "%s", shq (q1, sizeof q1, lib_path (q2, sizeof q2, "%s/mame", L.sys)));
    snprintf (cmd + strlen (cmd), (size_t) n - strlen (cmd), " -rompath %s -verifyroms %s 2>&1", shq (q2, sizeof q2, rp), g->name);
}
void lib_verify_parse (int i, const char* out)
{
    Entry* e = &L.files[i];
    const GameInfo* g = &L.db[e->db];
    e->vdetail[0] = 0;
    if (strstr (out, " is good")) e->vstatus = V_OK;
    else if (strstr (out, " is best available")) e->vstatus = V_BEST;
    else
    {
        // which dependency is missing?
        const int bi = g->romof[0] ? db_find (g->romof) : -1;
        if (bi >= 0 && L.db[bi].isbios && ! lib_has_set (g->romof))
        { e->vstatus = V_NEEDBIOS; snprintf (e->vdetail, sizeof e->vdetail, "Add %s.zip to MAME/bios", g->romof); }
        else if (g->parent[0] && ! lib_has_set (g->parent))
        { e->vstatus = V_NEEDPARENT; snprintf (e->vdetail, sizeof e->vdetail, "Add the parent set %s.zip", g->parent); }
        else
        {
            e->vstatus = V_BAD;
            const char* nf = strstr (out, "NOT FOUND");
            if (nf == NULL) nf = strstr (out, "INCORRECT");
            if (nf)
            {
                const char* ls = nf; while (ls > out && ls[-1] != '\n') --ls;
                const char* le = strchr (nf, '\n'); const int n = le ? (int) (le - ls) : (int) strlen (ls);
                snprintf (e->vdetail, sizeof e->vdetail, "%.*s", n, ls);
                for (char* c = e->vdetail; *c; ++c) if (*c == '\t') *c = ' ';
            }
            else snprintf (e->vdetail, sizeof e->vdetail, "Needs a ROM set matching MAME 0.242");
        }
    }
}
int lib_verify (int i)
{
    Entry* e = &L.files[i];
    if (e->db < 0 || L.db[e->db].isbios) return e->vstatus;
    char cmd[1600], out[8192];
    lib_verify_cmd (i, cmd, sizeof cmd);
    run_capture (cmd, out, sizeof out);
    lib_verify_parse (i, out);
    return e->vstatus;
}

// ------------------------------------------------------------------ user data (favorites, history, benchmarks)
void user_load (void)
{
    char p[400], line[256];
    FILE* f = fopen (lib_path (p, sizeof p, "%s/favorites.txt", L.user), "r");
    if (f) { while (fgets (line, sizeof line, f)) { chomp (line); Entry* e = lib_entry (line); if (e) e->fav = 1; } fclose (f); }
    f = fopen (lib_path (p, sizeof p, "%s/history.tsv", L.user), "r");
    if (f)
    {
        while (fgets (line, sizeof line, f))
        {
            char name[64]; int plays; long secs, last;
            if (sscanf (line, "%63s %d %ld %ld", name, &plays, &secs, &last) != 4) continue;
            Entry* e = lib_entry (name);
            if (e) { e->plays = plays; e->playSecs = secs; e->last = last; }
        }
        fclose (f);
    }
    f = fopen (lib_path (p, sizeof p, "%s/bench.tsv", L.user), "r");
    if (f)
    {
        while (fgets (line, sizeof line, f))
        {
            char name[64]; int sp;
            if (sscanf (line, "%63s %d", name, &sp) != 2) continue;
            Entry* e = lib_entry (name);
            if (e) e->bench = sp;
        }
        fclose (f);
    }
}
static const char* setName (const Entry* e) { return e->db >= 0 ? L.db[e->db].name : e->file; }
void user_save (void)
{
    char p[400], t[420];
    lib_path (p, sizeof p, "%s/favorites.txt", L.user); snprintf (t, sizeof t, "%s.tmp", p);
    FILE* f = fopen (t, "w");
    if (f) { for (int i = 0; i < L.nfiles; ++i) if (L.files[i].fav) fprintf (f, "%s\n", setName (&L.files[i])); fclose (f); rename (t, p); }
    lib_path (p, sizeof p, "%s/history.tsv", L.user); snprintf (t, sizeof t, "%s.tmp", p);
    f = fopen (t, "w");
    if (f)
    {
        for (int i = 0; i < L.nfiles; ++i)
            if (L.files[i].plays) fprintf (f, "%s %d %ld %ld\n", setName (&L.files[i]), L.files[i].plays, L.files[i].playSecs, L.files[i].last);
        fclose (f); rename (t, p);
    }
}
void user_save_bench (void)
{
    char p[400], t[420];
    lib_path (p, sizeof p, "%s/bench.tsv", L.user); snprintf (t, sizeof t, "%s.tmp", p);
    FILE* f = fopen (t, "w");
    if (f == NULL) return;
    for (int i = 0; i < L.nfiles; ++i) if (L.files[i].bench) fprintf (f, "%s %d\n", setName (&L.files[i]), L.files[i].bench);
    fclose (f); rename (t, p);
}
void user_toggle_fav (Entry* e) { e->fav = ! e->fav; user_save(); }
void user_record_plays (void)
{
    char p[400], line[256];
    lib_path (p, sizeof p, "%s/plays.log", L.run);
    FILE* f = fopen (p, "r");
    if (f == NULL) return;
    while (fgets (line, sizeof line, f))
    {
        char name[64]; long t0, t1; int code;
        if (sscanf (line, "%63s %ld %ld %d", name, &t0, &t1, &code) < 3) continue;
        Entry* e = lib_entry (name);
        if (e == NULL) continue;
        e->plays++; e->last = t1; if (t1 > t0) e->playSecs += t1 - t0;
    }
    fclose (f);
    user_save();
    unlink (p);
}

// ------------------------------------------------------------------ pad maps
void padmap_defaults (PadMap* p)
{
    memset (p, 0, sizeof *p);
    p->threshold = 1; p->exitHold = 1.5f; p->turbo = 12; p->lights = 0; p->touchExit = 1;
}
const char* padmap_get (const PadMap* p, const char* src)
{
    for (int i = 0; i < p->n; ++i) if (strcmp (p->m[i].src, src) == 0) return p->m[i].act;
    return "NONE";
}
void padmap_set (PadMap* p, const char* src, const char* act)
{
    for (int i = 0; i < p->n; ++i)
        if (strcmp (p->m[i].src, src) == 0)
        {
            if (strcmp (act, "NONE") == 0) { p->m[i] = p->m[--p->n]; return; }
            copyField (p->m[i].act, sizeof p->m[i].act, act); return;
        }
    if (strcmp (act, "NONE") == 0 || p->n >= 64) return;
    copyField (p->m[p->n].src, sizeof p->m[p->n].src, src); copyField (p->m[p->n].act, sizeof p->m[p->n].act, act); ++p->n;
}
void padmap_template (PadMap* p, const char* id)
{
    const int keep[5] = { p->threshold, (int) (p->exitHold * 1000), p->turbo, p->lights, p->touchExit };
    // keep learned hardware-button assignments (HW:...) when switching templates
    MapLine hw[64]; int nhw = 0;
    for (int i = 0; i < p->n; ++i) if (strncmp (p->m[i].src, "HW:", 3) == 0) hw[nhw++] = p->m[i];
    padmap_defaults (p);
    p->threshold = keep[0]; p->exitHold = keep[1] / 1000.f; p->turbo = keep[2]; p->lights = keep[3]; p->touchExit = keep[4];
    for (int t = 0; t < NUM_TEMPLATES; ++t)
    {
        if (strcmp (kTemplates[t].id, id) != 0) continue;
        for (int k = 0; k < 20 && kTemplates[t].map[k][0]; ++k) padmap_set (p, kTemplates[t].map[k][0], kTemplates[t].map[k][1]);
    }
    for (int i = 0; i < nhw; ++i) padmap_set (p, hw[i].src, hw[i].act);
}
int padmap_load (PadMap* p, const char* file)
{
    FILE* f = fopen (file, "r");
    if (f == NULL) return -1;
    padmap_defaults (p);
    char line[256];
    while (fgets (line, sizeof line, f))
    {
        chomp (line);
        if (line[0] == '#' || line[0] == 0) continue;
        if (line[0] == '@')
        {
            char k[32]; float v;
            if (sscanf (line + 1, "%31s %f", k, &v) != 2) continue;
            if (! strcmp (k, "threshold")) p->threshold = (int) v;
            else if (! strcmp (k, "exit_hold")) p->exitHold = v;
            else if (! strcmp (k, "turbo_hz")) p->turbo = (int) v;
            else if (! strcmp (k, "lights")) p->lights = (int) v;
            else if (! strcmp (k, "touch_exit")) p->touchExit = (int) v;
            continue;
        }
        char* sp = strrchr (line, ' ');                           // "SOURCE ACTION", source may contain spaces (HW:Tap Tempo)
        if (sp == NULL) continue;
        *sp = 0;
        if (actionIndex (sp + 1) > 0) padmap_set (p, line, sp + 1);
    }
    fclose (f);
    return 0;
}
int padmap_save (const PadMap* p, const char* file)
{
    char t[600]; snprintf (t, sizeof t, "%s.tmp", file);
    FILE* f = fopen (t, "w");
    if (f == NULL) return -1;
    fprintf (f, "# MPC Arcade pad map: SOURCE ACTION (sources: PAD1-16, QLINK1-4, HW:<button>)\n");
    fprintf (f, "@threshold %d\n@exit_hold %.2f\n@turbo_hz %d\n@lights %d\n@touch_exit %d\n", p->threshold, (double) p->exitHold, p->turbo, p->lights, p->touchExit);
    for (int i = 0; i < p->n; ++i) fprintf (f, "%s %s\n", p->m[i].src, p->m[i].act);
    fclose (f);
    return rename (t, file);
}
const char* auto_template (const GameInfo* g)
{
    if (strstr (g->controls, "doublejoy")) return "twinstick";
    if (strstr (g->controls, "paddle") || strstr (g->controls, "dial") || strstr (g->controls, "pedal") || ! strcmp (g->genre, "Racing")) return "racing";
    if (! strcmp (g->genre, "Fighting")) return g->buttons >= 5 ? "fighting" : "beatemup";
    if (! strcmp (g->genre, "Beat 'em up")) return "beatemup";
    if (! strcmp (g->genre, "Platform") || ! strcmp (g->genre, "Run & Gun")) return "platform";
    if (! strcmp (g->genre, "Shoot 'em up")) return "shmup";
    return "classic";
}
void padmap_for_game (PadMap* p, const Entry* e, char* where, int n)
{
    char f[600];
    const GameInfo* g = e && e->db >= 0 ? &L.db[e->db] : NULL;
    if (g && padmap_load (p, lib_path (f, sizeof f, "%s/games/%s.conf", L.prof, g->name)) == 0) { snprintf (where, (size_t) n, "Saved for %s", g->name); return; }
    if (g && g->parent[0] && padmap_load (p, lib_path (f, sizeof f, "%s/games/%s.conf", L.prof, g->parent)) == 0) { snprintf (where, (size_t) n, "Saved for %s (parent)", g->parent); return; }
    const int haveDefault = padmap_load (p, lib_path (f, sizeof f, "%s/default.conf", L.prof)) == 0;
    const char* t = g ? game_get (g->name, "template") : "";
    if (g && (! t[0] || ! strcmp (t, "auto"))) t = strcmp (set_get ("template"), "auto") ? set_get ("template") : auto_template (g);
    if (g && t[0] && strcmp (t, "default"))
    {
        if (! haveDefault) padmap_defaults (p);
        padmap_template (p, t);
        for (int k = 0; k < NUM_TEMPLATES; ++k) if (! strcmp (kTemplates[k].id, t)) snprintf (where, (size_t) n, "%s layout (automatic)", kTemplates[k].name);
        return;
    }
    if (! haveDefault) { padmap_defaults (p); padmap_template (p, "classic"); snprintf (where, (size_t) n, "Classic layout"); return; }
    snprintf (where, (size_t) n, "Your default layout");
}
int padmap_write_active (const PadMap* p, int uiMode)
{
    char f[600];
    PadMap q = *p;
    if (uiMode)
    {
        // library: knobs scroll, the joystick pads navigate; EXIT taps act as "back" after the hold time
        for (int i = 0; i < q.n; ++i) if (! strcmp (q.m[i].act, "KNOB_X") || ! strcmp (q.m[i].act, "KNOB_Y")) copyField (q.m[i].act, sizeof q.m[i].act, "SCROLL");
        if (! strcmp (padmap_get (&q, "HW:Data wheel"), "NONE")) padmap_set (&q, "HW:Data wheel", "SCROLL");
        if (! strcmp (padmap_get (&q, "HW:Wheel push"), "NONE")) padmap_set (&q, "HW:Wheel push", "SELECT");
    }
    lib_path (f, sizeof f, "%s/active.conf", L.run);
    if (padmap_save (&q, f) != 0) return -1;
    if (uiMode) { FILE* a = fopen (f, "a"); if (a) { fprintf (a, "@mode ui\n"); fclose (a); } }
    return 0;
}
static char layoutSrc[80][48], layoutId[80][200]; static int nlayout = -1;
static void layoutLoad (void)
{
    char p[600], line[300];
    nlayout = 0;
    FILE* f = fopen (lib_path (p, sizeof p, "%s/layout.conf", L.prof), "r");
    if (f == NULL) return;
    while (fgets (line, sizeof line, f) && nlayout < 80)
    {
        chomp (line);
        if (line[0] == '#') continue;
        char* tab = strchr (line, '\t');                           // "SOURCE<TAB>raw-id"
        if (tab == NULL) continue;
        *tab = 0;
        copyField (layoutSrc[nlayout], 48, line); copyField (layoutId[nlayout], 200, tab + 1); ++nlayout;
    }
    fclose (f);
}
static void layoutSave (void)
{
    char p[600], t[620];
    lib_path (p, sizeof p, "%s/layout.conf", L.prof); snprintf (t, sizeof t, "%s.tmp", p);
    FILE* f = fopen (t, "w");
    if (f == NULL) return;
    fprintf (f, "# MPC Arcade: which hardware control is which (learned on this MPC). SOURCE<TAB>raw id\n");
    for (int i = 0; i < nlayout; ++i) fprintf (f, "%s\t%s\n", layoutSrc[i], layoutId[i]);
    fclose (f); rename (t, p);
}
int layout_count (void) { layoutLoad(); return nlayout; }
int layout_has (const char* src) { if (nlayout < 0) layoutLoad(); for (int i = 0; i < nlayout; ++i) if (! strcmp (layoutSrc[i], src)) return 1; return 0; }
int layout_set (const char* src, const char* id)
{
    layoutLoad();
    for (int i = 0; i < nlayout; ++i) if (! strcmp (layoutId[i], id) && strcmp (layoutSrc[i], src)) { layoutSrc[i][0] = 0; }   // one id, one name
    int k = -1;
    for (int i = 0; i < nlayout; ++i) if (! strcmp (layoutSrc[i], src)) k = i;
    if (k < 0 && nlayout < 80) k = nlayout++;
    if (k < 0) return -1;
    copyField (layoutSrc[k], 48, src); copyField (layoutId[k], 200, id);
    int w = 0;
    for (int i = 0; i < nlayout; ++i) if (layoutSrc[i][0]) { if (w != i) { memcpy (layoutSrc[w], layoutSrc[i], 48); memcpy (layoutId[w], layoutId[i], 200); } ++w; }
    nlayout = w;
    layoutSave();
    return 0;
}
int layout_remove (const char* src)
{
    layoutLoad();
    int w = 0;
    for (int i = 0; i < nlayout; ++i) if (strcmp (layoutSrc[i], src)) { if (w != i) { memcpy (layoutSrc[w], layoutSrc[i], 48); memcpy (layoutId[w], layoutId[i], 200); } ++w; }
    nlayout = w; layoutSave();
    return 0;
}

// ------------------------------------------------------------------ performance profile + launch
const char* suggested_profile (const Entry* e)
{
    if (e->bench <= 0) return NULL;
    if (e->bench >= 140) return "compatibility";
    if (e->bench >= 105) return "balanced";
    if (e->bench >= 85) return "performance";
    return "lightweight";
}
static FILE* lf;
static void A (const char* a) { fprintf (lf, "ARG:%s\n", a); }
static void AV (const char* a, const char* v) { A (a); A (v); }
int launch_write (const Entry* e)
{
    if (e == NULL || e->db < 0) return -1;
    const GameInfo* g = &L.db[e->db];
    const char* name = g->name;
    char p[600], t[620], b[700];
    lib_path (p, sizeof p, "%s/launch", L.run); snprintf (t, sizeof t, "%s.tmp", p);
    lf = fopen (t, "w");
    if (lf == NULL) return -1;
    fprintf (lf, "GAME:%s\n", name);
    // performance profile
    const char* prof = eff_get (name, "profile");
    if (! strcmp (game_get (name, "profile"), "") && set_geti ("autoprofile") && suggested_profile (e)) prof = suggested_profile (e);
    fprintf (lf, "PROFILE:%s\n", prof);
    AV ("-rompath", lib_path (b, sizeof b, "%s;%s", L.roms, L.bios));
    AV ("-homepath", L.sys);
    AV ("-cfg_directory", lib_path (b, sizeof b, "%s/config/mame", L.root));
    AV ("-inipath", lib_path (b, sizeof b, "%s/config/mame", L.root));
    AV ("-nvram_directory", lib_path (b, sizeof b, "%s/saves/nvram", L.root));
    AV ("-state_directory", lib_path (b, sizeof b, "%s/saves/states", L.root));
    AV ("-snapshot_directory", lib_path (b, sizeof b, "%s/screenshots", L.root));
    AV ("-artpath", lib_path (b, sizeof b, "%s/artwork", L.root));
    AV ("-samplepath", lib_path (b, sizeof b, "%s/samples", L.root));
    AV ("-snapname", "%g/%i");
    AV ("-video", "soft");
    AV ("-sound", "sdl");
    A ("-nowindow"); A ("-skip_gameinfo"); A ("-mouse"); A ("-noreadconfig");
    AV ("-uifont", "default");
    // profile -> speed / sound settings
    int rate = 48000, latency = 2, frameskip = -1;
    if (! strcmp (prof, "performance")) { rate = 44100; latency = 3; }
    else if (! strcmp (prof, "compatibility")) { frameskip = 0; latency = 2; }
    else if (! strcmp (prof, "lightweight")) { rate = 22050; latency = 4; }
    if (frameskip == 0) { A ("-noautoframeskip"); AV ("-frameskip", "0"); }
    else { A ("-autoframeskip"); AV ("-frameskip", ! strcmp (prof, "lightweight") ? "10" : "6"); }
    snprintf (b, sizeof b, "%d", rate); AV ("-samplerate", b);
    snprintf (b, sizeof b, "%d", latency); AV ("-audio_latency", b);
    snprintf (b, sizeof b, "%d", atoi (eff_get (name, "volume"))); AV ("-volume", b);
    // display
    const char* scale = eff_get (name, "scale");
    const int vertical = g->rotate == 90 || g->rotate == 270;
    if (! strcmp (scale, "stretch")) A ("-nokeepaspect"); else A ("-keepaspect");
    if (! strcmp (scale, "pixel")) A ("-nounevenstretch"); else A ("-unevenstretch");
    A (atoi (eff_get (name, "smooth")) ? "-filter" : "-nofilter");
    const char* vmode = eff_get (name, "vertical");
    if (vertical && ! strcmp (vmode, "ror")) A ("-ror");
    else if (vertical && ! strcmp (vmode, "rol")) A ("-rol");
    // quick resume (only drivers that support save states)
    if (atoi (eff_get (name, "resume")) && g->savestate) A ("-autosave");
    // auto screenshot for the library, once
    lib_path (b, sizeof b, "%s/screenshots/%s", L.root, name);
    if (set_geti ("autosnap") && ! file_exists (b)) AV ("-autoboot_script", lib_path (t, sizeof t, "%s/autosnap.lua", L.sys));
    AV ("-autoboot_delay", "0");
    A (name);
    // environment for our SDL framebuffer driver
    const int low = ! strcmp (prof, "lightweight") || atoi (eff_get (name, "lowres"));
    fprintf (lf, "ENV:SDL_FBSCALE=%d\n", low ? 2 : 1);
    fprintf (lf, "ENV:SDL_FBDOUBLE=%d\n", set_geti ("tearfree") ? 1 : 0);
    fprintf (lf, "ENV:SDL_FBROTATE=%d\n", set_geti ("rotate_panel"));
    const int sl = atoi (eff_get (name, "scanlines"));
    if (sl > 0 && ! strcmp (scale, "pixel") && g->sh > 0 && ! vertical)
    {
        // panel 800 px tall (MPC X): MAME scales the game by the largest whole factor that fits
        const int panelH = 800, panelW = 1280, div = low ? 2 : 1;
        int k = (panelH / div) / g->sh;
        const int kw = (panelW / div) / (g->sw > 0 ? g->sw : 1);
        if (kw < k) k = kw;
        if (k >= 2 || (k >= 1 && div == 2))
        {
            const int period = k * div, top = (panelH - g->sh * period) / 2;
            const int pct = sl == 1 ? 25 : (sl == 2 ? 45 : 65);
            fprintf (lf, "ENV:SDL_FBSCANLINES=%d:%d:%d\n", period, top, pct);
        }
    }
    const char* dev = set_get ("audiodev");
    if (dev[0] && strcmp (dev, "default")) fprintf (lf, "ENV:AUDIODEV=%s\n", dev);
    fclose (lf);
    lib_path (t, sizeof t, "%s/launch.tmp", L.run);
    return rename (t, p);
}

int layout_get (int i, const char** src, const char** id)
{
    if (nlayout < 0) layoutLoad();
    if (i < 0 || i >= nlayout) return 0;
    *src = layoutSrc[i]; *id = layoutId[i];
    return 1;
}
const char* layout_src_for (const char* id)
{
    if (nlayout < 0) layoutLoad();
    for (int i = 0; i < nlayout; ++i) if (! strcmp (layoutId[i], id)) return layoutSrc[i];
    return NULL;
}

// ------------------------------------------------------------------ zip identification
typedef struct { unsigned crc; int set; int nroms; } CrcEnt;
static CrcEnt* crcdb; static int ncrc = -1;
static int cmpCrc (const void* a, const void* b) { const unsigned x = ((const CrcEnt*) a)->crc, y = ((const CrcEnt*) b)->crc; return x < y ? -1 : x > y; }
static void loadCrcDb (void)
{
    char p[400], line[128];
    ncrc = 0;
    FILE* f = fopen (lib_path (p, sizeof p, "%s/crcdb.tsv", L.sys), "r");
    if (f == NULL) return;
    int cap = 65536; crcdb = (CrcEnt*) malloc (sizeof (CrcEnt) * (size_t) cap);
    while (fgets (line, sizeof line, f))
    {
        char set[64]; unsigned crc; int nr;
        if (sscanf (line, "%x %63s %d", &crc, set, &nr) != 3) continue;
        const int si = db_find (set);
        if (si < 0) continue;
        if (ncrc == cap) { cap *= 2; crcdb = (CrcEnt*) realloc (crcdb, sizeof (CrcEnt) * (size_t) cap); }
        crcdb[ncrc].crc = crc; crcdb[ncrc].set = si; crcdb[ncrc].nroms = nr; ++ncrc;
    }
    fclose (f);
    qsort (crcdb, (size_t) ncrc, sizeof (CrcEnt), cmpCrc);
}
static unsigned rd32 (const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned) p[3] << 24); }
static unsigned rd16 (const unsigned char* p) { return p[0] | (p[1] << 8); }
int zip_identify (const char* path, char* setOut, int n, int* matched, int* needed)
{
    if (ncrc < 0) loadCrcDb();
    *matched = *needed = 0; setOut[0] = 0;
    FILE* f = fopen (path, "rb");
    if (f == NULL || ncrc == 0) { if (f) fclose (f); return 0; }
    fseek (f, 0, SEEK_END);
    const long size = ftell (f);
    const long tail = size < 66000 ? size : 66000;
    unsigned char* buf = (unsigned char*) malloc ((size_t) tail);
    fseek (f, size - tail, SEEK_SET);
    if (fread (buf, 1, (size_t) tail, f) != (size_t) tail) { free (buf); fclose (f); return 0; }
    long eocd = -1;
    for (long i = tail - 22; i >= 0; --i) if (rd32 (buf + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { free (buf); fclose (f); return 0; }
    const unsigned entries = rd16 (buf + eocd + 10), cdSize = rd32 (buf + eocd + 12), cdOff = rd32 (buf + eocd + 16);
    unsigned char* cd = (unsigned char*) malloc (cdSize + 1);
    fseek (f, (long) cdOff, SEEK_SET);
    const int okRead = fread (cd, 1, cdSize, f) == cdSize;
    fclose (f); free (buf);
    if (! okRead) { free (cd); return 0; }
    // vote: for every member CRC, every set that uses it gets a point
    int* votes = (int*) calloc ((size_t) L.ndb, sizeof (int));
    unsigned pos = 0;
    for (unsigned e = 0; e < entries && pos + 46 <= cdSize; ++e)
    {
        if (rd32 (cd + pos) != 0x02014b50) break;
        const unsigned crc = rd32 (cd + pos + 16);
        const unsigned nl = rd16 (cd + pos + 28), xl = rd16 (cd + pos + 30), cl = rd16 (cd + pos + 32);
        CrcEnt key = { crc, 0, 0 };
        CrcEnt* hit = (CrcEnt*) bsearch (&key, crcdb, (size_t) ncrc, sizeof (CrcEnt), cmpCrc);
        if (hit)
        {
            while (hit > crcdb && hit[-1].crc == crc) --hit;
            int last = -1;
            for (; hit < crcdb + ncrc && hit->crc == crc; ++hit) if (hit->set != last) { votes[hit->set]++; last = hit->set; }
        }
        pos += 46 + nl + xl + cl;
    }
    free (cd);
    // best: most matches; ties -> the set needing fewer ROMs (the more exact fit), then parents over clones
    int best = -1; int bestNeed = 0;
    for (int i = 0; i < L.ndb; ++i)
    {
        if (! votes[i]) continue;
        int need = 0;
        // number of ROMs this set needs: from any of its crc entries
        for (int k = 0; k < ncrc && ! need; ++k) if (crcdb[k].set == i) need = crcdb[k].nroms;
        const int better = best < 0 || votes[i] > votes[best] ||
            (votes[i] == votes[best] && (need < bestNeed || (need == bestNeed && ! L.db[i].parent[0] && L.db[best].parent[0])));
        if (better) { best = i; bestNeed = need; }
    }
    if (best >= 0) { snprintf (setOut, (size_t) n, "%s", L.db[best].name); *matched = votes[best]; *needed = bestNeed; }
    free (votes);
    return best >= 0;
}
