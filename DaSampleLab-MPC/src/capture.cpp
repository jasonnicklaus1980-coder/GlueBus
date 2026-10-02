// Da Sample Lab Capture: a native MPC OS VST2 insert effect that records whatever passes through it (an audio track,
// the inputs, a submix) to a 24-bit WAV at MPC's sample rate in /sdcard/SampleLab/Samples/Captures, so Da Sample Lab
// can chop it straight away (its LATEST key loads the newest capture). The audio passes through unchanged.
//
// Threads: the audio thread only copies samples into a lock-free ring buffer (no allocation, no locks, no files);
// a writer thread streams the ring to disk, finishes the WAV header and renames the file when the take ends.
// Parameters: 0 Record (any change = a tap: start / stop), 1 Arm (start on signal), 2 Threshold, 3 Max Length,
//             4 Status (text), 5 Time (text), 6 Meter (read-only, 0..127 = -60..0 dBFS).
#include "vst2.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CA_EXPORT extern "C" __attribute__ ((visibility ("default")))

namespace
{
constexpr int kVersion = 1000;
enum { P_REC, P_ARM, P_THRESH, P_MAXLEN, P_STATUS, P_TIME, P_METER, NPARAMS };
const char* const kKeys[NPARAMS] { "record", "arm", "threshold", "maxlen", "status", "time", "meter" };
const char* const kNames[NPARAMS] { "Record", "Arm On Signal", "Threshold", "Max Length", "Status", "Time", "Meter" };
const float kDefaults[NPARAMS] { 0.f, 0.f, 0.6f, 295.f / 595.f, 0.f, 0.f, 0.f };     // threshold -28 dB, max 5:00
enum State { IDLE, ARMED, RECORDING, STOPPING };

double nowSec() { timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
void copyStr (void* dst, const char* s, size_t max) { if (dst == nullptr) return; std::strncpy ((char*) dst, s, max - 1); ((char*) dst)[max - 1] = 0; }
const char* rootDir()
{
    static char r[300];
    if (! r[0]) std::snprintf (r, sizeof r, "%s", std::getenv ("SAMPLELAB_ROOT") ? std::getenv ("SAMPLELAB_ROOT") : "/sdcard/SampleLab");
    return r;
}
void mkdirs (const char* path)
{
    char b[600]; std::snprintf (b, sizeof b, "%s", path);
    for (char* p = b + 1; *p; ++p) if (*p == '/') { *p = 0; mkdir (b, 0755); *p = '/'; }
    mkdir (b, 0755);
}
float threshDb (float n) { return -60.f + n * 54.f; }                 // -60 .. -6 dBFS
float maxLenSec (float n) { return 5.f + n * 595.f; }                 // 5 s .. 10 min

struct Text
{
    char buf[2][96] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    bool publish (const char* s) { if (! std::strcmp (get(), s)) return false; const int i = 1 - live.load(); std::snprintf (buf[i], sizeof buf[i], "%s", s); live.store (i); return true; }
};

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> nv[NPARAMS];
    float seen[NPARAMS], sent[NPARAMS];
    double sr = 44100;
    // ring buffer of interleaved stereo floats (single producer: audio thread; single consumer: writer)
    float* ring = nullptr; long ringCap = 0;                   // frames
    std::atomic<long> wpos { 0 }, rpos { 0 };
    std::atomic<int> state { IDLE }, overflow { 0 };
    std::atomic<long> recorded { 0 };                          // frames written into the ring for this take
    long maxFrames = 0;
    float preroll[2 * 4096] {}; int preN = 0, preW = 0;        // the last ~50 ms before an armed start
    pthread_t writer {}; bool writerOn = false; std::atomic<int> quit { 0 };
    Text status, timeText; int gen[2] {};
    float meterPeak = 0.f; int meterCount = 0;
    double guardUntil = 0, lastTimeText = 0;
    char lastFile[512] {};

    Plugin()
    {
        for (int i = 0; i < NPARAMS; ++i) { nv[i].store (kDefaults[i]); seen[i] = kDefaults[i]; sent[i] = -1.f; }
        guardUntil = nowSec() + 2.5;
        mkdirs ((std::string (rootDir()) + "/Samples/Captures").c_str());
        setStatus ("Tap RECORD to capture what passes through");
        setTime ("00:00.0");
        allocRing();
    }
    ~Plugin()
    {
        if (state.load() == RECORDING || state.load() == ARMED) state = STOPPING;
        quit = 1;
        if (writerOn) pthread_join (writer, nullptr);
        std::free (ring);
    }
    void allocRing()
    {
        // 8 seconds of headroom: the SD card can stall; the writer normally keeps up within a few ms
        std::free (ring);
        ringCap = (long) (8 * sr);
        ring = (float*) std::calloc ((size_t) ringCap * 2, sizeof (float));
        wpos = 0; rpos = 0;
    }
    void notify (int i) { if (master) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load()); }
    void setText (Text& t, int which, int param, const char* s) { if (t.publish (s)) { gen[which] = (gen[which] + 1) % 1000; nv[param].store (gen[which] / 1000.f); } }
    void setStatus (const char* s) { setText (status, 0, P_STATUS, s); }
    void setTime (const char* s) { setText (timeText, 1, P_TIME, s); }
    void startWriter();
    // ------------------------------------------------------------------------------------ UI thread
    void poll()
    {
        const float v = nv[P_REC].load();
        if (v != seen[P_REC]) { seen[P_REC] = v; press(); }
        for (int i = P_ARM; i <= P_MAXLEN; ++i) seen[i] = nv[i].load();
    }
    void press()
    {
        if (nowSec() < guardUntil) return;
        const int st = state.load();
        if (st == IDLE)
        {
            if (! ring) { setStatus ("Out of memory"); return; }
            if (writerOn) { pthread_join (writer, nullptr); writerOn = false; }
            maxFrames = (long) (maxLenSec (nv[P_MAXLEN].load()) * sr);
            recorded = 0; overflow = 0; preN = 0; preW = 0;
            rpos.store (wpos.load());
            quit = 0;
            const bool arm = nv[P_ARM].load() > 0.5f;
            state = arm ? ARMED : RECORDING;
            startWriter();
            char b[96]; std::snprintf (b, sizeof b, arm ? "Armed: waiting for signal above %.0f dB" : "Recording\xe2\x80\xa6 tap RECORD to stop", (double) threshDb (nv[P_THRESH].load()));
            setStatus (b);
        }
        else if (st == ARMED) { state = STOPPING; setStatus ("Cancelled"); }
        else if (st == RECORDING) { state = STOPPING; setStatus ("Saving\xe2\x80\xa6"); }
    }
    // ------------------------------------------------------------------------------------ audio thread
    void push (const float* l, const float* r, int n)
    {
        long w = wpos.load (std::memory_order_relaxed);
        const long rd = rpos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            if (w - rd >= ringCap) { overflow = 1; break; }              // the writer fell 8 s behind: drop (reported)
            float* p = ring + 2 * (w % ringCap); p[0] = l[i]; p[1] = r[i]; ++w;
        }
        wpos.store (w, std::memory_order_release);
    }
    void process (float** in, float** out, int n)
    {
        const float* L = in[0]; const float* R = in[1];
        if (out[0] != L) std::memcpy (out[0], L, sizeof (float) * (size_t) n);   // pass-through, bit exact
        if (out[1] != R) std::memcpy (out[1], R, sizeof (float) * (size_t) n);
        float pk = 0.f;
        for (int i = 0; i < n; ++i) { pk = std::fmax (pk, std::fabs (L[i])); pk = std::fmax (pk, std::fabs (R[i])); }
        const int st = state.load();
        if (st == ARMED)
        {
            const float th = std::pow (10.f, threshDb (nv[P_THRESH].load()) / 20.f);
            int at = -1;
            for (int i = 0; i < n && at < 0; ++i) if (std::fabs (L[i]) >= th || std::fabs (R[i]) >= th) at = i;
            if (at < 0)
            {
                for (int i = 0; i < n; ++i) { preroll[2 * preW] = L[i]; preroll[2 * preW + 1] = R[i]; preW = (preW + 1) % 2048; if (preN < 2048) ++preN; }
            }
            else
            {
                // keep up to ~50 ms before the trigger so the attack isn't cut
                const int keep = std::min (preN, (int) (0.05 * sr));
                for (int k = keep; k > 0; --k) { const int idx = ((preW - k) % 2048 + 2048) % 2048; push (&preroll[2 * idx], &preroll[2 * idx + 1], 1); }
                push (L + at, R + at, n - at);
                recorded = keep + (n - at);
                state = RECORDING;
            }
        }
        else if (st == RECORDING)
        {
            const long room = maxFrames - recorded.load();
            const int m = (int) std::min ((long) n, std::max (0L, room));
            push (L, R, m);
            recorded += m;
            if (recorded.load() >= maxFrames) state = STOPPING;
        }
        // meter: peak hold, published about 20 times a second
        meterPeak = std::fmax (meterPeak * 0.9f, pk);
        if (++meterCount * n >= sr / 20)
        {
            meterCount = 0;
            const float db = meterPeak > 1e-6f ? 20.f * std::log10 (meterPeak) : -120.f;
            nv[P_METER].store (std::fmin (1.f, std::fmax (0.f, (db + 60.f) / 60.f)));
        }
        for (int i = P_STATUS; i < NPARAMS; ++i) if (sent[i] != nv[i].load()) { sent[i] = nv[i].load(); notify (i); }
    }
    // ------------------------------------------------------------------------------------ writer thread
    void showRec (long frames, long& shown)
    {
        lastTimeText = nowSec(); shown = frames;
        const double s = frames / sr; char b[32];
        std::snprintf (b, sizeof b, "REC %02d:%04.1f", (int) (s / 60), s - 60 * (int) (s / 60)); setTime (b);
    }
    void writerLoop()
    {
        char dir[400]; std::snprintf (dir, sizeof dir, "%s/Samples/Captures", rootDir());
        mkdirs (dir);
        time_t t = time (nullptr); struct tm tm; localtime_r (&t, &tm);
        char name[64]; strftime (name, sizeof name, "Capture %Y-%m-%d %H.%M.%S", &tm);
        char path[600], part[620];
        std::snprintf (path, sizeof path, "%s/%s.wav", dir, name); std::snprintf (part, sizeof part, "%s.part", path);
        FILE* f = std::fopen (part, "wb");
        if (! f) { setStatus ("Can't write to SampleLab/Samples/Captures (drive full?)"); state = IDLE; return; }
        unsigned char hdr[44] {}; std::fwrite (hdr, 1, 44, f);              // filled in at the end
        long frames = 0, shown = -1;
        static unsigned char buf[6 * 4096];
        bool failed = false;
        for (;;)
        {
            const long w = wpos.load (std::memory_order_acquire), r = rpos.load (std::memory_order_relaxed);
            long n = std::min (w - r, 4096L);
            if (n > 0)
            {
                for (long i = 0; i < n; ++i)
                {
                    const float* p = ring + 2 * ((r + i) % ringCap);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float x = std::fmax (-1.f, std::fmin (1.f, p[c]));
                        int32_t v = (int32_t) std::lround ((double) x * 8388608.0); if (v > 8388607) v = 8388607;
                        unsigned char* o = buf + 6 * i + 3 * c; o[0] = (unsigned char) v; o[1] = (unsigned char) (v >> 8); o[2] = (unsigned char) (v >> 16);
                    }
                }
                rpos.store (r + n, std::memory_order_release);
                if (std::fwrite (buf, 6, (size_t) n, f) != (size_t) n) { failed = true; break; }
                frames += n;
                if (nowSec() - lastTimeText > 0.2) showRec (frames, shown);
                continue;
            }
            if (frames != shown && nowSec() - lastTimeText > 0.05) showRec (frames, shown);   // caught up: exact length
            if (state.load() == STOPPING || quit.load()) break;
            usleep (5000);
        }
        // header
        auto w32 = [] (unsigned char* p, uint32_t v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); };
        const uint32_t bytes = (uint32_t) (frames * 6);
        std::memcpy (hdr, "RIFF", 4); w32 (hdr + 4, 36 + bytes); std::memcpy (hdr + 8, "WAVEfmt ", 8);
        w32 (hdr + 16, 16); hdr[20] = 1; hdr[22] = 2; w32 (hdr + 24, (uint32_t) sr); w32 (hdr + 28, (uint32_t) sr * 6); hdr[32] = 6; hdr[34] = 24;
        std::memcpy (hdr + 36, "data", 4); w32 (hdr + 40, bytes);
        std::fseek (f, 0, SEEK_SET);
        failed = failed || std::fwrite (hdr, 1, 44, f) != 44;
        failed = (std::fclose (f) != 0) || failed;
        char b[160];
        if (failed) { std::remove (part); setStatus ("Capture failed: the drive is full or was removed"); }
        else if (frames == 0) { std::remove (part); setStatus ("Nothing recorded"); }
        else
        {
            std::rename (part, path);
            std::snprintf (lastFile, sizeof lastFile, "%s", path);
            // tell Da Sample Lab (its LATEST key): latest.txt holds the path of the newest capture
            char lp[600], lt[620]; std::snprintf (lp, sizeof lp, "%s/latest.txt", dir); std::snprintf (lt, sizeof lt, "%s.tmp", lp);
            FILE* l = std::fopen (lt, "w"); if (l) { std::fprintf (l, "%s\n", path); std::fclose (l); std::rename (lt, lp); }
            std::snprintf (b, sizeof b, "Saved %s (%.1f s)%s", name, frames / sr, overflow.load() ? " - some audio was dropped" : "");
            setStatus (b);
        }
        const double s = frames / sr; std::snprintf (b, sizeof b, "%02d:%04.1f", (int) (s / 60), s - 60 * (int) (s / 60)); setTime (b);
        state = IDLE;
    }
    void display (int i, char* out, size_t max)
    {
        switch (i)
        {
            case P_REC: std::snprintf (out, max, "%s", state.load() == RECORDING ? "Recording" : (state.load() == ARMED ? "Armed" : "Stopped")); return;
            case P_ARM: std::snprintf (out, max, "%s", nv[P_ARM].load() > 0.5f ? "On" : "Off"); return;
            case P_THRESH: std::snprintf (out, max, "%.0f dB", (double) threshDb (nv[P_THRESH].load())); return;
            case P_MAXLEN: { const int s = (int) maxLenSec (nv[P_MAXLEN].load()); std::snprintf (out, max, "%d:%02d", s / 60, s % 60); return; }
            case P_STATUS: std::snprintf (out, max, "%s", status.get()); return;
            case P_TIME: std::snprintf (out, max, "%s", timeText.get()); return;
            default: std::snprintf (out, max, "%d", (int) std::lround (nv[i].load() * 127)); return;
        }
    }
};
void* writerMain (void* p) { static_cast<Plugin*> (p)->writerLoop(); return nullptr; }
void Plugin::startWriter() { writerOn = pthread_create (&writer, nullptr, writerMain, this) == 0; if (! writerOn) { state = IDLE; setStatus ("Can't start the recorder"); } }

