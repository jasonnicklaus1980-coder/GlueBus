// Da Clip Pads - clip launcher / sample performance instrument for Akai MPC OS (Gen1, 32-bit ARM), built on the
// GlueBus / RadioReady plugin components (dependency-free VST2 core, parameter model, host notification,
// packaging, TUI.json skin pipeline). Needs only libc / libm / libpthread; MPC draws the skin from /sdcard/Synths.
//
// What the MPC plugin environment offers (and what this plugin therefore uses):
//  - VST2 instrument on a plugin track (like the JV-880 port): stereo out, MIDI notes in from the pads and the
//    sequencer (effProcessEvents), host tempo / position / play state (audioMasterGetTime), state saved with the
//    project (effGetChunk / effSetChunk).
//  - No custom drawing: the screen is a TUI.json skin whose controls are bound to parameters. Readouts (names,
//    info lines, pad states, waveform, markers, meters) are read-only parameters the plugin updates.
//  - Files: plain POSIX file access to the SD card. Samples are WAV files in /sdcard/Clips (and sub-folders);
//    kits are saved to /sdcard/Clips/Kits.
//  - No host time-stretch or sample-rate conversion API: both are done here (TimeStretch/, AudioEngine/Sample.h).
//
// Threads: the host thread (setParameter, chunks), the loader thread (scans folders, reads and converts WAVs,
// frees retired samples) and the audio thread (engine + screen readouts). The audio thread never allocates,
// frees or touches the SD card.
#include "AudioEngine/Engine.h"
#include "PresetSystem/Presets.h"
#include "State/State.h"
#include "UI/Params.h"
#include "vst2.h"
#include <dirent.h>
#include <new>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define CP_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
using namespace cp;

constexpr int kVersion = 1000;
constexpr const char* kVersionText = "1.0.0";
constexpr int kMaxFiles = 512, kMaxKits = 128;
constexpr double kMaxSampleSeconds = 360.0;              // 6 minutes per sample
constexpr double kMemoryBudget = 320.0 * 1024 * 1024;    // all loaded samples together
constexpr double kRetireGrace = 3.0;                     // seconds before a retired sample is freed

// ------------------------------------------------------------------------------------------------ library
struct Entry { char path[256]; char name[96]; };
struct Library { Entry e[kMaxFiles]; int count = 0; };
struct KitList { Entry e[kMaxKits]; int count = 0; };

const char* clipsFolder()
{
    const char* e = std::getenv ("CLIPS_FOLDER");
    return e != nullptr && *e != 0 ? e : "/sdcard/Clips";
}
bool hasExt (const char* n, const char* ext)
{
    const size_t l = std::strlen (n), le = std::strlen (ext);
    return l > le && n[0] != '.' && strcasecmp (n + l - le, ext) == 0;
}
void scanFolder (Library& lib, const char* dir, const char* prefix, int depth)
{
    DIR* d = opendir (dir);
    if (d == nullptr) return;
    while (dirent* de = readdir (d))
    {
        if (de->d_name[0] == '.' || lib.count >= kMaxFiles) continue;
        if (depth == 0 && std::strcmp (de->d_name, "Kits") == 0) continue;
        char full[512]; std::snprintf (full, sizeof full, "%s/%s", dir, de->d_name);
        if (hasExt (de->d_name, ".wav"))
        {
            Entry& e = lib.e[lib.count++];
            std::snprintf (e.path, sizeof e.path, "%s%s", prefix, de->d_name);
            std::snprintf (e.name, sizeof e.name, "%s", e.path);
            e.name[std::strlen (e.name) - 4] = 0;
        }
        else if (depth < 2)
        {
            DIR* sub = opendir (full);
            if (sub != nullptr) { closedir (sub); char pre[256]; std::snprintf (pre, sizeof pre, "%s%s/", prefix, de->d_name); scanFolder (lib, full, pre, depth + 1); }
        }
    }
    closedir (d);
}
void scanKits (KitList& kl)
{
    kl.count = 0;
    char dir[512]; std::snprintf (dir, sizeof dir, "%s/Kits", clipsFolder());
    DIR* d = opendir (dir);
    if (d == nullptr) return;
    while (dirent* de = readdir (d))
        if (hasExt (de->d_name, ".dcpkit") && kl.count < kMaxKits)
        {
            Entry& e = kl.e[kl.count++];
            std::snprintf (e.path, sizeof e.path, "%s/%s", dir, de->d_name);
            std::snprintf (e.name, sizeof e.name, "%s", de->d_name);
            e.name[std::strlen (e.name) - 7] = 0;
        }
    closedir (d);
}
int cmpEntry (const void* a, const void* b) { return strcasecmp (((const Entry*) a)->name, ((const Entry*) b)->name); }

