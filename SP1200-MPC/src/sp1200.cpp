// SP1200 - 12-bit / 26.04 kHz sampling-drum-machine character insert for Akai MPC OS (Gen1, 32-bit ARM),
// built from the GlueBus components (same dependency-free VST2 core, parameter model, host notification,
// packaging and skin pipeline). Needs only libc/libm. No GUI: MPC draws the slider skin from /sdcard/Synths.
//
// Signal path (modelled on the SP-1200 circuit, see docs/CIRCUIT.md):
//   input level > ADC (hard clip at full scale, 12-bit two's-complement truncation, no anti-alias filter)
//   > sample memory > drop-sample playback at the 26.04 kHz DAC clock (tuning = read-rate, no interpolation)
//   > decay (digital, before the DAC) > 12-bit DAC with zero-order hold
//   > output-channel filter: Out 1-2 SSM2044 dynamic 4-pole VCF, Out 3-4 fixed ~7.5 kHz, Out 5-6 fixed ~10 kHz,
//     Out 7-8 unfiltered > output amp > mix / volume
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#define SP_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
// Parameter indices: the skin (tools/make_skin.py) and Q-Link maps bind to these numbers.
// P_TUNE..P_VOLUME are the eight front-panel sliders (left to right).
enum ParamId
{
    P_INPUT, P_TUNE, P_DECAY, P_CHANNEL, P_SWEEP, P_FLOOR, P_MIX, P_VOLUME, P_MODE, P_BYPASS,
    P_COUNT
};
enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_LOG };
enum Fmt  { F_NUM, F_HZ, F_SEC, F_DECAY, F_TUNE };

// ---- hardware constants ----
constexpr double kSpRate    = 26040.0;   // SP-1200 sample / DAC clock (Hz)
constexpr float  kFull      = 2048.f;    // 12-bit two's complement: -2048 .. +2047
constexpr float  kDecayOff  = 4.f;       // decay slider at the top = no decay
constexpr float  kOut34Hz   = 7500.f;    // fixed output filters, measured on the hardware (approx.)
constexpr float  kOut56Hz   = 10000.f;
constexpr float  kDynTopHz  = 12000.f;   // SSM2044 on Out 1-2: fully open just after a hit...
constexpr float  kSsmRes    = 0.55f;     // ...fixed low resonance set by the feedback resistor (k of 4)
constexpr int    kPsBits    = 12, kPsLen = 1 << kPsBits, kPsMask = kPsLen - 1;   // pitch-mode sample memory (157 ms)
constexpr float  kPsWin     = 2048.f;    // pitch-mode splice window in SP samples (79 ms)
constexpr float  kPsXfade   = 0.2f;      // share of the window spent crossfading between the two read heads
constexpr float  kPsFade    = 0.0038f;   // per-SP-sample step of the tune-0 <-> read-heads fade (~10 ms)

const char* const kChannelNames[] { "Out 1-2 Dyn", "Out 3-4", "Out 5-6", "Out 7-8" };
const char* const kModeNames[]    { "45>33 Grit", "Pitch" };

struct ParamDef
{
    const char* name; const char* unit; Kind kind;
    float lo, hi, step, def;
    const char* const* choices; int n; Fmt fmt; const char* numFmt;
};

#define CH(names) names, (int) (sizeof (names) / sizeof (names[0]))
const ParamDef kParams[P_COUNT] =
{
    { "Input",     "dB", K_FLOAT, -24.f, 12.f, 0.1f,  0.f,    nullptr, 0, F_NUM,  "%+.1f" },
    { "Tune",      "st", K_FLOAT, -12.f,  7.f, 1.f,   0.f,    nullptr, 0, F_TUNE, "" },
    { "Decay",     "",   K_LOG,   0.02f, kDecayOff, 0.f, kDecayOff, nullptr, 0, F_DECAY, "" },
    { "Channel",   "",   K_CHOICE, 0.f,   3.f, 1.f,   2.f,    CH (kChannelNames), F_NUM, "" },
    { "Dyn Sweep", "",   K_LOG,   0.001f, 0.25f, 0.f, 0.012f, nullptr, 0, F_SEC,  "" },
    { "Dyn Floor", "",   K_LOG,   100.f, 2000.f, 0.f, 250.f,  nullptr, 0, F_HZ,   "" },
    { "Mix",       "%",  K_FLOAT,   0.f, 100.f, 1.f, 100.f,   nullptr, 0, F_NUM,  "%.0f" },
    { "Volume",    "dB", K_FLOAT, -24.f,  12.f, 0.1f, 0.f,    nullptr, 0, F_NUM,  "%+.1f" },
    { "Tune Mode", "",   K_CHOICE, 0.f,   1.f, 1.f,   0.f,    CH (kModeNames), F_NUM, "" },
    { "Bypass",    "",   K_BOOL,   0.f,   1.f, 1.f,   0.f,    nullptr, 0, F_NUM,  "" },
};

