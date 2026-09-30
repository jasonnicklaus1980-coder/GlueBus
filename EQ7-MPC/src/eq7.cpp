// EQ7 - 7-band parametric EQ for Akai MPC OS (Gen1, 32-bit ARM), built from the GlueBus / SP1200 / ASR10 components
// (dependency-free VST2 core, RBJ biquads, parameter/preset model, host notification, packaging, skin pipeline).
// Needs only libc/libm. No GUI: MPC draws the skin from /sdcard/Synths.
//
// Each band: On, Type (Peak, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass), Freq, Gain, Q, Slope
// (12 / 24 / 48 dB/oct for the cuts). Zero latency. Frequency, gain and Q glide (no zipper noise); flat bands are
// skipped, so a flat EQ is bit-transparent.
// The screen's graph is made of read-only parameters that the plugin sends to MPC (audioMasterAutomate):
//  - curve: the combined response at 64 log-spaced frequencies, re-sent when a band changes
//  - analyzer: the output spectrum in 32 log-spaced bands (2048-point FFT, ~14 updates/s), switchable
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#define EQ_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
constexpr int kBands = 7, kPerBand = 6;
enum BandParam { B_ON, B_TYPE, B_FREQ, B_GAIN, B_Q, B_SLOPE };
constexpr int kCurvePts = 64, kSpecBands = 32;
enum { P_OUTPUT = kBands * kPerBand, P_BYPASS, P_ANALYZER, P_CURVE0,       // 42, 43, 44, 45..108
       P_SPEC0 = P_CURVE0 + kCurvePts, P_COUNT = P_SPEC0 + kSpecBands };   // 109..140, 141
constexpr float kCurveRange = 18.f;                          // curve display: +-18 dB
constexpr float kSpecFloor = -84.f;                          // analyzer display: -84 .. 0 dBFS
constexpr int kFftBits = 11, kFft = 1 << kFftBits;           // 2048-point analyzer FFT
inline int bp (int band, int what) { return band * kPerBand + what; }

enum FType { T_PEAK, T_LSHELF, T_HSHELF, T_LCUT, T_HCUT, T_NOTCH, T_BPASS, T_COUNT, T_NONE = -1 };   // T_NONE: band off/flat
const char* const kTypeNames[]  { "Peak", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
const char* const kSlopeNames[] { "12 dB", "24 dB", "48 dB" };

enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_LOG, K_READ };
enum Fmt  { F_NUM, F_HZ, F_GAIN, F_Q, F_CURVE, F_SPEC };

struct ParamDef
{
    char name[16]; const char* unit; Kind kind;
    float lo, hi, step, def;
    const char* const* choices; int n; Fmt fmt;
};

// band defaults: low shelf, five peaks, high shelf
const int   kDefType[kBands] { T_LSHELF, T_PEAK, T_PEAK, T_PEAK, T_PEAK, T_PEAK, T_HSHELF };
const float kDefFreq[kBands] { 60.f, 150.f, 400.f, 1000.f, 2500.f, 6000.f, 10000.f };
const float kDefQ[kBands]    { 0.707f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.707f };

ParamDef kParams[P_COUNT];
float kCurveHz[kCurvePts], kSpecLo[kSpecBands], kSpecHi[kSpecBands];

