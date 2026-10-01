// SP1200 2.0 - a software model of the E-mu SP-1200 signal path, as a native MPC OS VST2 insert (Gen1, 32-bit ARM).
// Built from the GlueBus components (dependency-free VST2 core, parameter model, host notification, packaging, skin
// pipeline). Needs only libc/libm. MPC draws the skin from /sdcard/Synths.
//
// The DSP lives in src/sp/ (one header per stage, see docs/CIRCUIT.md):
//   AnalogInput > ADC (Quantizer12Bit) > SamplerMemory > PlaybackEngine (PitchEngine) > DAC > ReconstructionFilter
//   (SSMModel on Out 1-2) > AnalogOutput, with NoiseModel sources at their stages, HardwareVariation for tolerances,
//   AliasingEngine for the stage options, Analyzer for the screen's spectrum and scope.
// This file: parameters, factory presets (VST programs), the screen readouts, and the VST2 entry points.
#include "sp/Model.h"
#include "vst2.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#define SP_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
using namespace sp;

enum ParamId
{
    P_INPUT, P_DRIVE, P_PITCH, P_SRATE, P_BITS, P_SSM, P_ANALOG, P_HISS, P_OUTPUT, P_MIX,
    P_CHANNEL, P_MODE, P_DECAY, P_SWEEP, P_FLOOR, P_BYPASS,
    P_MACHINE, P_QUALITY, P_RANGE, P_HEADROOM, P_KNEE, P_AAF, P_QMODE, P_CONVN, P_ALIAS, P_RECON, P_LEVEL,
    P_SSMRES, P_SSMPATH, P_ANALOGON, P_DIGITALON, P_SSMON, P_AMPSON, P_FILTON, P_OUTHEAD, P_SETTLE,
    P_HUM, P_MAINS, P_GROUND, P_DIGN, P_ANAN, P_NCOLOR, P_NLEVEL, P_VARIATION, P_UNIT,
    P_ANALYZER, P_TAP, P_SCOPEMS, P_FREEZE,
    P_SPECIN0, P_SPECOUT0 = P_SPECIN0 + 16, P_SCOPE0 = P_SPECOUT0 + 16, P_INFO = P_SCOPE0 + 32, P_ALIASTXT,
    P_COUNT
};
enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_LOG, K_READ, K_TEXT };
enum Fmt { F_DB, F_DBOFF, F_ST, F_HZ, F_BITS, F_PCT, F_SEC, F_DECAY, F_NUM, F_LEVEL, F_COLOR, F_UNIT };

const char* const kChannels[] { "Out 1-2 Dyn", "Out 3-4", "Out 5-6", "Out 7-8" };
const char* const kModes[] { "45>33 Grit", "Pitch", "Replay" };
const char* const kMachines[] { "SP-1200", "SP-12", "S1200 Ref" };
const char* const kQualities[] { "Eco", "Normal", "Accurate", "Reference" };
const char* const kRanges[] { "HW -8..+7", "Ext -12..+12" };
const char* const kQModes[] { "Mid-tread", "Truncate" };
const char* const kAliases[] { "Authentic", "Enhanced", "Bypass" };
const char* const kRecons[] { "Analog ZOH", "Raw Steps", "Ideal LPF" };
const char* const kPaths[] { "Out 1-2 (HW)", "All Outputs" };
const char* const kMains[] { "60 Hz", "50 Hz" };
const char* const kTaps[] { "Input", "Post-ADC", "Post-DAC", "Output" };
const char* const kScopeMs[] { "1 ms", "2 ms", "5 ms", "10 ms", "20 ms" };
const float kScopeVals[] { 1, 2, 5, 10, 20 };

