// ASR10 - Ensoniq ASR-10 sampler character insert for Akai MPC OS (Gen1, 32-bit ARM), built from the GlueBus /
// SP1200 components (dependency-free VST2 core, parameter/preset model, host notification, packaging, skin
// pipeline). Needs only libc/libm. No GUI: MPC draws the slider skin from /sdcard/Synths.
//
// Signal path (modelled on the ASR-10, see docs/CIRCUIT.md):
//   input level > sigma-delta ADC (steep anti-alias low-pass at 0.45 x rate, hard clip, 16-bit) at 29.76 kHz
//   ("30k") or 44.1 kHz > sample memory > OTTO voice playback with linear interpolation (tuning = read rate)
//   > OTTO 4-pole digital filter (two cutoffs FC1/FC2, four chip modes, no resonance) > 16-bit DAC
//   > mix / volume
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#define ASR_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
// Parameter indices: the skin (tools/make_skin.py) and Q-Link maps bind to these numbers.
enum ParamId
{
    P_INPUT, P_TUNE, P_FINE, P_FC1, P_FC2, P_MIX, P_VOLUME, P_RATE, P_FMODE, P_TMODE, P_BYPASS,
    P_COUNT
};
enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_LOG };
enum Fmt  { F_NUM, F_TUNE, F_FC1, F_FC2 };

// ---- hardware constants ----
constexpr double kRate30    = 29761.9;   // ASR-10 sampling rates (Hz): "30k" and 44.1k
constexpr double kRate44    = 44100.0;
constexpr float  kFull      = 32768.f;   // 16-bit two's complement
constexpr float  kAAFrac    = 0.45f;     // sigma-delta decimation filter edge, as a fraction of the sampling rate
constexpr float  kFcOpen    = 19500.f;   // FC1 / FC2 at or above this = low-pass stage open (bit-transparent)
constexpr float  kFcOff     = 21.f;      // FC2 at or below this in the high-pass modes = high-pass off
constexpr int    kPsBits    = 13, kPsLen = 1 << kPsBits, kPsMask = kPsLen - 1;   // pitch-mode sample memory
constexpr float  kPsWin     = 4096.f;    // pitch-mode splice window, in samples at the sampling rate (93-138 ms)
constexpr float  kPsXfade   = 0.2f;      // share of the window spent crossfading between the two read heads
constexpr float  kDirect    = 2.f;       // read position behind the write position on the direct path (samples)

const char* const kRateNames[]  { "30 kHz", "44.1 kHz" };
// OTTO filter modes (ES5505 LP3/LP4 bits): poles 1-2 are always low-pass on FC1, poles 3-4 are chosen here
const char* const kFModeNames[] { "LP2 / HP2", "LP3 / HP1", "LP2 / LP2", "LP3 / LP1" };
const char* const kTModeNames[] { "Pitch", "Rate" };

struct ParamDef
{
    const char* name; const char* unit; Kind kind;
    float lo, hi, step, def;
    const char* const* choices; int n; Fmt fmt; const char* numFmt;
};

#define CH(names) names, (int) (sizeof (names) / sizeof (names[0]))
const ParamDef kParams[P_COUNT] =
{
    { "Input",       "dB", K_FLOAT, -24.f, 12.f, 0.1f,  0.f,    nullptr, 0, F_NUM,  "%+.1f" },
    { "Tune",        "st", K_FLOAT, -12.f, 12.f, 1.f,   0.f,    nullptr, 0, F_TUNE, "" },
    { "Fine",        "ct", K_FLOAT, -50.f, 50.f, 1.f,   0.f,    nullptr, 0, F_NUM,  "%+.0f" },
    { "Filter 1",    "",   K_LOG,   100.f, 20000.f, 0.f, 20000.f, nullptr, 0, F_FC1, "" },
    { "Filter 2",    "",   K_LOG,    20.f, 20000.f, 0.f, 20.f,  nullptr, 0, F_FC2,  "" },
    { "Mix",         "%",  K_FLOAT,   0.f, 100.f, 1.f, 100.f,   nullptr, 0, F_NUM,  "%.0f" },
    { "Volume",      "dB", K_FLOAT, -24.f, 12.f, 0.1f,  0.f,    nullptr, 0, F_NUM,  "%+.1f" },
    { "Sample Rate", "",   K_CHOICE,  0.f,  1.f, 1.f,   1.f,    CH (kRateNames),  F_NUM, "" },
    { "Filter Mode", "",   K_CHOICE,  0.f,  3.f, 1.f,   0.f,    CH (kFModeNames), F_NUM, "" },
    { "Tune Mode",   "",   K_CHOICE,  0.f,  1.f, 1.f,   0.f,    CH (kTModeNames), F_NUM, "" },
    { "Bypass",      "",   K_BOOL,    0.f,  1.f, 1.f,   0.f,    nullptr, 0, F_NUM,  "" },
};

