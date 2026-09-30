// Da DJ Decks - two-deck DJ player for Akai MPC OS (Gen1, 32-bit ARM), built from the GlueBus / RadioReady
// components (dependency-free VST2 core, parameter model, host notification, packaging, skin pipeline).
// Needs only libc/libm/libpthread. No GUI: MPC draws the skin from /sdcard/Synths.
//
// An insert effect: the MPC audio coming in passes through (MPC IN level) and the two decks are mixed on top.
// Tracks are WAV files in /sdcard/DJ (and one level of sub-folders). A loader thread scans the folder, reads the
// files and finds each track's tempo and beat grid, so the audio thread never touches the SD card.
// Per deck: track select + LOAD, PLAY/PAUSE, CUE, pitch (+-8/16/50 %), SYNC (tempo + beat), nudge, beat loops,
// gain, 3-band kill EQ, filter, channel fader. Mixer: crossfader (smooth / cut), master, MPC input level.
// The screen's times, BPM, spinning platters, progress bars, beat lights and meters are read-only parameters
// that the plugin sends to MPC (audioMasterAutomate) when they change.
#include "Deck.h"
#include "Wav.h"
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <new>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>

#define DJ_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
// ------------------------------------------------------------------------------------------------ parameters
enum DeckParam
{
    D_TRACK, D_LOAD, D_PLAY, D_CUE, D_PITCH, D_RANGE, D_SYNC, D_NUDGE_DN, D_NUDGE_UP, D_LOOP,
    D_GAIN, D_HIGH, D_MID, D_LOW, D_FILTER, D_FADER, D_LOADED,
    D_TIME, D_REMAIN, D_BPM, D_PLATTER, D_PROGRESS, D_VU, D_STATUS, D_BEAT,       // read-only
    D_COUNT                                                                         // 25
};
constexpr int kDeckParams = D_COUNT;
inline int dp (int deck, int what) { return deck * kDeckParams + what; }
enum { M_XFADE = 2 * kDeckParams, M_CURVE, M_MASTER, M_INPUT, M_RESCAN, M_VU_L, M_VU_R, M_LIBRARY, P_COUNT };   // 50..57, 58

constexpr int kMaxTracks = 256;
constexpr float kVuFloor = -40.f, kVuTop = 6.f;
const char* const kRangeNames[] { "+-8 %", "+-16 %", "+-50 %" };
const double kRanges[] { 0.08, 0.16, 0.50 };
const char* const kLoopNames[] { "Off", "1 Beat", "2 Beats", "4 Beats", "8 Beats", "16 Beats" };
const int kLoopBeats[] { 0, 1, 2, 4, 8, 16 };
const char* const kCurveNames[] { "Smooth", "Cut" };

enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_MOMENT, K_READ, K_TRACK };
enum Fmt  { F_NUM, F_DB, F_EQ, F_PCT, F_PITCH, F_FILTER, F_TIME, F_REMAIN, F_BPM, F_VU, F_TEXT, F_XFADE, F_MASTER, F_LOADED };
struct ParamDef { char name[24]; const char* unit; Kind kind; float lo, hi, step, def; const char* const* choices; int n; Fmt fmt; };
ParamDef kParams[P_COUNT];

