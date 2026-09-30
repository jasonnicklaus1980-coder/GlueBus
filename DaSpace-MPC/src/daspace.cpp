// Da Space - spatial & reverb plugin for Akai MPC OS (Gen1, 32-bit ARM), built from the GlueBus / RadioReady
// components (dependency-free VST2 core, parameter model, host notification, packaging, skin pipeline).
// Needs only libc/libm. No GUI: MPC draws the skin from /sdcard/Synths.
// Seven algorithms (Room, Hall, Plate, Classic, Ice, Meta, Reflex - see Reverb.h), 34 presets including High Sky.
#include "Reverb.h"
#include "presets.h"
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#define SPACE_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
enum
{
    P_PRESET, P_PREV, P_NEXT, P_ALGO, P_MIX, P_PRE, P_SIZE, P_DECAY, P_DAMP, P_LOWCUT, P_DIFF, P_MOD, P_WIDTH,
    P_SHIMMER, P_COLOR, P_FREEZE, P_DUCK, P_OUT,
    P_INMETER, P_WETMETER, P_INFO,                                            // read-only
    P_COUNT
};
const char* const kAlgoNames[] { "Room", "Hall", "Plate", "Classic", "Ice", "Meta", "Reflex" };
const char* const kAlgoInfo[] {
    "Natural room: early reflections + short tail",
    "Concert hall: smooth, wide, deep",
    "Plate: dense and bright, great on vocals/snare",
    "Classic legacy reverb: grainy, vintage",
    "Ice: bright, glassy, sparkling resonances",
    "Meta: huge, lush, deeply modulated space",
    "Reflex: early reflections, room without the wash" };
const char* presetNames[kNumPresets];
constexpr float kMeterFloor = -60.f, kMeterTop = 6.f;

enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_MOMENT, K_READ, K_LOG };
enum Fmt  { F_NUM, F_PCT, F_MS, F_SEC, F_HZ, F_DB, F_COLOR, F_METER, F_TEXT };
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
    for (int i = 0; i < kNumPresets; ++i) presetNames[i] = kPresets[i].name;
    const Preset& z = kPresets[0];
    def (P_PRESET, "Preset", "", K_CHOICE, 0, kNumPresets - 1, 1, 0, presetNames, kNumPresets);
    def (P_PREV, "Prev Preset", "", K_MOMENT, 0, 1, 1, 0);
    def (P_NEXT, "Next Preset", "", K_MOMENT, 0, 1, 1, 0);
    def (P_ALGO, "Algorithm", "", K_CHOICE, 0, sp::A_COUNT - 1, 1, (float) z.algo, kAlgoNames, sp::A_COUNT);
    def (P_MIX, "Mix", "%", K_FLOAT, 0, 100, 1, z.mix * 100, nullptr, 0, F_PCT);
    def (P_PRE, "Pre-Delay", "ms", K_FLOAT, 0, 250, 1, z.pre, nullptr, 0, F_MS);
    def (P_SIZE, "Size", "%", K_FLOAT, 0, 100, 1, z.size * 100, nullptr, 0, F_PCT);
    def (P_DECAY, "Decay", "s", K_LOG, 0.2f, 20, 0, z.decay, nullptr, 0, F_SEC);
    def (P_DAMP, "Damping", "Hz", K_LOG, 1000, 20000, 0, z.damp, nullptr, 0, F_HZ);
    def (P_LOWCUT, "Low Cut", "Hz", K_LOG, 20, 1000, 0, z.lowcut, nullptr, 0, F_HZ);
    def (P_DIFF, "Diffusion", "%", K_FLOAT, 0, 100, 1, z.diff * 100, nullptr, 0, F_PCT);
    def (P_MOD, "Modulation", "%", K_FLOAT, 0, 100, 1, z.mod * 100, nullptr, 0, F_PCT);
    def (P_WIDTH, "Width", "%", K_FLOAT, 0, 150, 1, z.width * 100, nullptr, 0, F_PCT);
    def (P_SHIMMER, "Shimmer", "%", K_FLOAT, 0, 100, 1, z.shimmer * 100, nullptr, 0, F_PCT);
    def (P_COLOR, "Color", "", K_FLOAT, -100, 100, 1, z.color * 100, nullptr, 0, F_COLOR);
    def (P_FREEZE, "Freeze", "", K_BOOL, 0, 1, 1, 0);
    def (P_DUCK, "Ducking", "%", K_FLOAT, 0, 100, 1, z.duck * 100, nullptr, 0, F_PCT);
    def (P_OUT, "Output", "dB", K_FLOAT, -24, 12, 0.1f, z.out, nullptr, 0, F_DB);
    def (P_INMETER, "Input Meter", "dB", K_READ, kMeterFloor, kMeterTop, 0, kMeterFloor, nullptr, 0, F_METER);
    def (P_WETMETER, "Reverb Meter", "dB", K_READ, kMeterFloor, kMeterTop, 0, kMeterFloor, nullptr, 0, F_METER);
    def (P_INFO, "Algorithm Info", "", K_READ, 0, 1000, 0, 0, nullptr, 0, F_TEXT);
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

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> v[P_COUNT], nv[P_COUNT];
    std::atomic<unsigned> seq { 0 };           // odd while a preset is being written
    int program = 0;
    float sr = 44100.f;
    bool notifying = false;
    sp::Reverb rv;
    sp::Params cur;
    float meterIn = kMeterFloor, meterWet = kMeterFloor, sent[P_COUNT];
    int meterCountdown = 0, infoGen = 0, lastAlgoShown = -1;

    Plugin()
    {
        for (int i = 0; i < P_COUNT; ++i) setPlain (i, kParams[i].def);
        for (float& s : sent) s = 1e9f;
        rv.prepare (sr);
    }
    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    float get (int i) const { return v[i].load(); }

    void loadPreset (int i)
    {
        if (i < 0 || i >= kNumPresets) return;
        const Preset& p = kPresets[i];
        seq.fetch_add (1);
        program = i;
        setPlain (P_PRESET, (float) i); setPlain (P_ALGO, (float) p.algo); setPlain (P_MIX, p.mix * 100); setPlain (P_PRE, p.pre);
        setPlain (P_SIZE, p.size * 100); setPlain (P_DECAY, p.decay); setPlain (P_DAMP, p.damp); setPlain (P_LOWCUT, p.lowcut);
        setPlain (P_DIFF, p.diff * 100); setPlain (P_MOD, p.mod * 100); setPlain (P_WIDTH, p.width * 100); setPlain (P_SHIMMER, p.shimmer * 100);
        setPlain (P_COLOR, p.color * 100); setPlain (P_FREEZE, 0); setPlain (P_DUCK, p.duck * 100); setPlain (P_OUT, p.out);
        seq.fetch_add (1);
    }
    void notifyHost()
    {
        if (master == nullptr || notifying) return;
        notifying = true;
        for (int i = 0; i < P_INMETER; ++i) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
        master (&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
        notifying = false;
    }
    void hostSet (int i, float norm)
    {
        const ParamDef& d = kParams[i];
        if (d.kind == K_READ) return;
        const float old = get (i), plain = toPlain (d, norm);
        if (d.kind == K_MOMENT)
        {
            if (notifying || plain < 0.5f || old >= 0.5f) { setPlain (i, plain); return; }
            loadPreset ((program + (i == P_NEXT ? 1 : kNumPresets - 1)) % kNumPresets);
            setPlain (i, 0.f);
            notifyHost();
            return;
        }
        nv[i].store (clampf (norm, 0.f, 1.f)); v[i].store (plain);
        if (i == P_PRESET && ! notifying && (int) plain != program) { loadPreset ((int) plain); notifyHost(); }
    }

    void readParams()
    {
        const unsigned s1 = seq.load();
        if (s1 & 1) return;                                               // a preset is being written: keep the last set
        sp::Params p;
        p.algo = (int) get (P_ALGO); p.mix = get (P_MIX) * 0.01f; p.predelayMs = get (P_PRE); p.size = get (P_SIZE) * 0.01f;
        p.decay = get (P_DECAY); p.dampHz = get (P_DAMP); p.lowCutHz = get (P_LOWCUT); p.diffusion = get (P_DIFF) * 0.01f;
        p.modulation = get (P_MOD) * 0.01f; p.width = get (P_WIDTH) * 0.01f; p.shimmer = get (P_SHIMMER) * 0.01f;
        p.color = get (P_COLOR) * 0.01f; p.freeze = get (P_FREEZE) > 0.5f; p.ducking = get (P_DUCK) * 0.01f; p.outDb = get (P_OUT);
        if (seq.load() == s1) cur = p;
    }
    void send (int i, float plain, float threshold)
    {
        setPlain (i, plain);
        if (std::fabs (plain - sent[i]) < threshold) return;
        sent[i] = plain;
        if (master != nullptr) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int pos = 0; pos < n; pos += 64)
        {
            const int m = (n - pos) < 64 ? (n - pos) : 64;
            readParams();
            if (outL + pos != inL + pos) std::memcpy (outL + pos, inL + pos, sizeof (float) * (size_t) m);
            if (outR + pos != inR + pos) std::memcpy (outR + pos, inR + pos, sizeof (float) * (size_t) m);
            rv.process (cur, outL + pos, outR + pos, m);
            const float fall = 20.f * m / sr;
            const float di = clampf (20.f * std::log10 (rv.takeInPeak() + 1e-9f), kMeterFloor, kMeterTop);
            const float dw = clampf (20.f * std::log10 (rv.takeWetPeak() + 1e-9f), kMeterFloor, kMeterTop);
            meterIn = di > meterIn ? di : std::fmax (di, meterIn - fall);
            meterWet = dw > meterWet ? dw : std::fmax (dw, meterWet - fall);
            meterCountdown -= m;
            if (meterCountdown <= 0)
            {
                meterCountdown += (int) (sr / 25.f);
                send (P_INMETER, meterIn, 1.f); send (P_WETMETER, meterWet, 1.f);
                const int a = (int) get (P_ALGO);
                if (a != lastAlgoShown) { lastAlgoShown = a; infoGen = (infoGen + 1) % 1000; send (P_INFO, (float) infoGen, 0.5f); }
            }
        }
    }

    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float x = get (idx);
        if (idx == P_INFO) { std::snprintf (out, max, "%s", kAlgoInfo[(int) clampf (get (P_ALGO), 0, sp::A_COUNT - 1)]); return; }
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[(int) clampf (x, 0.f, (float) (d.n - 1))]); return; }
        if (d.kind == K_BOOL || d.kind == K_MOMENT) { std::snprintf (out, max, "%s", x >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_PCT:   std::snprintf (out, max, "%.0f %%", (double) x); return;
            case F_MS:    std::snprintf (out, max, "%.0f ms", (double) x); return;
            case F_SEC:   std::snprintf (out, max, x < 10.f ? "%.2f s" : "%.1f s", (double) x); return;
            case F_HZ:    if (x < 1000.f) std::snprintf (out, max, "%.0f Hz", (double) x); else std::snprintf (out, max, "%.1f kHz", (double) (x * 0.001f)); return;
            case F_DB:    std::snprintf (out, max, "%+.1f dB", (double) x); return;
            case F_COLOR: if (std::fabs (x) < 0.5f) std::snprintf (out, max, "Neutral"); else std::snprintf (out, max, "%s %.0f", x < 0 ? "Dark" : "Bright", (double) std::fabs (x)); return;
            case F_METER: if (x <= kMeterFloor) std::snprintf (out, max, "-inf"); else std::snprintf (out, max, "%.1f dB", (double) x); return;
            default:      std::snprintf (out, max, "%.2f", (double) x); return;
        }
    }
};