// ---- factory presets: defaults + overrides ----
struct Ov { int id; float v; };
struct Preset { const char* name; const Ov* ov; int n; };
#define PRESET(sym, ...) const Ov sym[] = { __VA_ARGS__ };
#define ENTRY(name, sym) { name, sym, (int) (sizeof (sym) / sizeof (sym[0])) }

PRESET (pInit,      { P_RATE, 1 })
PRESET (p30k,       { P_RATE, 0 })
PRESET (pDrums30,   { P_RATE, 0 }, { P_INPUT, 3.f }, { P_FC2, 40.f })
PRESET (pDusty,     { P_RATE, 0 }, { P_FC1, 6000.f }, { P_FC2, 80.f }, { P_FMODE, 0 })
PRESET (pPitchDn,   { P_RATE, 0 }, { P_TUNE, -3.f }, { P_TMODE, 0 })
PRESET (pRate5,     { P_RATE, 0 }, { P_TUNE, -5.f }, { P_TMODE, 1 })
PRESET (pKeys,      { P_RATE, 1 }, { P_FC1, 8000.f }, { P_FMODE, 1 }, { P_FC2, 30.f })
PRESET (pThin,      { P_RATE, 0 }, { P_FC1, 9000.f }, { P_FC2, 300.f }, { P_FMODE, 0 })
PRESET (pUp5,       { P_RATE, 0 }, { P_TUNE, 5.f }, { P_TMODE, 0 })
PRESET (pSub,       { P_RATE, 0 }, { P_FC1, 500.f }, { P_FC2, 500.f }, { P_FMODE, 2 })
PRESET (pHot,       { P_RATE, 0 }, { P_INPUT, 9.f }, { P_VOLUME, -6.f })
PRESET (pParallel,  { P_RATE, 0 }, { P_TUNE, -7.f }, { P_TMODE, 1 }, { P_MIX, 50.f })

const Preset kPresets[] =
{
    ENTRY ("Init 44.1k", pInit),         ENTRY ("30k Dark", p30k),            ENTRY ("30k Drums Punch", pDrums30),
    ENTRY ("Dusty Loop", pDusty),        ENTRY ("Pitch Down -3", pPitchDn),   ENTRY ("Rate -5 Crunch", pRate5),
    ENTRY ("Warm Keys", pKeys),          ENTRY ("Thin Break", pThin),         ENTRY ("Up +5 Chop", pUp5),
    ENTRY ("Sub Filter", pSub),          ENTRY ("Hot Input", pHot),           ENTRY ("Parallel 30k", pParallel),
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float flush (float x) { return std::fabs (x) < 1e-20f ? 0.f : x; }
inline float fromDb (float db) { return std::exp2 (db * 0.16609640f); }
// 16-bit converter: clip at full scale, round to the nearest code
inline float q16 (float x)
{
    float c = std::floor (x * kFull + 0.5f);
    c = c < -kFull ? -kFull : (c > kFull - 1.f ? kFull - 1.f : c);
    return c * (1.f / kFull);
}

float toPlain (const ParamDef& d, float norm)
{
    norm = clampf (norm, 0.f, 1.f);
    if (d.kind == K_BOOL) return norm >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return std::floor (norm * (float) (d.n - 1) + 0.5f);
    if (d.kind == K_LOG) return d.lo * std::pow (d.hi / d.lo, norm);
    float v = d.lo + norm * (d.hi - d.lo);
    if (d.step > 0.f) v = d.lo + std::floor ((v - d.lo) / d.step + 0.5f) * d.step;
    return clampf (v, d.lo, d.hi);
}
float toNorm (const ParamDef& d, float plain)
{
    if (d.kind == K_BOOL) return plain >= 0.5f ? 1.f : 0.f;
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
    else             std::snprintf (out, max, "%.1f kHz", (double) (hz * 0.001f));
}

// ---- RBJ biquad (GlueBus component), transposed direct form II, stereo; used for the ADC decimation filter ----
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    inline float run (int c, float x)
    {
        const float y = b0 * x + z1[c];
        z1[c] = flush (b1 * x - a1 * y + z2[c]);
        z2[c] = flush (b2 * x - a2 * y);
        return y;
    }
    void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0.f; }
    void lowpass (double fs, double f, double q)
    {
        f = f < 10.0 ? 10.0 : (f > 0.49 * fs ? 0.49 * fs : f);
        const double w = 2.0 * 3.14159265358979 * f / fs, cw = std::cos (w), sw = std::sin (w), al = sw / (2 * q);
        const double a0 = 1 + al;
        b0 = (float) ((1 - cw) / 2 / a0); b1 = (float) ((1 - cw) / a0); b2 = b0;
        a1 = (float) (-2 * cw / a0);      a2 = (float) ((1 - al) / a0);
    }
};

