// RadioReady EQ - professional 8-band EQ with a QUICK RADIO READY section, for Akai MPC OS (Gen1, 32-bit ARM).
// Dependency-free VST2 (hand-written ABI in vst2.h), needs only libc/libm. No GUI: MPC draws the skin from /sdcard/Synths.
//
//  - DSP (EQEngine): 8 fully parametric bands (Bell, Low/High Shelf, High/Low Pass 12/24/48 dB, Notch, Band Pass),
//    each on Stereo, Left/Mid or Right/Side; 64-bit filters; frequency/gain/Q glide, 10 ms crossfades on type/slope/
//    routing changes (click-free); flat bands skipped (bit-transparent when flat).
//  - QUICK RADIO READY: Amount + Low-End Control, Vocal Clarity, Air & Presence, Punch. Its boost is capped at 4 dB
//    and it is level-matched, so it can't push the output into clipping by itself. Amount 0 % = no effect.
//  - Input/Output gain, Auto Gain (level-matches the EQ), Stereo or Mid/Side, Zero Latency or HQ 2x oversampling.
//  - 56 factory presets (Init + 50 radio-ready + 5 translation), category filter, favourites, prev/next,
//    A/B compare, undo/redo, reset. Custom presets: MPC's own plugin preset save/load (every setting is a parameter).
//  - Display: EQ curve (64 points), 32-band analyzer (input or output), in/out peak meters - read-only parameters that
//    the plugin sends to MPC (audioMasterAutomate), only when they change.
#include "EQEngine.h"
#include "Oversampler.h"
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>

#define RR_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
using rr::FilterType;