void def (int i, const char* name, const char* unit, Kind k, float lo, float hi, float step, float d,
          const char* const* ch = nullptr, int n = 0, Fmt f = F_NUM)
{
    ParamDef& p = kParams[i];
    std::snprintf (p.name, sizeof p.name, "%s", name);
    p.unit = unit; p.kind = k; p.lo = lo; p.hi = hi; p.step = step; p.def = d; p.choices = ch; p.n = n; p.fmt = f;
}
void initParams()
{
    for (int d = 0; d < 2; ++d)
    {
        char nm[24]; const char D = d == 0 ? 'A' : 'B';
        auto N = [&] (const char* what) { std::snprintf (nm, sizeof nm, "%c %s", D, what); return nm; };
        def (dp (d, D_TRACK), N ("Track"), "", K_TRACK, 0, 1, 0, 0);
        def (dp (d, D_LOAD), N ("Load"), "", K_MOMENT, 0, 1, 1, 0);
        def (dp (d, D_PLAY), N ("Play"), "", K_BOOL, 0, 1, 1, 0);
        def (dp (d, D_CUE), N ("Cue"), "", K_MOMENT, 0, 1, 1, 0);
        def (dp (d, D_PITCH), N ("Pitch"), "%", K_FLOAT, -1, 1, 0, 0, nullptr, 0, F_PITCH);
        def (dp (d, D_RANGE), N ("Pitch Range"), "", K_CHOICE, 0, 2, 1, 0, kRangeNames, 3);
        def (dp (d, D_SYNC), N ("Sync"), "", K_MOMENT, 0, 1, 1, 0);
        def (dp (d, D_NUDGE_DN), N ("Nudge -"), "", K_MOMENT, 0, 1, 1, 0);
        def (dp (d, D_NUDGE_UP), N ("Nudge +"), "", K_MOMENT, 0, 1, 1, 0);
        def (dp (d, D_LOOP), N ("Loop"), "", K_CHOICE, 0, 5, 1, 0, kLoopNames, 6);
        def (dp (d, D_GAIN), N ("Gain"), "dB", K_FLOAT, -12, 12, 0.1f, 0, nullptr, 0, F_DB);
        def (dp (d, D_HIGH), N ("High"), "dB", K_FLOAT, -24, 6, 0.1f, 0, nullptr, 0, F_EQ);
        def (dp (d, D_MID), N ("Mid"), "dB", K_FLOAT, -24, 6, 0.1f, 0, nullptr, 0, F_EQ);
        def (dp (d, D_LOW), N ("Low"), "dB", K_FLOAT, -24, 6, 0.1f, 0, nullptr, 0, F_EQ);
        def (dp (d, D_FILTER), N ("Filter"), "", K_FLOAT, -1, 1, 0, 0, nullptr, 0, F_FILTER);
        def (dp (d, D_FADER), N ("Volume"), "", K_FLOAT, 0, 1, 0, 0.8f, nullptr, 0, F_PCT);
        def (dp (d, D_LOADED), N ("Loaded Track"), "", K_FLOAT, 0, kMaxTracks, 1, 0, nullptr, 0, F_LOADED);   // restores the track with a project
        def (dp (d, D_TIME), N ("Time"), "", K_READ, 0, 3600, 0, 0, nullptr, 0, F_TIME);
        def (dp (d, D_REMAIN), N ("Remaining"), "", K_READ, 0, 3600, 0, 0, nullptr, 0, F_REMAIN);
        def (dp (d, D_BPM), N ("BPM"), "", K_READ, 0, 400, 0, 0, nullptr, 0, F_BPM);
        def (dp (d, D_PLATTER), N ("Platter"), "", K_READ, 0, 1, 0, 0);
        def (dp (d, D_PROGRESS), N ("Position"), "", K_READ, 0, 1, 0, 0, nullptr, 0, F_PCT);
        def (dp (d, D_VU), N ("Meter"), "dB", K_READ, kVuFloor, kVuTop, 0, kVuFloor, nullptr, 0, F_VU);
        def (dp (d, D_STATUS), N ("Now Playing"), "", K_READ, 0, 1000, 0, 0, nullptr, 0, F_TEXT);
        def (dp (d, D_BEAT), N ("Beat"), "", K_READ, 0, 1, 0, 0);
    }
    def (M_XFADE, "Crossfader", "", K_FLOAT, -1, 1, 0, 0, nullptr, 0, F_XFADE);
    def (M_CURVE, "Crossfader Curve", "", K_CHOICE, 0, 1, 1, 0, kCurveNames, 2);
    def (M_MASTER, "Master", "dB", K_FLOAT, -60, 6, 0.1f, 0, nullptr, 0, F_MASTER);
    def (M_INPUT, "MPC In", "", K_FLOAT, 0, 1, 0, 1, nullptr, 0, F_PCT);
    def (M_RESCAN, "Rescan", "", K_MOMENT, 0, 1, 1, 0);
    def (M_VU_L, "Master Meter L", "dB", K_READ, kVuFloor, kVuTop, 0, kVuFloor, nullptr, 0, F_VU);
    def (M_VU_R, "Master Meter R", "dB", K_READ, kVuFloor, kVuTop, 0, kVuFloor, nullptr, 0, F_VU);
    def (M_LIBRARY, "Library", "", K_READ, 0, 1000, 0, 0, nullptr, 0, F_TEXT);
}

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
float toPlain (const ParamDef& d, float norm)
{
    norm = clampf (norm, 0.f, 1.f);
    if (d.kind == K_TRACK) return norm;
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return norm >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return std::floor (norm * (float) (d.n - 1) + 0.5f);
    float v = d.lo + norm * (d.hi - d.lo);
    if (d.step > 0.f) v = d.lo + std::floor ((v - d.lo) / d.step + 0.5f) * d.step;
    return clampf (v, d.lo, d.hi);
}
float toNorm (const ParamDef& d, float plain)
{
    if (d.kind == K_TRACK) return clampf (plain, 0.f, 1.f);
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return plain >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return d.n > 1 ? clampf (plain / (float) (d.n - 1), 0.f, 1.f) : 0.f;
    return clampf ((plain - d.lo) / (d.hi - d.lo), 0.f, 1.f);
}
void copyStr (void* dst, const char* src, size_t max = 24)
{
    if (dst == nullptr) return;
    std::strncpy ((char*) dst, src, max - 1);
    ((char*) dst)[max - 1] = 0;
}
void fmtTime (char* out, size_t max, double s, bool minus)
{
    if (s < 0) s = 0;
    const int t = (int) s;
    std::snprintf (out, max, "%s%02d:%02d", minus ? "-" : "", t / 60, t % 60);
}

