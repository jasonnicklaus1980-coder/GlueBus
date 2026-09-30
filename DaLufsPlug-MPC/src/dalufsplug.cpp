// Da Lufs Plug - loudness meter for Akai MPC OS (Gen1, 32-bit ARM), built from the GlueBus / RadioReady
// components (dependency-free VST2 core, parameter model, host notification, packaging, skin pipeline).
// Needs only libc/libm. No GUI: MPC draws the skin from /sdcard/Synths. Audio passes through untouched (bit-exact).
//
// Measures per ITU-R BS.1770-4 / EBU R128: Momentary (400 ms) and Short-term (3 s) with maxima, gated Integrated,
// Loudness Range (LRA), True Peak (dBTP, oversampled) and elapsed time. Presets = streaming / broadcast targets
// (Spotify, Apple Music, YouTube, ...): each sets a target loudness and a true-peak ceiling, and the plugin tells you
// what the platform will do with your track (turn it down / up by how much) and whether the peaks are safe.
// The screen's meter bar, numbers, status lines and the 60-second history are read-only parameters pushed to MPC.
#include "Loudness.h"
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#define LM_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
// ------------------------------------------------------------------------------------------------ platforms
// mode: 0 = normalises down only, 1 = normalises up and down, 2 = no normalisation / reference target,
//       3 = no normalisation at all (plays as delivered)
// tol: the "on target" window (a range such as -12 .. -9 LUFS is a centre +- tol)
// loudCeiling: stricter true-peak ceiling recommended for masters louder than the target (0 = none)
struct Platform { const char* name; float target, ceiling; int mode; float tol, loudCeiling; const char* note; };
const Platform kPlatforms[] = {
    { "Spotify",             -14.f,  -1.0f, 1, 0.5f, -2.0f, "Turns loud tracks down, quiet ones up (peak-limited); -2 dBTP for louder masters" },
    { "Spotify Loud",        -11.f,  -1.0f, 1, 0.5f,  0.f,  "Spotify's Loud playback setting" },
    { "Spotify Quiet",       -19.f,  -1.0f, 1, 0.5f,  0.f,  "Spotify's Quiet playback setting" },
    { "Apple Music",         -16.f,  -1.0f, 1, 0.5f,  0.f,  "Sound Check" },
    { "YouTube / YT Music",  -14.f,  -1.0f, 0, 0.5f,  0.f,  "Turns loud tracks down, never up" },
    { "Amazon Music",        -14.f,  -2.0f, 0, 0.5f,  0.f,  "" },
    { "Tidal",               -14.f,  -1.0f, 0, 0.5f,  0.f,  "" },
    { "Tidal Audiophile",    -18.f,  -1.0f, 0, 0.5f,  0.f,  "Tidal's audiophile mode" },
    { "Deezer",              -15.f,  -1.0f, 0, 0.5f,  0.f,  "" },
    { "SoundCloud",          -14.f,  -1.0f, 3, 0.5f,  0.f,  "No normalisation: plays as delivered" },
    { "TikTok / IG Reels",   -10.5f, -1.0f, 2, 1.5f,  0.f,  "Typical range -12 .. -9 LUFS" },
    { "Apple Podcasts",      -16.f,  -1.0f, 1, 0.5f,  0.f,  "" },
    { "Spotify Podcasts",    -14.f,  -1.0f, 1, 0.5f,  0.f,  "" },
    { "Broadcast EBU R128",  -23.f,  -1.0f, 2, 0.5f,  0.f,  "European TV / radio" },
    { "US TV ATSC A/85",     -24.f,  -2.0f, 2, 0.5f,  0.f,  "US broadcast (LKFS)" },
    { "CD / Club Master",     -9.f,  -0.3f, 2, 0.5f,  0.f,  "Loud master, no normalisation" },
    { "Custom",              -14.f,  -1.0f, 2, 0.5f,  0.f,  "Set your own target and ceiling" },
};
constexpr int kNumPlatforms = (int) (sizeof (kPlatforms) / sizeof (kPlatforms[0]));
const char* platformNames[kNumPlatforms];