// ---- factory presets: defaults + overrides ----
struct Ov { int id; float v; };
struct Preset { const char* name; const Ov* ov; int n; };
#define PRESET(sym, ...) const Ov sym[] = { __VA_ARGS__ };
#define ENTRY(name, sym) { name, sym, (int) (sizeof (sym) / sizeof (sym[0])) }

PRESET (pInit,      { P_CHANNEL, 2 })
PRESET (pClean,     { P_CHANNEL, 3 })
PRESET (pDusty,     { P_CHANNEL, 1 })
PRESET (pTom,       { P_CHANNEL, 0 }, { P_SWEEP, 0.03f }, { P_FLOOR, 300.f })
PRESET (pKick,      { P_CHANNEL, 0 }, { P_SWEEP, 0.06f }, { P_FLOOR, 180.f }, { P_INPUT, 3.f })
PRESET (p4533,      { P_CHANNEL, 1 }, { P_TUNE, -5.f }, { P_MODE, 0 })       // 45 rpm sampled, tuned down to 33
PRESET (pPitchDn,   { P_CHANNEL, 2 }, { P_TUNE, -4.f }, { P_MODE, 1 })
PRESET (pChop,      { P_CHANNEL, 2 }, { P_DECAY, 0.25f })
PRESET (pHat,       { P_CHANNEL, 3 }, { P_DECAY, 0.08f })
PRESET (pCrunch,    { P_CHANNEL, 1 }, { P_TUNE, -12.f }, { P_MODE, 0 })
PRESET (pUp7,       { P_CHANNEL, 3 }, { P_TUNE, 7.f }, { P_MODE, 1 })
PRESET (pHot,       { P_CHANNEL, 2 }, { P_INPUT, 9.f }, { P_VOLUME, -6.f })
PRESET (pParallel,  { P_CHANNEL, 1 }, { P_TUNE, -7.f }, { P_MODE, 0 }, { P_MIX, 50.f })