// ------------------------------------------------------------------------------------------------ library
struct Entry { char path[512]; char name[96]; };
struct Library { Entry e[kMaxTracks]; int count = 0; };

const char* djFolder()
{
    const char* e = std::getenv ("DJ_FOLDER");
    return e != nullptr && *e != 0 ? e : "/sdcard/DJ";
}
bool isWav (const char* n)
{
    const size_t l = std::strlen (n);
    return l > 4 && n[0] != '.' && strcasecmp (n + l - 4, ".wav") == 0;
}
void scanFolder (Library& lib, const char* dir, const char* prefix, int depth)
{
    DIR* d = opendir (dir);
    if (d == nullptr) return;
    while (dirent* de = readdir (d))
    {
        if (de->d_name[0] == '.' || lib.count >= kMaxTracks) continue;
        char full[512]; std::snprintf (full, sizeof full, "%s/%s", dir, de->d_name);
        if (isWav (de->d_name))
        {
            Entry& e = lib.e[lib.count++];
            std::snprintf (e.path, sizeof e.path, "%s", full);
            char nm[96]; std::snprintf (nm, sizeof nm, "%s%s", prefix, de->d_name);
            nm[std::strlen (nm) - 4] = 0;                                      // without ".wav"
            std::snprintf (e.name, sizeof e.name, "%s", nm);
        }
        else if (depth == 0)
        {
            DIR* sub = opendir (full);
            if (sub != nullptr) { closedir (sub); char pre[96]; std::snprintf (pre, sizeof pre, "%s/", de->d_name); scanFolder (lib, full, pre, 1); }
        }
    }
    closedir (d);
}
int cmpEntry (const void* a, const void* b) { return strcasecmp (((const Entry*) a)->name, ((const Entry*) b)->name); }

// ------------------------------------------------------------------------------------------------ plugin
struct Text   // written by one thread into the idle half, published by flipping the index
{
    char buf[2][112] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    void publish (const char* s) { const int idle = 1 - live.load(); std::snprintf (buf[idle], sizeof buf[idle], "%s", s); live.store (idle); }
};

