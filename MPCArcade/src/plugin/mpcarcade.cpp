// MPC Arcade launcher: a native MPC OS VST2 instrument that opens the MAME arcade from the MPC's own plugin screen.
//
// MAME needs the whole machine (screen, sound, pads), which MPC keeps while it runs, so the plugin can't show the
// games inside MPC. What it does: on a PLUGIN track pick "MPC Arcade", choose a game with Q-Link 1 (or open the
// library), tap PLAY / LIBRARY / RESUME, then tap the same button again within 6 seconds to confirm. The plugin starts
// MAME/system/launch.sh, which steps the MPC app aside, runs the arcade, and starts the MPC app again when you leave.
// Unsaved MPC changes are lost when the MPC app closes: the screen says so before you confirm.
//
// Parameters: 0 Game (index into the library's list), 1 Library, 2 Play, 3 Resume (buttons: any change = a tap),
//             4 Status (text), 5 Details (text). The plugin makes no sound.
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MA_EXPORT extern "C" __attribute__ ((visibility ("default")))
extern char** environ;

namespace
{
constexpr int kVersion = 1000;
enum { P_GAME, P_LIBRARY, P_PLAY, P_RESUME, P_STATUS, P_DETAILS, NPARAMS };
const char* const kKeys[NPARAMS] { "game", "library", "play", "resume", "status", "details" };
const char* const kNames[NPARAMS] { "Game", "Open Library", "Play Game", "Resume Last", "Status", "Details" };
constexpr int kMaxGames = 512;

double nowSec() { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
void copyStr (void* dst, const char* s, size_t max) { if (dst == nullptr) return; std::strncpy ((char*) dst, s, max - 1); ((char*) dst)[max - 1] = 0; }

// text shown by MPC's UI thread, written from the UI / audio threads: double buffered
struct Text
{
    char buf[2][96] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    bool publish (const char* s)
    {
        if (std::strcmp (buf[live.load()], s) == 0) return false;
        const int idle = 1 - live.load();
        std::snprintf (buf[idle], sizeof buf[idle], "%s", s);
        live.store (idle); return true;
    }
};

struct Game { char name[20]; char title[64]; char info[64]; };

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> nv[NPARAMS];
    float seen[NPARAMS];
    char root[300] {};
    Game games[kMaxGames]; int nGames = 0; char lastGame[20] {};
    Text status, details; int statusGen = 0, detailsGen = 0;
    double openedAt = 0, armedAt = -1; int armed = -1;
    double launchedAt = -1;

    Plugin()
    {
        for (int i = 0; i < NPARAMS; ++i) { nv[i].store (0.f); seen[i] = 0.f; }
        openedAt = nowSec();
        findRoot();
        loadGames();
        idleStatus();
    }
    void findRoot()
    {
        std::snprintf (root, sizeof root, "/media/az01-internal/MAME");
        FILE* f = std::fopen ("/sdcard/vst/mpcarcade.root", "r");
        if (f) { char b[300]; if (std::fgets (b, sizeof b, f)) { b[std::strcspn (b, "\r\n")] = 0; if (b[0]) std::snprintf (root, sizeof root, "%s", b); } std::fclose (f); }
    }
    // the library writes config/library/plugin_list.tsv: name<TAB>title<TAB>info (ready games, favorites first)
    void loadGames()
    {
        nGames = 0;
        char p[400]; std::snprintf (p, sizeof p, "%s/config/library/plugin_list.tsv", root);
        FILE* f = std::fopen (p, "r");
        if (f == nullptr) return;
        char line[300];
        while (std::fgets (line, sizeof line, f) && nGames < kMaxGames)
        {
            line[std::strcspn (line, "\r\n")] = 0;
            if (line[0] == '#') { if (! std::strncmp (line, "#last ", 6)) std::snprintf (lastGame, sizeof lastGame, "%s", line + 6); continue; }
            char* t1 = std::strchr (line, '\t'); if (! t1) continue; *t1 = 0;
            char* t2 = std::strchr (t1 + 1, '\t'); if (t2) *t2 = 0;
            Game& g = games[nGames++];
            std::snprintf (g.name, sizeof g.name, "%s", line);
            std::snprintf (g.title, sizeof g.title, "%s", t1 + 1);
            std::snprintf (g.info, sizeof g.info, "%s", t2 ? t2 + 1 : "");
        }
        std::fclose (f);
    }
    bool installed() const
    {
        char p[400]; struct stat st;
        std::snprintf (p, sizeof p, "%s/system/launch.sh", root);
        return stat (p, &st) == 0;
    }
    int gameIndex() const { return nGames ? (int) std::lround (nv[P_GAME].load() * (nGames > 1 ? nGames - 1 : 0)) : -1; }
    void notify (int i) { if (master) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load()); }
    void setText (Text& t, int& gen, int param, const char* s)
    {
        if (! t.publish (s)) return;
        gen = (gen + 1) % 1000;
        nv[param].store (gen / 1000.f); seen[param] = nv[param].load();
        notify (param);
    }
    void idleStatus()
    {
        char b[96];
        if (! installed()) std::snprintf (b, sizeof b, "Arcade files not found in %s", root);
        else if (nGames == 0) std::snprintf (b, sizeof b, "Tap LIBRARY to open the arcade and add games");
        else std::snprintf (b, sizeof b, "%d game%s ready \xc2\xb7 Q-Link 1 picks one", nGames, nGames == 1 ? "" : "s");
        setText (status, statusGen, P_STATUS, b);
        const int gi = gameIndex();
        setText (details, detailsGen, P_DETAILS, gi >= 0 ? games[gi].info : "");
    }
    void launch (int which)
    {
        const char* mode = which == P_LIBRARY ? "library" : (which == P_PLAY ? "play" : "resume");
        const int gi = gameIndex();
        char script[400]; std::snprintf (script, sizeof script, "%s/system/launch.sh", root);
        char game[24] = "";
        if (which == P_PLAY && gi >= 0) std::snprintf (game, sizeof game, "%s", games[gi].name);
        if (which == P_RESUME) std::snprintf (game, sizeof game, "%s", lastGame);
        char* argv[] = { (char*) "sh", script, (char*) mode, game, nullptr };
        // double fork: the launcher must not inherit MPC's open files (audio / MIDI devices) and must not stay MPC's
        // child. Between fork and exec only async-signal-safe calls (setsid, close, execve, _exit).
        const pid_t pid = fork();
        if (pid == 0)
        {
            setsid();
            if (fork() != 0) _exit (0);
            for (int fd = 3; fd < 4096; ++fd) close (fd);
            execve ("/bin/sh", argv, environ);
            _exit (127);
        }
        if (pid > 0)
        {
            waitpid (pid, nullptr, 0);
            launchedAt = nowSec();
            setText (status, statusGen, P_STATUS, "Starting the arcade\xe2\x80\xa6 the MPC app closes now");
        }
        else setText (status, statusGen, P_STATUS, "Couldn't start the arcade (see MAME/logs)");
    }
    void press (int which)
    {
        const double t = nowSec();
        if (t - openedAt < 2.5) return;                                  // project load / plugin restore: not a tap
        if (launchedAt > 0 && t - launchedAt < 20) return;
        if (! installed()) { idleStatus(); return; }
        if (which == P_PLAY && gameIndex() < 0) { setText (status, statusGen, P_STATUS, "No games yet: tap LIBRARY to add some"); return; }
        if (which == P_RESUME && ! lastGame[0]) { setText (status, statusGen, P_STATUS, "Nothing played yet: tap LIBRARY or PLAY"); return; }
        if (armed == which && t - armedAt < 6.0) { armed = -1; launch (which); return; }
        armed = which; armedAt = t;
        setText (status, statusGen, P_STATUS, "Tap again to start \xe2\x80\x94 save first: the MPC app closes");
    }
    // called on every UI / audio callback: buttons are "any change of value = a tap"
    void poll()
    {
        for (int i = P_LIBRARY; i <= P_RESUME; ++i)
        {
            const float v = nv[i].load();
            if (v != seen[i]) { seen[i] = v; press (i); }
        }
        const float g = nv[P_GAME].load();
        if (g != seen[P_GAME])
        {
            seen[P_GAME] = g;
            const int gi = gameIndex();
            setText (details, detailsGen, P_DETAILS, gi >= 0 ? games[gi].info : "");
        }
        if (armed >= 0 && nowSec() - armedAt >= 6.0) { armed = -1; idleStatus(); }
    }
    void display (int i, char* out, size_t max)
    {
        switch (i)
        {
            case P_GAME:
            {
                const int gi = gameIndex();
                std::snprintf (out, max, "%s", gi >= 0 ? games[gi].title : "(no games yet)");
                return;
            }
            case P_LIBRARY: std::snprintf (out, max, "Library"); return;
            case P_PLAY: std::snprintf (out, max, "Play"); return;
            case P_RESUME: std::snprintf (out, max, "Resume"); return;
            case P_STATUS: std::snprintf (out, max, "%s", status.get()); return;
            case P_DETAILS: std::snprintf (out, max, "%s", details.get()); return;
            default: out[0] = 0;
        }
    }
};