// ------------------------------------------------------------------------------------------------ factory presets
struct PBand { int on, type; float freq, gain, q; int slope, target; };
struct FactoryPreset { const char* name; int category, mode; float quick[5]; float outGain; PBand b[8]; };
const FactoryPreset kPresets[] = {
#include "presets.inc"
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
static_assert (kNumPresets <= 64, "favourites are a 64-bit mask");
const char* presetNames[kNumPresets];   // filled in initParams()

// category filter (Prev / Next); preset categories 1..6 map straight onto filter entries 1..6
const char* const kFilterNames[] { "All", "Hip-Hop", "Vocals", "Drums", "Master Bus", "Instruments", "Translation", "Favorites" };
constexpr int kNumFilters = 8, kFilterFav = 7;

// ------------------------------------------------------------------------------------------------ parameters
constexpr int kBands = rr::kUserBands, kPerBand = 7;
enum BandParam { B_ON, B_TYPE, B_FREQ, B_GAIN, B_Q, B_SLOPE, B_TARGET };
constexpr int kCurvePts = 64, kSpecBands = 32, kMeters = 4;
enum
{
    P_PRESET, P_CATEGORY, P_FAV, P_PREV, P_NEXT,                                   // 0..4
    P_BAND0,                                                                       // 5..60
    P_IN = P_BAND0 + kBands * kPerBand, P_OUT, P_MODE, P_HQ, P_AUTOGAIN, P_BYPASS, // 61..66
    P_AMOUNT, P_LOWEND, P_VOCAL, P_AIR, P_PUNCH,                                   // 67..71
    P_ANALYZER, P_VIEW, P_AB, P_COPY, P_UNDO, P_REDO, P_RESET,                     // 72..78
    P_CURVE0,                                                                      // 79..142
    P_SPEC0 = P_CURVE0 + kCurvePts,                                                // 143..174
    P_METER0 = P_SPEC0 + kSpecBands,                                               // 175..178 in L, in R, out L, out R
    P_LEVEL = P_METER0 + kMeters,                                                  // 179 automatic level compensation
    P_COUNT                                                                        // 180
};
inline int bp (int band, int what) { return P_BAND0 + band * kPerBand + what; }
constexpr float kCurveRange = 18.f, kSpecFloor = -84.f, kMeterFloor = -60.f, kMeterTop = 6.f;
constexpr int kFftBits = 11, kFft = 1 << kFftBits;

const char* const kTypeNames[]   { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass" };
const char* const kSlopeNames[]  { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
const char* const kTargetNames[] { "Stereo", "Left/Mid", "Right/Side" };
const char* const kModeNames[]   { "Stereo", "Mid/Side" };
const char* const kHQNames[]     { "Zero Latency", "HQ 2x" };
const char* const kAnaNames[]    { "Analyzer Off", "Analyzer Out", "Analyzer In" };
const char* const kViewNames[]   { "View L/Mid", "View R/Side" };
const char* const kABNames[]     { "A", "B" };

enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_LOG, K_MOMENT, K_READ };
enum Fmt  { F_NUM, F_HZ, F_GAIN, F_Q, F_PCT, F_DB, F_METER };
struct ParamDef
{
    char name[20]; const char* unit; Kind kind;
    float lo, hi, step, def;
    const char* const* choices; int n; Fmt fmt;
};
ParamDef kParams[P_COUNT];
float kCurveHz[kCurvePts], kSpecLo[kSpecBands], kSpecHi[kSpecBands];

void def (int i, const char* name, const char* unit, Kind k, float lo, float hi, float step, float d,
          const char* const* ch = nullptr, int n = 0, Fmt f = F_NUM)
{
    ParamDef& p = kParams[i];
    std::snprintf (p.name, sizeof p.name, "%s", name);
    p.unit = unit; p.kind = k; p.lo = lo; p.hi = hi; p.step = step; p.def = d; p.choices = ch; p.n = n; p.fmt = f;
}
void initParams()
{
    for (int i = 0; i < kNumPresets; ++i) presetNames[i] = kPresets[i].name;
    def (P_PRESET, "Preset", "", K_CHOICE, 0, (float) (kNumPresets - 1), 1, 0, presetNames, kNumPresets);
    def (P_CATEGORY, "Category", "", K_CHOICE, 0, kNumFilters - 1, 1, 0, kFilterNames, kNumFilters);
    def (P_FAV, "Favorite", "", K_BOOL, 0, 1, 1, 0);
    def (P_PREV, "Prev Preset", "", K_MOMENT, 0, 1, 1, 0);
    def (P_NEXT, "Next Preset", "", K_MOMENT, 0, 1, 1, 0);
    const FactoryPreset& init = kPresets[0];
    for (int b = 0; b < kBands; ++b)
    {
        char nm[20];
        auto name = [&] (const char* what) { std::snprintf (nm, sizeof nm, "Band %d %s", b + 1, what); return nm; };
        const PBand& d = init.b[b];
        def (bp (b, B_ON), name ("On"), "", K_BOOL, 0, 1, 1, (float) d.on);
        def (bp (b, B_TYPE), name ("Type"), "", K_CHOICE, 0, rr::kNumFilterTypes - 1, 1, (float) d.type, kTypeNames, rr::kNumFilterTypes);
        def (bp (b, B_FREQ), name ("Freq"), "Hz", K_LOG, 20, 20000, 0, d.freq, nullptr, 0, F_HZ);
        def (bp (b, B_GAIN), name ("Gain"), "dB", K_FLOAT, -18, 18, 0.1f, d.gain, nullptr, 0, F_GAIN);
        def (bp (b, B_Q), name ("Q"), "", K_LOG, 0.1f, 10, 0, d.q, nullptr, 0, F_Q);
        def (bp (b, B_SLOPE), name ("Slope"), "", K_CHOICE, 0, 2, 1, (float) d.slope, kSlopeNames, 3);
        def (bp (b, B_TARGET), name ("Channel"), "", K_CHOICE, 0, 2, 1, (float) d.target, kTargetNames, 3);
    }
    def (P_IN, "Input Gain", "dB", K_FLOAT, -24, 24, 0.1f, 0, nullptr, 0, F_GAIN);
    def (P_OUT, "Output Gain", "dB", K_FLOAT, -24, 24, 0.1f, 0, nullptr, 0, F_GAIN);
    def (P_MODE, "Stereo Mode", "", K_CHOICE, 0, 1, 1, 0, kModeNames, 2);
    def (P_HQ, "Quality", "", K_CHOICE, 0, 1, 1, 0, kHQNames, 2);
    def (P_AUTOGAIN, "Auto Gain", "", K_BOOL, 0, 1, 1, 0);
    def (P_BYPASS, "Bypass", "", K_BOOL, 0, 1, 1, 0);
    def (P_AMOUNT, "RR Amount", "%", K_FLOAT, 0, 100, 1, 0, nullptr, 0, F_PCT);
    def (P_LOWEND, "Low-End Control", "%", K_FLOAT, 0, 100, 1, 0, nullptr, 0, F_PCT);
    def (P_VOCAL, "Vocal Clarity", "%", K_FLOAT, 0, 100, 1, 0, nullptr, 0, F_PCT);
    def (P_AIR, "Air & Presence", "%", K_FLOAT, 0, 100, 1, 0, nullptr, 0, F_PCT);
    def (P_PUNCH, "Punch", "%", K_FLOAT, 0, 100, 1, 0, nullptr, 0, F_PCT);
    def (P_ANALYZER, "Analyzer", "", K_CHOICE, 0, 2, 1, 1, kAnaNames, 3);
    def (P_VIEW, "Curve View", "", K_CHOICE, 0, 1, 1, 0, kViewNames, 2);
    def (P_AB, "A/B", "", K_CHOICE, 0, 1, 1, 0, kABNames, 2);
    def (P_COPY, "Copy to Other", "", K_MOMENT, 0, 1, 1, 0);
    def (P_UNDO, "Undo", "", K_MOMENT, 0, 1, 1, 0);
    def (P_REDO, "Redo", "", K_MOMENT, 0, 1, 1, 0);
    def (P_RESET, "Reset Preset", "", K_MOMENT, 0, 1, 1, 0);
    for (int i = 0; i < kCurvePts; ++i)
    {
        kCurveHz[i] = 20.f * std::pow (1000.f, (float) i / (kCurvePts - 1));
        char nm[20];
        if (kCurveHz[i] < 1000.f) std::snprintf (nm, sizeof nm, "Curve %.0f", (double) kCurveHz[i]);
        else std::snprintf (nm, sizeof nm, "Curve %.1fk", (double) (kCurveHz[i] * 0.001f));
        def (P_CURVE0 + i, nm, "dB", K_READ, -kCurveRange, kCurveRange, 0, 0, nullptr, 0, F_DB);
    }
    for (int i = 0; i < kSpecBands; ++i)
    {
        kSpecLo[i] = 20.f * std::pow (1000.f, (float) i / kSpecBands);
        kSpecHi[i] = 20.f * std::pow (1000.f, (float) (i + 1) / kSpecBands);
        char nm[20]; std::snprintf (nm, sizeof nm, "Spectrum %d", i + 1);
        def (P_SPEC0 + i, nm, "dB", K_READ, kSpecFloor, 0, 0, kSpecFloor, nullptr, 0, F_DB);
    }
    const char* const meterNames[] { "Meter In L", "Meter In R", "Meter Out L", "Meter Out R" };
    for (int i = 0; i < kMeters; ++i) def (P_METER0 + i, meterNames[i], "dB", K_READ, kMeterFloor, kMeterTop, 0, kMeterFloor, nullptr, 0, F_METER);
    def (P_LEVEL, "Level Comp", "dB", K_READ, -12, 12, 0, 0, nullptr, 0, F_GAIN);
}

inline bool isState (int i)   // what presets, A/B, undo and redo capture
{
    return i == P_PRESET || (i >= P_BAND0 && i <= P_PUNCH && i != P_BYPASS && i != P_HQ);   // Quality is a machine setting
}

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
float toPlain (const ParamDef& d, float norm)
{
    norm = clampf (norm, 0.f, 1.f);
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return norm >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return std::floor (norm * (float) (d.n - 1) + 0.5f);
    if (d.kind == K_LOG) return d.lo * std::pow (d.hi / d.lo, norm);
    float v = d.lo + norm * (d.hi - d.lo);
    if (d.step > 0.f) v = d.lo + std::floor ((v - d.lo) / d.step + 0.5f) * d.step;
    return clampf (v, d.lo, d.hi);
}
float toNorm (const ParamDef& d, float plain)
{
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return plain >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return d.n > 1 ? clampf (plain / (float) (d.n - 1), 0.f, 1.f) : 0.f;
    if (d.kind == K_LOG) return clampf (std::log (clampf (plain, d.lo, d.hi) / d.lo) / std::log (d.hi / d.lo), 0.f, 1.f);
    return clampf ((plain - d.lo) / (d.hi - d.lo), 0.f, 1.f);
}
void copyStr (void* dst, const char* src, size_t max = 24)
{
    if (dst == nullptr) return;
    std::strncpy ((char*) dst, src, max - 1);
    ((char*) dst)[max - 1] = 0;
}
void fmtHz (char* out, size_t max, float hz)
{
    if (hz < 1000.f) std::snprintf (out, max, "%.0f Hz", (double) hz);
    else if (hz < 10000.f) std::snprintf (out, max, "%.2f kHz", (double) (hz * 0.001f));
    else std::snprintf (out, max, "%.1f kHz", (double) (hz * 0.001f));
}

// ------------------------------------------------------------------------------------------------ favourites
// One list for all instances, kept next to the plugin (/sdcard/vst/radioready.favorites: one preset name per line).
std::atomic<uint64_t> gFavs { 0 };
const char* favPath()
{
    const char* e = std::getenv ("RR_FAVORITES_FILE");
    return e != nullptr && *e != 0 ? e : "/sdcard/vst/radioready.favorites";
}
void loadFavs()
{
    FILE* f = std::fopen (favPath(), "r");
    if (f == nullptr) return;
    uint64_t m = 0; char line[96];
    while (std::fgets (line, sizeof line, f) != nullptr)
    {
        line[std::strcspn (line, "\r\n")] = 0;
        for (int i = 0; i < kNumPresets; ++i) if (std::strcmp (line, kPresets[i].name) == 0) m |= 1ull << i;
    }
    std::fclose (f);
    gFavs.store (m);
}
void saveFavs()
{
    char tmp[512]; std::snprintf (tmp, sizeof tmp, "%s.new", favPath());
    FILE* f = std::fopen (tmp, "w");
    if (f == nullptr) return;
    const uint64_t m = gFavs.load();
    for (int i = 0; i < kNumPresets; ++i) if (m & (1ull << i)) std::fprintf (f, "%s\n", kPresets[i].name);
    std::fclose (f);
    std::rename (tmp, favPath());
}

// ------------------------------------------------------------------------------------------------ plugin
struct Spin
{
    std::atomic<int> f { 0 };
    void lock() { while (f.exchange (1, std::memory_order_acquire) != 0) {} }
    bool tryLock() { return f.exchange (1, std::memory_order_acquire) == 0; }
    void unlock() { f.store (0, std::memory_order_release); }
};

struct Snapshot { float v[P_CURVE0]; };
constexpr int kHistory = 32;
constexpr int kChunk = 64;

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;

    std::atomic<float> v[P_COUNT];      // plain values
    std::atomic<float> nv[P_COUNT];     // exact normalised positions (returned unrounded: no stuck Q-Link steps)
    std::atomic<unsigned> seq { 0 };    // odd while a whole state (preset, undo, A/B) is being written
    std::atomic<int> paramGen { 1 };    // bumped on every change that moves the curve
    int program = 0;
    float sr = 44100.f;
    bool notifying = false;             // re-entrancy guard for host notifications

    // undo / redo / A/B (guarded by lock; the audio thread only ever try-locks)
    Spin lock;
    Snapshot hist[kHistory]; int histCur = 0, histTop = 0;
    Snapshot slot[2]; bool slotValid[2] { false, false }; int abSide = 0;
    Snapshot lastSeen; int quiet = 0; bool dirty = false;

    // DSP (audio thread)
    rr::EQEngine eng, engHQ;
    rr::Oversampler2x os;
    bool hq = false, hqFading = false; int hqFadePos = 0, hqFadeLen = 441;
    double bufL[kChunk], bufR[kChunk], hqL[2 * kChunk], hqR[2 * kChunk], xL[kChunk], xR[kChunk];
    float anaIn[kChunk * 2];

    // display (audio thread)
    int curveGen = 0, curveCountdown = 0, curveView = -1; float curveSent[kCurvePts];
    float ring[kFft] {}; int ringPos = 0, specCountdown = 0; bool specWasOn = false;
    float fre[kFft] {}, fim[kFft] {}, win[kFft] {}, cosT[kFft / 2] {}, sinT[kFft / 2] {};
    int rev[kFft] {};
    float specDisp[kSpecBands], specSent[kSpecBands];
    float meter[kMeters], meterSent[kMeters]; int meterCountdown = 0;
    float levelSent = 1e9f;

    Plugin()
    {
        for (int i = 0; i < P_COUNT; ++i) setPlain (i, kParams[i].def);
        for (int i = 0; i < kFft; ++i)
        {
            win[i] = 0.5f - 0.5f * std::cos (6.2831853f * i / kFft);
            int r = 0; for (int b = 0; b < kFftBits; ++b) r |= ((i >> b) & 1) << (kFftBits - 1 - b);
            rev[i] = r;
        }
        for (int i = 0; i < kFft / 2; ++i) { cosT[i] = std::cos (6.2831853f * i / kFft); sinT[i] = -std::sin (6.2831853f * i / kFft); }
        prepare (44100.f);
    }

    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    float get (int i) const { return v[i].load(); }

    // ---------------------------------------------------------------------------- state snapshots
    void capture (Snapshot& s) const { for (int i = 0; i < P_CURVE0; ++i) s.v[i] = v[i].load(); }
    static bool sameState (const Snapshot& a, const Snapshot& b)
    {
        for (int i = 0; i < P_CURVE0; ++i) if (isState (i) && a.v[i] != b.v[i]) return false;
        return true;
    }
    void applySnapshot (const Snapshot& s)          // caller holds lock
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // odd: the audio thread keeps its previous settings
        for (int i = 0; i < P_CURVE0; ++i) if (isState (i)) setPlain (i, s.v[i]);
        program = (int) s.v[P_PRESET];
        setPlain (P_FAV, (gFavs.load() >> program) & 1 ? 1.f : 0.f);
        seq.fetch_add (1, std::memory_order_acq_rel);
        paramGen.fetch_add (1);
    }
    void pushHistory (const Snapshot& s)            // caller holds lock
    {
        if (histCur == kHistory - 1) { std::memmove (&hist[0], &hist[1], sizeof (Snapshot) * (kHistory - 1)); --histCur; }
        hist[++histCur] = s; histTop = histCur;
    }
    void commitPending()                            // caller holds lock: an edit that hasn't been pushed yet
    {
        Snapshot now; capture (now);
        if (! sameState (now, hist[histCur])) pushHistory (now);
        lastSeen = now; dirty = false; quiet = 0;
    }
    void resetHistory()
    {
        lock.lock();
        capture (hist[0]); histCur = histTop = 0; lastSeen = hist[0]; dirty = false; quiet = 0;
        slotValid[0] = slotValid[1] = false;
        lock.unlock();
    }

    // ---------------------------------------------------------------------------- presets
    void presetSnapshot (int i, Snapshot& s)
    {
        capture (s);
        const FactoryPreset& p = kPresets[i];
        s.v[P_PRESET] = (float) i;
        for (int b = 0; b < kBands; ++b)
        {
            const PBand& d = p.b[b];
            s.v[bp (b, B_ON)] = (float) d.on; s.v[bp (b, B_TYPE)] = (float) d.type; s.v[bp (b, B_FREQ)] = d.freq;
            s.v[bp (b, B_GAIN)] = d.gain; s.v[bp (b, B_Q)] = d.q; s.v[bp (b, B_SLOPE)] = (float) d.slope; s.v[bp (b, B_TARGET)] = (float) d.target;
        }
        s.v[P_IN] = 0.f; s.v[P_OUT] = p.outGain; s.v[P_MODE] = (float) p.mode; s.v[P_AUTOGAIN] = 0.f;
        for (int k = 0; k < 5; ++k) s.v[P_AMOUNT + k] = p.quick[k];
        // Quality (HQ) is a machine setting, not part of a preset: keep it
    }
    void loadPreset (int i, bool undoable = true)
    {
        if (i < 0 || i >= kNumPresets) return;
        Snapshot s; presetSnapshot (i, s);
        lock.lock();
        if (undoable) commitPending();
        applySnapshot (s);
        if (undoable) commitPending();
        lock.unlock();
    }
    bool inFilter (int i, int filter) const
    {
        if (filter == 0) return true;
        if (filter == kFilterFav) return (gFavs.load() >> i) & 1;
        return kPresets[i].category == filter;
    }
    void step (int dir)
    {
        const int filter = (int) get (P_CATEGORY);
        int i = program;
        for (int n = 0; n < kNumPresets; ++n)
        {
            i = (i + dir + kNumPresets) % kNumPresets;
            if (inFilter (i, filter)) { loadPreset (i); return; }
        }
    }

    // ---------------------------------------------------------------------------- host side
    void notifyHost()
    {
        if (master == nullptr || notifying) return;
        notifying = true;
        for (int i = 0; i < P_CURVE0; ++i) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
        master (&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
        notifying = false;
    }

    void hostSet (int i, float norm)
    {
        const ParamDef& d = kParams[i];
        if (d.kind == K_READ) return;
        const float old = get (i);
        const float plain = toPlain (d, norm);
        if (d.kind == K_MOMENT)
        {
            if (notifying || plain < 0.5f || old >= 0.5f) { setPlain (i, plain); return; }   // act on the press only
            setPlain (i, 1.f);
            action (i);
            setPlain (i, 0.f);                          // spring back, and tell MPC so the next press is a press again
            notifyHost();
            return;
        }
        nv[i].store (clampf (norm, 0.f, 1.f)); v[i].store (plain);
        if (notifying) return;
        switch (i)
        {
            case P_PRESET:
                if ((int) plain != program) { loadPreset ((int) plain); notifyHost(); }
                return;
            case P_HQ:                                  // latency: 0 (zero latency) or 31 samples (HQ 2x)
                fx.initialDelay = plain > 0.5f ? rr::Oversampler2x::kLatency : 0;
                if (master != nullptr && (int) old != (int) plain) master (&fx, 13 /* audioMasterIOChanged */, 0, 0, nullptr, 0.f);
                return;
            case P_FAV:
            {
                const uint64_t bit = 1ull << program, m = gFavs.load();
                const uint64_t nm = plain >= 0.5f ? (m | bit) : (m & ~bit);
                if (nm != m) { gFavs.store (nm); saveFavs(); }
                return;
            }
            case P_AB:
                if ((int) plain != abSide) switchAB ((int) plain);
                return;
            default:
                if (i >= P_BAND0 && i <= P_PUNCH) paramGen.fetch_add (1);
                if (i == P_VIEW) paramGen.fetch_add (1);
                return;
        }
    }

    void switchAB (int side)
    {
        lock.lock();
        commitPending();
        capture (slot[abSide]); slotValid[abSide] = true;
        if (! slotValid[side]) { slot[side] = slot[abSide]; slotValid[side] = true; }
        abSide = side;
        applySnapshot (slot[side]);
        setPlain (P_AB, (float) side);
        commitPending();
        lock.unlock();
        notifyHost();
    }

    void action (int i)
    {
        switch (i)
        {
            case P_PREV: step (-1); break;
            case P_NEXT: step (+1); break;
            case P_RESET: loadPreset (program); break;
            case P_COPY:
                lock.lock();
                capture (slot[1 - abSide]); slotValid[1 - abSide] = true;
                lock.unlock();
                break;
            case P_UNDO:
                lock.lock();
                commitPending();
                if (histCur > 0) { --histCur; applySnapshot (hist[histCur]); capture (lastSeen); }
                lock.unlock();
                break;
            case P_REDO:
                lock.lock();
                commitPending();
                if (histCur < histTop) { ++histCur; applySnapshot (hist[histCur]); capture (lastSeen); }
                lock.unlock();
                break;
            default: break;
        }
    }

    // audio thread: an edit becomes one undo step once the controls have been still for 400 ms
    void trackEdits (int n)
    {
        if (! lock.tryLock()) return;
        Snapshot now; capture (now);
        if (! sameState (now, lastSeen)) { lastSeen = now; quiet = 0; dirty = true; }
        else if (dirty && (quiet += n) >= (int) (0.4f * sr))
        {
            if (! sameState (now, hist[histCur])) pushHistory (now);
            dirty = false;
        }
        lock.unlock();
    }

    // ---------------------------------------------------------------------------- DSP settings
    bool readSettings (rr::BandSettings* bands, rr::QuickSettings& q, int& mode, float& in, float& out, bool& ag, bool& byp, bool& hqOn)
    {
        const unsigned s1 = seq.load (std::memory_order_acquire);
        if (s1 & 1) return false;
        for (int b = 0; b < kBands; ++b)
        {
            rr::BandSettings& s = bands[b];
            s.on = get (bp (b, B_ON)) > 0.5f;
            s.type = (FilterType) (int) clampf (get (bp (b, B_TYPE)), 0, rr::kNumFilterTypes - 1);
            s.freq = get (bp (b, B_FREQ)); s.gainDb = get (bp (b, B_GAIN)); s.q = get (bp (b, B_Q));
            s.slope = (int) get (bp (b, B_SLOPE)); s.target = (rr::Target) (int) clampf (get (bp (b, B_TARGET)), 0, 2);
        }
        q.amount = get (P_AMOUNT) * 0.01; q.lowEnd = get (P_LOWEND) * 0.01; q.vocal = get (P_VOCAL) * 0.01;
        q.air = get (P_AIR) * 0.01; q.punch = get (P_PUNCH) * 0.01;
        mode = (int) get (P_MODE); in = get (P_IN); out = get (P_OUT);
        ag = get (P_AUTOGAIN) > 0.5f; byp = get (P_BYPASS) > 0.5f; hqOn = get (P_HQ) > 0.5f;
        return seq.load (std::memory_order_acquire) == s1;
    }
    void pushSettings()
    {
        rr::BandSettings bands[kBands]; rr::QuickSettings q; int mode; float in, out; bool ag, byp, hqOn;
        if (! readSettings (bands, q, mode, in, out, ag, byp, hqOn)) return;   // a preset is being written: next chunk
        for (rr::EQEngine* e : { &eng, &engHQ })
        {
            for (int b = 0; b < kBands; ++b) e->setUserBand (b, bands[b]);
            e->setQuick (q); e->setStereoMode ((rr::StereoMode) mode); e->setGains (in, out); e->setAutoGain (ag); e->setBypass (byp);
        }
        if (hqOn != hq)
        {
            hq = hqOn; hqFading = true; hqFadePos = 0;
            (hq ? engHQ : eng).reset();                 // the incoming path starts clean, on its targets
            if (hq) os.reset();
        }
    }

    void prepare (float rate)
    {
        sr = rate;
        eng.prepare (rate); engHQ.prepare (2.0 * rate); os.reset();
        hqFadeLen = (int) (0.01f * rate); hqFading = false;
        resetDisplay();
        paramGen.fetch_add (1);
    }
    void resetDisplay()
    {
        curveGen = 0; curveCountdown = 0; curveView = -1;
        for (float& c : curveSent) c = 1e9f;
        std::memset (ring, 0, sizeof ring); ringPos = 0; specCountdown = 0;
        for (int i = 0; i < kSpecBands; ++i) { specDisp[i] = kSpecFloor; specSent[i] = 1e9f; }
        for (int i = 0; i < kMeters; ++i) { meter[i] = kMeterFloor; meterSent[i] = 1e9f; }
        levelSent = 1e9f;
    }
    void resetDSP() { eng.reset(); engHQ.reset(); os.reset(); hqFading = false; resetDisplay(); }

    // ---------------------------------------------------------------------------- display
    void send (int i, float plain)
    {
        setPlain (i, plain);
        if (master != nullptr) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
    }
    void updateCurve (int n)
    {
        curveCountdown -= n;
        const int gen = paramGen.load(), view = (int) get (P_VIEW);
        if ((gen == curveGen && view == curveView) || curveCountdown > 0) return;
        rr::BandSettings bands[kBands]; rr::QuickSettings q; int mode; float in, out; bool ag, byp, hqOn;
        if (! readSettings (bands, q, mode, in, out, ag, byp, hqOn)) return;
        curveGen = gen; curveView = view; curveCountdown = (int) (sr / 20.f);
        rr::BandSettings all[kBands + rr::kQuickBands]; int count = 0;
        const rr::Target hidden = view == 0 ? rr::Target::RightOrSide : rr::Target::LeftOrMid;
        for (int b = 0; b < kBands; ++b) if (bands[b].target != hidden) all[count++] = bands[b];
        rr::quickBands (q, all + count, sr); count += rr::kQuickBands;
        for (int i = 0; i < kCurvePts; ++i)
        {
            double db = 0;
            for (int b = 0; b < count; ++b) db += rr::bandResponseDb (all[b], kCurveHz[i], sr);
            const float d = clampf ((float) db, -kCurveRange, kCurveRange);
            if (std::fabs (d - curveSent[i]) > 0.05f) { curveSent[i] = d; send (P_CURVE0 + i, d); }
            else setPlain (P_CURVE0 + i, d);
        }
    }
    void fft()
    {
        for (int i = 0; i < kFft; ++i) if (rev[i] > i) { float t = fre[i]; fre[i] = fre[rev[i]]; fre[rev[i]] = t; t = fim[i]; fim[i] = fim[rev[i]]; fim[rev[i]] = t; }
        for (int len = 2; len <= kFft; len <<= 1)
        {
            const int half = len >> 1, stp = kFft / len;
            for (int i = 0; i < kFft; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float wr = cosT[j * stp], wi = sinT[j * stp];
                    const int a = i + j, b = a + half;
                    const float tr = fre[b] * wr - fim[b] * wi, ti = fre[b] * wi + fim[b] * wr;
                    fre[b] = fre[a] - tr; fim[b] = fim[a] - ti; fre[a] += tr; fim[a] += ti;
                }
        }
    }
    void sendSpec (int i, float db)
    {
        setPlain (P_SPEC0 + i, db);
        if (std::fabs (db - specSent[i]) > 2.f || (db <= kSpecFloor && specSent[i] > kSpecFloor))
        { specSent[i] = db; send (P_SPEC0 + i, db); }
    }
    void analyze (const float* L, const float* R, int n)
    {
        if ((int) get (P_ANALYZER) == 0)
        {
            if (specWasOn) for (int i = 0; i < kSpecBands; ++i) { specDisp[i] = kSpecFloor; sendSpec (i, kSpecFloor); }
            specWasOn = false; return;
        }
        specWasOn = true;
        for (int i = 0; i < n; ++i) { ring[ringPos] = 0.5f * (L[i] + R[i]); ringPos = (ringPos + 1) & (kFft - 1); }
        specCountdown -= n;
        if (specCountdown > 0) return;
        const int hop = (int) (sr / 14.f);
        specCountdown += hop;
        for (int i = 0; i < kFft; ++i) { fre[i] = ring[(ringPos + i) & (kFft - 1)] * win[i]; fim[i] = 0.f; }
        fft();
        const float binHz = sr / kFft, norm = 4.f / kFft, fall = 30.f * hop / sr;
        for (int bnd = 0; bnd < kSpecBands; ++bnd)
        {
            int k0 = (int) std::ceil (kSpecLo[bnd] / binHz), k1 = (int) std::floor (kSpecHi[bnd] / binHz);
            if (k1 < k0) k0 = k1 = (int) std::floor (std::sqrt (kSpecLo[bnd] * kSpecHi[bnd]) / binHz + 0.5f);
            k0 = k0 < 1 ? 1 : k0; k1 = k1 > kFft / 2 - 1 ? kFft / 2 - 1 : k1;
            float mag = 0.f;
            for (int k = k0; k <= k1; ++k) { const float m = fre[k] * fre[k] + fim[k] * fim[k]; if (m > mag) mag = m; }
            const float db = clampf (10.f * std::log10 (mag * norm * norm + 1e-20f), kSpecFloor, 0.f);
            specDisp[bnd] = db > specDisp[bnd] ? db : std::fmax (db, specDisp[bnd] - fall);
            sendSpec (bnd, specDisp[bnd]);
        }
    }
    void meters (const float* const* ch, int n)
    {
        const float fall = 20.f * n / sr;              // peak meters, 20 dB/s release
        for (int m = 0; m < kMeters; ++m)
        {
            float pk = 0.f;
            for (int i = 0; i < n; ++i) pk = std::fmax (pk, std::fabs (ch[m][i]));
            const float db = clampf (20.f * std::log10 (pk + 1e-9f), kMeterFloor, kMeterTop);
            meter[m] = db > meter[m] ? db : std::fmax (db, meter[m] - fall);
        }
        meterCountdown -= n;
        if (meterCountdown > 0) return;
        meterCountdown += (int) (sr / 25.f);            // at most 25 updates/s
        for (int m = 0; m < kMeters; ++m)
        {
            if (std::fabs (meter[m] - meterSent[m]) >= 1.f || (meter[m] <= kMeterFloor && meterSent[m] > kMeterFloor))
            { meterSent[m] = meter[m]; send (P_METER0 + m, meter[m]); }
            else setPlain (P_METER0 + m, meter[m]);
        }
        const rr::EQEngine& e = hq ? engHQ : eng;
        const float lvl = clampf ((float) (e.quickCompensationDb() + e.autoGainDb()), -12.f, 12.f);
        if (std::fabs (lvl - levelSent) >= 0.1f) { levelSent = lvl; send (P_LEVEL, lvl); }
    }

    // ---------------------------------------------------------------------------- audio
    void runHQ (double* l, double* r, int m)
    {
        for (int i = 0; i < m; ++i) { os.up (0, l[i], hqL[2 * i], hqL[2 * i + 1]); os.up (1, r[i], hqR[2 * i], hqR[2 * i + 1]); }
        engHQ.process (hqL, hqR, 2 * m);
        for (int i = 0; i < m; ++i) { l[i] = os.down (0, hqL[2 * i], hqL[2 * i + 1]); r[i] = os.down (1, hqR[2 * i], hqR[2 * i + 1]); }
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = (n - pos) < kChunk ? (n - pos) : kChunk;
            pushSettings();
            const float* il = inL + pos; const float* ir = inR + pos;
            for (int i = 0; i < m; ++i) { bufL[i] = il[i]; bufR[i] = ir[i]; }
            if (! hqFading)
            {
                if (hq) runHQ (bufL, bufR, m); else eng.process (bufL, bufR, m);
            }
            else
            {
                std::memcpy (xL, bufL, sizeof (double) * (size_t) m); std::memcpy (xR, bufR, sizeof (double) * (size_t) m);
                eng.process (hq ? xL : bufL, hq ? xR : bufR, m);        // zero-latency path
                runHQ (hq ? bufL : xL, hq ? bufR : xR, m);              // oversampled path
                for (int i = 0; i < m; ++i)                             // buf = incoming path, x = outgoing
                {
                    const double t = hqFadePos < hqFadeLen ? (double) ++hqFadePos / hqFadeLen : 1.0;
                    bufL[i] = xL[i] + (bufL[i] - xL[i]) * t; bufR[i] = xR[i] + (bufR[i] - xR[i]) * t;
                }
                if (hqFadePos >= hqFadeLen) { hqFading = false; (hq ? eng : engHQ).reset(); if (! hq) os.reset(); }
            }
            float* ol = outL + pos; float* orr = outR + pos;
            // input copies first (in-place hosts)
            for (int i = 0; i < m; ++i) { anaIn[i] = il[i]; anaIn[kChunk + i] = ir[i]; }
            for (int i = 0; i < m; ++i)
            {
                ol[i] = (float) (std::fabs (bufL[i]) < 1e-30 ? 0.0 : bufL[i]);
                orr[i] = (float) (std::fabs (bufR[i]) < 1e-30 ? 0.0 : bufR[i]);
            }
            const float* ch[kMeters] { anaIn, anaIn + kChunk, ol, orr };
            meters (ch, m);
            if ((int) get (P_ANALYZER) == 2) analyze (anaIn, anaIn + kChunk, m); else analyze (ol, orr, m);
            updateCurve (m);
            trackEdits (m);
        }
    }

    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float p = get (idx);
        if (idx == P_FAV) { std::snprintf (out, max, "%s", p >= 0.5f ? "Favorite" : "-"); return; }
        if (idx >= P_BAND0 && idx < P_IN && (idx - P_BAND0) % kPerBand == B_TARGET)
        {
            const bool ms = get (P_MODE) > 0.5f; const int t = (int) p;
            std::snprintf (out, max, "%s", t == 0 ? "Stereo" : (t == 1 ? (ms ? "Mid" : "Left") : (ms ? "Side" : "Right")));
            return;
        }
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[(int) clampf (p, 0.f, (float) (d.n - 1))]); return; }
        if (d.kind == K_BOOL || d.kind == K_MOMENT) { std::snprintf (out, max, "%s", p >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_HZ:    fmtHz (out, max, p); return;
            case F_GAIN:  std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_Q:     std::snprintf (out, max, "%.2f", (double) p); return;
            case F_PCT:   std::snprintf (out, max, "%.0f%%", (double) p); return;
            case F_DB:    std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_METER: if (p <= kMeterFloor) std::snprintf (out, max, "-inf"); else std::snprintf (out, max, "%.1f dB", (double) p); return;
            default:      std::snprintf (out, max, "%.2f", (double) p); return;
        }
    }
};