void initParams()
{
    for (int b = 0; b < kBands; ++b)
    {
        auto set = [&] (int w, const char* nm, const char* unit, Kind k, float lo, float hi, float step, float def,
                        const char* const* ch, int n, Fmt f)
        {
            ParamDef& d = kParams[bp (b, w)];
            std::snprintf (d.name, sizeof d.name, "%d %s", b + 1, nm);
            d.unit = unit; d.kind = k; d.lo = lo; d.hi = hi; d.step = step; d.def = def; d.choices = ch; d.n = n; d.fmt = f;
        };
        set (B_ON,    "On",    "",   K_BOOL,   0.f, 1.f, 1.f, 1.f, nullptr, 0, F_NUM);
        set (B_TYPE,  "Type",  "",   K_CHOICE, 0.f, (float) (T_COUNT - 1), 1.f, (float) kDefType[b], kTypeNames, T_COUNT, F_NUM);
        set (B_FREQ,  "Freq",  "",   K_LOG,   20.f, 20000.f, 0.f, kDefFreq[b], nullptr, 0, F_HZ);
        set (B_GAIN,  "Gain",  "dB", K_FLOAT, -18.f, 18.f, 0.1f, 0.f, nullptr, 0, F_GAIN);
        set (B_Q,     "Q",     "",   K_LOG,   0.1f, 18.f, 0.f, kDefQ[b], nullptr, 0, F_Q);
        set (B_SLOPE, "Slope", "",   K_CHOICE, 0.f, 2.f, 1.f, 1.f, kSlopeNames, 3, F_NUM);
    }
    ParamDef& o = kParams[P_OUTPUT];
    std::snprintf (o.name, sizeof o.name, "Output"); o.unit = "dB"; o.kind = K_FLOAT; o.lo = -18.f; o.hi = 18.f; o.step = 0.1f; o.def = 0.f;
    o.choices = nullptr; o.n = 0; o.fmt = F_GAIN;
    ParamDef& by = kParams[P_BYPASS];
    std::snprintf (by.name, sizeof by.name, "Bypass"); by.unit = ""; by.kind = K_BOOL; by.lo = 0.f; by.hi = 1.f; by.step = 1.f; by.def = 0.f;
    by.choices = nullptr; by.n = 0; by.fmt = F_NUM;
    ParamDef& an = kParams[P_ANALYZER];
    std::snprintf (an.name, sizeof an.name, "Analyzer"); an.unit = ""; an.kind = K_BOOL; an.lo = 0.f; an.hi = 1.f; an.step = 1.f; an.def = 1.f;
    an.choices = nullptr; an.n = 0; an.fmt = F_NUM;
    for (int i = 0; i < kSpecBands; ++i)
    {
        ParamDef& c = kParams[P_SPEC0 + i];
        kSpecLo[i] = 20.f * std::pow (1000.f, (float) i / kSpecBands);           // 32 equal log bands, 20 Hz .. 20 kHz
        kSpecHi[i] = 20.f * std::pow (1000.f, (float) (i + 1) / kSpecBands);
        std::snprintf (c.name, sizeof c.name, "Spectrum %d", i + 1);
        c.unit = "dB"; c.kind = K_READ; c.lo = kSpecFloor; c.hi = 0.f; c.step = 0.f; c.def = kSpecFloor;
        c.choices = nullptr; c.n = 0; c.fmt = F_SPEC;
    }
    for (int i = 0; i < kCurvePts; ++i)
    {
        ParamDef& c = kParams[P_CURVE0 + i];
        kCurveHz[i] = 20.f * std::pow (1000.f, (float) i / (kCurvePts - 1));      // 20 Hz .. 20 kHz
        if (kCurveHz[i] < 1000.f) std::snprintf (c.name, sizeof c.name, "Curve %.0f", (double) kCurveHz[i]);
        else std::snprintf (c.name, sizeof c.name, "Curve %.1fk", (double) (kCurveHz[i] * 0.001f));
        c.unit = "dB"; c.kind = K_READ; c.lo = -kCurveRange; c.hi = kCurveRange; c.step = 0.f; c.def = 0.f;
        c.choices = nullptr; c.n = 0; c.fmt = F_CURVE;
    }
}

// ---- factory presets: overrides on top of the defaults ----
struct Ov { int id; float v; };
struct Preset { const char* name; const Ov* ov; int n; };
#define T(b) bp (b - 1, B_TYPE)
#define ON(b) bp (b - 1, B_ON)
#define F(b) bp (b - 1, B_FREQ)
#define G(b) bp (b - 1, B_GAIN)
#define Q(b) bp (b - 1, B_Q)
#define S(b) bp (b - 1, B_SLOPE)
#define PRESET(sym, ...) const Ov sym[] = { { P_OUTPUT, 0.f }, __VA_ARGS__ };
#define ENTRY(name, sym) { name, sym, (int) (sizeof (sym) / sizeof (sym[0])) }

PRESET (pFlat,       { P_OUTPUT, 0.f })
// drums
PRESET (pKickPunch,  { T(1), T_LCUT }, { F(1), 28.f }, { S(1), 1 }, { F(2), 60.f }, { G(2), 4.f }, { Q(2), 1.2f },
                     { F(3), 350.f }, { G(3), -5.f }, { Q(3), 1.5f }, { F(5), 3500.f }, { G(5), 4.f }, { Q(5), 1.4f })
PRESET (pKickSub,    { T(1), T_LCUT }, { F(1), 25.f }, { F(2), 50.f }, { G(2), 5.f }, { Q(2), 1.f },
                     { F(3), 300.f }, { G(3), -4.f }, { T(7), T_HCUT }, { F(7), 9000.f })
PRESET (pSnareCrack, { T(1), T_LCUT }, { F(1), 90.f }, { F(2), 200.f }, { G(2), 3.f }, { F(3), 900.f }, { G(3), -3.f },
                     { Q(3), 2.f }, { F(6), 5000.f }, { G(6), 4.f }, { T(7), T_HSHELF }, { F(7), 10000.f }, { G(7), 2.f })
PRESET (pSnareFat,   { T(1), T_LCUT }, { F(1), 70.f }, { F(2), 180.f }, { G(2), 4.f }, { Q(2), 1.2f }, { F(4), 1200.f },
                     { G(4), -2.f }, { T(7), T_HCUT }, { F(7), 12000.f })