void processReplacing (AEffect* e, float** in, float** out, int32_t n)
{
    (void) in;
    static_cast<Plugin*> (e->object)->poll();
    std::memset (out[0], 0, sizeof (float) * (size_t) n);
    std::memset (out[1], 0, sizeof (float) * (size_t) n);
}
void processAccumulating (AEffect* e, float** in, float** out, int32_t n) { (void) in; (void) out; (void) n; static_cast<Plugin*> (e->object)->poll(); }
void setParameter (AEffect* e, int32_t i, float v)
{
    if (i < 0 || i >= P_STATUS) return;                                  // status texts are owned by the plugin
    Plugin* p = static_cast<Plugin*> (e->object);
    p->nv[i].store (v < 0 ? 0 : (v > 1 ? 1 : v));
    p->poll();
}
float getParameter (AEffect* e, int32_t i)
{
    if (i < 0 || i >= NPARAMS) return 0.f;
    return static_cast<Plugin*> (e->object)->nv[i].load();
}
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    (void) val; (void) opt;
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose:            p->~Plugin(); std::free (p); return 0;
        case effGetParamName:     if (idx >= 0 && idx < NPARAMS) copyStr (ptr, kNames[idx], 28); return 0;
        case effGetParamLabel:    if (ptr != nullptr) ((char*) ptr)[0] = 0; return 0;
        case effGetParamDisplay:
            if (idx >= 0 && idx < NPARAMS && ptr != nullptr) { char b[96]; p->poll(); p->display (idx, b, sizeof b); copyStr (ptr, b, 64); }
            return 0;
        case effCanBeAutomated:   return 0;                               // nothing here should ever be automated
        case effMainsChanged:     if (val) { p->loadGames(); p->idleStatus(); } return 0;
        case effProcessEvents:    return 1;
        case effGetProgram:       return 0;
        case effGetProgramName:
        case effGetProgramNameIndexed: copyStr (ptr, "MPC Arcade", 24); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "MPC Arcade", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetPlugCategory:  return kPlugCategSynth;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return 0;
        case effCanDo:
        {
            const char* s = (const char*) ptr;
            if (s == nullptr) return 0;
            if (! std::strcmp (s, "receiveVstEvents") || ! std::strcmp (s, "receiveVstMidiEvent")) return 1;
            return -1;
        }
        default: return 0;
    }
}
} // namespace

MA_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    void* mem = std::calloc (1, sizeof (Plugin));                         // no operator new: keeps libstdc++ out
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic;
    fx.dispatcher = dispatcher;
    fx.process = processAccumulating;
    fx.processReplacing = processReplacing;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.numPrograms = 1;
    fx.numParams = NPARAMS;
    fx.numInputs = 0;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing | effFlagsIsSynth;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('M' << 24) | ('P' << 16) | ('C' << 8) | 'A';             // 'MPCA'
    fx.version = kVersion;
    return &fx;
}
// for tools/make_skin.py and the tests (not used by MPC)
MA_EXPORT const char* MA_ParamKey (int i) { return (i >= 0 && i < NPARAMS) ? kKeys[i] : ""; }
MA_EXPORT int MA_ParamCount() { return NPARAMS; }