// ---- OTTO (ES5505) voice filter: four one-pole stages, poles 1-2 low-pass on K1 (FC1), poles 3-4 per mode ----
struct Otto
{
    float s[2][4] {};                 // one-pole low-pass states per channel
    int lastMode = -1;                // poles 3-4 change type with the mode
    void clear() { std::memset (s, 0, sizeof s); lastMode = -1; }
    static inline float lp (float& st, float x, float k) { st = flush (st + k * (x - st)); return st; }
    static inline float hp (float& st, float x, float k) { return x - lp (st, x, k); }
    // k = 1 means an open low-pass (y = x exactly), k = 0 an off high-pass (y = x exactly)
    inline float run (int c, float x, int mode, float k1, float k2, bool prime)
    {
        x = lp (s[c][0], x, k1);
        x = lp (s[c][1], x, k1);
        if (prime)   // mode just changed: poles 3-4 start out passing their input, then settle (a restart would click)
        {
            const bool lp3 = mode == 1 || mode == 3, lp4 = mode >= 2;
            s[c][2] = lp3 ? x : 0.f;
            s[c][3] = lp4 ? x : 0.f;
        }
        switch (mode)
        {
            case 0:  x = hp (s[c][2], x, k2); return hp (s[c][3], x, k2);     // LP2 / HP2   (chip: HP/K2 + HP/K2)
            case 1:  x = lp (s[c][2], x, k1); return hp (s[c][3], x, k2);     // LP3 / HP1   (LP3: LP/K1 + HP/K2)
            case 2:  x = lp (s[c][2], x, k2); return lp (s[c][3], x, k2);     // LP2 / LP2   (LP4: LP/K2 + LP/K2)
            default: x = lp (s[c][2], x, k1); return lp (s[c][3], x, k2);     // LP3 / LP1   (LP3|LP4: LP/K1 + LP/K2)
        }
    }
};
inline float onePoleK (float hz, float fs) { return 1.f - std::exp (-6.2831853f * hz / fs); }

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;

    std::atomic<float> v[P_COUNT];     // plain (un-normalised) parameter values, used by the DSP and display
    std::atomic<float> nv[P_COUNT];    // exact normalised positions the host set. Returned unrounded, so small
                                       // Q-Link / touch moves on stepped sliders accumulate instead of being
                                       // rounded back to the same step (the MPC stuck-slider problem)
    int program = 0;
    float sr = 44100.f;

    // DSP state (audio thread only)
    Biquad aa[4];  float aaKey = -1.f;                 // 8-pole Butterworth decimation filter, last cutoff
    double adcPhase = 0.0;
    float mem[2][kPsLen] {};  int wpos = 0;            // sample memory at the sampling rate
    float psDelay = kDirect, psMix = 0.f;              // pitch-mode read heads
    Otto otto;
    float sIn = 1.f, sVol = 1.f, sMix = 1.f;
    bool snap = true;

    Plugin() { for (int i = 0; i < P_COUNT; ++i) setPlain (i, kParams[i].def); }

    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    void setNorm (int i, float norm)
    {
        norm = clampf (norm, 0.f, 1.f);
        nv[i].store (norm); v[i].store (toPlain (kParams[i], norm));
    }

    void applyPreset (int i)
    {
        if (i < 0 || i >= kNumPresets) return;
        program = i;
        for (int p = 0; p < P_COUNT; ++p) if (p != P_BYPASS) setPlain (p, kParams[p].def);
        for (int k = 0; k < kPresets[i].n; ++k) setPlain (kPresets[i].ov[k].id, kPresets[i].ov[k].v);
    }

    // Tell the host every parameter changed (after a preset load), then ask it to refresh its display.
    void notifyHostAll()
    {
        if (master == nullptr) return;
        for (int i = 0; i < P_COUNT; ++i)
            master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
        master (&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
    }

    void reset()
    {
        for (auto& b : aa) b.clear();
        aaKey = -1.f; adcPhase = 0.0;
        std::memset (mem, 0, sizeof mem); wpos = 0;
        psDelay = kDirect; psMix = 0.f;
        otto.clear();
        snap = true;
    }

    bool fmodeHighPass() const { return (int) v[P_FMODE].load() <= 1; }

    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float p = v[idx].load();
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[(int) clampf (p, 0.f, (float) (d.n - 1))]); return; }
        if (d.kind == K_BOOL)   { std::snprintf (out, max, "%s", p >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_TUNE: if (p > -0.5f && p < 0.5f) std::snprintf (out, max, "0"); else std::snprintf (out, max, "%+.0f", (double) p); return;
            case F_FC1:  if (p >= kFcOpen) std::snprintf (out, max, "Open"); else fmtHz (out, max, p); return;
            case F_FC2:
                if (fmodeHighPass() && p <= kFcOff) std::snprintf (out, max, "Off");
                else if (! fmodeHighPass() && p >= kFcOpen) std::snprintf (out, max, "Open");
                else fmtHz (out, max, p);
                return;
            default:     std::snprintf (out, max, d.numFmt, (double) p); return;
        }
    }

    // linear interpolation between two stored samples (OTTO: sample1 * (1 - frac) + sample2 * frac)
    inline float readAt (int c, float pos) const
    {
        const float fl = std::floor (pos);
        const int i = (int) fl;
        const float f = pos - fl;
        const float a = mem[c][i & kPsMask], b = mem[c][(i + 1) & kPsMask];
        return a + f * (b - a);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (v[P_BYPASS].load() > 0.5f)
        {
            if (outL != inL) std::memcpy (outL, inL, sizeof (float) * (size_t) n);
            if (outR != inR) std::memcpy (outR, inR, sizeof (float) * (size_t) n);
            snap = true;
            return;
        }

        const float semis     = v[P_TUNE].load() + v[P_FINE].load() * 0.01f;
        const bool  rateMode  = v[P_TMODE].load() > 0.5f;
        const bool  retune    = std::fabs (semis) > 0.004f;
        const float ratio     = std::exp2 (semis / 12.f);                     // equal-tempered, cent resolution
        const double fsS      = v[P_RATE].load() > 0.5f ? kRate44 : kRate30;   // chosen sampling rate
        const double writeHz  = rateMode ? fsS * (double) ratio : fsS;          // rate mode: sample slower/faster
        const double writeInc = writeHz / sr;                                   // memory samples per host sample
        const float  readInc  = rateMode ? (float) writeInc : (float) (fsS / sr) * ratio;
        const float  dStep    = (float) writeInc - readInc;                     // head delay change per host sample
        const int    fmode    = (int) v[P_FMODE].load();
        const float  fc1      = v[P_FC1].load(), fc2 = v[P_FC2].load();
        const float  k1       = fc1 >= kFcOpen ? 1.f : onePoleK (fc1, sr);
        const bool   hpModes  = fmode <= 1;
        const float  k2       = hpModes ? (fc2 <= kFcOff ? 0.f : onePoleK (fc2, sr))
                                        : (fc2 >= kFcOpen ? 1.f : onePoleK (fc2, sr));
        const float  psFade   = 1.f - std::exp (-1.f / (0.01f * sr));           // ~10 ms tune-0 <-> heads fade

        // sigma-delta decimation filter: 8-pole Butterworth at 0.45 x the effective sampling rate
        const float aaHz = (float) std::fmin (kAAFrac * writeHz, 0.45 * sr);
        if (aaHz != aaKey)
        {
            const double q[4] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
            for (int s = 0; s < 4; ++s) aa[s].lowpass (sr, aaHz, q[s]);
            aaKey = aaHz;
        }
        const bool aaOn = aaHz < 0.449f * sr;          // at 44.1k on a 44.1k host the converter is flat to Nyquist

        const float tIn  = fromDb (v[P_INPUT].load());
        const float tVol = fromDb (v[P_VOLUME].load());
        const float tMix = v[P_MIX].load() * 0.01f;
        if (snap) { sIn = tIn; sVol = tVol; sMix = tMix; snap = false; }
        const float sm = 1.f - std::exp (-1.f / (0.02f * sr));

        for (int i = 0; i < n; ++i)
        {
            sIn += (tIn - sIn) * sm;  sVol += (tVol - sVol) * sm;  sMix += (tMix - sMix) * sm;
            float x[2] { inL[i] * sIn, inR[i] * sIn };

            // ---- ADC: decimation filter, then 16-bit samples written to memory at the sampling rate ----
            if (aaOn) for (int c = 0; c < 2; ++c) for (auto& b : aa) x[c] = b.run (c, x[c]);
            adcPhase += writeInc;
            while (adcPhase >= 1.0)
            {
                adcPhase -= 1.0;
                mem[0][wpos] = q16 (x[0]); mem[1][wpos] = q16 (x[1]);
                wpos = (wpos + 1) & kPsMask;
            }
            // continuous write position: the newest sample is wpos - 1, plus the fraction of the next period
            const float wf = (float) (wpos - 1) + (float) adcPhase;

            // ---- OTTO playback: linear interpolation; tune 0 (and Rate mode) read just behind the write head ----
            float y[2];
            const float direct[2] { readAt (0, wf - kDirect), readAt (1, wf - kDirect) };
            psMix += (((! rateMode && retune) ? 1.f : 0.f) - psMix) * psFade;
            if (psMix < 1e-4f) { psMix = 0.f; y[0] = direct[0]; y[1] = direct[1]; }
            else
            {
                // two read heads sweeping through memory; one plays solo (exact pitch) and they crossfade only
                // for kPsXfade of the time, away from each wrap
                psDelay += dStep;
                while (psDelay < kDirect) psDelay += kPsWin;
                while (psDelay >= kDirect + kPsWin) psDelay -= kPsWin;
                const float ph = psDelay - kDirect;                         // 0 .. kPsWin
                const float d2 = ph + kPsWin * 0.5f >= kPsWin ? psDelay - kPsWin * 0.5f : psDelay + kPsWin * 0.5f;
                const float tri = 1.f - std::fabs (2.f * ph / kPsWin - 1.f);
                const float g1 = clampf ((tri - 0.5f) / kPsXfade + 0.5f, 0.f, 1.f), g2 = 1.f - g1;
                for (int c = 0; c < 2; ++c)
                {
                    const float heads = readAt (c, wf - psDelay) * g1 + readAt (c, wf - d2) * g2;
                    y[c] = direct[c] + psMix * (heads - direct[c]);
                }
            }

            // ---- OTTO voice filter, then the 16-bit DAC ----
            const bool prime = fmode != otto.lastMode;
            y[0] = q16 (otto.run (0, y[0], fmode, k1, k2, prime));
            y[1] = q16 (otto.run (1, y[1], fmode, k1, k2, prime));
            otto.lastMode = fmode;

            const float dryG = 1.f - sMix;          // exact at both ends
            outL[i] = (y[0] * sMix + inL[i] * dryG) * sVol;
            outR[i] = (y[1] * sMix + inR[i] * dryG) * sVol;
        }
    }
};