struct ParamDef
{
    const char* key; const char* name; const char* unit; Kind kind; float lo, hi, step, def;
    const char* const* choices; int n; Fmt fmt;
};
#define CH(a) a, (int) (sizeof (a) / sizeof (a[0]))
ParamDef kParams[P_COUNT];
char readNames[P_COUNT][24], readKeys[P_COUNT][16];
void initParams()
{
    auto f = [] (int i, const char* key, const char* name, const char* unit, Kind k, float lo, float hi, float step, float def, Fmt fm)
    { kParams[i] = { key, name, unit, k, lo, hi, step, def, nullptr, 0, fm }; };
    auto c = [] (int i, const char* key, const char* name, const char* const* ch, int n, int def)
    { kParams[i] = { key, name, "", K_CHOICE, 0, (float) (n - 1), 1, (float) def, ch, n, F_NUM }; };
    f (P_INPUT, "input", "Input", "dB", K_FLOAT, -24, 24, 0.1f, 0, F_DB);
    f (P_DRIVE, "drive", "Drive", "dB", K_FLOAT, 0, 24, 0.1f, 0, F_DB);
    f (P_PITCH, "pitch", "Pitch", "st", K_FLOAT, -12, 12, 1, 0, F_ST);
    f (P_SRATE, "srate", "Sample Rate", "Hz", K_LOG, 4000, 48000, 0, 26040, F_HZ);
    f (P_BITS, "bits", "Bits", "bit", K_FLOAT, 4, 16, 1, 12, F_BITS);
    f (P_SSM, "ssm", "SSM Char", "dB", K_FLOAT, -12, 18, 0.1f, 0, F_DB);
    f (P_ANALOG, "analog", "Analog", "%", K_FLOAT, 0, 200, 1, 100, F_PCT);
    f (P_HISS, "hiss", "Hiss", "dB", K_FLOAT, -60, 30, 0.5f, 0, F_DBOFF);
    f (P_OUTPUT, "output", "Output", "dB", K_FLOAT, -24, 12, 0.1f, 0, F_DB);
    f (P_MIX, "mix", "Mix", "%", K_FLOAT, 0, 100, 1, 100, F_PCT);
    c (P_CHANNEL, "channel", "Output Channel", CH (kChannels), 2);
    c (P_MODE, "mode", "Tune Mode", CH (kModes), 0);
    f (P_DECAY, "decay", "Decay", "", K_LOG, 0.02f, kDecayOff, 0, kDecayOff, F_DECAY);
    f (P_SWEEP, "sweep", "Dyn Sweep", "", K_LOG, 0.001f, 0.25f, 0, 0.012f, F_SEC);
    f (P_FLOOR, "floor", "Dyn Floor", "Hz", K_LOG, 100, 2000, 0, 250, F_HZ);
    f (P_BYPASS, "bypass", "Bypass", "", K_BOOL, 0, 1, 1, 0, F_NUM);
    c (P_MACHINE, "machine", "Machine", CH (kMachines), 0);
    c (P_QUALITY, "quality", "Quality", CH (kQualities), 2);
    c (P_RANGE, "range", "Pitch Range", CH (kRanges), 0);
    f (P_HEADROOM, "headroom", "Input Headroom", "dB", K_FLOAT, 0, 24, 0.5f, 8, F_DB);
    f (P_KNEE, "knee", "Input Knee", "%", K_FLOAT, 0, 100, 1, 30, F_PCT);
    f (P_AAF, "aaf", "Anti-Alias Filter", "Hz", K_LOG, 4000, 22000, 0, 15000, F_HZ);
    c (P_QMODE, "qmode", "Quantizer", CH (kQModes), 0);
    f (P_CONVN, "convn", "Converter Noise", "dB", K_FLOAT, -60, 30, 0.5f, 0, F_DBOFF);
    c (P_ALIAS, "alias", "Alias", CH (kAliases), 0);
    c (P_RECON, "recon", "Reconstruction", CH (kRecons), 0);
    f (P_LEVEL, "level", "Voice Level", "", K_FLOAT, 0, 255, 1, 255, F_LEVEL);
    f (P_SSMRES, "ssmres", "SSM Resonance", "%", K_FLOAT, 0, 100, 1, 0, F_PCT);
    c (P_SSMPATH, "ssmpath", "SSM Path", CH (kPaths), 0);
    f (P_ANALOGON, "analogon", "Analog Stages", "", K_BOOL, 0, 1, 1, 1, F_NUM);
    f (P_DIGITALON, "digitalon", "Digital Stages", "", K_BOOL, 0, 1, 1, 1, F_NUM);
    f (P_SSMON, "ssmon", "SSM Stage", "", K_BOOL, 0, 1, 1, 1, F_NUM);
    f (P_AMPSON, "ampson", "Amp Stages", "", K_BOOL, 0, 1, 1, 1, F_NUM);
    f (P_FILTON, "filton", "Filters", "", K_BOOL, 0, 1, 1, 1, F_NUM);
    f (P_OUTHEAD, "outhead", "Output Headroom", "dB", K_FLOAT, 0, 24, 0.5f, 12, F_DB);
    f (P_SETTLE, "settle", "S/H Settling", "%", K_FLOAT, 5, 100, 1, 100, F_PCT);
    f (P_HUM, "hum", "Hum", "dB", K_FLOAT, -60, 30, 0.5f, -60, F_DBOFF);
    c (P_MAINS, "mains", "Mains", CH (kMains), 0);
    f (P_GROUND, "ground", "Ground", "dB", K_FLOAT, -60, 30, 0.5f, -60, F_DBOFF);
    f (P_DIGN, "dign", "Digital Noise", "dB", K_FLOAT, -60, 30, 0.5f, -60, F_DBOFF);
    f (P_ANAN, "anan", "Analog Noise", "dB", K_FLOAT, -60, 30, 0.5f, 0, F_DBOFF);
    f (P_NCOLOR, "ncolor", "Noise Color", "", K_FLOAT, -100, 100, 1, 0, F_COLOR);
    f (P_NLEVEL, "nlevel", "Noise Level", "dB", K_FLOAT, -60, 12, 0.5f, 0, F_DBOFF);
    f (P_VARIATION, "variation", "Component Var", "%", K_FLOAT, 0, 100, 1, 0, F_PCT);
    f (P_UNIT, "unit", "Unit", "", K_FLOAT, 1, 16, 1, 1, F_UNIT);
    f (P_ANALYZER, "analyzer", "Analyzer", "", K_BOOL, 0, 1, 1, 0, F_NUM);   // off: no screen page shows it (saves CPU)
    c (P_TAP, "tap", "Scope Tap", CH (kTaps), 2);
    c (P_SCOPEMS, "scopems", "Scope Time", CH (kScopeMs), 1);
    f (P_FREEZE, "freeze", "Freeze", "", K_BOOL, 0, 1, 1, 0, F_NUM);
    for (int k = 0; k < 16; ++k)
    {
        std::snprintf (readKeys[P_SPECIN0 + k], 16, "specin%d", k); std::snprintf (readNames[P_SPECIN0 + k], 24, "Spectrum In %d", k + 1);
        std::snprintf (readKeys[P_SPECOUT0 + k], 16, "specout%d", k); std::snprintf (readNames[P_SPECOUT0 + k], 24, "Spectrum Out %d", k + 1);
        kParams[P_SPECIN0 + k] = { readKeys[P_SPECIN0 + k], readNames[P_SPECIN0 + k], "", K_READ, 0, 1, 0, 0, nullptr, 0, F_NUM };
        kParams[P_SPECOUT0 + k] = { readKeys[P_SPECOUT0 + k], readNames[P_SPECOUT0 + k], "", K_READ, 0, 1, 0, 0, nullptr, 0, F_NUM };
    }
    for (int k = 0; k < 32; ++k)
    {
        std::snprintf (readKeys[P_SCOPE0 + k], 16, "scope%d", k); std::snprintf (readNames[P_SCOPE0 + k], 24, "Scope %d", k + 1);
        kParams[P_SCOPE0 + k] = { readKeys[P_SCOPE0 + k], readNames[P_SCOPE0 + k], "", K_READ, 0, 1, 0, 0, nullptr, 0, F_NUM };
    }
    kParams[P_INFO] = { "info", "Signal Path", "", K_TEXT, 0, 1000, 0, 0, nullptr, 0, F_NUM };
    kParams[P_ALIASTXT] = { "aliastxt", "Images / Alias", "", K_TEXT, 0, 1000, 0, 0, nullptr, 0, F_NUM };
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

// ---------------------------------------------------------------------------------------------- presets
struct Ov { int id; float v; };
struct Preset { const char* name; const Ov* ov; int n; };
#define PRESET(sym, ...) const Ov sym[] = { __VA_ARGS__ };
#define ENTRY(name, sym) { name, sym, (int) (sizeof (sym) / sizeof (sym[0])) }
const Ov pNone[] = { { P_MIX, 100 } };
PRESET (pDrums, { P_CHANNEL, 3 }, { P_INPUT, 3 }, { P_DRIVE, 3 })
PRESET (pKick, { P_CHANNEL, 0 }, { P_SWEEP, 0.06f }, { P_FLOOR, 180 }, { P_INPUT, 3 }, { P_SSM, 3 })
PRESET (pSnare, { P_CHANNEL, 2 }, { P_DECAY, 0.25f }, { P_DRIVE, 4 })
PRESET (pHats, { P_CHANNEL, 3 }, { P_DECAY, 0.08f })
PRESET (pBreak, { P_CHANNEL, 2 }, { P_MODE, 0 }, { P_PITCH, -2 }, { P_INPUT, 2 })
PRESET (pVinyl, { P_CHANNEL, 1 }, { P_PITCH, -5 }, { P_HISS, 6 }, { P_ANAN, 3 })
PRESET (pSoul, { P_CHANNEL, 1 }, { P_PITCH, -5 }, { P_DRIVE, 3 })
PRESET (pJazz, { P_CHANNEL, 1 }, { P_PITCH, -3 }, { P_HISS, 3 })
PRESET (pBass, { P_CHANNEL, 0 }, { P_FLOOR, 400 }, { P_SWEEP, 0.25f }, { P_SSM, 6 })
PRESET (pVocal, { P_CHANNEL, 2 }, { P_PITCH, -2 })
PRESET (pPiano, { P_CHANNEL, 2 }, { P_PITCH, -4 })
PRESET (pStrings, { P_CHANNEL, 1 }, { P_PITCH, -5 }, { P_HISS, 3 })
PRESET (pChop, { P_CHANNEL, 2 }, { P_DECAY, 0.25f })
PRESET (pPitchUp, { P_MODE, 1 }, { P_PITCH, 5 }, { P_CHANNEL, 3 })
PRESET (pPitchDn, { P_MODE, 1 }, { P_PITCH, -5 }, { P_CHANNEL, 3 })
PRESET (pCrunch, { P_PITCH, -8 }, { P_CHANNEL, 3 }, { P_INPUT, 3 })
PRESET (pAlias, { P_CHANNEL, 3 }, { P_AAF, 22000 }, { P_PITCH, -7 })
PRESET (pHiss, { P_HISS, 12 }, { P_ANAN, 6 })
PRESET (pMax, { P_MACHINE, 2 }, { P_PITCH, -8 }, { P_INPUT, 6 }, { P_DRIVE, 9 }, { P_CHANNEL, 3 })
PRESET (pReplayUp, { P_MODE, 2 }, { P_PITCH, 7 }, { P_CHANNEL, 3 })
PRESET (pReplayDn, { P_MODE, 2 }, { P_PITCH, -8 }, { P_CHANNEL, 3 })
PRESET (pSp12, { P_MACHINE, 1 })
PRESET (pSp12Drums, { P_MACHINE, 1 }, { P_CHANNEL, 3 }, { P_INPUT, 3 })
PRESET (pRef, { P_MACHINE, 2 })
PRESET (pRefDown, { P_MACHINE, 2 }, { P_PITCH, -5 })
PRESET (pSsmWarm, { P_CHANNEL, 0 }, { P_FLOOR, 1200 }, { P_SWEEP, 0.25f }, { P_SSM, 6 })
PRESET (pSsmDrive, { P_CHANNEL, 0 }, { P_SSM, 12 }, { P_FLOOR, 2000 })
PRESET (pSsmPunch, { P_CHANNEL, 0 }, { P_SWEEP, 0.03f }, { P_FLOOR, 300 }, { P_SSM, 6 })
PRESET (pSsmAnalog, { P_SSMPATH, 1 }, { P_SSM, 9 }, { P_ANALOG, 140 })
PRESET (pSsmOut, { P_CHANNEL, 0 }, { P_DRIVE, 9 }, { P_SSM, 6 })
PRESET (pMpc, { P_SRATE, 40000 }, { P_CHANNEL, 3 }, { P_AAF, 18000 })
PRESET (pMpcBus, { P_SRATE, 40000 }, { P_CHANNEL, 3 }, { P_AAF, 18000 }, { P_DRIVE, 6 })
PRESET (pMpcSample, { P_SRATE, 40000 }, { P_AAF, 18000 }, { P_PITCH, -2 })
PRESET (pMpcVinyl, { P_SRATE, 40000 }, { P_AAF, 18000 }, { P_HISS, 6 }, { P_HUM, -6 }, { P_PITCH, -3 })
PRESET (pMpcBass, { P_SRATE, 40000 }, { P_AAF, 18000 }, { P_CHANNEL, 1 }, { P_DRIVE, 4 })
PRESET (pLofiHiss, { P_HISS, 15 }, { P_NCOLOR, -50 }, { P_ANAN, 6 })
PRESET (pLofiDust, { P_HISS, 9 }, { P_GROUND, -6 }, { P_PITCH, -5 }, { P_CHANNEL, 1 })
PRESET (pLofiDigital, { P_BITS, 8 }, { P_DIGN, 12 }, { P_SRATE, 16000 })
PRESET (pLofiCrunch, { P_BITS, 8 }, { P_SRATE, 11025 }, { P_AAF, 22000 }, { P_DRIVE, 6 })
PRESET (pLofiMachine, { P_HUM, 0 }, { P_GROUND, 0 }, { P_HISS, 6 }, { P_VARIATION, 80 }, { P_PITCH, -4 })
PRESET (pClean12, { P_CHANNEL, 3 }, { P_ANALOG, 0 }, { P_HISS, -60 }, { P_ANAN, -60 })
PRESET (pAnalogOnly, { P_DIGITALON, 0 }, { P_DRIVE, 9 })
PRESET (pDigitalOnly, { P_ANALOGON, 0 })
PRESET (pHot, { P_INPUT, 9 }, { P_OUTPUT, -6 })
PRESET (pParallel, { P_PITCH, -7 }, { P_MIX, 50 })
PRESET (pTom, { P_CHANNEL, 0 }, { P_SWEEP, 0.03f }, { P_FLOOR, 300 })
PRESET (pDusty, { P_CHANNEL, 1 })
PRESET (pUnit7, { P_VARIATION, 100 }, { P_UNIT, 7 })
PRESET (pUnit12, { P_VARIATION, 70 }, { P_UNIT, 12 }, { P_HUM, -12 })
PRESET (p4533, { P_PITCH, -5 }, { P_CHANNEL, 1 })
PRESET (pRaw, { P_CHANNEL, 3 }, { P_RECON, 1 })
PRESET (pIdeal, { P_RECON, 2 })
PRESET (pRefQ, { P_QUALITY, 3 })
const Preset kPresets[] =
{
    ENTRY ("SP-1200 Hardware", pNone), ENTRY ("SP-1200 Drums", pDrums), ENTRY ("SP-1200 Kick", pKick), ENTRY ("SP-1200 Snare", pSnare),
    ENTRY ("SP-1200 Hats", pHats), ENTRY ("SP-1200 Break", pBreak), ENTRY ("SP-1200 Vinyl", pVinyl), ENTRY ("SP-1200 Soul", pSoul),
    ENTRY ("SP-1200 Jazz", pJazz), ENTRY ("SP-1200 Bass", pBass), ENTRY ("SP-1200 Vocal", pVocal), ENTRY ("SP-1200 Piano", pPiano),
    ENTRY ("SP-1200 Strings", pStrings), ENTRY ("SP-1200 Chop", pChop), ENTRY ("SP-1200 Pitch Up", pPitchUp), ENTRY ("SP-1200 Pitch Down", pPitchDn),
    ENTRY ("SP-1200 Crunch", pCrunch), ENTRY ("SP-1200 Alias", pAlias), ENTRY ("SP-1200 Hiss", pHiss), ENTRY ("SP-1200 Maximum Grit", pMax),
    ENTRY ("SP-1200 Replay +7", pReplayUp), ENTRY ("SP-1200 Replay -8", pReplayDn), ENTRY ("SP-12 Hardware", pSp12), ENTRY ("SP-12 Drums", pSp12Drums),
    ENTRY ("S1200 Reference", pRef), ENTRY ("S1200 Pitch Down", pRefDown), ENTRY ("SSM Warm", pSsmWarm), ENTRY ("SSM Drive", pSsmDrive),
    ENTRY ("SSM Punch", pSsmPunch), ENTRY ("SSM Analog", pSsmAnalog), ENTRY ("SSM Output", pSsmOut), ENTRY ("MPC Style", pMpc),
    ENTRY ("MPC Drum Bus", pMpcBus), ENTRY ("MPC Sample", pMpcSample), ENTRY ("MPC Vinyl", pMpcVinyl), ENTRY ("MPC Bass", pMpcBass),
    ENTRY ("Lofi Hiss", pLofiHiss), ENTRY ("Lofi Dust", pLofiDust), ENTRY ("Lofi Digital", pLofiDigital), ENTRY ("Lofi Crunch", pLofiCrunch),
    ENTRY ("Lofi Machine", pLofiMachine), ENTRY ("Clean 12-Bit", pClean12), ENTRY ("Analog Only", pAnalogOnly), ENTRY ("Digital Only", pDigitalOnly),
    ENTRY ("Hot Input Clip", pHot), ENTRY ("Parallel Dirt", pParallel), ENTRY ("Tom Dyn Out 1", pTom), ENTRY ("Dusty Out 3-4", pDusty),
    ENTRY ("Vintage Unit #7", pUnit7), ENTRY ("Vintage Unit #12", pUnit12), ENTRY ("45 to 33 Break", p4533), ENTRY ("Out 7-8 Raw Steps", pRaw),
    ENTRY ("Ideal Recon (non-HW)", pIdeal), ENTRY ("Reference Quality", pRefQ),
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

// ---------------------------------------------------------------------------------------------- plugin
struct Text
{
    char buf[2][96] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    bool publish (const char* s)
    {
        if (std::strcmp (get(), s) == 0) return false;
        const int idle = 1 - live.load(); std::snprintf (buf[idle], sizeof buf[idle], "%s", s); live.store (idle); return true;
    }
};

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> v[P_COUNT], nv[P_COUNT];
    float sent[P_COUNT];
    int program = 0;
    float sr = 44100.f;
    Model model;
    Text info, aliasTxt; int infoGen = 0, aliasGen = 0;
    int displayCountdown = 0;

    Plugin() { for (int i = 0; i < P_COUNT; ++i) { setPlain (i, kParams[i].def); sent[i] = 1e9f; } model.prepare (sr); }
    void setPlain (int i, float plain) { v[i].store (plain); nv[i].store (toNorm (kParams[i], plain)); }
    void setNorm (int i, float norm)
    {
        const ParamDef& d = kParams[i];
        if (d.kind == K_READ || d.kind == K_TEXT) return;
        norm = clampf (norm, 0.f, 1.f);
        nv[i].store (norm); v[i].store (toPlain (d, norm));       // exact host position kept (stepped Q-Link moves add up)
    }
    float get (int i) const { return v[i].load(); }
    void applyPreset (int i)
    {
        if (i < 0 || i >= kNumPresets) return;
        program = i;
        for (int p = 0; p < P_COUNT; ++p)
            if (p != P_BYPASS && p != P_ANALYZER && p != P_TAP && p != P_SCOPEMS && p != P_FREEZE && kParams[p].kind != K_READ && kParams[p].kind != K_TEXT)
                setPlain (p, kParams[p].def);
        for (int k = 0; k < kPresets[i].n; ++k) setPlain (kPresets[i].ov[k].id, kPresets[i].ov[k].v);
    }
    void notifyHostAll()
    {
        if (master == nullptr) return;
        for (int i = 0; i < P_COUNT; ++i) if (kParams[i].kind != K_READ && kParams[i].kind != K_TEXT) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
        master (&fx, audioMasterUpdateDisplay, 0, 0, nullptr, 0.f);
    }
    Settings settings() const
    {
        Settings s;
        s.inputDb = get (P_INPUT); s.driveDb = get (P_DRIVE); s.pitch = get (P_PITCH); s.srate = get (P_SRATE); s.bits = get (P_BITS);
        s.ssmDb = get (P_SSM); s.analog = get (P_ANALOG) * 0.01f; s.hissDb = get (P_HISS); s.outDb = get (P_OUTPUT); s.mix = get (P_MIX) * 0.01f;
        s.channel = (int) get (P_CHANNEL); s.mode = (int) get (P_MODE); s.decay = get (P_DECAY); s.sweep = get (P_SWEEP); s.floorHz = get (P_FLOOR);
        s.bypass = get (P_BYPASS) > 0.5f; s.machine = (int) get (P_MACHINE); s.quality = (int) get (P_QUALITY); s.hwRange = get (P_RANGE) < 0.5f;
        s.headroomDb = get (P_HEADROOM); s.knee = get (P_KNEE) * 0.01f; s.aaHz = get (P_AAF); s.qmode = (int) get (P_QMODE); s.convDb = get (P_CONVN);
        s.alias = (int) get (P_ALIAS); s.recon = (int) get (P_RECON); s.level8 = (int) get (P_LEVEL); s.ssmRes = get (P_SSMRES) * 0.01f;
        s.ssmAll = get (P_SSMPATH) > 0.5f; s.analogOn = get (P_ANALOGON) > 0.5f; s.digitalOn = get (P_DIGITALON) > 0.5f; s.ssmOn = get (P_SSMON) > 0.5f;
        s.ampsOn = get (P_AMPSON) > 0.5f; s.filtersOn = get (P_FILTON) > 0.5f; s.outHeadDb = get (P_OUTHEAD); s.settle = get (P_SETTLE) * 0.01f;
        s.humDb = get (P_HUM); s.hum50 = get (P_MAINS) > 0.5f; s.groundDb = get (P_GROUND); s.digDb = get (P_DIGN); s.anaDb = get (P_ANAN);
        s.color = get (P_NCOLOR) * 0.01f; s.noiseDb = get (P_NLEVEL); s.variation = get (P_VARIATION) * 0.01f; s.unit = (int) get (P_UNIT);
        s.analyzer = get (P_ANALYZER) > 0.5f; s.tap = (int) get (P_TAP); s.scopeMs = kScopeVals[clampi ((int) get (P_SCOPEMS), 0, 4)];
        s.freeze = get (P_FREEZE) > 0.5f;
        return s;
    }

    // ---------------------------------------------------------------- screen readouts (audio thread)
    void send (int i, float plain, float thr)
    {
        setPlain (i, plain);
        if (std::fabs (plain - sent[i]) < thr) return;
        sent[i] = plain;
        if (master != nullptr) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
    }
    void setText (Text& t, int param, int& gen, const char* s)
    {
        if (! t.publish (s)) return;
        gen = (gen + 1) % 1000;
        send (param, (float) gen, 0.5f);
    }
    static int specLevel (float db) { return clampi ((int) std::lround ((db + 84.f) / 84.f * 10.f), 0, 10); }
    static int scopeLevel (float x) { return clampi ((int) std::lround (x * 5.f) + 5, 0, 10); }
    void updateDisplay (const Settings& s)
    {
        char t[96];
        const double F = s.machine == MA_SP12 ? kSp12Rate : clampd (s.srate, 4000.0, 48000.0);
        const int semis = PitchEngine::clampSemis (s.pitch, s.hwRange);
        const double capture = s.mode == PM_GRIT ? F * PitchEngine::ratio (semis) : F;
        std::snprintf (t, sizeof t, "SAMPLE %.2fk  DAC %.2fk  %d-BIT  %+d ST  %s", capture / 1000, F / 1000, (int) std::lround (s.bits), semis, kModes[clampi (s.mode, 0, 2)]);
        setText (info, P_INFO, infoGen, t);
        if (! s.analyzer) return;
        if (! s.freeze) model.an.captureScope (s.scopeMs);
        for (int k = 0; k < 16; ++k)
        {
            send (P_SPECIN0 + k, (float) (specLevel (model.an.bandIn[2 * k]) * 11 + specLevel (model.an.bandIn[2 * k + 1])) / 127.f, 0.5f / 127.f);
            send (P_SPECOUT0 + k, (float) (specLevel (model.an.bandOut[2 * k]) * 11 + specLevel (model.an.bandOut[2 * k + 1])) / 127.f, 0.5f / 127.f);
        }
        for (int k = 0; k < 32; ++k)
            send (P_SCOPE0 + k, (float) (scopeLevel (model.an.scope[2 * k]) * 11 + scopeLevel (model.an.scope[2 * k + 1])) / 127.f, 0.5f / 127.f);
        // energy of the output above the converter's Nyquist (images + aliases that land there), relative to the total
        const double nyq = std::fmin (capture, F) * 0.5;
        double above = 0, total = 0;
        for (int b = 0; b < Analyzer::kBands; ++b)
        {
            const double fc = 40.0 * std::pow (500.0, (b + 0.5) / Analyzer::kBands);
            const double p = std::pow (10.0, model.an.bandOut[b] / 10.0);
            total += p; if (fc > nyq) above += p;
        }
        if (total < 1e-9) std::snprintf (t, sizeof t, "ABOVE %.1f kHz: --", nyq / 1000);
        else std::snprintf (t, sizeof t, "ABOVE %.1f kHz: %.0f dB", nyq / 1000, 10 * std::log10 (above / total + 1e-12));
        setText (aliasTxt, P_ALIASTXT, aliasGen, t);
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const Settings s = settings();
        model.process (inL, inR, outL, outR, n, s);
        displayCountdown -= n;
        if (displayCountdown <= 0) { displayCountdown += (int) (sr / 15.f); updateDisplay (s); }
    }
    void display (int idx, char* out, size_t max) const
    {
        const ParamDef& d = kParams[idx];
        const float p = v[idx].load();
        if (idx == P_INFO) { std::snprintf (out, max, "%s", info.get()); return; }
        if (idx == P_ALIASTXT) { std::snprintf (out, max, "%s", aliasTxt.get()); return; }
        if (d.kind == K_READ) { std::snprintf (out, max, "%.2f", (double) p); return; }
        if (d.kind == K_CHOICE) { std::snprintf (out, max, "%s", d.choices[clampi ((int) p, 0, d.n - 1)]); return; }
        if (d.kind == K_BOOL) { std::snprintf (out, max, "%s", p >= 0.5f ? "On" : "Off"); return; }
        switch (d.fmt)
        {
            case F_DB: std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_DBOFF: if (p <= -59.9f) std::snprintf (out, max, "Off"); else std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case F_ST:
            {
                const bool hw = get (P_RANGE) < 0.5f; const int st = (int) std::lround (p), c = PitchEngine::clampSemis (p, hw);
                if (c != st) std::snprintf (out, max, "%+d (HW %+d)", st, c); else if (st == 0) std::snprintf (out, max, "0"); else std::snprintf (out, max, "%+d", st);
                return;
            }
            case F_HZ:
                if (idx == P_AAF && p >= 21900.f) { std::snprintf (out, max, "Off"); return; }
                if (idx == P_SRATE && std::fabs (p - 26040.f) < 60.f) { std::snprintf (out, max, "26.04 kHz HW"); return; }
                if (p >= 1000.f) std::snprintf (out, max, "%.2f kHz", (double) p / 1000); else std::snprintf (out, max, "%.0f Hz", (double) p);
                return;
            case F_BITS: { const int b = (int) std::lround (p); std::snprintf (out, max, b == 12 ? "12 bit HW" : "%d bit", b); return; }
            case F_PCT: std::snprintf (out, max, "%.0f %%", (double) p); return;
            case F_SEC: if (p < 1.f) std::snprintf (out, max, "%.0f ms", (double) p * 1000); else std::snprintf (out, max, "%.2f s", (double) p); return;
            case F_DECAY: if (p >= kDecayOff * 0.98f) std::snprintf (out, max, "Off"); else if (p < 1.f) std::snprintf (out, max, "%.0f ms", (double) p * 1000); else std::snprintf (out, max, "%.2f s", (double) p); return;
            case F_LEVEL: std::snprintf (out, max, "%d / 255", (int) p); return;
            case F_COLOR: if (std::fabs (p) < 0.5f) std::snprintf (out, max, "White"); else std::snprintf (out, max, "%s %.0f", p < 0 ? "Dark" : "Bright", (double) std::fabs (p)); return;
            case F_UNIT: std::snprintf (out, max, "#%d", (int) p); return;
            default: std::snprintf (out, max, "%.2f", (double) p); return;
        }
    }
};

// ---------------------------------------------------------------------------------------------- VST2 entry points
void processReplacing (AEffect* e, float** in, float** out, int32_t n) { static_cast<Plugin*> (e->object)->process (in[0], in[1], out[0], out[1], n); }
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
void setParameter (AEffect* e, int32_t i, float norm) { if (i >= 0 && i < P_COUNT) static_cast<Plugin*> (e->object)->setNorm (i, norm); }
float getParameter (AEffect* e, int32_t i) { return (i >= 0 && i < P_COUNT) ? static_cast<Plugin*> (e->object)->nv[i].load() : 0.f; }
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose:            p->~Plugin(); std::free (p); return 0;
        case effSetProgram:       p->applyPreset ((int) val); p->notifyHostAll(); return 0;
        case effGetProgram:       return p->program;
        case effGetProgramName:   copyStr (ptr, kPresets[p->program].name); return 0;
        case effGetProgramNameIndexed: if (idx < 0 || idx >= kNumPresets) return 0; copyStr (ptr, kPresets[idx].name); return 1;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].name); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kParams[idx].unit, 8); return 0;
        case effGetParamDisplay:  if (idx >= 0 && idx < P_COUNT && ptr != nullptr) { char b[96]; p->display (idx, b, sizeof b); copyStr (ptr, b, 64); } return 0;
        case effCanBeAutomated:   return (idx >= 0 && idx < P_COUNT && kParams[idx].kind != K_READ && kParams[idx].kind != K_TEXT) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) { p->sr = opt; p->model.prepare (opt); } return 0;
        case effMainsChanged:     if (val) p->model.prepare (p->sr); return 0;
        case effSetBypass:        p->setPlain (P_BYPASS, val ? 1.f : 0.f); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "SP1200", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "GlueBus", 32); return 1;
        case effGetVendorVersion: return 2000;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return 1;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;
        case effCanDo:            if (ptr != nullptr && std::strcmp ((const char*) ptr, "bypass") == 0) return 1; return 0;
        default: return 0;
    }
}
void initOnce() { static bool done = false; if (! done) { initParams(); done = true; } }
} // namespace

SP_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    initOnce();
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
    fx.initialDelay = kLatency;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('S' << 24) | ('P' << 16) | ('1' << 8) | '2';   // 'SP12' = 0x53503132 (same plugin, upgraded in place)
    fx.version = 2000;
    return &fx;
}
// stable parameter keys for tools/make_skin.py
SP_EXPORT const char* SP_ParamKey (int i) { initOnce(); return (i >= 0 && i < P_COUNT) ? kParams[i].key : ""; }
SP_EXPORT int SP_ParamCount() { return P_COUNT; }
SP_EXPORT const char* SP_PresetName (int i) { return (i >= 0 && i < kNumPresets) ? kPresets[i].name : ""; }
SP_EXPORT int SP_PresetCount() { return kNumPresets; }