PRESET (pHats,       { T(1), T_LCUT }, { F(1), 400.f }, { S(1), 2 }, { F(5), 3000.f }, { G(5), -2.f }, { Q(5), 2.f },
                     { T(7), T_HSHELF }, { F(7), 12000.f }, { G(7), 2.5f })
PRESET (pHatsTame,   { T(1), T_LCUT }, { F(1), 300.f }, { F(6), 7000.f }, { G(6), -4.f }, { Q(6), 1.5f },
                     { T(7), T_HCUT }, { F(7), 14000.f })
PRESET (pDrumBus,    { T(1), T_LCUT }, { F(1), 25.f }, { F(2), 70.f }, { G(2), 2.5f }, { F(3), 400.f }, { G(3), -2.5f },
                     { F(5), 4000.f }, { G(5), 1.5f }, { T(7), T_HSHELF }, { F(7), 11000.f }, { G(7), 2.f })
PRESET (pBoomBap,    { T(1), T_LCUT }, { F(1), 35.f }, { F(2), 90.f }, { G(2), 3.f }, { F(3), 500.f }, { G(3), -2.f },
                     { T(7), T_HCUT }, { F(7), 9000.f }, { S(7), 0 })
PRESET (pToms,       { T(1), T_LCUT }, { F(1), 60.f }, { F(2), 110.f }, { G(2), 3.f }, { F(3), 500.f }, { G(3), -4.f },
                     { Q(3), 1.5f }, { F(5), 4000.f }, { G(5), 3.f })
// bass
PRESET (p808,        { T(1), T_LCUT }, { F(1), 28.f }, { S(1), 2 }, { F(2), 55.f }, { G(2), 3.f }, { F(3), 250.f },
                     { G(3), -3.f }, { F(5), 1200.f }, { G(5), 2.f }, { Q(5), 1.2f })
PRESET (p808Clean,   { T(1), T_LCUT }, { F(1), 30.f }, { F(3), 300.f }, { G(3), -4.f }, { T(7), T_HCUT }, { F(7), 5000.f })
PRESET (pBassDI,     { T(1), T_LCUT }, { F(1), 35.f }, { F(2), 80.f }, { G(2), 2.f }, { F(3), 250.f }, { G(3), -3.f },
                     { F(5), 800.f }, { G(5), 2.5f }, { T(7), T_HCUT }, { F(7), 8000.f })
PRESET (pSynthBass,  { T(1), T_LCUT }, { F(1), 30.f }, { F(2), 100.f }, { G(2), 2.f }, { F(4), 700.f }, { G(4), -2.f },
                     { F(5), 2000.f }, { G(5), 2.f })
// vocals
PRESET (pVocal,      { T(1), T_LCUT }, { F(1), 90.f }, { S(1), 1 }, { F(3), 300.f }, { G(3), -3.f }, { F(5), 3000.f },
                     { G(5), 2.f }, { T(7), T_HSHELF }, { F(7), 10000.f }, { G(7), 3.f })
PRESET (pRapVocal,   { T(1), T_LCUT }, { F(1), 100.f }, { F(2), 180.f }, { G(2), 1.5f }, { F(3), 400.f }, { G(3), -3.f },
                     { F(5), 4000.f }, { G(5), 3.f }, { F(6), 7000.f }, { G(6), -2.f }, { Q(6), 3.f },
                     { T(7), T_HSHELF }, { F(7), 12000.f }, { G(7), 2.f })
PRESET (pVocalAir,   { T(1), T_LCUT }, { F(1), 80.f }, { F(4), 2500.f }, { G(4), -1.5f }, { T(7), T_HSHELF },
                     { F(7), 12000.f }, { G(7), 5.f })
PRESET (pDeEss,      { T(1), T_LCUT }, { F(1), 80.f }, { F(6), 6500.f }, { G(6), -6.f }, { Q(6), 4.f })
PRESET (pBgVox,      { T(1), T_LCUT }, { F(1), 180.f }, { F(3), 400.f }, { G(3), -4.f }, { F(5), 3000.f },
                     { G(5), -2.f }, { T(7), T_HSHELF }, { F(7), 9000.f }, { G(7), 2.f })
PRESET (pWarmVocal,  { T(1), T_LCUT }, { F(1), 70.f }, { F(2), 200.f }, { G(2), 2.f }, { F(5), 3500.f }, { G(5), -1.5f },
                     { T(7), T_HCUT }, { F(7), 15000.f })