// ---- VST2 entry points ----
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
    static_cast<Plugin*> (e->object)->setNorm (i, norm);
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
        case effSetProgram:       p->applyPreset ((int) val); p->notifyHostAll(); return 0;
        case effGetProgram:       return p->program;
        case effGetProgramName:   copyStr (ptr, kPresets[p->program].name); return 0;
        case effGetProgramNameIndexed:
            if (idx < 0 || idx >= kNumPresets) return 0;
            copyStr (ptr, kPresets[idx].name); return 1;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].name); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].unit); return 0;
        case effGetParamDisplay:
            if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[32]; p->display (idx, b, sizeof b); copyStr (ptr, b); }
            return 0;
        case effCanBeAutomated:   return (idx >= 0 && idx < P_COUNT) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) { p->sr = opt; p->reset(); } return 0;
        case effMainsChanged:     p->reset(); return 0;
        case effSetBypass:        p->setPlain (P_BYPASS, val ? 1.f : 0.f); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "ASR10", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "GlueBus", 32); return 1;
        case effGetVendorVersion: return 1000;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return 1;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;      // 32-bit float only
        case effCanDo:
            if (ptr != nullptr && std::strcmp ((const char*) ptr, "bypass") == 0) return 1;
            return 0;
        default: return 0;
    }
}
} // namespace

ASR_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    void* mem = std::calloc (1, sizeof (Plugin));   // no operator new: keeps libstdc++ out of the link
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    p->applyPreset (0);
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
    fx.uniqueID = ('A' << 24) | ('S' << 16) | ('R' << 8) | '1';   // 'ASR1' = 0x41535231
    fx.version = 1000;
    return &fx;
}