void processReplacing (AEffect* e, float** in, float** out, int32_t n) { static_cast<Plugin*> (e->object)->process (in, out, n); }
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    float a[512], b[512];
    for (int32_t s = 0; s < n; s += 512)
    {
        const int m = n - s < 512 ? n - s : 512;
        float* ins[2] { in[0] + s, in[1] + s }; float* o[2] { a, b };
        p->process (ins, o, m);
        for (int i = 0; i < m; ++i) { out[0][s + i] += a[i]; out[1][s + i] += b[i]; }
    }
}
void setParameter (AEffect* e, int32_t i, float v)
{
    if (i < 0 || i > P_MAXLEN) return;
    Plugin* p = static_cast<Plugin*> (e->object);
    p->nv[i].store (v < 0 ? 0 : (v > 1 ? 1 : v));
    p->poll();
}
float getParameter (AEffect* e, int32_t i) { return (i >= 0 && i < NPARAMS) ? static_cast<Plugin*> (e->object)->nv[i].load() : 0.f; }
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    (void) val;
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose: p->~Plugin(); std::free (p); return 0;
        case effGetParamName: if (idx >= 0 && idx < NPARAMS) copyStr (ptr, kNames[idx], 28); return 0;
        case effGetParamLabel: if (ptr) ((char*) ptr)[0] = 0; return 0;
        case effGetParamDisplay: if (idx >= 0 && idx < NPARAMS && ptr) { char b[128]; p->display (idx, b, sizeof b); copyStr (ptr, b, 64); } return 0;
        case effCanBeAutomated: return 0;
        case effSetSampleRate:
            if (opt > 1000.f && p->state.load() == IDLE && std::fabs (opt - p->sr) > 0.5) { p->sr = opt; p->allocRing(); }
            else if (opt > 1000.f && p->state.load() == IDLE) p->sr = opt;
            return 0;
        case effMainsChanged: return 0;
        case effGetProgram: return 0;
        case effGetProgramName: case effGetProgramNameIndexed: copyStr (ptr, "Da Sample Lab Capture", 24); return 1;
        case effGetEffectName: case effGetProductString: copyStr (ptr, "Da Sample Lab Capture", 32); return 1;
        case effGetVendorString: copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetPlugCategory: return kPlugCategEffect;
        case effGetVstVersion: return 2400;
        case effGetTailSize: return 0;
        case effCanDo: return -1;
        default: return 0;
    }
}
} // namespace

CA_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    void* mem = std::calloc (1, sizeof (Plugin));
    if (! mem) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic; fx.dispatcher = dispatcher; fx.process = processAccumulating; fx.processReplacing = processReplacing;
    fx.setParameter = setParameter; fx.getParameter = getParameter;
    fx.numPrograms = 1; fx.numParams = NPARAMS; fx.numInputs = 2; fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing;
    fx.ioRatio = 1.f; fx.object = p;
    fx.uniqueID = ('D' << 24) | ('S' << 16) | ('L' << 8) | 'C';           // 'DSLC'
    fx.version = kVersion;
    return &fx;
}
CA_EXPORT const char* CA_ParamKey (int i) { return (i >= 0 && i < NPARAMS) ? kKeys[i] : ""; }
CA_EXPORT int CA_ParamCount() { return NPARAMS; }