// instruments
PRESET (pKeys,       { T(1), T_LCUT }, { F(1), 60.f }, { F(3), 350.f }, { G(3), -2.f }, { F(5), 2500.f }, { G(5), 1.5f })
PRESET (pRhodes,     { T(1), T_LCUT }, { F(1), 70.f }, { F(2), 180.f }, { G(2), 2.f }, { F(5), 3000.f }, { G(5), -2.f },
                     { T(7), T_HCUT }, { F(7), 10000.f })
PRESET (pPiano,      { T(1), T_LCUT }, { F(1), 40.f }, { F(3), 250.f }, { G(3), -2.f }, { F(5), 4000.f }, { G(5), 1.5f },
                     { T(7), T_HSHELF }, { F(7), 10000.f }, { G(7), 1.5f })
PRESET (pPads,       { T(1), T_LCUT }, { F(1), 150.f }, { F(3), 500.f }, { G(3), -2.f }, { T(7), T_HCUT }, { F(7), 9000.f },
                     { S(7), 0 })
PRESET (pGuitar,     { T(1), T_LCUT }, { F(1), 90.f }, { F(3), 250.f }, { G(3), -3.f }, { F(5), 2500.f }, { G(5), 2.f },
                     { T(7), T_HCUT }, { F(7), 12000.f })
PRESET (pStrings,    { T(1), T_LCUT }, { F(1), 80.f }, { F(3), 300.f }, { G(3), -1.5f }, { F(6), 5000.f }, { G(6), 2.f })
PRESET (pBrass,      { T(1), T_LCUT }, { F(1), 80.f }, { F(3), 400.f }, { G(3), -2.f }, { F(5), 1500.f }, { G(5), 2.f },
                     { T(7), T_HCUT }, { F(7), 12000.f })
// samples / lo-fi / effects
PRESET (pSampleChop, { T(1), T_LCUT }, { F(1), 40.f }, { F(3), 300.f }, { G(3), -2.f }, { T(7), T_HCUT }, { F(7), 12000.f })
PRESET (pDusty,      { T(1), T_LCUT }, { F(1), 80.f }, { F(2), 150.f }, { G(2), 2.f }, { F(5), 2500.f }, { G(5), 2.f },
                     { T(7), T_HCUT }, { F(7), 7000.f }, { S(7), 1 })
PRESET (pTelephone,  { T(1), T_LCUT }, { F(1), 400.f }, { S(1), 2 }, { F(4), 1500.f }, { G(4), 4.f },
                     { T(7), T_HCUT }, { F(7), 3500.f }, { S(7), 2 })
PRESET (pRadio,      { T(1), T_LCUT }, { F(1), 250.f }, { S(1), 1 }, { F(4), 1800.f }, { G(4), 3.f },
                     { T(7), T_HCUT }, { F(7), 5500.f }, { S(7), 1 })
PRESET (pUnderwater, { T(7), T_HCUT }, { F(7), 500.f }, { S(7), 2 }, { F(2), 120.f }, { G(2), 3.f })
PRESET (pThinOut,    { T(1), T_LCUT }, { F(1), 700.f }, { S(1), 2 })
PRESET (pBassOnly,   { T(7), T_HCUT }, { F(7), 180.f }, { S(7), 2 })
PRESET (pNoBass,     { T(1), T_LCUT }, { F(1), 180.f }, { S(1), 2 })
PRESET (pMidsOnly,   { T(4), T_BPASS }, { F(4), 1000.f }, { Q(4), 0.7f })
PRESET (pNotch60,    { T(3), T_NOTCH }, { F(3), 60.f }, { Q(3), 8.f }, { T(4), T_NOTCH }, { F(4), 120.f }, { Q(4), 8.f })
PRESET (pMudCut,     { F(3), 300.f }, { G(3), -4.f }, { Q(3), 1.f })
PRESET (pSmile,      { F(1), 80.f }, { G(1), 4.f }, { F(4), 1000.f }, { G(4), -3.f }, { Q(4), 0.7f },
                     { F(7), 10000.f }, { G(7), 4.f })
// buses
PRESET (pMixBus,     { T(1), T_LCUT }, { F(1), 22.f }, { F(2), 100.f }, { G(2), 1.f }, { F(4), 1000.f }, { G(4), -0.5f },
                     { T(7), T_HSHELF }, { F(7), 12000.f }, { G(7), 1.5f })
PRESET (pMaster,     { T(1), T_LCUT }, { F(1), 20.f }, { S(1), 1 }, { F(3), 300.f }, { G(3), -1.f }, { Q(3), 0.7f },
                     { T(7), T_HSHELF }, { F(7), 14000.f }, { G(7), 1.f })
PRESET (pLoudness,   { F(1), 60.f }, { G(1), 3.f }, { F(7), 12000.f }, { G(7), 3.f })