constexpr int kChunk = 64;

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> v[P_COUNT], nv[P_COUNT];
    float sr = 44100.f;
    bool notifying = false;

    // library (loader thread writes, UI thread reads under the lock; the audio thread never touches it)
    pthread_mutex_t libLock;
    Library* lib = nullptr; Library* libSpare = nullptr;
    std::atomic<int> libCount { 0 };

    // loader thread
    pthread_t loader {}; bool loaderRunning = false;
    std::atomic<bool> quit { false }, rescanReq { true };
    std::atomic<int> loadReq[2];
    std::atomic<dj::Track*> pending[2], retired[2];
    std::atomic<int> pendingIndex[2];
    std::atomic<bool> loading[2];
    std::atomic<const char*> loadError[2];
    char errorName[2][96] {};

    // requests from the UI thread, served by the audio thread
    std::atomic<bool> cueReq[2], syncReq[2];
    std::atomic<int> nudgeReq[2];

    // audio thread
    dj::Deck deck[2];
    int loadedIndex[2] { -1, -1 };
    float bufA[2][kChunk], bufB[2][kChunk];
    double xfA = 1, xfB = 1;
    double gMaster = 1, gInput = 1;
    float vu[2] { kVuFloor, kVuFloor }, mvu[2] { kVuFloor, kVuFloor };
    float sent[P_COUNT];
    int displayCountdown = 0;
    Text status[2], library;
    int textGen[3] {};

    Plugin()
    {
        for (int i = 0; i < P_COUNT; ++i) setPlain (i, kParams[i].def);
        for (float& s : sent) s = 1e9f;
        for (int d = 0; d < 2; ++d)
        {
            loadReq[d].store (-1); pending[d].store (nullptr); retired[d].store (nullptr); pendingIndex[d].store (-1);
            loading[d].store (false); loadError[d].store (nullptr); cueReq[d].store (false); syncReq[d].store (false); nudgeReq[d].store (0);
            status[d].publish ("Empty: pick a track, press LOAD");
        }
        library.publish ("Scanning...");
        pthread_mutex_init (&libLock, nullptr);
        lib = (Library*) std::calloc (1, sizeof (Library));
        libSpare = (Library*) std::calloc (1, sizeof (Library));
        prepare (44100.f);
    }
    ~Plugin()
    {
        stopLoader();
        for (int d = 0; d < 2; ++d)
        {
            dj::freeTrack (deck[d].track); deck[d].track = nullptr;
            dj::freeTrack (pending[d].exchange (nullptr)); dj::freeTrack (retired[d].exchange (nullptr));
        }
        std::free (lib); std::free (libSpare);
        pthread_mutex_destroy (&libLock);
    }
    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    float get (int i) const { return v[i].load(); }
    void prepare (float rate) { sr = rate; for (auto& d : deck) d.prepare (rate); }

    // ---------------------------------------------------------------------------- loader thread
    static void* loaderMain (void* arg) { static_cast<Plugin*> (arg)->loaderLoop(); return nullptr; }
    void startLoader() { loaderRunning = pthread_create (&loader, nullptr, loaderMain, this) == 0; }
    void stopLoader() { if (loaderRunning) { quit.store (true); pthread_join (loader, nullptr); loaderRunning = false; } }
    void loaderLoop()
    {
        while (! quit.load())
        {
            for (int d = 0; d < 2; ++d) dj::freeTrack (retired[d].exchange (nullptr));
            if (rescanReq.exchange (false))
            {
                libSpare->count = 0;
                scanFolder (*libSpare, djFolder(), "", 0);
                std::qsort (libSpare->e, (size_t) libSpare->count, sizeof (Entry), cmpEntry);
                pthread_mutex_lock (&libLock);
                Library* t = lib; lib = libSpare; libSpare = t;
                libCount.store (lib->count);
                pthread_mutex_unlock (&libLock);
            }
            for (int d = 0; d < 2; ++d)
            {
                const int idx = loadReq[d].exchange (-1);
                if (idx < 0) continue;
                char path[512] = {}, name[96] = {};
                pthread_mutex_lock (&libLock);
                if (idx < lib->count) { std::snprintf (path, sizeof path, "%s", lib->e[idx].path); std::snprintf (name, sizeof name, "%s", lib->e[idx].name); }
                pthread_mutex_unlock (&libLock);
                if (path[0] == 0) continue;
                loading[d].store (true); loadError[d].store (nullptr);
                const char* err = nullptr;
                dj::Track* t = dj::loadWav (path, &err);
                if (t != nullptr)
                {
                    std::snprintf (t->name, sizeof t->name, "%s", name);
                    dj::analyzeBeats (t);
                    pendingIndex[d].store (idx);
                    dj::freeTrack (pending[d].exchange (t));                    // an unconsumed older load is dropped
                }
                else { std::snprintf (errorName[d], sizeof errorName[d], "%s", name); loadError[d].store (err); }
                loading[d].store (false);
            }
            usleep (10000);
        }
    }

    // ---------------------------------------------------------------------------- host side
    void notifyHost (int from, int to)
    {
        if (master == nullptr || notifying) return;
        notifying = true;
        for (int i = from; i < to; ++i) if (kParams[i].kind != K_READ) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
        master (&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
        notifying = false;
    }
    int selectedIndex (int d) const
    {
        const int n = libCount.load();
        if (n <= 0) return -1;
        const int i = (int) (get (dp (d, D_TRACK)) * n);
        return i >= n ? n - 1 : i;
    }
    void hostSet (int i, float norm)
    {
        const ParamDef& p = kParams[i];
        if (p.kind == K_READ) return;
        const float old = get (i), plain = toPlain (p, norm);
        if (p.kind == K_MOMENT)
        {
            if (notifying || plain < 0.5f || old >= 0.5f) { setPlain (i, plain); return; }
            if (i == M_RESCAN) rescanReq.store (true);
            else
            {
                const int d = i / kDeckParams, w = i % kDeckParams;
                if (w == D_LOAD) { const int s = selectedIndex (d); if (s >= 0) loadReq[d].store (s); }
                if (w == D_CUE) cueReq[d].store (true);
                if (w == D_SYNC) syncReq[d].store (true);
                if (w == D_NUDGE_DN) nudgeReq[d].fetch_sub (1);
                if (w == D_NUDGE_UP) nudgeReq[d].fetch_add (1);
            }
            setPlain (i, 0.f);                          // spring back, and tell MPC so the next press is a press again
            if (master != nullptr && ! notifying) { notifying = true; master (&fx, 0, i, 0, nullptr, 0.f); notifying = false; }
            return;
        }
        nv[i].store (clampf (norm, 0.f, 1.f)); v[i].store (plain);
        if (i < M_XFADE && i % kDeckParams == D_LOADED && ! notifying)           // project restore: reload that track
        {
            const int d = i / kDeckParams, idx = (int) plain - 1;
            if (idx >= 0 && idx != loadedIndex[d]) loadReq[d].store (idx);
        }
    }

    // ---------------------------------------------------------------------------- display
    void send (int i, float plain, float threshold)
    {
        setPlain (i, plain);
        if (std::fabs (plain - sent[i]) < threshold) return;
        sent[i] = plain;
        if (master != nullptr) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
    }
    void setText (Text& t, int param, int& gen, const char* s)
    {
        if (std::strcmp (t.get(), s) == 0) return;
        t.publish (s);
        gen = (gen + 1) % 1000;
        send (param, (float) gen, 0.5f);
    }
    void updateDisplay()
    {
        char s[112];
        for (int d = 0; d < 2; ++d)
        {
            dj::Deck& k = deck[d];
            const dj::Track* t = k.track;
            const double pitch = kRanges[(int) get (dp (d, D_RANGE))] * get (dp (d, D_PITCH));
            if (t != nullptr)
            {
                const double posS = k.pos / t->rate;
                send (dp (d, D_TIME), (float) posS, 0.5f);
                send (dp (d, D_REMAIN), (float) ((t->frames - k.pos) / t->rate / (1.0 + pitch)), 0.5f);
                send (dp (d, D_BPM), (float) k.effectiveBpm (pitch), 0.05f);
                const double rev = posS * (100.0 / 3.0) / 60.0;                    // 33 1/3 rpm
                send (dp (d, D_PLATTER), (float) (rev - std::floor (rev)), 0.004f);
                send (dp (d, D_PROGRESS), (float) (k.pos / t->frames), 0.004f);
                double bar = 0;
                if (t->bpm > 0) { bar = dj::fmodd ((k.pos - t->offset) / k.beatFrames(), 4.0) / 4.0; if (bar < 0) bar += 1.0; }
                send (dp (d, D_BEAT), (float) bar, 0.03f);
            }
            send (dp (d, D_VU), vu[d], 1.f);
            if (loading[d].load()) std::snprintf (s, sizeof s, "Loading...");
            else if (const char* e = loadError[d].load()) std::snprintf (s, sizeof s, "Can't load: %s", e);
            else if (t != nullptr) std::snprintf (s, sizeof s, "%s", t->name);
            else std::snprintf (s, sizeof s, "Empty: pick a track, press LOAD");
            setText (status[d], dp (d, D_STATUS), textGen[d], s);
        }
        send (M_VU_L, mvu[0], 1.f); send (M_VU_R, mvu[1], 1.f);
        const int n = libCount.load();
        if (rescanReq.load()) std::snprintf (s, sizeof s, "Scanning...");
        else if (n == 0) std::snprintf (s, sizeof s, "No WAV files in %s", djFolder());
        else std::snprintf (s, sizeof s, "%d track%s in %s", n, n == 1 ? "" : "s", djFolder());
        setText (library, M_LIBRARY, textGen[2], s);
    }

    // ---------------------------------------------------------------------------- audio
    void serveRequests (int d)
    {
        dj::Deck& k = deck[d];
        if (retired[d].load() == nullptr)
            if (dj::Track* t = pending[d].exchange (nullptr))
            {
                retired[d].store (k.track);
                k.setTrack (t);
                loadedIndex[d] = pendingIndex[d].load();
                setPlain (dp (d, D_PLAY), 0.f); setPlain (dp (d, D_LOOP), 0.f);
                setPlain (dp (d, D_LOADED), (float) (loadedIndex[d] + 1));
                notifyHost (dp (d, 0), dp (d, D_TIME));
            }
        if (k.track == nullptr) { cueReq[d].store (false); syncReq[d].store (false); nudgeReq[d].store (0); return; }
        if (cueReq[d].exchange (false))
        {
            if (k.playing) { k.jump (k.cue); setPlain (dp (d, D_PLAY), 0.f); k.playing = false; notifyHost (dp (d, D_PLAY), dp (d, D_PLAY) + 1); }
            else k.cue = k.pos;
        }
        if (const int n = nudgeReq[d].exchange (0)) k.jump (k.pos + n * 0.02 * k.track->rate);   // 20 ms per press
        if (syncReq[d].exchange (false)) sync (d);
        if (k.alignOnStart && get (dp (d, D_PLAY)) > 0.5f && ! k.playing && deck[1 - d].playing) align (d);
        const int loop = kLoopBeats[(int) get (dp (d, D_LOOP))];
        if (loop != k.loopBeats) k.setLoop (loop);
        if (k.takeEndReached()) { setPlain (dp (d, D_PLAY), 0.f); notifyHost (dp (d, D_PLAY), dp (d, D_PLAY) + 1); }
    }
    void sync (int d)
    {
        dj::Deck& me = deck[d]; dj::Deck& other = deck[1 - d];
        if (me.track == nullptr || other.track == nullptr || me.track->bpm <= 0 || other.track->bpm <= 0) return;
        const double otherPitch = kRanges[(int) get (dp (1 - d, D_RANGE))] * get (dp (1 - d, D_PITCH));
        const double target = other.effectiveBpm (otherPitch);
        double bestPitch = 1e9;
        for (double mul : { 1.0, 2.0, 0.5 })                                     // half / double time if closer
        {
            const double p = target * mul / me.track->bpm - 1.0;
            if (std::fabs (p) < std::fabs (bestPitch)) bestPitch = p;
        }
        if (std::fabs (bestPitch) > 0.5) return;                                  // out of reach even at +-50 %
        int range = (int) get (dp (d, D_RANGE));
        while (range < 2 && std::fabs (bestPitch) > kRanges[range]) ++range;
        setPlain (dp (d, D_RANGE), (float) range);
        setPlain (dp (d, D_PITCH), (float) (bestPitch / kRanges[range]));
        if (me.playing) align (d); else me.alignOnStart = true;                   // line the beats up now, or on PLAY
        notifyHost (dp (d, D_PITCH), dp (d, D_RANGE) + 1);
    }
    void align (int d)                                                            // match the other deck's beat phase
    {
        dj::Deck& me = deck[d]; dj::Deck& other = deck[1 - d];
        me.alignOnStart = false;
        if (other.track == nullptr || other.track->bpm <= 0 || me.beatFrames() <= 0) return;
        double delta = other.beatPhase (other.pos) - me.beatPhase (me.pos);
        delta -= std::floor (delta + 0.5);
        if (me.pos + delta * me.beatFrames() < 0) delta += 1.0;                  // parked on the first beat: go forward
        me.jump (me.pos + delta * me.beatFrames());
    }
    dj::DeckControls controls (int d) const
    {
        dj::DeckControls c;
        c.play = get (dp (d, D_PLAY)) > 0.5f;
        c.pitch = kRanges[(int) get (dp (d, D_RANGE))] * get (dp (d, D_PITCH));
        c.gainDb = get (dp (d, D_GAIN)); c.hiDb = get (dp (d, D_HIGH)); c.midDb = get (dp (d, D_MID)); c.lowDb = get (dp (d, D_LOW));
        c.filter = get (dp (d, D_FILTER)); c.fader = get (dp (d, D_FADER));
        return c;
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = (n - pos) < kChunk ? (n - pos) : kChunk;
            for (int d = 0; d < 2; ++d) serveRequests (d);
            // crossfader, master, input
            const double x = (get (M_XFADE) + 1.0) * 0.5;
            double ta, tb;
            if ((int) get (M_CURVE) == 0) { ta = std::cos (x * M_PI * 0.5); tb = std::sin (x * M_PI * 0.5); }
            else { ta = std::fmin (1.0, (1.0 - x) * 12.0); tb = std::fmin (1.0, x * 12.0); }
            if ((int) get (M_CURVE) == 0) { if (x <= 0.5) ta = 1.0; if (x >= 0.5) tb = 1.0; }   // smooth: both full at the centre
            const float mdb = get (M_MASTER);
            const double tm = mdb <= -59.9f ? 0.0 : std::pow (10.0, mdb / 20.0), ti = get (M_INPUT);
            const double k = 1.0 - std::exp (-(double) m / (0.01 * sr));
            xfA += (ta - xfA) * k; xfB += (tb - xfB) * k; gMaster += (tm - gMaster) * k; gInput += (ti - gInput) * k;
            if (std::fabs (xfA - ta) < 1e-6) { xfA = ta; }
            if (std::fabs (xfB - tb) < 1e-6) { xfB = tb; }
            if (std::fabs (gMaster - tm) < 1e-6) { gMaster = tm; }
            if (std::fabs (gInput - ti) < 1e-6) { gInput = ti; }
            const bool live[2] { deck[0].track != nullptr, deck[1].track != nullptr };
            if (live[0]) deck[0].render (controls (0), sr, bufA[0], bufA[1], m);
            if (live[1]) deck[1].render (controls (1), sr, bufB[0], bufB[1], m);
            float pk[2] { 0.f, 0.f };
            for (int i = 0; i < m; ++i)
            {
                const float il = inL[pos + i], ir = inR[pos + i];
                float l = il, r = ir;
                if (gInput != 1.0) { l = (float) (il * gInput); r = (float) (ir * gInput); }
                double dl = 0, dr = 0;
                if (live[0]) { dl += bufA[0][i] * xfA; dr += bufA[1][i] * xfA; }
                if (live[1]) { dl += bufB[0][i] * xfB; dr += bufB[1][i] * xfB; }
                if (live[0] || live[1]) { l = (float) (l + dl * gMaster); r = (float) (r + dr * gMaster); }
                outL[pos + i] = l; outR[pos + i] = r;
                pk[0] = std::fmax (pk[0], std::fabs (l)); pk[1] = std::fmax (pk[1], std::fabs (r));
            }
            // meters: peak with a 20 dB/s release
            const float fall = 20.f * m / sr;
            for (int d = 0; d < 2; ++d)
            {
                const float db = clampf (20.f * std::log10 ((live[d] ? deck[d].peak : 0.f) * (float) (d == 0 ? xfA : xfB) * (float) gMaster + 1e-9f), kVuFloor, kVuTop);
                vu[d] = db > vu[d] ? db : std::fmax (db, vu[d] - fall);
                const float mdbc = clampf (20.f * std::log10 (pk[d] + 1e-9f), kVuFloor, kVuTop);
                mvu[d] = mdbc > mvu[d] ? mdbc : std::fmax (mdbc, mvu[d] - fall);
            }
            displayCountdown -= m;
            if (displayCountdown <= 0) { displayCountdown += (int) (sr / 20.f); updateDisplay(); }
        }
    }

    void display (int idx, char* out, size_t max)
    {
        const ParamDef& p = kParams[idx];
        const float x = get (idx);
        if (idx < M_XFADE)
        {
            const int d = idx / kDeckParams, w = idx % kDeckParams;
            if (w == D_TRACK)
            {
                const int s = selectedIndex (d);
                pthread_mutex_lock (&libLock);
                if (s >= 0 && s < lib->count) std::snprintf (out, max, "%d. %s", s + 1, lib->e[s].name);
                else std::snprintf (out, max, "No tracks in DJ folder");
                pthread_mutex_unlock (&libLock);
                return;
            }
            if (w == D_STATUS) { std::snprintf (out, max, "%s", status[d].get()); return; }
            if (w == D_PLAY) { std::snprintf (out, max, "%s", x > 0.5f ? "Playing" : "Paused"); return; }
            if (w == D_PITCH) { std::snprintf (out, max, "%+.2f %%", (double) (x * kRanges[(int) get (dp (d, D_RANGE))] * 100.0)); return; }
        }
        if (idx == M_LIBRARY) { std::snprintf (out, max, "%s", library.get()); return; }
        if (p.kind == K_CHOICE) { std::snprintf (out, max, "%s", p.choices[(int) clampf (x, 0.f, (float) (p.n - 1))]); return; }
        if (p.kind == K_BOOL || p.kind == K_MOMENT) { std::snprintf (out, max, "%s", x >= 0.5f ? "On" : "Off"); return; }
        switch (p.fmt)
        {
            case F_DB:     std::snprintf (out, max, "%+.1f dB", (double) x); return;
            case F_EQ:     if (x <= -23.9f) std::snprintf (out, max, "Kill"); else std::snprintf (out, max, "%+.1f dB", (double) x); return;
            case F_PCT:    std::snprintf (out, max, "%.0f %%", (double) (x * 100.f)); return;
            case F_FILTER: if (std::fabs (x) <= 0.02f) { std::snprintf (out, max, "Off"); return; }
                           std::snprintf (out, max, "%s %.0f %%", x < 0 ? "LPF" : "HPF", (double) (std::fabs (x) * 100.f)); return;
            case F_TIME:   fmtTime (out, max, x, false); return;
            case F_REMAIN: fmtTime (out, max, x, true); return;
            case F_BPM:    if (x <= 0.f) std::snprintf (out, max, "--.-"); else std::snprintf (out, max, "%.1f", (double) x); return;
            case F_VU:     if (x <= kVuFloor) std::snprintf (out, max, "-inf"); else std::snprintf (out, max, "%.1f dB", (double) x); return;
            case F_XFADE:  if (std::fabs (x) < 0.02f) std::snprintf (out, max, "Center"); else std::snprintf (out, max, "%s %.0f %%", x < 0 ? "A" : "B", (double) (std::fabs (x) * 100.f)); return;
            case F_MASTER: if (x <= -59.9f) std::snprintf (out, max, "Off"); else std::snprintf (out, max, "%+.1f dB", (double) x); return;
            case F_LOADED: if (x < 0.5f) std::snprintf (out, max, "None"); else std::snprintf (out, max, "#%d", (int) x); return;
            default:       std::snprintf (out, max, "%.2f", (double) x); return;
        }
    }
};