// ------------------------------------------------------------------------------------------------ plugin
struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> v[P_COUNT], nv[P_COUNT];
    float sent[P_COUNT];
    int textGen[P_COUNT];
    Text texts[P_COUNT];
    bool notifying = false;
    float sr = 44100.f;

    ClipSettings set[kClips];
    Rel<int> sceneMask[kScenes];
    Engine eng;
    Rel<float> curTempo { 90.f };
    Rel<float> focusPos { 0.f };

    // library / kits (loader writes, host thread reads under the lock)
    pthread_mutex_t libLock;
    Library* lib = nullptr; Library* libSpare = nullptr; KitList* kits = nullptr; KitList* kitsSpare = nullptr;
    std::atomic<int> libCount { 0 }, kitCount { 0 };

    // loader thread
    pthread_t loader {}; bool loaderRunning = false;
    std::atomic<bool> quit { false }, rescanReq { true };
    pthread_mutex_t reqLock;
    char reqPath[kClips][256] {}; int reqGen[kClips] {}, doneGen[kClips] {};
    std::atomic<int> loadingMask { 0 }, failedMask { 0 };
    std::atomic<double> liveBytes { 0.0 };
    struct Retired { Sample* s; double at; };
    Retired retired[128] {}; int nRetired = 0;

    // status line (host thread writes, audio thread shows it)
    pthread_mutex_t msgLock;
    char msg[96] {}; double msgAt = -100.0;

    // FLIP undo, chunk
    ClipSettings undo; int undoClip = -1;
    StateWriter chunk;
    Rng rng;
    int displayCountdown = 0;
    int presetCount = 0;
    struct ThumbKey { const Sample* s = nullptr; float a = -1, b = -1; int rev = -1, norm = -1; bool operator!= (const ThumbKey& o) const { return s != o.s || a != o.a || b != o.b || rev != o.rev || norm != o.norm; } };
    ThumbKey thumbKey[kClips], waveKey;
    double waveW0 = -1, waveW = -1;

    Plugin()
    {
        for (int i = 0; i < P_COUNT; ++i) { setPlain (i, params()[i].def); sent[i] = 1e9f; textGen[i] = 0; }
        for (int c = 0; c < kClips; ++c) { set[c].note = 36 + c; setPlain (P_CVOL0 + c, set[c].vol); setPlain (P_CPAN0 + c, set[c].pan); }
        for (auto& m : sceneMask) m = 0;
        eng.set = set; eng.sceneMask = sceneMask;
        pthread_mutex_init (&libLock, nullptr); pthread_mutex_init (&reqLock, nullptr); pthread_mutex_init (&msgLock, nullptr);
        lib = (Library*) std::calloc (1, sizeof (Library)); libSpare = (Library*) std::calloc (1, sizeof (Library));
        kits = (KitList*) std::calloc (1, sizeof (KitList)); kitsSpare = (KitList*) std::calloc (1, sizeof (KitList));
        factoryPresets (presetCount);
        rng.seed ((uint32_t) (nowSeconds() * 1000003.0));
        eng.rng.seed (rng.next());
        texts[P_STATUS].publish ("Tap a pad, then EDIT to load a sample into it");
        hann();                                                                  // build the grain window table now
        prepare (44100.f);
        refreshSelected (false);
    }
    ~Plugin()
    {
        stopLoader();
        for (int c = 0; c < kClips; ++c) { eng.setSample (c, nullptr); freeSample (eng.pending[c].exchange (nullptr)); }
        for (auto& r : eng.retireRing) freeSample (r.exchange (nullptr));
        for (int i = 0; i < nRetired; ++i) freeSample (retired[i].s);
        std::free (lib); std::free (libSpare); std::free (kits); std::free (kitsSpare);
        pthread_mutex_destroy (&libLock); pthread_mutex_destroy (&reqLock); pthread_mutex_destroy (&msgLock);
    }
    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (params()[i], plain)); }
    float get (int i) const { return v[i].load(); }
    int sel() const { return clampi ((int) get (P_SEL), 0, kClips - 1); }
    void prepare (float rate)
    {
        const bool changed = std::fabs (rate - sr) > 0.5f;
        sr = rate; eng.prepare (rate);
        if (changed) for (int c = 0; c < kClips; ++c) if (set[c].file[0]) requestLoad (c, set[c].file);   // convert again
    }
    void status (const char* fmt, ...) __attribute__ ((format (printf, 2, 3)))
    {
        char b[96]; va_list ap; va_start (ap, fmt); std::vsnprintf (b, sizeof b, fmt, ap); va_end (ap);
        pthread_mutex_lock (&msgLock);
        std::snprintf (msg, sizeof msg, "%s", b); msgAt = nowSeconds();
        pthread_mutex_unlock (&msgLock);
    }
    const Sample* uiSmp (int c) const { return eng.uiSample[c].load(); }

    // ---------------------------------------------------------------------------- loader thread
    static void* loaderMain (void* arg) { static_cast<Plugin*> (arg)->loaderLoop(); return nullptr; }
    void startLoader() { loaderRunning = pthread_create (&loader, nullptr, loaderMain, this) == 0; }
    void stopLoader() { if (loaderRunning) { quit.store (true); pthread_join (loader, nullptr); loaderRunning = false; } }
    void requestLoad (int c, const char* pathIn)
    {
        char path[256]; std::snprintf (path, sizeof path, "%s", pathIn);
        pthread_mutex_lock (&reqLock);
        std::snprintf (reqPath[c], sizeof reqPath[c], "%s", path);
        ++reqGen[c];
        pthread_mutex_unlock (&reqLock);
        loadingMask.fetch_or (1 << c); failedMask.fetch_and (~(1 << c));
        if (! loaderRunning) serveLoads();                                          // tests without the thread
    }
    void rescan()
    {
        libSpare->count = 0;
        scanFolder (*libSpare, clipsFolder(), "", 0);
        std::qsort (libSpare->e, (size_t) libSpare->count, sizeof (Entry), cmpEntry);
        scanKits (*kitsSpare);
        std::qsort (kitsSpare->e, (size_t) kitsSpare->count, sizeof (Entry), cmpEntry);
        pthread_mutex_lock (&libLock);
        Library* t = lib; lib = libSpare; libSpare = t;
        KitList* k = kits; kits = kitsSpare; kitsSpare = k;
        libCount.store (lib->count); kitCount.store (kits->count);
        pthread_mutex_unlock (&libLock);
    }
    void freeRetired (bool all)
    {
        const double now = nowSeconds();
        for (auto& r : eng.retireRing)
            if (Sample* s = r.exchange (nullptr))
            {
                if (nRetired < 128) retired[nRetired++] = { s, now };
                else { liveBytes.store (liveBytes.load() - (double) s->frames * 4); freeSample (s); }   // fallback
            }
        int k = 0;
        for (int i = 0; i < nRetired; ++i)
        {
            if (all || now - retired[i].at > kRetireGrace) { liveBytes.store (liveBytes.load() - (double) retired[i].s->frames * 4); freeSample (retired[i].s); }
            else retired[k++] = retired[i];
        }
        nRetired = k;
    }
    void serveLoads()
    {
        for (int c = 0; c < kClips; ++c)
        {
            pthread_mutex_lock (&reqLock);
            const int g = reqGen[c];
            char path[256]; std::snprintf (path, sizeof path, "%s", reqPath[c]);
            const bool want = g != doneGen[c];
            pthread_mutex_unlock (&reqLock);
            if (! want) continue;
            char full[600];
            if (path[0] == '/') std::snprintf (full, sizeof full, "%s", path);
            else std::snprintf (full, sizeof full, "%s/%s", clipsFolder(), path);
            const char* err = nullptr;
            Sample* s = nullptr;
            if (liveBytes.load() > kMemoryBudget) err = "sample memory full (clear some clips)";
            else s = loadSample (full, path, sr, kMaxSampleSeconds, &err);
            pthread_mutex_lock (&reqLock);
            const bool current = reqGen[c] == g;
            doneGen[c] = g;
            pthread_mutex_unlock (&reqLock);
            if (s != nullptr && current)
            {
                liveBytes.store (liveBytes.load() + (double) s->frames * 4);
                if (Sample* old = eng.pending[c].exchange (s)) { liveBytes.store (liveBytes.load() - (double) old->frames * 4); freeSample (old); }
                loadingMask.fetch_and (~(1 << c));
            }
            else
            {
                freeSample (s);
                if (current)
                {
                    loadingMask.fetch_and (~(1 << c)); failedMask.fetch_or (1 << c);
                    status ("Clip %d: can't load %s (%s)", c + 1, path, err ? err : "error");
                }
            }
        }
    }
    void loaderLoop()
    {
        while (! quit.load())
        {
            freeRetired (false);
            if (rescanReq.exchange (false)) rescan();
            serveLoads();
            usleep (10000);
        }
        freeRetired (true);
    }

    // ---------------------------------------------------------------------------- host notification
    void notify (int i)
    {
        if (master == nullptr || notifying) return;
        notifying = true;
        master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
        notifying = false;
    }
    void notifyRange (int from, int to)
    {
        if (master == nullptr || notifying) return;
        notifying = true;
        for (int i = from; i < to; ++i) { const Kind k = params()[i].kind; if (k != K_READ && k != K_TEXT) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load()); }
        master (&fx, audioMasterUpdateDisplay, 0, 0, nullptr, 0.f);
        notifying = false;
    }
    void setAndNotify (int i, float plain) { setPlain (i, plain); notify (i); }

    // ---------------------------------------------------------------------------- selected-clip proxies
    float proxyGet (int i, int c) const
    {
        const ClipSettings& s = set[c];
        switch (i)
        {
            case P_MODE: return (float) (int) s.mode; case P_CQUANT: return (float) (int) s.quant; case P_REVERSE: return (float) (int) s.reverse;
            case P_SEMI: return (float) (int) s.semis; case P_CENT: return (float) (int) s.cents; case P_PMODE: return (float) (int) s.pmode;
            case P_SLEN: return (float) (int) s.slen; case P_STYPE: return (float) (int) s.stype;
            case P_START: return s.start; case P_END: return s.end; case P_LSTART: return s.lstart; case P_LEND: return s.lend;
            case P_FADEIN: return s.fadeIn; case P_FADEOUT: return s.fadeOut; case P_XFADE: return s.xfade;
            case P_NORMALIZE: return (float) (int) s.normalize; case P_FILT: return (float) (int) s.filt; case P_CUT: return s.cutoff;
            case P_RES: return s.res; case P_DRIVE: return s.drive; case P_RSEND: return s.rsend; case P_DSEND: return s.dsend;
            case P_VINTAGE: return (float) (int) s.vintage; case P_FOLLOW: return (float) (int) s.follow; case P_FTIME: return (float) (int) s.ftime;
            case P_FSCENE: return (float) (int) s.fscene; case P_NOTE: return (float) (int) s.note; case P_SEQ: return (float) (int) s.seqOn;
            default:
                if (i >= P_INSCENE0 && i <= P_INSCENE3) return (float) ((sceneMask[i - P_INSCENE0] >> c) & 1);
                return 0.f;
        }
    }
    void proxySet (int i, int c, float x)
    {
        ClipSettings& s = set[c];
        const int n = (int) std::lround (x);
        switch (i)
        {
            case P_MODE: s.mode = n; break; case P_CQUANT: s.quant = n; break; case P_REVERSE: s.reverse = n; break;
            case P_SEMI: s.semis = n; break; case P_CENT: s.cents = n; break; case P_PMODE: s.pmode = n; break;
            case P_SLEN: s.slen = n; break; case P_STYPE: s.stype = n; break;
            case P_START: s.start = x; focusPos = x; break; case P_END: s.end = x; focusPos = x; break;
            case P_LSTART: s.lstart = x; focusPos = x; break; case P_LEND: s.lend = x; focusPos = x; break;
            case P_FADEIN: s.fadeIn = x; break; case P_FADEOUT: s.fadeOut = x; break; case P_XFADE: s.xfade = x; break;
            case P_NORMALIZE: s.normalize = n; break; case P_FILT: s.filt = n; break; case P_CUT: s.cutoff = x; break;
            case P_RES: s.res = x; break; case P_DRIVE: s.drive = x; break; case P_RSEND: s.rsend = x; break; case P_DSEND: s.dsend = x; break;
            case P_VINTAGE: s.vintage = n; break; case P_FOLLOW: s.follow = n; break; case P_FTIME: s.ftime = n; break;
            case P_FSCENE: s.fscene = n; break; case P_NOTE: s.note = n; break; case P_SEQ: s.seqOn = n; break;
            default:
                if (i >= P_INSCENE0 && i <= P_INSCENE3)
                {
                    const int sc = i - P_INSCENE0;
                    sceneMask[sc] = n ? ((int) sceneMask[sc] | (1 << c)) : ((int) sceneMask[sc] & ~(1 << c));
                }
        }
    }
    int sliceSel() const { return clampi ((int) get (P_SLICE), 0, kMaxSlices - 1); }
    void refreshSlice (bool tell)
    {
        const ClipSettings& s = set[sel()];
        const int n = s.sliceCount, k = std::min (sliceSel(), std::max (0, n - 1));
        setPlain (P_SLICE, (float) k);
        setPlain (P_SLICEPOS, n > 0 ? (float) s.slice[k] : 0.f);
        setPlain (P_SLICEREV, n > 0 ? (float) (((int) s.sliceRev >> k) & 1) : 0.f);
        setPlain (P_SLICEPITCH, n > 0 ? (float) (int) s.slicePitch[k] : 0.f);
        if (tell) { notify (P_SLICE); notify (P_SLICEPOS); notify (P_SLICEREV); notify (P_SLICEPITCH); }
    }
    void refreshSelected (bool tell)
    {
        const int c = sel();
        for (int i = P_MODE; i <= P_SEQ; ++i) setPlain (i, proxyGet (i, c));
        refreshSlice (false);
        if (tell) { notifyRange (P_MODE, P_SEQ + 1); notify (P_SEL); notify (P_SLICE); notify (P_SLICEPOS); notify (P_SLICEREV); notify (P_SLICEPITCH); }
    }
    void syncClipParams (bool tell)                                           // per-clip mixer params from the settings
    {
        for (int c = 0; c < kClips; ++c)
        {
            setPlain (P_CVOL0 + c, set[c].vol); setPlain (P_CPAN0 + c, set[c].pan); setPlain (P_CMUTE0 + c, (float) (int) set[c].mute);
            if (tell) { notify (P_CVOL0 + c); notify (P_CPAN0 + c); notify (P_CMUTE0 + c); }
        }
    }
    void select (int c)
    {
        if (c == sel()) return;
        setPlain (P_SEL, (float) c);
        refreshSelected (true);
    }

    // ---------------------------------------------------------------------------- parameter changes
    void hostSet (int i, float norm)
    {
        const ParamDef& p = params()[i];
        if (p.kind == K_READ || p.kind == K_TEXT) return;
        const float old = get (i), plain = toPlain (p, norm);
        if (p.kind == K_MOMENT)
        {
            if (notifying || plain < 0.5f || old >= 0.5f) { setPlain (i, plain); return; }
            setPlain (i, 1.f);
            action (i);
            setPlain (i, 0.f);                          // spring back, and tell MPC so the next press is a press again
            if (master != nullptr && ! notifying) { notifying = true; master (&fx, audioMasterAutomate, i, 0, nullptr, 0.f); notifying = false; }
            return;
        }
        if (p.kind == K_LIST) { nv[i].store (clampf (norm, 0.f, 1.f)); v[i].store (plain); return; }
        setPlain (i, plain);
        if (isProxy (i)) { proxySet (i, sel(), plain); return; }
        if (i == P_SEL) { refreshSelected (! notifying); return; }
        if (i >= P_PM_PLAY && i <= P_PM_MUTE)                                 // radio buttons
        {
            for (int k = P_PM_PLAY; k <= P_PM_MUTE; ++k) if (k != i) { setPlain (k, 0.f); notify (k); }
            if (plain < 0.5f) setAndNotify (i, 1.f);
            return;
        }
        if (i >= P_CVOL0 && i < P_CVOL0 + kClips) { set[i - P_CVOL0].vol = plain; return; }
        if (i >= P_CPAN0 && i < P_CPAN0 + kClips) { set[i - P_CPAN0].pan = plain; return; }
        if (i >= P_CMUTE0 && i < P_CMUTE0 + kClips) { set[i - P_CMUTE0].mute = plain > 0.5f ? 1 : 0; return; }
        if (i == P_SLICE) { refreshSlice (! notifying); return; }
        if (i == P_SLICEPOS)
        {
            ClipSettings& s = set[sel()]; const int k = sliceSel();
            if (s.sliceCount <= 0) return;
            if (k == 0) { s.start = plain; setAndNotify (P_START, plain); }
            moveSlice (s, k, plain);
            if (k == 0) s.slice[0] = plain;
            setPlain (P_SLICEPOS, s.slice[k]); focusPos = s.slice[k];
            return;
        }
        if (i == P_SLICEREV) { ClipSettings& s = set[sel()]; const int k = sliceSel(); s.sliceRev = plain > 0.5f ? ((int) s.sliceRev | (1 << k)) : ((int) s.sliceRev & ~(1 << k)); return; }
        if (i == P_SLICEPITCH) { set[sel()].slicePitch[sliceSel()] = (int) plain; return; }
        if (i == P_LEARN) { if (plain > 0.5f) { eng.learnNote.store (-1); status ("MIDI LEARN: hit a pad or key for clip %d", sel() + 1); } return; }
        if (i == P_SMP_PRESET) { applySamplerPreset ((int) plain); return; }
        if (i >= P_BITS && i <= P_SMP_OUT && ! notifying && (int) get (P_SMP_PRESET) != 0) setAndNotify (P_SMP_PRESET, 0.f);   // now Custom
    }
    void applySamplerPreset (int k)
    {
        int n = 0; const VintagePreset* vp = vintagePresets (n);
        if (k < 1 || k > n) return;
        const VintageParams& q = vp[k - 1].p;
        const float vals[] { q.bits, q.rate, q.aa, q.quantize, q.sat, q.noise, q.crackle, q.outDb };
        for (int j = 0; j < 8; ++j) { setPlain (P_BITS + j, vals[j]); notify (P_BITS + j); }
    }

    // ---------------------------------------------------------------------------- button actions
    int libSelected() const
    {
        const int n = libCount.load();
        if (n <= 0) return -1;
        const int i = (int) (get (P_FILE) * n);
        return i >= n ? n - 1 : i;
    }
    int kitSelected() const
    {
        const int n = kitCount.load();
        if (n <= 0) return -1;
        const int i = (int) (get (P_KIT) * n);
        return i >= n ? n - 1 : i;
    }
    void action (int i)
    {
        const int c = sel();
        if (i >= P_PAD0 && i < P_PAD0 + kClips) { padPress (i - P_PAD0); return; }
        if (i >= P_SPAD0 && i < P_SPAD0 + kClips) { eng.sliceReq.store ((int) get (P_SLICEBANK) * 16 + (i - P_SPAD0)); return; }
        if (i >= P_SCENE0 && i <= P_SCENE3) { scenePress (i - P_SCENE0); return; }
        switch (i)
        {
            case P_STOPALL: eng.stopAllReq.store (true); break;
            case P_RESTART: eng.restartReq.store (true); status ("Restart on the next bar"); break;
            case P_RETRIGALL: eng.retrigAllReq.store (true); break;
            case P_RESCAN: rescanReq.store (true); status ("Scanning %s ...", clipsFolder()); break;
            case P_PREVIEW: eng.previewReq.store (c); break;
            case P_LOAD:
            {
                const int k = libSelected();
                if (k < 0) { status ("No WAV files in %s", clipsFolder()); break; }
                char path[256];
                pthread_mutex_lock (&libLock); std::snprintf (path, sizeof path, "%s", lib->e[k].path); pthread_mutex_unlock (&libLock);
                std::snprintf (set[c].file, sizeof set[c].file, "%s", path);
                set[c].start = 0.f; set[c].end = 1.f; set[c].lstart = 0.f; set[c].lend = 1.f; set[c].sliceCount = 0; set[c].seqOn = 0;
                refreshSelected (true);
                requestLoad (c, path);
                status ("Clip %d: loading %s", c + 1, path);
                break;
            }
            case P_CLEAR:
                set[c].file[0] = 0; set[c].resetSound();
                eng.killReq[c].store (1);
                for (auto& m : sceneMask) m = (int) m & ~(1 << c);
                loadingMask.fetch_and (~(1 << c)); failedMask.fetch_and (~(1 << c));
                syncClipParams (true); refreshSelected (true);
                status ("Clip %d cleared", c + 1);
                break;
            case P_TRIM:
            {
                const Sample* s = uiSmp (c);
                if (s == nullptr) { status ("Load a sample first"); break; }
                const float a = (float) s->trimStart / s->frames, b = (float) s->trimEnd / s->frames;
                const float lo = std::fmin (set[c].start, set[c].end), hi = std::fmax (set[c].start, set[c].end);
                set[c].start = std::fmax (lo, a); set[c].end = std::fmin (hi, b);
                if (set[c].end <= set[c].start) { set[c].start = a; set[c].end = b; }
                set[c].lstart = std::fmax ((float) set[c].lstart, (float) set[c].start); set[c].lend = std::fmin ((float) set[c].lend, (float) set[c].end);
                refreshSelected (true);
                status ("Trimmed the silence off clip %d", c + 1);
                break;
            }
            case P_AUTOFADE: set[c].fadeIn = 5.f; set[c].fadeOut = 30.f; refreshSelected (true); status ("Fades: 5 ms in, 30 ms out"); break;
            case P_AUTOXF: set[c].xfade = 25.f; refreshSelected (true); status ("Loop crossfade: 25 ms"); break;
            case P_CHOP: doChop (c); break;
            case P_ASSIGN: padAssign (c); break;
            case P_FLIP: flip (c); break;
            case P_UNDO: undoFlip(); break;
            case P_PRESETLOAD: loadPreset ((int) get (P_PRESET)); break;
            case P_KITLOAD: loadKit(); break;
            case P_KITSAVE: saveKit(); break;
            default: break;
        }
    }
    int padMode() const
    {
        if (get (P_PM_STOP) > 0.5f) return 1;
        if (get (P_PM_SELECT) > 0.5f) return 2;
        if (get (P_PM_MUTE) > 0.5f) return 3;
        return 0;
    }
    void padPress (int c)
    {
        switch (padMode())
        {
            case 1: eng.stopReq[c].store (1); break;
            case 2: select (c); break;
            case 3: set[c].mute = set[c].mute ? 0 : 1; setAndNotify (P_CMUTE0 + c, (float) (int) set[c].mute); break;
            default:
                select (c);
                if (set[c].file[0] == 0) status ("Clip %d is empty: open EDIT, pick a sample, press LOAD", c + 1);
                else eng.trigReq[c].fetch_add (1);
                break;
        }
    }
    void scenePress (int s)
    {
        if (get (P_SCENESTORE) > 0.5f)
        {
            int mask = 0, n = 0;
            for (int c = 0; c < kClips; ++c) if (eng.view[c].playing || eng.run[c].qStart) { mask |= 1 << c; ++n; }
            sceneMask[s] = mask;
            setAndNotify (P_SCENESTORE, 0.f);
            refreshSelected (true);
            status ("Scene %d stored: %d clip%s", s + 1, n, n == 1 ? "" : "s");
            return;
        }
        if (sceneMask[s] == 0) { status ("Scene %d is empty: play some clips, press STORE, then the scene", s + 1); return; }
        eng.sceneReq.store (s);
    }

    // ---------------------------------------------------------------------------- CHOP / PAD ASSIGN
    void doChop (int c)
    {
        const Sample* s = uiSmp (c);
        if (s == nullptr) { status ("Load a sample into clip %d first", c + 1); return; }
        const int n = chop (set[c], s, chopCount ((int) get (P_CHOPN)), (int) get (P_CHOPMODE));
        if (n == 0) { status ("Region too short to chop"); return; }
        setPlain (P_SLICE, 0.f);
        refreshSelected (true);
        status ("Clip %d chopped into %d slices (%s)", c + 1, n, (int) get (P_CHOPMODE) == CH_TRANSIENT ? "transients" : "equal");
    }
    void padAssign (int c)
    {
        const ClipSettings& src = set[c];
        const int n = src.sliceCount;
        if (n <= 0 || uiSmp (c) == nullptr) { status ("CHOP the clip first"); return; }
        int done = 0, first = -1, last = -1;
        for (int k = 0; k < n; ++k)
        {
            int j = -1;
            for (int t = 0; t < kClips; ++t) if (t != c && set[t].file[0] == 0 && ! ((loadingMask.load() >> t) & 1)) { j = t; break; }
            if (j < 0) break;
            ClipSettings& d = set[j];
            d.copySound (src);
            std::snprintf (d.file, sizeof d.file, "%s", src.file);
            d.start = src.slice[k]; d.end = src.slice[k + 1]; d.lstart = d.start; d.lend = d.end;
            d.mode = M_ONESHOT; d.quant = 1;                                     // one-shot, no launch quantise
            d.semis = clampi ((int) src.semis + (int) src.slicePitch[k], -12, 12);
            if ((src.sliceRev >> k) & 1) d.reverse = d.reverse ? 0 : 1;
            d.sliceCount = 0; d.seqOn = 0; d.follow = FA_OFF;
            eng.shareReq[j].store (c + 1);
            if (first < 0) first = j;
            last = j; ++done;
        }
        syncClipParams (true);
        if (done == 0) status ("No empty clips to assign slices to (CLEAR some first)");
        else status ("%d slice%s assigned to clips %d-%d", done, done == 1 ? "" : "s", first + 1, last + 1);
    }

    // ---------------------------------------------------------------------------- FLIP
    void flip (int c)
    {
        const Sample* smp = uiSmp (c);
        if (smp == nullptr) { status ("Load a sample into clip %d first", c + 1); return; }
        ClipSettings& s = set[c];
        undo.copySound (s); undoClip = c;
        const bool lp = get (P_LOCK_PITCH) > 0.5f, lr = get (P_LOCK_REV) > 0.5f, lpos = get (P_LOCK_POS) > 0.5f;
        const bool lf = get (P_LOCK_FILT) > 0.5f, lsl = get (P_LOCK_SLICES) > 0.5f;
        enum { O_REV, O_PITCH, O_START, O_END, O_STUTTER, O_REPEAT, O_HALF, O_DOUBLE, O_FILTER, O_CHOP, O_REARRANGE, O_N };
        static const char* const names[] { "REVERSE", "PITCH", "START", "END", "STUTTER", "REPEAT", "HALF SPEED", "DOUBLE SPEED", "FILTER", "RANDOM CHOP", "REARRANGE" };
        bool allowed[O_N] { ! lr, ! lp, ! lpos, ! lpos, ! lpos, ! lpos, ! lp, ! lp, ! lf, ! lpos, ! lsl };
        int pool[O_N], np = 0;
        for (int k = 0; k < O_N; ++k) if (allowed[k]) pool[np++] = k;
        if (np == 0) { status ("Everything is locked"); return; }
        auto ensureSlices = [&]
        {
            if ((int) s.sliceCount >= 2) return true;
            return chop (s, smp, 8, CH_TRANSIENT) > 0;
        };
        const float lo = std::fmin (s.start, s.end), hi = std::fmax (s.start, s.end), len = hi - lo;
        auto snap = [&] (float x)                                                  // to the nearest onset within 30 ms
        {
            const double f = x * smp->frames, win = 0.03 * smp->rate;
            double best = f, bd = win;
            for (int k = 0; k < smp->onsetCount; ++k) { const double d = std::fabs (smp->onset[k] - f); if (d < bd) { bd = d; best = (double) smp->onset[k]; } }
            return (float) (best / smp->frames);
        };
        const int want = 2 + rng.below (3);
        bool pitchDone = false, loopDone = false, used[O_N] {};
        char desc[96] = "FLIP:"; int ops = 0;
        for (int tries = 0; tries < 40 && ops < want; ++tries)
        {
            const int o = pool[rng.below (np)];
            if (used[o]) continue;
            const bool isPitch = o == O_PITCH || o == O_HALF || o == O_DOUBLE;
            const bool isLoop = o == O_STUTTER || o == O_REPEAT || o == O_CHOP || o == O_REARRANGE;
            if ((isPitch && pitchDone) || (isLoop && loopDone)) continue;
            bool ok = true;
            switch (o)
            {
                case O_REV: s.reverse = s.reverse ? 0 : 1; break;
                case O_PITCH: { static const int k[] { -12, -7, -5, -3, 3, 5, 7, 12 }; s.semis = k[rng.below (8)]; break; }
                case O_START: s.start = snap (clampf (lo + len * (rng.uni() * 0.25f), 0.f, hi - 0.01f * len)); s.end = hi; break;
                case O_END: s.end = snap (clampf (hi - len * (rng.uni() * 0.25f), lo + 0.01f * len, 1.f)); s.start = lo; break;
                case O_STUTTER:
                {
                    const double beats = rng.uni() < 0.5f ? 0.25 : 0.125;
                    const double frames = beats * 60.0 / std::fmax (40.f, (float) curTempo) * smp->rate;
                    const float w = (float) (frames / smp->frames);
                    float st = snap (lo + len * rng.uni() * 0.75f);
                    if (st + w > hi) st = hi - w;
                    s.lstart = std::fmax (lo, st); s.lend = std::fmin (hi, s.lstart + w);
                    if (s.mode == M_ONESHOT) s.mode = M_LOOP;
                    s.xfade = 3.f; s.seqOn = 0;
                    break;
                }
                case O_REPEAT:
                case O_CHOP:
                {
                    if (! ensureSlices()) { ok = false; break; }
                    const int n = s.sliceCount, k = rng.below (n), span = o == O_CHOP ? 1 + rng.below (std::min (3, n - k)) : 1;
                    if (o == O_REPEAT) { s.lstart = s.slice[k]; s.lend = s.slice[k + 1]; if (s.mode == M_ONESHOT) s.mode = M_LOOP; }
                    else { s.start = s.slice[k]; s.end = s.slice[k + span]; s.lstart = s.start; s.lend = s.end; }
                    s.seqOn = 0;
                    break;
                }
                case O_HALF: s.pmode = PM_RESAMPLE; s.semis = -12; break;
                case O_DOUBLE: s.pmode = PM_RESAMPLE; s.semis = 12; break;
                case O_FILTER:
                {
                    const int t = 1 + rng.below (3);
                    s.filt = t;
                    s.cutoff = t == FT_LP ? 400.f * std::pow (15.f, rng.uni()) : (t == FT_HP ? 150.f * std::pow (15.f, rng.uni()) : 300.f * std::pow (12.f, rng.uni()));
                    s.res = 0.1f + 0.5f * rng.uni();
                    break;
                }
                case O_REARRANGE:
                {
                    if (! ensureSlices()) { ok = false; break; }
                    const int n = s.sliceCount;
                    int perm[kMaxSlices]; for (int k = 0; k < n; ++k) perm[k] = k;
                    if (rng.uni() < 0.5f) for (int k = n - 1; k > 0; --k) { const int j = rng.below (k + 1); const int t = perm[k]; perm[k] = perm[j]; perm[j] = t; }
                    else for (int k = 0; k < n; ++k) perm[k] = rng.uni() < 0.6f ? k : rng.below (n);   // repeats allowed
                    for (int k = 0; k < n; ++k) s.seq[k] = perm[k];
                    s.seqOn = 1;
                    break;
                }
            }
            if (! ok) { used[o] = true; continue; }
            used[o] = true; ++ops;
            if (isPitch) pitchDone = true;
            if (isLoop) loopDone = true;
            char part[32];
            if (o == O_PITCH) std::snprintf (part, sizeof part, " %+d st", (int) s.semis);
            else std::snprintf (part, sizeof part, " %s", names[o]);
            if (std::strlen (desc) + std::strlen (part) + 2 < sizeof desc) { if (ops > 1) std::strcat (desc, " +"); std::strcat (desc, part); }
        }
        refreshSelected (true);
        status ("%s", ops ? desc : "FLIP: nothing changed (check the locks)");
    }
    void undoFlip()
    {
        if (undoClip < 0) { status ("Nothing to undo"); return; }
        ClipSettings tmp; tmp.copySound (set[undoClip]);
        set[undoClip].copySound (undo);
        undo.copySound (tmp);                                                   // pressing again redoes
        if (undoClip != sel()) select (undoClip); else refreshSelected (true);
        status ("Flip undone on clip %d (press again to redo)", undoClip + 1);
    }

    // ---------------------------------------------------------------------------- state / presets / kits
    int findParam (const char* key) const
    {
        for (int i = 0; i < P_COUNT; ++i) if (std::strcmp (params()[i].key, key) == 0) return i;
        return -1;
    }
    void serialize (StateWriter& w)
    {
        w.len = 0; if (w.buf) w.buf[0] = 0;
        w.add ("DaClipPads 1");
        for (int i = 0; i < P_COUNT; ++i)
            if (params()[i].save) w.add ("p.%s=%.6g", params()[i].key, (double) get (i));
        for (int c = 0; c < kClips; ++c) writeClip (w, c, set[c]);
        for (int s = 0; s < kScenes; ++s) w.add ("scene%d=%d", s, (int) sceneMask[s]);
    }
    void applyState (const char* text, size_t len, bool full)
    {
        float before[P_COUNT]; for (int i = 0; i < P_COUNT; ++i) before[i] = nv[i].load();
        if (full)
        {
            for (int c = 0; c < kClips; ++c) { set[c].resetSound(); set[c].file[0] = 0; set[c].note = 36 + c; }
            for (auto& m : sceneMask) m = 0;
        }
        parseState (text, len, [&] (const char* k, const char* val)
        {
            if (! std::strncmp (k, "p.", 2))
            {
                const int i = findParam (k + 2);
                if (i >= 0 && params()[i].save) setPlain (i, clampf ((float) std::atof (val), params()[i].lo, params()[i].hi));
            }
            else if (k[0] == 'c' && k[1] >= '0' && k[1] <= '9')
            {
                const int c = toInt (k + 1); const char* dot = std::strchr (k, '.');
                if (dot && c >= 0 && c < kClips) readClipField (set[c], dot + 1, val);
            }
            else if (! std::strncmp (k, "all.", 4)) { for (auto& c : set) readClipField (c, k + 4, val); }
            else if (! std::strncmp (k, "row", 3))
            {
                const int r = toInt (k + 3); const char* dot = std::strchr (k, '.');
                if (dot && r >= 0 && r < 4) for (int c = 4 * r; c < 4 * r + 4; ++c) readClipField (set[c], dot + 1, val);
            }
            else if (! std::strncmp (k, "scene", 5)) { const int s = toInt (k + 5); if (s >= 0 && s < kScenes) sceneMask[s] = toInt (val) & 0xffff; }
        });
        if (full)
        {
            eng.stopAllReq.store (true);
            for (int c = 0; c < kClips; ++c)
            {
                if (set[c].file[0]) requestLoad (c, set[c].file);
                else { eng.killReq[c].store (1); loadingMask.fetch_and (~(1 << c)); failedMask.fetch_and (~(1 << c)); }
            }
        }
        syncClipParams (false);
        refreshSelected (false);
        if (master != nullptr && ! notifying)                                    // tell MPC what changed
        {
            notifying = true;
            for (int i = 0; i < P_COUNT; ++i)
            {
                const Kind kd = params()[i].kind;
                if (kd == K_READ || kd == K_TEXT || kd == K_MOMENT) continue;
                if (std::fabs (nv[i].load() - before[i]) > 1e-7f) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
            }
            master (&fx, audioMasterUpdateDisplay, 0, 0, nullptr, 0.f);
            notifying = false;
        }
    }
    void loadPreset (int k)
    {
        int n = 0; const FactoryPreset* fp = factoryPresets (n);
        if (k < 0 || k >= n) return;
        applyState (fp[k].text, std::strlen (fp[k].text), false);
        status ("Preset: %s / %s", fp[k].category, fp[k].name);
    }
    void saveKit()
    {
        char dir[512]; std::snprintf (dir, sizeof dir, "%s/Kits", clipsFolder());
        mkdir (clipsFolder(), 0755); mkdir (dir, 0755);
        char path[600]; int n = 1;
        for (; n < 1000; ++n) { std::snprintf (path, sizeof path, "%s/Kit %03d.dcpkit", dir, n); if (access (path, F_OK) != 0) break; }
        StateWriter w; serialize (w);
        FILE* f = std::fopen (path, "wb");
        if (f == nullptr || w.buf == nullptr) { if (f) std::fclose (f); status ("Can't write %s", path); return; }
        const bool ok = std::fwrite (w.buf, 1, w.len, f) == w.len;
        std::fclose (f);
        rescanReq.store (true);
        status (ok ? "Saved Kits/Kit %03d" : "Error writing Kit %03d", n);
    }
    void loadKit()
    {
        const int k = kitSelected();
        if (k < 0) { status ("No kits in %s/Kits (SAVE KIT makes one)", clipsFolder()); return; }
        char path[256], name[96];
        pthread_mutex_lock (&libLock); std::snprintf (path, sizeof path, "%s", kits->e[k].path); std::snprintf (name, sizeof name, "%s", kits->e[k].name); pthread_mutex_unlock (&libLock);
        FILE* f = std::fopen (path, "rb");
        if (f == nullptr) { status ("Can't open kit %s", name); return; }
        std::fseek (f, 0, SEEK_END); const long size = std::ftell (f); std::fseek (f, 0, SEEK_SET);
        if (size <= 0 || size > 4 * 1024 * 1024) { std::fclose (f); status ("Kit %s is damaged", name); return; }
        char* buf = (char*) std::malloc ((size_t) size + 1);
        if (buf == nullptr) { std::fclose (f); return; }
        const size_t got = std::fread (buf, 1, (size_t) size, f); std::fclose (f); buf[got] = 0;
        applyState (buf, got, true);
        std::free (buf);
        status ("Kit loaded: %s", name);
    }
    intptr_t getChunk (void** data)
    {
        serialize (chunk);
        *data = chunk.buf;
        return (intptr_t) chunk.len;
    }
    void setChunk (const void* data, intptr_t size)
    {
        if (data == nullptr || size <= 0) return;
        const char* t = (const char*) data;
        if (size < 10 || std::strncmp (t, "DaClipPads", 10) != 0) return;         // not ours
        applyState (t, (size_t) size, true);
    }

    // ---------------------------------------------------------------------------- screen readouts (audio thread)
    void send (int i, float plain, float threshold)
    {
        setPlain (i, plain);
        if (std::fabs (plain - sent[i]) < threshold) return;
        sent[i] = plain;
        if (master != nullptr) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
    }
    void setText (int i, const char* s)
    {
        if (! texts[i].publish (s)) return;
        textGen[i] = (textGen[i] + 1) % 1000;
        send (i, (float) textGen[i], 0.5f);
    }
    static int level (float peak)                                          // 0..10 on a 40 dB scale
    {
        if (peak <= 1e-4f) return 0;
        const float db = 20.f * std::log10 (peak);
        return clampi ((int) std::lround ((db + 40.f) / 40.f * 10.f), peak > 0.003f ? 1 : 0, 10);
    }
    float envPeak (const Sample* s, double a, double b) const              // a, b normalised
    {
        if (b < a) { const double t = a; a = b; b = t; }
        int i0 = (int) std::floor (a * kEnvPoints), i1 = (int) std::ceil (b * kEnvPoints);
        i0 = clampi (i0, 0, kEnvPoints - 1); i1 = clampi (i1, i0 + 1, kEnvPoints);
        int m = 0; for (int i = i0; i < i1; ++i) m = std::max (m, (int) s->env[i]);
        return m / 255.f;
    }
    void clipInfo (int c, char* a, size_t na, char* b, size_t nb)
    {
        const ClipRun& r = eng.run[c]; const ClipSettings& s = set[c];
        if (r.smp == nullptr) { a[0] = b[0] = 0; return; }
        Region R; buildRegion (s, r.smp, sr, R);
        const double lenSec = eng.fitLen (c, R) / sr;
        const double beats = eng.fitBeats (c, R) > 0 ? eng.fitBeats (c, R) : Engine::autoBeats (lenSec, eng.tr.tempo);
        if (s.loops() || s.slen != SL_OFF)
        {
            const double bars = beats / eng.tr.beatsPerBar;
            char len[16];
            if (bars >= 1.0) std::snprintf (len, sizeof len, "%g BAR%s", bars, bars == 1.0 ? "" : "S");
            else std::snprintf (len, sizeof len, "%g BEAT%s", beats, beats == 1.0 ? "" : "S");
            std::snprintf (a, na, "%.0f BPM  %s", beats * 60.0 / lenSec, len);
        }
        else std::snprintf (a, na, "ONE SHOT  %.2f s", R.L / sr);
        static const char* const mode[] { "1-SHOT", "LOOP", "GATE", "TOGGLE" };
        char vol[16];
        if ((float) s.vol <= -59.9f) std::snprintf (vol, sizeof vol, "OFF"); else std::snprintf (vol, sizeof vol, "%+.1fdB", (double) (float) s.vol);
        std::snprintf (b, nb, "%s%s  %+dst  %s%s", mode[clampi (s.mode, 0, 3)], s.reverse ? " REV" : "", (int) s.semis, vol, s.pmode == PM_STRETCH ? "  STR" : "");
    }
    void updateDisplay()
    {
        char t[96], t2[96];
        const int selc = sel();
        for (int c = 0; c < kClips; ++c)
        {
            const ClipRun& r = eng.run[c]; const ClipSettings& s = set[c];
            if (r.smp != nullptr && ! ((loadingMask.load() >> c) & 1)) std::snprintf (t, sizeof t, "%s", r.smp->name);
            else if ((loadingMask.load() >> c) & 1) std::snprintf (t, sizeof t, "Loading...");
            else if ((failedMask.load() >> c) & 1) std::snprintf (t, sizeof t, "Missing file");
            else std::snprintf (t, sizeof t, "-");
            setText (P_CNAME0 + c, t);
            clipInfo (c, t, sizeof t, t2, sizeof t2);
            setText (P_CINFO0 + c, t); setText (P_CINFOB0 + c, t2);
            const ClipView& vw = eng.view[c];
            const int prog = clampi ((int) (vw.progress * 18.f), 0, 17);
            send (P_CSTAT0 + c, (float) (vw.state * 18 + (vw.playing ? prog : 0)) / 127.f, 0.5f / 127.f);
            // thumbnail: 8 bars over the play region (recomputed only when the clip changes)
            ThumbKey key; key.s = r.smp; key.a = s.start; key.b = s.end; key.rev = s.reverse; key.norm = s.normalize;
            if (key != thumbKey[c]) thumbKey[c] = key; else continue;
            for (int k = 0; k < kThumbBoxes; ++k)
            {
                int lv[2] { 0, 0 };
                if (r.smp != nullptr)
                    for (int h = 0; h < 2; ++h)
                    {
                        const int bar = s.reverse ? 7 - (2 * k + h) : 2 * k + h;
                        const float lo = std::fmin (s.start, s.end), hi = std::fmax (s.start, s.end);
                        const float g = s.normalize && r.smp->peak > 1e-4f ? std::fmin (16.f, 0.98f / r.smp->peak) : 1.f;
                        lv[h] = level (envPeak (r.smp, lo + (hi - lo) * bar / 8.0, lo + (hi - lo) * (bar + 1) / 8.0) * g);
                    }
                send (P_THUMB0 + c * 4 + k, (float) (lv[0] * kLevels + lv[1]) / 127.f, 0.5f / 127.f);
            }
        }
        // selected clip: waveform + markers
        const ClipRun& r = eng.run[selc]; const ClipSettings& s = set[selc];
        const double z = std::pow (2.0, (int) get (P_ZOOM)), w = 1.0 / z;
        const double w0 = clampd ((double) (float) focusPos - w * 0.5, 0.0, 1.0 - w);
        const int bars = kWaveBoxes * 2;
        double head = -1.0;
        Region R; const bool haveR = r.smp != nullptr && buildRegion (s, r.smp, sr, R);
        if (haveR && eng.playing (selc)) { float g; head = physical (R, r.main.pos, g) / r.smp->frames; }
        const int n = s.sliceCount, ks = std::min (sliceSel(), std::max (0, n - 1));
        ThumbKey wk; wk.s = r.smp; wk.norm = s.normalize;
        const bool waveChanged = wk != waveKey || w0 != waveW0 || w != waveW;
        waveKey = wk; waveW0 = w0; waveW = w;
        for (int b = 0; b < kWaveBoxes && waveChanged; ++b)
        {
            int lv[2] { 0, 0 };
            if (r.smp != nullptr)
                for (int h = 0; h < 2; ++h)
                {
                    const int j = 2 * b + h;
                    const float g = s.normalize && r.smp->peak > 1e-4f ? std::fmin (16.f, 0.98f / r.smp->peak) : 1.f;
                    lv[h] = level (envPeak (r.smp, w0 + w * j / bars, w0 + w * (j + 1) / bars) * g);
                }
            send (P_WAVE0 + b, (float) (lv[0] * kLevels + lv[1]) / 127.f, 0.5f / 127.f);
        }
        const double seg = w / kMarks;
        const float lo = std::fmin (s.start, s.end), hi = std::fmax (s.start, s.end);
        auto in = [&] (double x, double a) { return x >= a - seg * 0.5 && x < a + seg * 0.5; };
        for (int j = 0; j < kMarks; ++j)
        {
            int st = 0;
            const double x = w0 + seg * (j + 0.5);
            if (r.smp != nullptr)
            {
                if (x >= lo && x < hi) st = 1;
                if (s.loops() && x >= s.lstart && x < s.lend && x >= lo && x < hi) st = 2;
                for (int k = 1; k < n; ++k) if (in (x, s.slice[k])) st = 8;
                if (s.loops() && in (x, s.lstart)) st = 5;
                if (s.loops() && in (x, s.lend)) st = 6;
                if (in (x, lo)) st = 3;
                if (in (x, hi)) st = 4;
                if (n > 0 && in (x, s.slice[ks])) st = 9;
                if (head >= 0 && in (x, head)) st = 7;
            }
            send (P_MARK0 + j, (float) st / 9.f, 0.5f / 9.f);
        }
        if (r.smp != nullptr)
            std::snprintf (t, sizeof t, "CLIP %d  %s  %.2f s  %.1f kHz%s", selc + 1, r.smp->name, r.smp->frames / r.smp->rate, r.smp->fileRate / 1000.0,
                           r.smp->fileRate != r.smp->rate ? " (converted)" : "");
        else std::snprintf (t, sizeof t, "CLIP %d  empty", selc + 1);
        setText (P_SELNAME, t);
        if (n > 0 && r.smp != nullptr)
            std::snprintf (t, sizeof t, "SLICE %d/%d  %.3f s  %+d st%s%s", ks + 1, n, (s.slice[ks + 1] - s.slice[ks]) * r.smp->frames / r.smp->rate,
                           (int) s.slicePitch[ks], ((s.sliceRev >> ks) & 1) ? "  REV" : "", s.seqOn ? "  REARRANGED" : "");
        else std::snprintf (t, sizeof t, "No slices: set CHOPS and press CHOP");
        setText (P_SLICEINFO, t);
        // scenes, beat, transport
        for (int k = 0; k < kScenes; ++k)
        {
            const int st = eng.activeScene == k ? 3 : (eng.queuedScene == k ? 2 : (sceneMask[k] != 0 ? 1 : 0));
            send (P_SCENELED0 + k, (float) st / 3.f, 0.1f);
        }
        const double bib = eng.tr.beatInBar();
        send (P_BEATLED, (float) ((std::floor (bib) + 0.5) / std::fmax (4.0, eng.tr.beatsPerBar)), 0.01f);
        const long bar = eng.tr.barNumber() + 1;
        std::snprintf (t, sizeof t, "%s %s %.2f BPM   BAR %ld.%d", eng.tr.hostTempo ? "HOST" : "INT", eng.tr.hostPlaying ? "\xE2\x96\xB6" : "\xE2\x96\xA0",
                       eng.tr.tempo, bar, (int) bib + 1);
        setText (P_TRANSPORT, t);
        // status
        bool shown = false;
        if (pthread_mutex_trylock (&msgLock) == 0)
        {
            if (nowSeconds() - msgAt < 5.0) { std::snprintf (t, sizeof t, "%s", msg); shown = true; }
            pthread_mutex_unlock (&msgLock);
            if (! shown)
            {
                const int learn = eng.learnNote.load();
                if (get (P_LEARN) > 0.5f) std::snprintf (t, sizeof t, "MIDI LEARN: hit a pad or key for clip %d", selc + 1);
                else if (eng.queuedScene >= 0) std::snprintf (t, sizeof t, "Scene %d starts on the next %s", eng.queuedScene + 1, Transport::name (p_sceneQuant()));
                else std::snprintf (t, sizeof t, "Pad mode: %s   Quantize: %s", (const char*[]) { "PLAY", "STOP", "SELECT", "MUTE" }[padMode()], Transport::name ((int) get (P_QUANT)));
                (void) learn;
            }
            setText (P_STATUS, t);
        }
        const int n2 = libCount.load();
        if (rescanReq.load()) std::snprintf (t, sizeof t, "Scanning...");
        else if (n2 == 0) std::snprintf (t, sizeof t, "No WAV files in %s", clipsFolder());
        else std::snprintf (t, sizeof t, "%d sample%s in %s  |  %d kit%s", n2, n2 == 1 ? "" : "s", clipsFolder(), kitCount.load(), kitCount.load() == 1 ? "" : "s");
        setText (P_LIBINFO, t);
        send (P_METER_L, clampf (gainToDb (eng.peakL), kMeterLo, kMeterHi), 1.f);
        send (P_METER_R, clampf (gainToDb (eng.peakR), kMeterLo, kMeterHi), 1.f);
    }
    int p_sceneQuant() const { const int q = (int) get (P_SCENEQ); return q == 0 ? (int) get (P_QUANT) : q - 1; }

    // MIDI LEARN (audio thread): the first note after LEARN goes to the selected clip
    void serveLearn()
    {
        if (get (P_LEARN) < 0.5f) return;
        const int n = eng.learnNote.exchange (-1);
        if (n < 0) return;
        const int c = sel();
        set[c].note = n;
        setPlain (P_NOTE, (float) n); setPlain (P_LEARN, 0.f);
        if (master != nullptr) { master (&fx, audioMasterAutomate, P_NOTE, 0, nullptr, nv[P_NOTE].load()); master (&fx, audioMasterAutomate, P_LEARN, 0, nullptr, 0.f); }
        char nn[24]; noteName (n, nn, sizeof nn);
        status ("Clip %d now plays from note %s", c + 1, nn);
    }

    // ---------------------------------------------------------------------------- audio
    void fillEngineParams()
    {
        EngineParams& e = eng.p;
        e.tempo = get (P_TEMPO); e.sync = get (P_SYNC) > 0.5f; e.swing = get (P_SWING) / 100.f; e.quant = (int) get (P_QUANT);
        const int sq = (int) get (P_SCENEQ); e.sceneQuant = sq == 0 ? -1 : sq - 1;
        e.sceneFollow = (int) get (P_SCENEFOLLOW); e.sceneBars = kSceneBars[clampi ((int) get (P_SCENEBARS), 0, 4)];
        e.hostFollow = (int) get (P_HOSTFOLLOW); e.quality = (int) get (P_QUALITY);
        e.velSens = get (P_VELSENS); e.sliceNote = (int) get (P_SLICENOTE); e.midiChannel = (int) get (P_MIDICH);
        e.muteAll = get (P_MUTEALL) > 0.5f; e.selected = sel();
        e.samplerOn = get (P_SMP_ON) > 0.5f;
        e.vintage = { get (P_BITS), get (P_RATE), get (P_AA), get (P_QUANTIZE), get (P_SAT), get (P_NOISE), get (P_CRACKLE), get (P_SMP_OUT) };
        MasterParams& m = e.master;
        m.sat = get (P_MSAT); m.thresh = get (P_CTHRESH); m.ratio = get (P_CRATIO); m.attackMs = get (P_CATTACK); m.releaseMs = get (P_CRELEASE);
        m.makeup = get (P_CMAKEUP); m.eqLow = get (P_EQLOW); m.eqMid = get (P_EQMID); m.eqMidHz = get (P_EQMIDF); m.eqHigh = get (P_EQHIGH);
        m.revSize = get (P_REVSIZE); m.revDamp = get (P_REVDAMP); m.revReturn = get (P_REVRET);
        m.delayBeats = delayBeatsFor ((int) get (P_DLYTIME), eng.tr.beatsPerBar); m.fb = get (P_DLYFB); m.tone = get (P_DLYTONE); m.dlyReturn = get (P_DLYRET);
        m.volDb = get (P_MASTER) <= -59.9f ? -120.f : get (P_MASTER); m.limiter = get (P_LIMIT) > 0.5f;
    }
    void process (float* outL, float* outR, int n)
    {
        fillEngineParams();
        const VstTimeInfo* ti = nullptr;
        if (master != nullptr)
            ti = (const VstTimeInfo*) master (&fx, audioMasterGetTime, 0, kVstPpqPosValid | kVstTempoValid | kVstBarsValid | kVstTimeSigValid, nullptr, 0.f);
        eng.process (outL, outR, n, ti);
        curTempo = (float) eng.tr.tempo;
        serveLearn();
        displayCountdown -= n;
        if (displayCountdown <= 0)
        {
            displayCountdown += (int) (sr / 20.f);
            updateDisplay();
            eng.peakL *= 0.5f; eng.peakR *= 0.5f;
        }
    }

    // ---------------------------------------------------------------------------- parameter text
    void display (int i, char* out, size_t max)
    {
        const ParamDef& p = params()[i];
        const float x = get (i);
        if (p.kind == K_TEXT) { std::snprintf (out, max, "%s", texts[i].get()); return; }
        if (i == P_FILE)
        {
            const int k = libSelected();
            pthread_mutex_lock (&libLock);
            if (k >= 0 && k < lib->count) std::snprintf (out, max, "%d. %s", k + 1, lib->e[k].name);
            else std::snprintf (out, max, "No samples in %s", clipsFolder());
            pthread_mutex_unlock (&libLock);
            return;
        }
        if (i == P_KIT)
        {
            const int k = kitSelected();
            pthread_mutex_lock (&libLock);
            if (k >= 0 && k < kits->count) std::snprintf (out, max, "%d. %s", k + 1, kits->e[k].name);
            else std::snprintf (out, max, "No kits yet");
            pthread_mutex_unlock (&libLock);
            return;
        }
        if (i == P_SEL)
        {
            const int c = clampi ((int) x, 0, kClips - 1);
            const char* f = set[c].file; const char* b = std::strrchr (f, '/');
            char nm[64]; std::snprintf (nm, sizeof nm, "%s", b ? b + 1 : f);
            const size_t l = std::strlen (nm); if (l > 4 && strcasecmp (nm + l - 4, ".wav") == 0) nm[l - 4] = 0;
            std::snprintf (out, max, "CLIP %d  %s", c + 1, nm[0] ? nm : "(empty)");
            return;
        }
        if (i == P_SLICE)
        {
            const int n = set[sel()].sliceCount;
            if (n <= 0) std::snprintf (out, max, "No slices"); else std::snprintf (out, max, "Slice %d of %d", (int) x + 1, n);
            return;
        }
        if (p.fmt == F_POS)
        {
            const Sample* s = uiSmp (sel());
            if (s != nullptr) std::snprintf (out, max, "%.3f s", (double) x * s->frames / s->rate);
            else std::snprintf (out, max, "%.1f %%", (double) (x * 100.f));
            return;
        }
        if (i == P_CSTAT0 || (i > P_CSTAT0 && i < P_CSTAT0 + kClips))
        {
            static const char* const k[] { "Empty", "Stopped", "Queued", "Playing", "Looping", "Stopping", "Muted" };
            std::snprintf (out, max, "%s", k[clampi ((int) std::lround (x * 127.f) / 18, 0, CS_COUNT - 1)]);
            return;
        }
        if (p.kind == K_BOOL && i >= P_PM_PLAY && i <= P_PM_MUTE) { std::snprintf (out, max, "%s", x > 0.5f ? "Active" : "-"); return; }
        formatValue (p, x, out, max);
    }
};