const Preset kPresets[] =
{
    ENTRY ("Flat", pFlat),
    ENTRY ("Kick Punch", pKickPunch),     ENTRY ("Kick Sub", pKickSub),         ENTRY ("Snare Crack", pSnareCrack),
    ENTRY ("Snare Fat", pSnareFat),       ENTRY ("Hats Bright", pHats),         ENTRY ("Hats Tame", pHatsTame),
    ENTRY ("Drum Bus", pDrumBus),         ENTRY ("Boom Bap Drums", pBoomBap),   ENTRY ("Toms", pToms),
    ENTRY ("808", p808),                  ENTRY ("808 Clean", p808Clean),       ENTRY ("Bass DI", pBassDI),
    ENTRY ("Synth Bass", pSynthBass),
    ENTRY ("Vocal", pVocal),              ENTRY ("Rap Vocal", pRapVocal),       ENTRY ("Vocal Air", pVocalAir),
    ENTRY ("De-Ess", pDeEss),             ENTRY ("Backing Vocals", pBgVox),     ENTRY ("Warm Vocal", pWarmVocal),
    ENTRY ("Keys", pKeys),                ENTRY ("Rhodes", pRhodes),            ENTRY ("Piano", pPiano),
    ENTRY ("Pads", pPads),                ENTRY ("Guitar", pGuitar),            ENTRY ("Strings", pStrings),
    ENTRY ("Brass", pBrass),
    ENTRY ("Sample Chop", pSampleChop),   ENTRY ("Dusty", pDusty),              ENTRY ("Telephone", pTelephone),
    ENTRY ("Radio", pRadio),              ENTRY ("Underwater", pUnderwater),    ENTRY ("Thin Out", pThinOut),
    ENTRY ("Bass Only", pBassOnly),       ENTRY ("No Bass", pNoBass),           ENTRY ("Mids Only", pMidsOnly),
    ENTRY ("Hum Notch 60", pNotch60),     ENTRY ("Mud Cut", pMudCut),           ENTRY ("Smile Curve", pSmile),
    ENTRY ("Mix Bus", pMixBus),           ENTRY ("Master Polish", pMaster),     ENTRY ("Loudness", pLoudness),
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
#undef T
#undef ON
#undef F
#undef G
#undef Q
#undef S

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float flush (float x) { return std::fabs (x) < 1e-20f ? 0.f : x; }
inline float fromDb (float db) { return std::exp2 (db * 0.16609640f); }

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
    else if (hz < 10000.f) std::snprintf (out, max, "%.2f kHz", (double) (hz * 0.001f));
    else std::snprintf (out, max, "%.1f kHz", (double) (hz * 0.001f));
}

// ---- RBJ biquad coefficients (GlueBus component) ----
struct Coef { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
Coef rbj (int type, double fs, double f, double q, double gainDb)
{
    f = f < 10.0 ? 10.0 : (f > 0.49 * fs ? 0.49 * fs : f);
    const double w = 2.0 * 3.14159265358979 * f / fs, cw = std::cos (w), sw = std::sin (w);
    const double A = std::pow (10.0, gainDb / 40.0), al = sw / (2 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case T_PEAK:   b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case T_LSHELF:
        {
            const double s = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;       a1 = -2 * ((A - 1) + (A + 1) * cw);     a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case T_HSHELF:
        {
            const double s = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;       a1 = 2 * ((A - 1) - (A + 1) * cw);       a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        case T_LCUT:   b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case T_HCUT:   b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case T_NOTCH:  b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case T_BPASS:  b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        default: break;
    }
    Coef c; c.b0 = (float) (b0 / a0); c.b1 = (float) (b1 / a0); c.b2 = (float) (b2 / a0); c.a1 = (float) (a1 / a0); c.a2 = (float) (a2 / a0);
    return c;
}
// Butterworth section Qs for the cut slopes: 12 dB = 1 section (uses the band's Q), 24 dB = 2, 48 dB = 4
const float kButterQ[3][4] { { 0.f, 0.f, 0.f, 0.f }, { 0.5411961f, 1.3065630f, 0.f, 0.f }, { 0.5097956f, 0.6013449f, 0.8999762f, 2.5629154f } };
const int kSections[3] { 1, 2, 4 };

// one band's settings -> up to 4 biquad sections
struct BandDesign { int type = T_NONE, nsec = 0; Coef c[4]; };
BandDesign designBand (int type, int slope, double fs, float f, float g, float q)
{
    BandDesign d; d.type = type;
    if (type == T_NONE) return d;
    if ((type == T_PEAK || type == T_LSHELF || type == T_HSHELF) && std::fabs (g) < 0.05f) { d.type = T_NONE; return d; }   // flat
    if (type == T_LCUT || type == T_HCUT)
    {
        slope = slope < 0 ? 0 : (slope > 2 ? 2 : slope);
        d.nsec = kSections[slope];
        for (int s = 0; s < d.nsec; ++s) d.c[s] = rbj (type, fs, f, slope == 0 ? q : kButterQ[slope][s], 0.0);
        return d;
    }
    d.nsec = 1; d.c[0] = rbj (type, fs, f, q, g);
    return d;
}
// magnitude (dB) of one band's sections at frequency hz
float bandDb (const BandDesign& d, double fs, double hz)
{
    if (d.type == T_NONE) return 0.f;
    const double w = 2.0 * 3.14159265358979 * hz / fs, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    double mag = 1.0;
    for (int s = 0; s < d.nsec; ++s)
    {
        const Coef& k = d.c[s];
        const double nr = k.b0 + k.b1 * c1 + k.b2 * c2, ni = -(k.b1 * s1 + k.b2 * s2);
        const double dr = 1.0 + k.a1 * c1 + k.a2 * c2, di = -(k.a1 * s1 + k.a2 * s2);
        mag *= std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di + 1e-30));
    }
    return (float) (20.0 * std::log10 (mag + 1e-12));
}

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;

