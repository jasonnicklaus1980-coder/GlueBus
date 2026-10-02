// MPC Arcade library app: game database, installed ROMs, user data, settings, MAME launch.
#pragma once
#include <time.h>

#define MAXGAMES 6000
#define MAXFILES 3000

typedef struct
{
    char name[20], parent[20], romof[20];
    char desc[100], year[8], manuf[56], source[24], genre[24];
    short rotate, sw, sh;          // rotation (0/90/180/270) and visible screen size before rotation
    char status;                   // 'g' good, 'i' imperfect, 'p' preliminary
    char savestate, players, buttons, isbios;
    char controls[40];             // e.g. "joy8way", "dial", "doublejoy4way", "paddle"
} GameInfo;

enum VStatus { V_UNKNOWN, V_OK, V_BEST, V_BAD, V_NEEDBIOS, V_NEEDPARENT, V_UNSUPPORTED, V_ISBIOS };

typedef struct
{
    int db;                        // index into the game database, -1 = not a set this MAME knows
    char file[128];                // file name in roms/ or bios/
    char dir;                      // 'r' roms, 'b' bios
    long long size; long mtime;
    int vstatus; char vdetail[120];
    int fav, plays; long playSecs; long last;
    int bench;                     // benchmark speed in % (0 = not measured)
} Entry;

typedef struct
{
    char root[256], sys[300], roms[300], bios[300], run[300], user[300], prof[300];
    GameInfo* db; int ndb;
    Entry files[MAXFILES]; int nfiles;
} Lib;

extern Lib L;

// paths and setup
int lib_init (const char* root);              // 0 = ok
const char* lib_path (char* buf, int n, const char* fmt, ...);  // printf into buf, returns buf
int db_find (const char* name);
int lib_scan (void);                          // (re)scan roms/ and bios/, keep verify cache; returns number of games
Entry* lib_entry (const char* name);          // installed entry by set name
int lib_has_set (const char* name);
int lib_verify (int fileIndex);               // runs mame -verifyroms (blocking)
void lib_verify_cmd (int fileIndex, char* cmd, int n);      // the command, for running it in the background
void lib_verify_parse (int fileIndex, const char* output);  // apply its output
void lib_save_verify (void);
int lib_needs_verify (int fileIndex);
const char* vstatus_text (int v);

// user data
void user_load (void);
void user_save (void);
void user_toggle_fav (Entry* e);
void user_record_plays (void);                // folds run/plays.log into history
void user_save_bench (void);

// settings: global (settings.conf) and per game (games/<name>.conf)
const char* set_get (const char* key);
int set_geti (const char* key);
void set_set (const char* key, const char* val);
void set_seti (const char* key, int v);
void settings_save (void);
const char* game_get (const char* game, const char* key);   // "" = use the global value
void game_set (const char* game, const char* key, const char* val);

// effective (per-game value or the global one)
const char* eff_get (const char* game, const char* key);

// launch
int launch_write (const Entry* e);            // writes run/launch for session.sh; 0 = ok
const char* auto_template (const GameInfo* g);
const char* suggested_profile (const Entry* e);

// pad profiles (profiles/default.conf, profiles/games/<name>.conf, profiles/layout.conf)
typedef struct { char src[48]; char act[24]; } MapLine;
typedef struct { MapLine m[64]; int n; int threshold; float exitHold; int turbo; int lights; int touchExit; } PadMap;
void padmap_defaults (PadMap* p);
void padmap_template (PadMap* p, const char* templateId);
int padmap_load (PadMap* p, const char* file);
int padmap_save (const PadMap* p, const char* file);
const char* padmap_get (const PadMap* p, const char* src);
void padmap_set (PadMap* p, const char* src, const char* act);
void padmap_for_game (PadMap* p, const Entry* e, char* whereOut, int n);  // resolved map + where it came from
int padmap_write_active (const PadMap* p, int uiMode);                   // run/active.conf for padbridge
int layout_count (void);                      // how many controls have been learned (layout.conf)
int layout_has (const char* src);
int layout_set (const char* src, const char* id);
int layout_remove (const char* src);

// small helpers
int run_capture (const char* cmd, char* out, int n);   // popen, returns exit status
long file_mtime (const char* path);
int file_exists (const char* path);
int mkdir_p (const char* path);
const char* shq (char* buf, int n, const char* s);     // single-quote for the shell

// learned hardware layout lookups
int layout_get (int i, const char** src, const char** id);   // 0 = no such index
const char* layout_src_for (const char* id);                  // "PAD5", "HW:Play" ... or NULL

// identify a zip by its members' CRC32s (read from the zip directory, nothing is unpacked)
// returns 1 and the best-matching set name; *matched / *needed = how many of that set's ROMs were found
int zip_identify (const char* path, char* setOut, int n, int* matched, int* needed);