// ------------------------------------------------------------------------------------------------ VST2 entry points
void processReplacing (AEffect* e, float** in, float** out, int32_t n)
{
    static_cast<Plugin*> (e->object)->process (in[0], in[1], out[0], out[1], n);
}
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)   // legacy VST2 process()
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
        case effClose:            p->~Plugin(); std::free (p); return 0;      // AEffect is embedded in Plugin
        case effSetProgram:
            if (val >= 0 && val < kNumPresets) { p->loadPreset ((int) val); p->notifyHost(); }
            return 0;
        case effGetProgram:       return p->program;
        // names up to 25 characters: MPC (a JUCE host) reads names into 256-byte buffers
        case effGetProgramName:   copyStr (ptr, kPresets[p->program].name, 32); return 0;
        case effGetProgramNameIndexed:
            if (idx < 0 || idx >= kNumPresets) return 0;
            copyStr (ptr, kPresets[idx].name, 32); return 1;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].name); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].unit); return 0;
        case effGetParamDisplay:
            if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[32]; p->display (idx, b, sizeof b); copyStr (ptr, b, 32); }
            return 0;
        case effCanBeAutomated:   return (idx >= 0 && idx < P_CURVE0 && kParams[idx].kind != K_MOMENT) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) p->prepare (opt); return 0;
        case effMainsChanged:     if (val != 0) p->resetDSP(); return 0;
        case effSetBypass:        p->setPlain (P_BYPASS, val ? 1.f : 0.f); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "RadioReady EQ", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return 1002;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return 1;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;      // 32-bit float I/O (64-bit inside)
        case effCanDo:
            if (ptr != nullptr && std::strcmp ((const char*) ptr, "bypass") == 0) return 1;
            return 0;
        default: return 0;
    }
}
} // namespace

RR_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    static bool inited = false;
    if (! inited) { initParams(); loadFavs(); inited = true; }
    void* mem = std::calloc (1, sizeof (Plugin));   // no operator new: keeps libstdc++ out of the link
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    p->loadPreset (0, false);
    p->resetHistory();
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic;
    fx.dispatcher = dispatcher;
    fx.process = processAccumulating;
    fx.processReplacing = processReplacing;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.numPrograms = kNumPresets;
    fx.numParams = P_COUNT;
    fx.numInputs = 2;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('R' << 24) | ('R' << 16) | ('E' << 8) | 'Q';   // 'RREQ' = 0x52524551
    fx.version = 1002;
    return &fx;
}