    std::atomic<float> v[P_COUNT];     // plain values (curve entries hold dB)
    std::atomic<float> nv[P_COUNT];    // exact normalised positions (returned unrounded: no stuck Q-Link steps)
    std::atomic<int> paramGen { 1 };   // bumped on every band change: the curve is recomputed when it moves
    int program = 0;
    float sr = 44100.f;

    // DSP state (audio thread only)
    struct Band
    {
        BandDesign d;
        float sf = 1000.f, sg = 0.f, sq = 1.f;           // smoothed frequency, gain, Q
        int type = -1, slope = -1;
        float z1[4][2] {}, z2[4][2] {};
    } band[kBands];
    float sOut = 1.f;
    bool snap = true;
    int curveGen = 0, curveCountdown = 0;
    float curveSent[kCurvePts];
    // analyzer (output spectrum)
    float ring[kFft] {};  int ringPos = 0, specCountdown = 0;  bool specWasOn = false;
    float fre[kFft] {}, fim[kFft] {}, win[kFft] {}, cosT[kFft / 2] {}, sinT[kFft / 2] {};
    int rev[kFft] {};
    float specDisp[kSpecBands], specSent[kSpecBands];

    Plugin()
    {
        for (int i = 0; i < P_COUNT; ++i) setPlain (i, kParams[i].def);
        for (float& c : curveSent) c = 1e9f;
        for (int i = 0; i < kFft; ++i)
        {
            win[i] = 0.5f - 0.5f * std::cos (6.2831853f * i / kFft);                  // Hann
            int r = 0; for (int b = 0; b < kFftBits; ++b) r |= ((i >> b) & 1) << (kFftBits - 1 - b);
            rev[i] = r;
        }
        for (int i = 0; i < kFft / 2; ++i) { cosT[i] = std::cos (6.2831853f * i / kFft); sinT[i] = -std::sin (6.2831853f * i / kFft); }
        for (int i = 0; i < kSpecBands; ++i) { specDisp[i] = kSpecFloor; specSent[i] = 1e9f; }
    }

    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    void setNorm (int i, float norm)
    {
        norm = clampf (norm, 0.f, 1.f);
        nv[i].store (norm); v[i].store (toPlain (kParams[i], norm));
    }
    int bandType (int b) const { return v[bp (b, B_ON)].load() > 0.5f ? (int) v[bp (b, B_TYPE)].load() : T_NONE; }

    void hostSet (int i, float norm)
    {
        if (kParams[i].kind == K_READ) return;                    // curve points are outputs
        setNorm (i, norm);
        if (i < P_OUTPUT) paramGen.fetch_add (1);
    }

    void applyPreset (int i)
    {
        if (i < 0 || i >= kNumPresets) return;
        program = i;
        for (int p = 0; p < P_CURVE0; ++p) if (p != P_BYPASS && p != P_ANALYZER) setPlain (p, kParams[p].def);
        for (int k = 0; k < kPresets[i].n; ++k) setPlain (kPresets[i].ov[k].id, kPresets[i].ov[k].v);
        paramGen.fetch_add (1);
    }