// ------------------------------------------------------------------------------------------------ VST2 entry points
void processReplacing (AEffect* e, float** in, float** out, int32_t n)
{
    static_cast<Plugin*> (e->object)->process (in[0], in[1], out[0], out[1], n);
}
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)
{
    float tl[256], tr[256];
    Plugin* p = static_cast<Plugin*> (e->object);
    for (int32_t pos = 0; pos < n; pos += 256)
    {
        const int32_t m = (n - pos) < 256 ? (n - pos) : 256;
        p->process (in[0] + pos, in[1] + pos, tl, tr, m);
        for (int32_t i = 0; i < m; ++i) { out[0][pos + i] += tl[i]; out[1][pos + i] += tr[i]; }
    }
}
void setParameter (AEffect* e, int32_t i, float norm)
{
    if (i < 0 || i >= P_COUNT) return;
    static_cast<Plugin*> (e->object)->hostSet (i, norm);
}
float getParameter (AEffect* e, int32_t i)
{
    if (i < 0 || i >= P_COUNT) return 0.f;
    return static_cast<Plugin*> (e->object)->nv[i].load();
}
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose:            p->~Plugin(); std::free (p); return 0;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].name); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].unit); return 0;
        case effGetParamDisplay:  // MPC (a JUCE host) reads into 256-byte buffers: track names up to 47 characters
            if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[64]; p->display (idx, b, sizeof b); copyStr (ptr, b, 48); }
            return 0;
        case effCanBeAutomated:   return (idx >= 0 && idx < P_COUNT && kParams[idx].kind != K_READ && kParams[idx].kind != K_MOMENT) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) p->prepare (opt); return 0;
        case effMainsChanged:     return 0;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "Da DJ Decks", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return 1000;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;
        default: return 0;
    }
}
} // namespace

DJ_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    static bool inited = false;
    if (! inited) { initParams(); inited = true; }
    void* mem = std::calloc (1, sizeof (Plugin));   // no operator new: keeps libstdc++ out of the link
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    p->startLoader();
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic;
    fx.dispatcher = dispatcher;
    fx.process = processAccumulating;
    fx.processReplacing = processReplacing;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.numPrograms = 0;
    fx.numParams = P_COUNT;
    fx.numInputs = 2;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('D' << 24) | ('J' << 16) | ('D' << 8) | 'K';   // 'DJDK' = 0x444a444b
    fx.version = 1000;
    return &fx;
}