const Preset kPresets[] =
{
    ENTRY ("Init Out 5-6", pInit),      ENTRY ("Clean Out 7-8", pClean),    ENTRY ("Dusty Out 3-4", pDusty),
    ENTRY ("Tom Dyn Out 1", pTom),      ENTRY ("Kick Dyn Thump", pKick),    ENTRY ("45 to 33 Break", p4533),
    ENTRY ("Pitch Down -4", pPitchDn),  ENTRY ("Chop Decay", pChop),        ENTRY ("Tight Hat Decay", pHat),
    ENTRY ("Crunch -12", pCrunch),      ENTRY ("Up +7 Pitch", pUp7),        ENTRY ("Hot Input Clip", pHot),
    ENTRY ("Parallel Dirt", pParallel),
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float flush (float x) { return std::fabs (x) < 1e-20f ? 0.f : x; }
inline float fromDb (float db) { return std::exp2 (db * 0.16609640f); }
inline float softSat (float x)   // tanh-like rational approximation, exact +-1 beyond |x|=3
{
    if (x >= 3.f) return 1.f;
    if (x <= -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
// 12-bit converter: clip at full scale, truncate (floor) to the two's-complement code, back to float
inline float q12 (float x)
{
    float c = std::floor (x * kFull);
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
void fmtSec (char* out, size_t max, float s)
{
    if (s < 1.f) std::snprintf (out, max, "%.0f ms", (double) (s * 1000.f));
    else         std::snprintf (out, max, "%.2f s", (double) s);
}

// ---- SSM2044-style 4-pole OTA low-pass: zero-delay-feedback ladder, OTA (tanh) input stage, fixed resonance ----
struct Ssm2044
{
    float s[4] {};
    float G = 1.f;
    void clear() { s[0] = s[1] = s[2] = s[3] = 0.f; }
    void setCutoff (float hz, float fs)
    {
        hz = clampf (hz, 20.f, 0.45f * fs);
        const float g = std::tan (3.14159265f * hz / fs);
        G = g / (1.f + g);
    }
    inline float run (float x)
    {
        const float b = 1.f - G;
        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float sigma = G3 * b * s[0] + G2 * b * s[1] + G * b * s[2] + b * s[3];
        const float y4 = (G4 * x + sigma) / (1.f + kSsmRes * G4);
        float u = softSat (0.8f * (x - kSsmRes * y4)) * 1.25f;          // OTA differential pair
        for (int i = 0; i < 4; ++i)
        {
            const float v = (u - s[i]) * G;
            const float y = v + s[i];
            s[i] = flush (y + v);
            u = y;
        }
        return u * (1.f + kSsmRes);                                      // passband gain back to unity
    }
};

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;

    std::atomic<float> v[P_COUNT];     // plain (un-normalised) parameter values, used by the DSP and display
    std::atomic<float> nv[P_COUNT];    // exact normalised positions the host set. Returned unrounded, so small
                                       // Q-Link / touch moves on stepped sliders (Tune, Output) accumulate
                                       // instead of being rounded back to the same step
    int program = 0;
    float sr = 44100.f;

    // DSP state (audio thread only)
    double adcPhase = 0.0, dacPhase = 0.0;
    float held[2] {}, dac[2] {};
    float mem[2][kPsLen] {};  int wpos = 0;  float psDelay = 0.f, psMix = 0.f;
    float envFast = 0.f, envSlow = 0.f, decayGain = 1.f, dynEnv = 0.f;
    int holdoff = 0;
    Ssm2044 filt[2];
    float sIn = 1.f, sVol = 1.f, sMix = 1.f;
    int lastChannel = -1;
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
        adcPhase = dacPhase = 0.0;
        for (int c = 0; c < 2; ++c) { held[c] = dac[c] = 0.f; filt[c].clear(); std::memset (mem[c], 0, sizeof mem[c]); }
        wpos = 0; psDelay = 0.f; psMix = 0.f;
        envFast = envSlow = 0.f; decayGain = 1.f; dynEnv = 0.f; holdoff = 0;
        snap = true; lastChannel = -1;
    }

    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float p = v[idx].load();
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[(int) clampf (p, 0.f, (float) (d.n - 1))]); return; }
        if (d.kind == K_BOOL)   { std::snprintf (out, max, "%s", p >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_TUNE:  if (p > -0.5f && p < 0.5f) std::snprintf (out, max, "0"); else std::snprintf (out, max, "%+.0f", (double) p); return;
            case F_DECAY: if (p >= kDecayOff * 0.98f) std::snprintf (out, max, "Off"); else fmtSec (out, max, p); return;
            case F_SEC:   fmtSec (out, max, p); return;
            case F_HZ:    fmtHz (out, max, p); return;
            default:      std::snprintf (out, max, d.numFmt, (double) p); return;
        }
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

        const float tuneSt  = v[P_TUNE].load();
        const bool  pitchMode = v[P_MODE].load() > 0.5f;
        const bool  retune  = tuneSt < -0.5f || tuneSt > 0.5f;
        const float ratio   = std::exp2 (tuneSt / 12.f);                      // equal-tempered semitone steps
        const double dacInc = kSpRate / sr;
        const double adcInc = (pitchMode ? 1.0 : (double) ratio) * dacInc;    // grit: sample at 26.04 kHz x ratio
        const float decayS  = v[P_DECAY].load();
        const bool  decayOn = decayS < kDecayOff * 0.98f;
        const float decayK  = std::exp (-6.9078f / (decayS * sr));             // -60 dB after the slider time
        const int   channel = (int) v[P_CHANNEL].load();
        const float sweepK  = std::exp (-1.f / (v[P_SWEEP].load() * sr));
        const float floorHz = v[P_FLOOR].load();
        const float fastK   = std::exp (-1.f / (0.004f * sr));
        const float slowK   = std::exp (-1.f / (0.08f * sr));
        const int   holdN   = (int) (0.04f * sr);

        if (channel != lastChannel)   // filter state is kept (clearing it would click); only the cutoff changes
        {
            const float hz = channel == 1 ? kOut34Hz : (channel == 2 ? kOut56Hz : kDynTopHz);
            filt[0].setCutoff (hz, sr); filt[1].G = filt[0].G;
            lastChannel = channel;
        }

        const float tIn  = fromDb (v[P_INPUT].load());
        const float tVol = fromDb (v[P_VOLUME].load());
        const float tMix = v[P_MIX].load() * 0.01f;
        if (snap) { sIn = tIn; sVol = tVol; sMix = tMix; snap = false; }
        const float sm = 1.f - std::exp (-1.f / (0.02f * sr));
        const float dynSpan = std::log2 (kDynTopHz / floorHz);

        for (int i = 0; i < n; ++i)
        {
            sIn += (tIn - sIn) * sm;  sVol += (tVol - sVol) * sm;  sMix += (tMix - sMix) * sm;
            const float x[2] { inL[i] * sIn, inR[i] * sIn };

            // ---- hit detector (stereo-linked): drives the decay envelope and the Out 1-2 filter envelope ----
            const float lev = std::fmax (std::fabs (x[0]), std::fabs (x[1]));
            envFast = flush (lev > envFast ? lev : envFast * fastK);
            envSlow = flush (slowK * envSlow + (1.f - slowK) * lev);
            if (holdoff > 0) --holdoff;
            else if (envFast > 0.01f && envFast > 2.f * envSlow) { decayGain = 1.f; dynEnv = 1.f; holdoff = holdN; }
            decayGain = decayOn ? flush (decayGain * decayK) : 1.f;
            dynEnv = flush (dynEnv * sweepK);

            // ---- ADC: sample-and-hold, no anti-alias filter, 12-bit ----
            adcPhase += adcInc;
            if (! pitchMode)
                while (adcPhase >= 1.0) { adcPhase -= 1.0; held[0] = q12 (x[0]); held[1] = q12 (x[1]); }
            else if (adcPhase >= 1.0) adcPhase -= std::floor (adcPhase);

            // ---- DAC clock (26.04 kHz): drop-sample playback, decay, 12-bit DAC, zero-order hold ----
            dacPhase += dacInc;
            while (dacPhase >= 1.0)
            {
                dacPhase -= 1.0;
                float smp[2] { held[0], held[1] };
                if (pitchMode)
                {
                    mem[0][wpos] = q12 (x[0]); mem[1][wpos] = q12 (x[1]);
                    // tune 0 plays the incoming sample directly; moving Tune off 0 (or back) fades to the
                    // read heads over ~10 ms instead of switching, which would click
                    psMix += ((retune ? 1.f : 0.f) - psMix) * kPsFade;
                    if (psMix < 1e-4f) { psMix = 0.f; smp[0] = mem[0][wpos]; smp[1] = mem[1][wpos]; }
                    else
                    {
                        // two drop-sample read heads (no interpolation) sweeping through memory; one plays solo
                        // (exact pitch) and they crossfade only for kPsXfade of the time, away from each wrap
                        psDelay += 1.f - ratio;
                        while (psDelay < 0.f) psDelay += kPsWin;
                        while (psDelay >= kPsWin) psDelay -= kPsWin;
                        const float d2 = psDelay + kPsWin * 0.5f >= kPsWin ? psDelay - kPsWin * 0.5f : psDelay + kPsWin * 0.5f;
                        const int r1 = (wpos - (int) psDelay) & kPsMask, r2 = (wpos - (int) d2) & kPsMask;
                        const float tri = 1.f - std::fabs (2.f * psDelay / kPsWin - 1.f);
                        const float g1 = clampf ((tri - 0.5f) / kPsXfade + 0.5f, 0.f, 1.f), g2 = 1.f - g1;
                        for (int c = 0; c < 2; ++c)
                            smp[c] = mem[c][wpos] + psMix * (mem[c][r1] * g1 + mem[c][r2] * g2 - mem[c][wpos]);
                    }
                    wpos = (wpos + 1) & kPsMask;
                }
                dac[0] = q12 (smp[0] * decayGain);
                dac[1] = q12 (smp[1] * decayGain);
            }

            // ---- output channel filter ----
            float y[2] { dac[0], dac[1] };
            if (channel == 0)
            {
                // SSM2044 VCF: opens on each hit, sweeps down to the floor trim (Z80-generated AR envelope)
                const float hz = floorHz * std::exp2 (dynSpan * dynEnv);
                filt[0].setCutoff (hz, sr); filt[1].G = filt[0].G;
            }
            // filters run on every output so switching between them is seamless; Out 7-8 takes the unfiltered DAC
            const float fl = filt[0].run (y[0]), fr = filt[1].run (y[1]);
            if (channel != 3) { y[0] = fl; y[1] = fr; }

            // output amplifier: gentle op-amp rounding, then mix + volume slider
            y[0] = softSat (y[0] * 0.5f) * 2.f;
            y[1] = softSat (y[1] * 0.5f) * 2.f;
            const float dryG = 1.f - sMix;          // exact at both ends: mix 100 = pure DAC staircase, 0 = dry
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
        case effGetProductString: copyStr (ptr, "SP1200", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "GlueBus", 32); return 1;
        case effGetVendorVersion: return 1010;
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

SP_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
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
    fx.uniqueID = ('S' << 24) | ('P' << 16) | ('1' << 8) | '2';   // 'SP12' = 0x53503132
    fx.version = 1010;
    return &fx;
}