void processReplacing (AEffect* e, float** in, float** out, int32_t n) { static_cast<Plugin*> (e->object)->process (in[0], in[1], out[0], out[1], n); }
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)
{
    float tl[256], tr[256];
    Plugin* p = static_cast<Plugin*> (e->object);
    for (int32_t pos = 0; pos < n; pos += 256)
    {
        const int32_t m = (n - pos) < 256 ? (n - pos) : 256;
        std::memcpy (tl, in[0] + pos, sizeof (float) * (size_t) m); std::memcpy (tr, in[1] + pos, sizeof (float) * (size_t) m);
        p->process (tl, tr, tl, tr, m);
        for (int32_t i = 0; i < m; ++i) { out[0][pos + i] += tl[i]; out[1][pos + i] += tr[i]; }
    }
}
void setParameter (AEffect* e, int32_t i, float norm) { if (i >= 0 && i < P_COUNT) static_cast<Plugin*> (e->object)->hostSet (i, norm); }
float getParameter (AEffect* e, int32_t i) { return (i >= 0 && i < P_COUNT) ? static_cast<Plugin*> (e->object)->nv[i].load() : 0.f; }
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose:            p->~Plugin(); std::free (p); return 0;
        case effSetProgram:       if (val >= 0 && val < kNumPresets) { p->loadPreset ((int) val); p->notifyHost(); } return 0;
        case effGetProgram:       return p->program;
        case effGetProgramName:   copyStr (ptr, kPresets[p->program].name, 32); return 0;
        case effGetProgramNameIndexed:
            if (idx < 0 || idx >= kNumPresets) return 0;
            copyStr (ptr, kPresets[idx].name, 32); return 1;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].name); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].unit); return 0;
        case effGetParamDisplay:  if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[64]; p->display (idx, b, sizeof b); copyStr (ptr, b, 48); } return 0;
        case effCanBeAutomated:   return (idx >= 0 && idx < P_INMETER && kParams[idx].kind != K_MOMENT) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) { p->sr = opt; p->rv.prepare (opt); } return 0;
        case effMainsChanged:     if (val != 0) p->rv.clear(); return 0;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "Da Space", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return 1001;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return (intptr_t) (p->sr * 20);
        case effSetProcessPrecision: return val == 0 ? 1 : 0;
        default: return 0;
    }
}
} // namespace

SPACE_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    static bool inited = false;
    if (! inited) { initParams(); inited = true; }
    void* mem = std::calloc (1, sizeof (Plugin));
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    p->loadPreset (0);
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
    fx.uniqueID = ('D' << 24) | ('S' << 16) | ('P' << 8) | 'C';   // 'DSPC' = 0x44535043
    fx.version = 1001;
    return &fx;
}