// ------------------------------------------------------------------------------------------------ VST2 entry points
void processReplacing (AEffect* e, float** /*in*/, float** out, int32_t n)
{
    static_cast<Plugin*> (e->object)->process (out[0], out[1], n);
}
void processAccumulating (AEffect* e, float** /*in*/, float** out, int32_t n)
{
    float tl[256], tr[256];
    Plugin* p = static_cast<Plugin*> (e->object);
    for (int32_t pos = 0; pos < n; pos += 256)
    {
        const int32_t m = (n - pos) < 256 ? (n - pos) : 256;
        p->process (tl, tr, m);
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
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, params()[idx].name, 28); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, params()[idx].unit, 8); return 0;
        case effGetParamDisplay:  // MPC (a JUCE host) reads into 256-byte buffers
            if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[96]; p->display (idx, b, sizeof b); copyStr (ptr, b, 64); }
            return 0;
        case effCanBeAutomated:
        {
            if (idx < 0 || idx >= P_COUNT) return 0;
            const Kind k = params()[idx].kind;
            return (k == K_READ || k == K_TEXT || k == K_MOMENT) ? 0 : 1;
        }
        case effSetSampleRate:    if (opt > 1000.f) p->prepare (opt); return 0;
        case effMainsChanged:     return 0;
        case effGetChunk:         return ptr != nullptr ? p->getChunk ((void**) ptr) : 0;
        case effSetChunk:         p->setChunk (ptr, val); return 1;
        case effProcessEvents:    p->eng.midi.add ((const VstEvents*) ptr, (int) p->get (P_MIDICH)); return 1;
        case effGetProgram:       return 0;
        case effGetProgramName:
        case effGetProgramNameIndexed: copyStr (ptr, "Da Clip Pads", 24); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "Da Clip Pads", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetPlugCategory:  return kPlugCategSynth;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return 0;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;
        case effCanDo:
        {
            const char* s = (const char*) ptr;
            if (s == nullptr) return 0;
            if (! std::strcmp (s, "receiveVstEvents") || ! std::strcmp (s, "receiveVstMidiEvent") || ! std::strcmp (s, "receiveVstTimeInfo")) return 1;
            if (! std::strcmp (s, "sendVstEvents") || ! std::strcmp (s, "sendVstMidiEvent") || ! std::strcmp (s, "offline")) return -1;
            return 0;
        }
        default: return 0;
    }
}
void initOnce()
{
    static bool inited = false;
    if (inited) return;
    int n = 0; const char* const* labels = factoryPresetLabels (n);
    initParams (n, labels);
    inited = true;
}
} // namespace

CP_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    initOnce();
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
    fx.numPrograms = 1;
    fx.numParams = P_COUNT;
    fx.numInputs = 0;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('D' << 24) | ('C' << 16) | ('L' << 8) | 'P';   // 'DCLP' = 0x44434c50
    fx.version = kVersion;
    return &fx;
}
// Stable parameter key for index i (used by tools/make_skin.py to lay out the skin; not used by MPC).
CP_EXPORT const char* DCP_ParamKey (int i)
{
    initOnce();
    return (i >= 0 && i < P_COUNT) ? params()[i].key : "";
}
CP_EXPORT int DCP_ParamCount() { return P_COUNT; }