// ------------------------------------------------------------------------------------------------ parameters
constexpr int kHist = 60;                                        // short-term history: 60 s, one column per second
enum
{
    P_PLATFORM, P_TARGET, P_CEIL, P_PREV, P_NEXT, P_RESET, P_PAUSE,                          // 0..6
    P_M, P_S, P_I, P_LRA, P_TP, P_MAXM, P_MAXS, P_TIME, P_GAIN, P_TPHEAD, P_TPOVER,         // 7..17 read-only
    P_STATUS, P_TPSTATUS, P_REL,                                                             // 18..20
    P_HIST0,                                                                                 // 21..80
    P_COUNT = P_HIST0 + kHist                                                                // 81
};
constexpr float kFloor = -70.f, kRelRange = 18.f, kHistRange = 12.f, kHistBlank = -12.2f;

enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_MOMENT, K_READ };
enum Fmt  { F_NUM, F_LUFS, F_LU, F_DBTP, F_DB, F_TIME, F_STATUS, F_TPSTATUS, F_REL };
struct ParamDef { char name[20]; const char* unit; Kind kind; float lo, hi, step, def; const char* const* choices; int n; Fmt fmt; };
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
    for (int i = 0; i < kNumPlatforms; ++i) platformNames[i] = kPlatforms[i].name;
    def (P_PLATFORM, "Platform", "", K_CHOICE, 0, kNumPlatforms - 1, 1, 0, platformNames, kNumPlatforms);
    def (P_TARGET, "Target", "LUFS", K_FLOAT, -30, -5, 0.5f, kPlatforms[0].target, nullptr, 0, F_LUFS);
    def (P_CEIL, "TP Ceiling", "dBTP", K_FLOAT, -6, 0, 0.1f, kPlatforms[0].ceiling, nullptr, 0, F_DBTP);
    def (P_PREV, "Prev Platform", "", K_MOMENT, 0, 1, 1, 0);
    def (P_NEXT, "Next Platform", "", K_MOMENT, 0, 1, 1, 0);
    def (P_RESET, "Reset", "", K_MOMENT, 0, 1, 1, 0);
    def (P_PAUSE, "Pause", "", K_BOOL, 0, 1, 1, 0);
    def (P_M, "Momentary", "LUFS", K_READ, kFloor, 5, 0, kFloor, nullptr, 0, F_LUFS);
    def (P_S, "Short Term", "LUFS", K_READ, kFloor, 5, 0, kFloor, nullptr, 0, F_LUFS);
    def (P_I, "Integrated", "LUFS", K_READ, kFloor, 5, 0, kFloor, nullptr, 0, F_LUFS);
    def (P_LRA, "Range", "LU", K_READ, 0, 40, 0, 0, nullptr, 0, F_LU);
    def (P_TP, "True Peak", "dBTP", K_READ, kFloor, 12, 0, kFloor, nullptr, 0, F_DBTP);
    def (P_MAXM, "Momentary Max", "LUFS", K_READ, kFloor, 5, 0, kFloor, nullptr, 0, F_LUFS);
    def (P_MAXS, "Short Term Max", "LUFS", K_READ, kFloor, 5, 0, kFloor, nullptr, 0, F_LUFS);
    def (P_TIME, "Time", "", K_READ, 0, 86400, 0, 0, nullptr, 0, F_TIME);
    def (P_GAIN, "Gain to Target", "dB", K_READ, -40, 40, 0, 0, nullptr, 0, F_DB);
    def (P_TPHEAD, "TP Headroom", "dB", K_READ, -20, 80, 0, 0, nullptr, 0, F_DB);
    def (P_TPOVER, "TP Over", "", K_READ, 0, 1, 0, 0);
    def (P_STATUS, "Status", "", K_READ, 0, 1000, 0, 0, nullptr, 0, F_STATUS);
    def (P_TPSTATUS, "TP Status", "", K_READ, 0, 1000, 0, 0, nullptr, 0, F_TPSTATUS);
    def (P_REL, "Meter vs Target", "LU", K_READ, -kRelRange, kRelRange, 0, -kRelRange, nullptr, 0, F_REL);
    for (int i = 0; i < kHist; ++i)
    {
        char nm[20]; std::snprintf (nm, sizeof nm, "History %d", i + 1);
        def (P_HIST0 + i, nm, "LU", K_READ, kHistBlank, kHistRange, 0, kHistBlank, nullptr, 0, F_REL);
    }
}

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
float toPlain (const ParamDef& d, float norm)
{
    norm = clampf (norm, 0.f, 1.f);
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return norm >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return std::floor (norm * (float) (d.n - 1) + 0.5f);
    float v = d.lo + norm * (d.hi - d.lo);
    if (d.step > 0.f) v = d.lo + std::floor ((v - d.lo) / d.step + 0.5f) * d.step;
    return clampf (v, d.lo, d.hi);
}
float toNorm (const ParamDef& d, float plain)
{
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

// ------------------------------------------------------------------------------------------------ plugin
struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> v[P_COUNT], nv[P_COUNT];
    int program = 0;
    float sr = 44100.f;
    bool notifying = false;

    lm::LoudnessMeter meter;
    lm::TruePeak tp;
    std::atomic<bool> resetReq { false };
    float sent[P_COUNT];                     // last value sent to MPC per read-only parameter
    float hist[kHist]; int histCount = 0, stepCount = 0;
    // status lines: written by the audio thread into the idle half, then published by flipping the index
    struct Text { char buf[2][40]; std::atomic<int> live { 0 }; const char* get() const { return buf[live.load()]; } };
    Text statusText, tpText;
    int statusGen = 0, tpGen = 0;
    float lastTarget = 1e9f, lastCeil = 1e9f; bool lastPaused = false; int lastProgram = -1;

    Plugin()
    {
        for (int i = 0; i < P_COUNT; ++i) setPlain (i, kParams[i].def);
        prepare (44100.f);
    }
    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    float get (int i) const { return v[i].load(); }

    void prepare (float rate) { sr = rate; meter.prepare (rate); tp.prepare (rate); clear(); }
    void clear()
    {
        meter.reset(); tp.reset();
        for (float& h : hist) h = kFloor;
        histCount = 0; stepCount = 0;
        for (float& s : sent) s = 1e9f;
        for (int i = P_M; i < P_COUNT; ++i) if (i != P_STATUS && i != P_TPSTATUS) setPlain (i, kParams[i].def);
        publish (statusText, "Measuring..."); publish (tpText, "True peak --");
        lastTarget = 1e9f; lastProgram = -1;
    }

    void notifyHost()
    {
        if (master == nullptr || notifying) return;
        notifying = true;
        for (int i = 0; i <= P_PAUSE; ++i) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
        master (&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
        notifying = false;
    }
    void loadPlatform (int i)
    {
        if (i < 0 || i >= kNumPlatforms) return;
        program = i;
        setPlain (P_PLATFORM, (float) i);
        if (i != kNumPlatforms - 1) { setPlain (P_TARGET, kPlatforms[i].target); setPlain (P_CEIL, kPlatforms[i].ceiling); }
    }
    void hostSet (int i, float norm)
    {
        const ParamDef& d = kParams[i];
        if (d.kind == K_READ) return;
        const float old = get (i), plain = toPlain (d, norm);
        if (d.kind == K_MOMENT)
        {
            if (notifying || plain < 0.5f || old >= 0.5f) { setPlain (i, plain); return; }
            if (i == P_PREV) loadPlatform ((program + kNumPlatforms - 1) % kNumPlatforms);
            if (i == P_NEXT) loadPlatform ((program + 1) % kNumPlatforms);
            if (i == P_RESET) resetReq.store (true);
            setPlain (i, 0.f);                          // spring back, and tell MPC so the next press is a press again
            notifyHost();
            return;
        }
        nv[i].store (clampf (norm, 0.f, 1.f)); v[i].store (plain);
        if (! notifying && i == P_PLATFORM && (int) plain != program) { loadPlatform ((int) plain); notifyHost(); }
    }

    // ---------------------------------------------------------------------------- display
    void send (int i, float plain, float threshold = 0.05f)
    {
        setPlain (i, plain);
        if (std::fabs (plain - sent[i]) < threshold) return;
        sent[i] = plain;
        if (master != nullptr) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
    }
    static void publish (Text& t, const char* text)
    {
        const int idle = 1 - t.live.load();
        std::snprintf (t.buf[idle], sizeof t.buf[idle], "%s", text);
        t.live.store (idle);
    }
    void setText (Text& t, int param, int& gen, const char* text)
    {
        if (std::strcmp (t.get(), text) == 0) return;
        publish (t, text);
        gen = (gen + 1) % 1000;
        send (param, (float) gen, 0.5f);               // a new value makes MPC fetch the new text
    }
    void update (bool stepped)
    {
        const float target = get (P_TARGET), userCeil = get (P_CEIL);
        const Platform& pf = kPlatforms[program];
        const bool paused = get (P_PAUSE) > 0.5f;
        const bool targetMoved = target != lastTarget || userCeil != lastCeil || paused != lastPaused || program != lastProgram;
        lastTarget = target; lastCeil = userCeil; lastPaused = paused; lastProgram = program;
        if (! stepped && ! targetMoved) return;
        const float M = (float) meter.momentary, S = (float) meter.shortTerm, I = (float) meter.integrated;
        send (P_M, M); send (P_S, S); send (P_I, I);
        send (P_LRA, (float) meter.range); send (P_MAXM, (float) meter.maxMomentary); send (P_MAXS, (float) meter.maxShortTerm);
        const float tpDb = clampf ((float) tp.dbtp(), kFloor, 12.f);
        send (P_TP, tpDb);
        send (P_TIME, std::floor ((float) meter.seconds()), 0.5f);
        const bool haveI = I > kFloor && meter.seconds() >= 3.0;
        // e.g. Spotify: -1 dBTP, but -2 dBTP once the master is louder than the target
        const bool loudMaster = haveI && pf.loudCeiling < 0.f && I > target + pf.tol;
        const float ceil = loudMaster ? std::fmin (userCeil, pf.loudCeiling) : userCeil;
        send (P_GAIN, haveI ? clampf (target - I, -40.f, 40.f) : 0.f);
        send (P_TPHEAD, tpDb > kFloor ? clampf (ceil - tpDb, -20.f, 80.f) : 0.f);
        send (P_TPOVER, tpDb > ceil ? 1.f : 0.f, 0.5f);
        send (P_REL, M > kFloor ? clampf (M - target, -kRelRange, kRelRange) : -kRelRange, 0.1f);

        // status lines
        char t[40];
        const int mode = pf.mode;
        const float diff = I - target;
        if (get (P_PAUSE) > 0.5f) std::snprintf (t, sizeof t, "Paused");
        else if (! haveI) std::snprintf (t, sizeof t, "Measuring...");
        else if (mode == 3) std::snprintf (t, sizeof t, "Not normalized: plays as is");
        else if (std::fabs (diff) <= pf.tol) std::snprintf (t, sizeof t, "On target");
        else if (diff > 0) std::snprintf (t, sizeof t, mode == 2 ? "%.1f LU above target" : "Turned down %.1f dB", (double) diff);
        else if (mode == 1) std::snprintf (t, sizeof t, "Turned up %.1f dB", (double) -diff);
        else if (mode == 0) std::snprintf (t, sizeof t, "Plays %.1f dB quieter", (double) -diff);
        else std::snprintf (t, sizeof t, "%.1f LU below target", (double) -diff);
        setText (statusText, P_STATUS, statusGen, t);
        if (tpDb <= kFloor) std::snprintf (t, sizeof t, "True peak --");
        else if (tpDb > ceil) std::snprintf (t, sizeof t, loudMaster ? "Loud master: peaks %.1f over -2" : "Peaks over ceiling by %.1f dB",
                                             (double) (tpDb - ceil));
        else std::snprintf (t, sizeof t, "Peaks OK, %.1f dB headroom", (double) (ceil - tpDb));
        setText (tpText, P_TPSTATUS, tpGen, t);

        // history: one column per second, newest on the right, relative to the target
        if (stepped && ++stepCount >= 10)
        {
            stepCount = 0;
            for (int i = 0; i < kHist - 1; ++i) hist[i] = hist[i + 1];
            hist[kHist - 1] = meter.shortTermValid() ? S : (M > kFloor ? M : kFloor);
            if (histCount < kHist) ++histCount;
        }
        if (stepped || targetMoved)
            for (int i = 0; i < kHist; ++i)
                send (P_HIST0 + i, hist[i] > kFloor ? clampf (hist[i] - target, -kHistRange, kHistRange) : kHistBlank, 0.1f);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (resetReq.exchange (false)) clear();
        bool stepped = false;
        if (get (P_PAUSE) < 0.5f)
            for (int i = 0; i < n; ++i) { stepped |= meter.add (inL[i], inR[i]); tp.add (inL[i], inR[i]); }
        if (outL != inL) std::memcpy (outL, inL, sizeof (float) * (size_t) n);   // audio passes through untouched
        if (outR != inR) std::memcpy (outR, inR, sizeof (float) * (size_t) n);
        update (stepped);
    }

    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float p = get (idx);
        switch (idx)
        {
            case P_STATUS:   std::snprintf (out, max, "%s", statusText.get()); return;
            case P_TPSTATUS: std::snprintf (out, max, "%s", tpText.get()); return;
            case P_TPOVER:   std::snprintf (out, max, "%s", p > 0.5f ? "OVER" : "OK"); return;
            default: break;
        }
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[(int) clampf (p, 0.f, (float) (d.n - 1))]); return; }
        if (d.kind == K_BOOL || d.kind == K_MOMENT) { std::snprintf (out, max, "%s", p >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_LUFS: case F_DBTP:
                if (p <= kFloor + 0.05f) std::snprintf (out, max, "-inf");
                else std::snprintf (out, max, p >= 0.05f ? "+%.1f" : "%.1f", (double) p);
                return;
            case F_LU:   std::snprintf (out, max, "%.1f", (double) p); return;
            case F_DB:   std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_REL:  if (p <= kHistBlank + 0.05f) std::snprintf (out, max, "--"); else std::snprintf (out, max, "%+.1f LU", (double) p); return;
            case F_TIME:
            {
                const int s = (int) p;
                std::snprintf (out, max, "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60); return;
            }
            default: std::snprintf (out, max, "%.2f", (double) p); return;
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
        case effSetProgram:       if (val >= 0 && val < kNumPlatforms) { p->loadPlatform ((int) val); p->notifyHost(); } return 0;
        case effGetProgram:       return p->program;
        case effGetProgramName:   copyStr (ptr, kPlatforms[p->program].name, 32); return 0;
        case effGetProgramNameIndexed:
            if (idx < 0 || idx >= kNumPlatforms) return 0;
            copyStr (ptr, kPlatforms[idx].name, 32); return 1;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].name); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].unit); return 0;
        case effGetParamDisplay:  // MPC (a JUCE host) reads into 256-byte buffers: status lines up to 31 characters
            if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[40]; p->display (idx, b, sizeof b); copyStr (ptr, b, 32); }
            return 0;
        case effCanBeAutomated:   return (idx == P_PLATFORM || idx == P_TARGET || idx == P_CEIL || idx == P_PAUSE) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) p->prepare (opt); return 0;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "Da Lufs Plug", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return 1004;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;
        default: return 0;
    }
}
} // namespace

LM_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    static bool inited = false;
    if (! inited) { initParams(); inited = true; }
    void* mem = std::calloc (1, sizeof (Plugin));   // no operator new: keeps libstdc++ out of the link
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    p->loadPlatform (0);
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic;
    fx.dispatcher = dispatcher;
    fx.process = processAccumulating;
    fx.processReplacing = processReplacing;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.numPrograms = kNumPlatforms;
    fx.numParams = P_COUNT;
    fx.numInputs = 2;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('D' << 24) | ('L' << 16) | ('P' << 8) | 'G';   // 'DLPG' = 0x444c5047
    fx.version = 1004;
    return &fx;
}