    void notifyHostAll()
    {
        if (master == nullptr) return;
        for (int i = 0; i < P_CURVE0; ++i) master (&fx, 0 /* audioMasterAutomate */, i, 0, nullptr, nv[i].load());
        master (&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
    }

    void reset()
    {
        for (auto& b : band) { std::memset (b.z1, 0, sizeof b.z1); std::memset (b.z2, 0, sizeof b.z2); b.type = -1; }
        snap = true; curveGen = 0; curveCountdown = 0;
        for (float& c : curveSent) c = 1e9f;
        std::memset (ring, 0, sizeof ring); ringPos = 0; specCountdown = 0;
        for (int i = 0; i < kSpecBands; ++i) { specDisp[i] = kSpecFloor; specSent[i] = 1e9f; }
    }

    // ---- analyzer: 2048-point FFT of the output, 32 log bands (peak bin per band), ~14 updates/s ----
    void fft()
    {
        for (int i = 0; i < kFft; ++i) if (rev[i] > i) { float t = fre[i]; fre[i] = fre[rev[i]]; fre[rev[i]] = t; t = fim[i]; fim[i] = fim[rev[i]]; fim[rev[i]] = t; }
        for (int len = 2; len <= kFft; len <<= 1)
        {
            const int half = len >> 1, step = kFft / len;
            for (int i = 0; i < kFft; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float wr = cosT[j * step], wi = sinT[j * step];
                    const int a = i + j, b = a + half;
                    const float tr = fre[b] * wr - fim[b] * wi, ti = fre[b] * wi + fim[b] * wr;
                    fre[b] = fre[a] - tr; fim[b] = fim[a] - ti; fre[a] += tr; fim[a] += ti;
                }
        }
    }
    void sendSpec (int i, float db)
    {
        setPlain (P_SPEC0 + i, db);
        // 2 dB (3 display frames) threshold: frame-to-frame flicker of quiet bands isn't worth an MPC redraw
        if (std::fabs (db - specSent[i]) > 2.f || (db <= kSpecFloor && specSent[i] > kSpecFloor))
        {
            specSent[i] = db;
            if (master != nullptr) master (&fx, 0 /* audioMasterAutomate */, P_SPEC0 + i, 0, nullptr, nv[P_SPEC0 + i].load());
        }
    }
    void analyze (const float* L, const float* R, int n)
    {
        const bool on = v[P_ANALYZER].load() > 0.5f;
        if (! on)
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
        const float binHz = sr / kFft, norm = 4.f / kFft, fall = 30.f * hop / sr;     // 0 dB = full-scale sine; 30 dB/s fall
        for (int bnd = 0; bnd < kSpecBands; ++bnd)
        {
            int k0 = (int) std::ceil (kSpecLo[bnd] / binHz), k1 = (int) std::floor (kSpecHi[bnd] / binHz);
            if (k1 < k0) k0 = k1 = (int) std::floor (std::sqrt (kSpecLo[bnd] * kSpecHi[bnd]) / binHz + 0.5f);   // narrower than a bin
            k0 = k0 < 1 ? 1 : k0; k1 = k1 > kFft / 2 - 1 ? kFft / 2 - 1 : k1;
            float mag = 0.f;
            for (int k = k0; k <= k1; ++k) { const float m = fre[k] * fre[k] + fim[k] * fim[k]; if (m > mag) mag = m; }
            float db = 10.f * std::log10 (mag * norm * norm + 1e-20f);
            db = clampf (db, kSpecFloor, 0.f);
            specDisp[bnd] = db > specDisp[bnd] ? db : std::fmax (db, specDisp[bnd] - fall);
            sendSpec (bnd, specDisp[bnd]);
        }
    }

    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float p = v[idx].load();
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[(int) clampf (p, 0.f, (float) (d.n - 1))]); return; }
        if (d.kind == K_BOOL)   { std::snprintf (out, max, "%s", p >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_HZ:    fmtHz (out, max, p); return;
            case F_GAIN:  std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_Q:     std::snprintf (out, max, p < 10.f ? "%.2f" : "%.1f", (double) p); return;
            case F_CURVE: std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_SPEC:  std::snprintf (out, max, "%.0f dB", (double) p); return;
            default:      std::snprintf (out, max, "%.1f", (double) p); return;
        }
    }

    // ---- curve display: combined response at 40 frequencies, sent to MPC when it changes (max ~20x/s) ----
    void updateCurve (int n)
    {
        curveCountdown -= n;
        const int gen = paramGen.load();
        if (gen == curveGen || curveCountdown > 0) return;
        curveGen = gen; curveCountdown = (int) (sr / 20.f);
        BandDesign d[kBands];
        for (int b = 0; b < kBands; ++b)
            d[b] = designBand (bandType (b), (int) v[bp (b, B_SLOPE)].load(), sr,
                               v[bp (b, B_FREQ)].load(), v[bp (b, B_GAIN)].load(), v[bp (b, B_Q)].load());
        for (int i = 0; i < kCurvePts; ++i)
        {
            float db = 0.f;
            for (int b = 0; b < kBands; ++b) db += bandDb (d[b], sr, kCurveHz[i] < 0.49f * sr ? kCurveHz[i] : 0.49f * sr);
            db = clampf (db, -kCurveRange, kCurveRange);
            setPlain (P_CURVE0 + i, db);
            if (std::fabs (db - curveSent[i]) > 0.05f)
            {
                curveSent[i] = db;
                if (master != nullptr) master (&fx, 0 /* audioMasterAutomate */, P_CURVE0 + i, 0, nullptr, nv[P_CURVE0 + i].load());
            }
        }
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        updateCurve (n);
        if (v[P_BYPASS].load() > 0.5f)
        {
            if (outL != inL) std::memcpy (outL, inL, sizeof (float) * (size_t) n);
            if (outR != inR) std::memcpy (outR, inR, sizeof (float) * (size_t) n);
            snap = true;
            analyze (outL, outR, n);
            return;
        }
        const float tOut = fromDb (v[P_OUTPUT].load());
        const float kSm = 1.f - std::exp (-16.f / (0.02f * sr));          // per-16-sample glide, ~20 ms
        const float kOut = 1.f - std::exp (-1.f / (0.02f * sr));

        for (int pos = 0; pos < n; pos += 16)
        {
            const int m = (n - pos) < 16 ? (n - pos) : 16;
            // ---- control rate: glide each band toward its settings, redesign when it moved ----
            for (int b = 0; b < kBands; ++b)
            {
                Band& B = band[b];
                const int type = bandType (b), slope = (int) v[bp (b, B_SLOPE)].load();
                const float tf = v[bp (b, B_FREQ)].load(), tg = v[bp (b, B_GAIN)].load(), tq = v[bp (b, B_Q)].load();
                bool redo = type != B.type || slope != B.slope;
                if (snap || B.type < 0) { B.sf = tf; B.sg = tg; B.sq = tq; redo = true; }
                else
                {
                    const float nf = B.sf * std::exp2 (std::log2 (tf / B.sf) * kSm);   // glide in octaves
                    const float ng = B.sg + (tg - B.sg) * kSm, nq = B.sq * std::exp2 (std::log2 (tq / B.sq) * kSm);
                    if (std::fabs (nf - B.sf) > 1e-4f * B.sf || std::fabs (ng - B.sg) > 1e-4f || std::fabs (nq - B.sq) > 1e-4f * B.sq)
                    { B.sf = nf; B.sg = ng; B.sq = nq; redo = true; }
                    if (std::fabs (B.sf - tf) < 1e-3f * tf) B.sf = tf;
                    if (std::fabs (B.sg - tg) < 1e-3f) B.sg = tg;
                    if (std::fabs (B.sq - tq) < 1e-3f * tq) B.sq = tq;
                }
                if (redo)
                {
                    const int oldSec = B.d.type == T_NONE ? 0 : B.d.nsec;   // a skipped (flat/off) band has stale state
                    B.d = designBand (type, slope, sr, B.sf, B.sg, B.sq);
                    for (int s = oldSec; s < B.d.nsec; ++s) for (int c = 0; c < 2; ++c) B.z1[s][c] = B.z2[s][c] = 0.f;
                    B.type = type; B.slope = slope;
                }
            }
            if (snap) { sOut = tOut; snap = false; }
            // ---- audio ----
            for (int i = pos; i < pos + m; ++i)
            {
                float x[2] { inL[i], inR[i] };
                for (int b = 0; b < kBands; ++b)
                {
                    Band& B = band[b];
                    if (B.d.type == T_NONE) continue;
                    for (int s = 0; s < B.d.nsec; ++s)
                    {
                        const Coef& k = B.d.c[s];
                        for (int c = 0; c < 2; ++c)
                        {
                            const float y = k.b0 * x[c] + B.z1[s][c];
                            B.z1[s][c] = flush (k.b1 * x[c] - k.a1 * y + B.z2[s][c]);
                            B.z2[s][c] = flush (k.b2 * x[c] - k.a2 * y);
                            x[c] = y;
                        }
                    }
                }
                sOut += (tOut - sOut) * kOut;
                if (std::fabs (sOut - tOut) < 1e-6f) sOut = tOut;
                outL[i] = x[0] * sOut;
                outR[i] = x[1] * sOut;
            }
        }
        analyze (outL, outR, n);
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
        case effCanBeAutomated:   return (idx >= 0 && idx < P_CURVE0) ? 1 : 0;   // curve / analyzer points are read-only
        case effSetSampleRate:    if (opt > 1000.f) { p->sr = opt; p->reset(); p->paramGen.fetch_add (1); } return 0;
        case effMainsChanged:     p->reset(); return 0;
        case effSetBypass:        p->setPlain (P_BYPASS, val ? 1.f : 0.f); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "EQ7", 32); return 1;
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

EQ_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    static bool inited = false;
    if (! inited) { initParams(); inited = true; }
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
    fx.uniqueID = ('E' << 24) | ('Q' << 16) | ('7' << 8) | 'P';   // 'EQ7P' = 0x45513750
    fx.version = 1000;
    return &fx;
}
