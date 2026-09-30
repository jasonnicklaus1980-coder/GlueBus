// Offline VST2 host test for RadioReady EQ (loads the plugin like MPC does, no MPC needed).
// Covers: identity/transparency, every filter type and slope against the curve display, Mid/Side and L/R routing,
// the QUICK RADIO READY section (0 % = untouched, boost cap, level match), click-free changes, presets (names,
// categories, gain staging on pink noise, all distinct), A/B, undo/redo, reset, prev/next + category filter,
// favourites, MPC stepped controls, analyzer, meters, bypass, denormals, sample rates 44.1-192 kHz, HQ 2x.
#include "vst2.h"
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ---- parameter map (must match src/radioready.cpp) ----
enum { P_PRESET, P_CATEGORY, P_FAV, P_PREV, P_NEXT, P_BAND0 };
enum { B_ON, B_TYPE, B_FREQ, B_GAIN, B_Q, B_SLOPE, B_TARGET };
enum { T_BELL, T_LSHELF, T_HSHELF, T_HP, T_LP, T_NOTCH, T_BP };
static const int P_IN = 61, P_OUT = 62, P_MODE = 63, P_HQ = 64, P_AUTOGAIN = 65, P_BYPASS = 66, P_AMOUNT = 67, P_LOWEND = 68,
    P_VOCAL = 69, P_AIR = 70, P_PUNCH = 71, P_ANALYZER = 72, P_VIEW = 73, P_AB = 74, P_COPY = 75, P_UNDO = 76, P_REDO = 77,
    P_RESET = 78, P_CURVE0 = 79, NCURVE = 64, P_SPEC0 = 143, NSPEC = 32, P_METER0 = 175, P_LEVEL = 179, P_COUNT = 180, NPROG = 56;
static int bp(int band, int what) { return P_BAND0 + (band - 1) * 7 + what; }     // bands numbered 1..8 here

static int automateCalls = 0, ioChanged = 0; static float lastAutomate[P_COUNT];
static intptr_t host(AEffect*, int32_t op, int32_t idx, intptr_t, void*, float opt) {
    if (op == 0) { ++automateCalls; if (idx >= 0 && idx < P_COUNT) lastAutomate[idx] = opt; }
    if (op == 13) ++ioChanged;
    return 2400;
}
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static float getNorm(int i) { return fx->getParameter(fx, i); }
static void choice(int i, int v, int n) { setNorm(i, (float) v / (n - 1)); }
static void press(int i) { setNorm(i, 1.f); }
static void band(int b, int type, float hz, float gain = 0, float q = 0.707f, int slope = 1, int target = 0) {
    setNorm(bp(b, B_ON), 1.f); choice(bp(b, B_TYPE), type, 7);
    setNorm(bp(b, B_FREQ), std::log(hz / 20.f) / std::log(1000.f));
    setNorm(bp(b, B_GAIN), (gain + 18.f) / 36.f);
    setNorm(bp(b, B_Q), std::log(q / 0.1f) / std::log(100.f));
    choice(bp(b, B_SLOPE), slope, 3); choice(bp(b, B_TARGET), target, 3);
}
static void gainDb(int i, float db) { setNorm(i, (db + 24.f) / 48.f); }
static void pct(int i, float p) { setNorm(i, p / 100.f); }
static std::string display(int i) { char d[64] = {}; D(effGetParamDisplay, i, 0, d); return d; }
static std::string progName() { char n[64] = {}; D(effGetProgramName, 0, 0, n); return n; }
static float curveDb(int i) { return getNorm(P_CURVE0 + i) * 36.f - 18.f; }
static float curveHz(int i) { return 20.f * std::pow(1000.f, (float) i / (NCURVE - 1)); }
static float db(double x) { return (float) (20.0 * std::log10(x + 1e-12)); }

static void proc(std::vector<float>& il, std::vector<float>& ir, std::vector<float>& ol, std::vector<float>& orr, int block = 512) {
    ol.assign(il.size(), 0); orr.assign(il.size(), 0);
    for (size_t pos = 0; pos < il.size(); pos += block) {
        int m = (int) std::min((size_t) block, il.size() - pos);
        float* in[2] = { &il[pos], &ir[pos] }; float* out[2] = { &ol[pos], &orr[pos] };
        fx->processReplacing(fx, in, out, m);
    }
}
static void run(float seconds) {                      // silence, to let glides / timers settle
    std::vector<float> l((size_t) (seconds * SR), 0.f), r = l, ol, orr; proc(l, r, ol, orr);
}
static double rms(const std::vector<float>& v, size_t from) {
    double s = 0; for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i]; return std::sqrt(s / (v.size() - from));
}
// gain (dB) of a sine through the plugin, left (or right) channel; lr: L and R inputs scale
static float sineGain(float hz, float lIn = 1.f, float rIn = 1.f, bool right = false) {
    const int n = (int) (0.35f * SR), skip = (int) (0.2f * SR);
    std::vector<float> l(n), r(n), ol, orr;
    for (int i = 0; i < n; ++i) { float s = 0.25f * std::sin(6.2831853f * hz * i / SR); l[i] = lIn * s; r[i] = rIn * s; }
    proc(l, r, ol, orr);
    const std::vector<float>& in = right ? r : l; const std::vector<float>& out = right ? orr : ol;
    return db(rms(out, skip) / (rms(in, skip) + 1e-20));
}
// band-limited pink noise (Paul Kellet's filter, then 40 Hz .. 12 kHz), mono in both channels
static std::vector<float> pink(int n, unsigned seed = 1) {
    std::vector<float> o(n); double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 8388608.0 - 1.0; };
    struct BQ { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double p(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    auto mk = [&](bool hp, double f, double q) { double w = 6.283185307 * f / SR, cw = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al; BQ b;
        if (hp) { b.b0 = (1 + cw) / 2 / a0; b.b1 = -(1 + cw) / a0; b.b2 = b.b0; } else { b.b0 = (1 - cw) / 2 / a0; b.b1 = (1 - cw) / a0; b.b2 = b.b0; }
        b.a1 = -2 * cw / a0; b.a2 = (1 - al) / a0; return b; };
    BQ f[4] = { mk(true, 40, 0.5412), mk(true, 40, 1.3066), mk(false, 12000, 0.5412), mk(false, 12000, 1.3066) };
    for (int i = 0; i < n; ++i) {
        double w = rnd();
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        double x = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.05; b6 = w * 0.115926;
        for (auto& q : f) x = q.p(x);
        o[i] = (float) x;
    }
    return o;
}
static float pinkChange() {                            // level change on pink noise, dB
    std::vector<float> l = pink((int) (2.5f * SR)), r = l, ol, orr;
    proc(l, r, ol, orr);
    const size_t skip = (size_t) (0.5f * SR);
    return db(rms(ol, skip) / rms(l, skip));
}
static void fresh(int preset = 0) { D(effSetProgram, 0, preset); D(effMainsChanged, 0, 1); run(0.1f); }

static const char* kNames[] = { "Init (Flat)",
    "Modern Radio Hip-Hop", "Classic Boom Bap", "West Coast G-Funk", "East Coast Punch", "Trap Radio Ready", "Dark Hip-Hop",
    "Clean Hip-Hop Mix", "Vintage Sample Warmth", "Heavy 808 Mix", "Commercial Rap",
    "Radio Lead Vocal", "Warm Male Vocal", "Bright Female Vocal", "Smooth Rap Vocal", "Aggressive Rap Vocal", "Vocal Presence",
    "Vocal Air", "Intimate Vocal", "Vocal Clarity", "Polished Vocal",
    "Punchy Kick", "Deep 808", "Snare Crack", "Classic MPC Drums", "Tight Drum Bus", "Modern Trap Drums", "Boom Bap Drums",
    "Drum Bus Glue", "Bright Hi-Hats", "Full Drum Mix",
    "Radio Master", "Commercial Loudness Prep", "Clean Master Bus", "Warm Master Bus", "Modern Hip-Hop Master", "Smooth High End",
    "Tight Low End", "Wide Mix Tonality", "Analog-Inspired Master", "Final Mix Polish",
    "Warm Piano", "Bright Piano", "Vintage Sample", "Synth Presence", "Electric Bass", "Acoustic Guitar", "String Clarity",
    "Dark Sample Restoration", "Bright Sample Restoration", "Full Instrument Mix",
    "Car Speakers", "Consumer Headphones", "Studio Monitors", "Small Speakers", "Streaming" };

int main(int argc, char** argv) {
    const char* favFile = "build/native/test.favorites";
    std::remove(favFile); setenv("RR_FAVORITES_FILE", favFile, 1);
    void* h = dlopen(argc > 1 ? argv[1] : "build/native/radioready.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    if (!entry) { std::printf("no VSTPluginMain\n"); return 2; }
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic, "magic");
    CHECK(fx->numInputs == 2 && fx->numOutputs == 2, "io");
    CHECK(fx->uniqueID == 0x52524551, "uid %08x", fx->uniqueID);
    CHECK(fx->numParams == P_COUNT && fx->numPrograms == NPROG, "counts %d/%d", fx->numParams, fx->numPrograms);
    CHECK(fx->initialDelay == 0, "zero latency by default");
    { char s[64] = {}; D(effGetEffectName, 0, 0, s); CHECK(std::strcmp(s, "RadioReady EQ") == 0, "name %s", s);
      D(effGetVendorString, 0, 0, s); CHECK(std::strcmp(s, "RadioReady Audio") == 0, "vendor %s", s); }
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effSetBlockSize, 0, 512); D(effMainsChanged, 0, 1);

    std::printf("Parameters:\n");
    for (int i = 0; i < P_COUNT; ++i) {
        char n[64] = {}, u[64] = {};
        D(effGetParamName, i, 0, n); D(effGetParamLabel, i, 0, u);
        if (i < 12 || (i >= 61 && i < 80) || i == P_SPEC0 || i >= P_METER0) std::printf("  %3d %-16s %-16s %s\n", i, n, display(i).c_str(), u);
        CHECK(std::strlen(n) > 0 && !display(i).empty(), "param %d name/display", i);
        CHECK((D(effCanBeAutomated, i) != 0) == (i < P_CURVE0 && i != P_PREV && i != P_NEXT && (i < P_COPY || i > P_RESET)), "automatable %d", i);
    }

    // ---- presets: names and order ----
    int names = 0;
    for (int i = 0; i < NPROG; ++i) { char n[64] = {}; D(effGetProgramNameIndexed, i, 0, n); names += std::strcmp(n, kNames[i]) == 0;
                                      if (std::strcmp(n, kNames[i]) != 0) std::printf("  program %d: '%s' vs '%s'\n", i, n, kNames[i]); }
    CHECK(names == NPROG, "program names (%d/%d match)", names, NPROG);

    // ---- transparency: Init is bit-exact, also with the Quick macros up while Amount = 0 ----
    {
        fresh(0);
        std::vector<float> l(20000), r(20000), ol, orr; unsigned s = 7;
        for (int i = 0; i < 20000; ++i) { s = s * 1664525u + 1013904223u; l[i] = (float) ((int) (s >> 9) - (1 << 22)) / (1 << 22); r[i] = -0.5f * l[i]; }
        proc(l, r, ol, orr);
        CHECK(ol == l && orr == r, "Init preset is bit-transparent");
        pct(P_LOWEND, 100); pct(P_VOCAL, 100); pct(P_AIR, 100); pct(P_PUNCH, 100); pct(P_AMOUNT, 0);
        proc(l, r, ol, orr);
        CHECK(ol == l && orr == r, "Quick section at Amount 0%% is bit-transparent");
    }

    // ---- QUICK RADIO READY: boost cap and level match ----
    {
        fresh(0);
        pct(P_AMOUNT, 100); pct(P_LOWEND, 100); pct(P_VOCAL, 100); pct(P_AIR, 100); pct(P_PUNCH, 100); run(0.3f);
        float mx = -99, mn = 99;
        for (float hz : { 60.f, 100.f, 150.f, 250.f, 400.f, 1000.f, 2000.f, 3200.f, 5000.f, 8000.f, 12000.f, 16000.f }) {
            const float g = sineGain(hz); mx = std::max(mx, g); mn = std::min(mn, g);
        }
        const float pk = pinkChange();
        std::printf("Quick 100%% all: response %+.2f .. %+.2f dB, pink-noise level change %+.2f dB, level comp %s\n", mn, mx, pk, display(P_LEVEL).c_str());
        CHECK(mx <= 4.05f, "quick boost capped (max %+.2f dB)", mx);
        CHECK(std::fabs(pk) < 0.75f, "quick section level-matched (%+.2f dB)", pk);
        CHECK(mx - mn > 2.f, "quick section does something (%.2f dB range)", mx - mn);
        for (int k = 0; k < 4; ++k) {                            // each macro on its own moves the curve where it should
            fresh(0); pct(P_AMOUNT, 100); pct(P_LOWEND + k, 100); run(0.3f);
            const float hz[4] = { 70.f, 3200.f, 14000.f, 120.f }, ref = sineGain(1000.f);
            const float g = sineGain(hz[k]) - (k == 1 ? sineGain(320.f) : (k == 0 ? sineGain(250.f) : ref));
            CHECK(g > 1.f, "macro %d shapes its range (%+.2f dB)", k, g);
        }
        fresh(0); pct(P_AMOUNT, 50); pct(P_AIR, 100); run(0.3f); const float half = sineGain(14000.f) - sineGain(1000.f);
        pct(P_AMOUNT, 100); run(0.3f); const float full = sineGain(14000.f) - sineGain(1000.f);
        CHECK(half > 0.3f * full && half < 0.7f * full, "Amount scales the section (%.2f vs %.2f dB)", half, full);
    }

    // ---- filter types / slopes: measured response vs the curve display, and known points ----
    {
        struct Case { int type; float hz, gain, q; int slope; float probe, expect, tol; const char* what; };
        const Case cases[] = {
            { T_BELL, 1000, 6, 1, 1, 1000, 6, 0.2f, "bell +6 @1k" }, { T_BELL, 1000, -12, 4, 1, 1000, -12, 0.3f, "bell -12 Q4" },
            { T_LSHELF, 100, 6, 0.707f, 1, 30, 6, 0.4f, "low shelf +6" }, { T_HSHELF, 5000, -6, 0.707f, 1, 16000, -6, 0.5f, "high shelf -6" },
            { T_HP, 100, 0, 0.707f, 0, 100, -3, 0.3f, "HP 12 dB -3 @fc" }, { T_HP, 100, 0, 0.707f, 1, 50, -24.1f, 1.f, "HP 24 dB @ -1 oct" },
            { T_HP, 100, 0, 0.707f, 2, 50, -48.2f, 2.f, "HP 48 dB @ -1 oct" }, { T_LP, 2000, 0, 0.707f, 1, 4000, -24.1f, 1.2f, "LP 24 dB @ +1 oct" },
            { T_NOTCH, 1000, 0, 2, 1, 1000, -70, 30.f, "notch" }, { T_BP, 1000, 0, 1, 1, 1000, 0, 0.3f, "band pass centre" } };
        for (const Case& c : cases) {
            fresh(0); band(4, c.type, c.hz, c.gain, c.q, c.slope); run(0.3f);
            const float g = sineGain(c.probe);
            CHECK(std::fabs(g - c.expect) <= c.tol, "%s: %+.2f dB (want %+.2f)", c.what, g, c.expect);
            if (c.type == T_NOTCH) CHECK(g < -30, "notch depth %.1f", g);
        }
        fresh(0); band(3, T_BELL, 300, -5, 1.5f); band(5, T_HSHELF, 8000, 4); band(1, T_HP, 40, 0, 0.707f, 2); run(0.3f);
        float worst = 0;
        for (int i = 6; i < NCURVE; i += 5) { const float d = std::fabs(sineGain(curveHz(i)) - curveDb(i)); worst = std::max(worst, d); }
        CHECK(worst < 0.3f, "curve display matches measured response (worst %.2f dB)", worst);
    }

    // ---- stereo routing: L/R and Mid/Side ----
    {
        fresh(0); band(4, T_BELL, 1000, 9, 1, 1, 1); run(0.3f);                       // Left only
        CHECK(std::fabs(sineGain(1000, 1, 1, false) - 9) < 0.3f && std::fabs(sineGain(1000, 1, 1, true)) < 0.05f, "L/R: left-only band");
        choice(P_MODE, 1, 2); choice(bp(4, B_TARGET), 2, 3); run(0.3f);             // Mid/Side, Side only
        CHECK(std::fabs(sineGain(1000, 1, 1)) < 0.05f, "M/S: side band leaves a mono signal alone (%+.2f)", sineGain(1000, 1, 1));
        CHECK(std::fabs(sineGain(1000, 1, -1) - 9) < 0.3f, "M/S: side band shapes the side (%+.2f)", sineGain(1000, 1, -1));
        choice(bp(4, B_TARGET), 1, 3); run(0.3f);                                     // Mid only
        CHECK(std::fabs(sineGain(1000, 1, 1) - 9) < 0.3f && std::fabs(sineGain(1000, 1, -1)) < 0.05f, "M/S: mid band");
        CHECK(display(bp(4, B_TARGET)) == "Mid", "target display follows mode (%s)", display(bp(4, B_TARGET)).c_str());
        choice(P_VIEW, 1, 2); run(0.2f);
        CHECK(std::fabs(curveDb(32)) < 0.1f, "curve view Side hides a Mid band");
        choice(P_VIEW, 0, 2); run(0.2f);
        CHECK(curveDb(38) > 5.f, "curve view Mid shows it (%.1f)", curveDb(38));
    }

    // ---- click-free: gain sweep, type switch, preset change on a running sine ----
    {
        fresh(0); band(4, T_BELL, 1000, 0, 1); run(0.2f);
        const int n = (int) SR; std::vector<float> l(n), r(n), ol, orr;
        for (int i = 0; i < n; ++i) l[i] = r[i] = 0.5f * std::sin(6.2831853f * 200.f * i / SR);
        float worst = 0;
        auto jumps = [&](std::vector<float>& o) { float w = 0; for (int i = 2; i < n; ++i) w = std::max(w, std::fabs(o[i] - 2 * o[i - 1] + o[i - 2])); return w; };
        const float base = 0.5f * std::pow(6.2831853f * 200.f / SR, 2.f);   // second difference of the clean sine
        for (int step = 0; step < 4; ++step) {
            std::vector<float> ol2, or2; ol2.resize(n); or2.resize(n);
            for (int pos = 0; pos < n; pos += 256) {
                if (pos == n / 2) {
                    if (step == 0) band(4, T_BELL, 1000, 18, 1);
                    if (step == 1) choice(bp(4, B_TYPE), T_LSHELF, 7);
                    if (step == 2) D(effSetProgram, 0, 9);
                    if (step == 3) choice(P_HQ, 1, 2);
                }
                float* in[2] = { &l[pos], &r[pos] }; float* out[2] = { &ol2[pos], &or2[pos] }; fx->processReplacing(fx, in, out, std::min(256, n - pos));
            }
            worst = std::max(worst, jumps(ol2) / base);
        }
        choice(P_HQ, 0, 2);
        CHECK(worst < 8.f, "no clicks on gain / type / preset / quality changes (worst 2nd diff %.1fx a clean sine)", worst);
    }

    // ---- presets: gain staging, distinct curves, categories ----
    {
        std::vector<std::vector<float>> curves;
        float worstLvl = 0; int worstIdx = 0;
        for (int p = 0; p < NPROG; ++p) {
            fresh(p); run(0.2f);
            CHECK(progName() == kNames[p] && display(P_PRESET) == kNames[p], "preset %d selected (%s / %s)", p, progName().c_str(), display(P_PRESET).c_str());
            const float lv = pinkChange();
            if (std::fabs(lv) > std::fabs(worstLvl)) { worstLvl = lv; worstIdx = p; }
            std::vector<float> c(NCURVE); for (int i = 0; i < NCURVE; ++i) c[i] = curveDb(i);
            choice(P_VIEW, 1, 2); run(0.15f); for (int i = 0; i < NCURVE; i += 4) c.push_back(curveDb(i)); choice(P_VIEW, 0, 2);
            for (size_t o = 0; o < curves.size(); ++o) {
                float d = 0; for (size_t i = 0; i < c.size(); ++i) d = std::max(d, std::fabs(c[i] - curves[o][i]));
                CHECK(d > 0.2f, "presets %d and %zu are too similar", p, o);
            }
            curves.push_back(c);
            float mx = -99; for (float v : c) mx = std::max(mx, v);
            CHECK(p == 0 || mx < 9.f, "preset %d boosts %.1f dB", p, mx);
            CHECK(std::fabs(lv) <= 1.0f, "preset %d (%s) gain-staged: %+.2f dB on pink noise", p, kNames[p], lv);
        }
        std::printf("Presets: worst level change on pink noise %+.2f dB (%s)\n", worstLvl, kNames[worstIdx]);
        for (int p : { 31, 35, 37, 38 }) { fresh(p); CHECK(display(P_MODE) == "Mid/Side", "%s is Mid/Side", kNames[p]); }
    }

    // ---- prev / next with the category filter, favourites ----
    {
        fresh(0); choice(P_CATEGORY, 2, 8);                          // Vocals
        press(P_NEXT); CHECK(progName() == "Radio Lead Vocal", "next in Vocals -> %s", progName().c_str());
        press(P_PREV); CHECK(progName() == "Polished Vocal", "prev wraps inside Vocals -> %s", progName().c_str());
        press(P_NEXT); press(P_NEXT); CHECK(progName() == "Warm Male Vocal", "next again -> %s", progName().c_str());
        CHECK(getNorm(P_NEXT) == 0.f && lastAutomate[P_NEXT] == 0.f, "buttons spring back");
        setNorm(P_FAV, 1.f);                                          // favourite "Warm Male Vocal"
        choice(P_CATEGORY, 6, 8); press(P_NEXT); CHECK(progName() == "Car Speakers", "Translation -> %s", progName().c_str());
        CHECK(getNorm(P_FAV) == 0.f, "fav flag follows the preset");
        choice(P_CATEGORY, 7, 8); press(P_NEXT); CHECK(progName() == "Warm Male Vocal", "Favorites -> %s", progName().c_str());
        CHECK(getNorm(P_FAV) == 1.f, "fav flag on");
        FILE* f = std::fopen(favFile, "r"); char line[64] = {};
        CHECK(f && std::fgets(line, sizeof line, f) && std::strcmp(line, "Warm Male Vocal\n") == 0, "favourites saved (%s)", line);
        if (f) std::fclose(f);
        choice(P_CATEGORY, 0, 8);
        choice(P_PRESET, 40, NPROG); CHECK(progName() == kNames[40], "Preset parameter loads (%s)", progName().c_str());
    }

    // ---- A/B, undo / redo, reset ----
    {
        fresh(0); run(0.5f);
        band(4, T_BELL, 1000, 6, 1); run(0.6f);                      // A: +6
        choice(P_AB, 1, 2); run(0.1f);                               // B starts as a copy of A
        CHECK(std::fabs(getNorm(bp(4, B_GAIN)) * 36 - 18 - 6) < 0.05f, "B starts as a copy of A");
        band(4, T_BELL, 1000, -6, 1); run(0.6f);                     // B: -6
        CHECK(std::fabs(sineGain(1000) + 6) < 0.3f, "B sounds");
        choice(P_AB, 0, 2); run(0.3f);
        CHECK(std::fabs(sineGain(1000) - 6) < 0.3f, "back to A (%+.2f)", sineGain(1000));
        choice(P_AB, 1, 2); run(0.3f);
        CHECK(std::fabs(sineGain(1000) + 6) < 0.3f, "back to B");
        press(P_COPY); choice(P_AB, 0, 2); run(0.3f);
        CHECK(std::fabs(sineGain(1000) + 6) < 0.3f, "copy B -> A");
        choice(P_AB, 0, 2);

        band(4, T_BELL, 1000, 3, 1); run(0.6f);
        band(4, T_BELL, 1000, 9, 1); run(0.6f);
        press(P_UNDO); run(0.3f); CHECK(std::fabs(sineGain(1000) - 3) < 0.3f, "undo -> +3 (%+.2f)", sineGain(1000));
        press(P_UNDO); run(0.3f); CHECK(std::fabs(sineGain(1000) + 6) < 0.3f, "undo -> -6 (%+.2f)", sineGain(1000));
        press(P_REDO); run(0.3f); CHECK(std::fabs(sineGain(1000) - 3) < 0.3f, "redo -> +3");
        press(P_REDO); run(0.3f); CHECK(std::fabs(sineGain(1000) - 9) < 0.3f, "redo -> +9");
        D(effSetProgram, 0, 11); run(0.3f);
        press(P_UNDO); run(0.3f); CHECK(std::fabs(sineGain(1000) - 9) < 0.3f && progName() == "Init (Flat)", "undo a preset load (%s)", progName().c_str());
        press(P_REDO); CHECK(progName() == "Radio Lead Vocal", "redo a preset load");
        band(2, T_BELL, 500, 12, 1); pct(P_AMOUNT, 80); run(0.2f);
        press(P_RESET); run(0.3f);
        CHECK(std::fabs(getNorm(P_AMOUNT) * 100 - 30) < 0.5f && getNorm(bp(2, B_GAIN)) != 30.f / 36.f, "reset reloads the preset");
    }

    // ---- MPC restore order: setting every parameter in index order reproduces an edited state ----
    {
        fresh(5); band(3, T_BELL, 700, -4, 2); pct(P_PUNCH, 90); run(0.3f);
        std::vector<float> saved(P_COUNT); for (int i = 0; i < P_COUNT; ++i) saved[i] = getNorm(i);
        fresh(0);
        for (int i = 0; i < P_COUNT; ++i) setNorm(i, saved[i]);
        int diff = 0; for (int i = 0; i < P_CURVE0; ++i) diff += std::fabs(getNorm(i) - saved[i]) > 1e-6f;
        CHECK(diff == 0, "restore in index order reproduces the state (%d differ)", diff);
    }

    // ---- stepped controls (MPC Q-Links): exact positions come back ----
    {
        fresh(0); float worst = 0;
        for (int k = 0; k <= 100; ++k) { setNorm(bp(4, B_FREQ), k / 100.f); worst = std::max(worst, std::fabs(getNorm(bp(4, B_FREQ)) - k / 100.f)); }
        for (int k = 0; k <= 50; ++k) { setNorm(bp(4, B_GAIN), 0.3f + k / 1000.f); worst = std::max(worst, std::fabs(getNorm(bp(4, B_GAIN)) - (0.3f + k / 1000.f))); }
        CHECK(worst < 1e-6f, "normalised values round-trip (%.2g)", worst);
    }

    // ---- gains, auto gain, bypass ----
    {
        fresh(0); gainDb(P_IN, 6); run(0.2f); CHECK(std::fabs(sineGain(1000) - 6) < 0.05f, "input gain");
        gainDb(P_IN, 0); gainDb(P_OUT, -9); run(0.2f); CHECK(std::fabs(sineGain(1000) + 9) < 0.05f, "output gain");
        fresh(0); band(2, T_LSHELF, 200, 9); band(7, T_HSHELF, 5000, 9); run(0.3f);
        const float loud = pinkChange();
        setNorm(P_AUTOGAIN, 1.f); run(0.3f);
        const float matched = pinkChange();
        CHECK(loud > 4 && std::fabs(matched) < 1.f, "auto gain (%+.2f -> %+.2f dB)", loud, matched);
        std::vector<float> l(8000), r(8000), ol, orr; for (int i = 0; i < 8000; ++i) l[i] = r[i] = std::sin(i * 0.05f) * 0.3f;
        setNorm(P_BYPASS, 1.f); run(0.1f); proc(l, r, ol, orr);
        CHECK(ol == l && orr == r, "bypass is bit-exact once faded");
        setNorm(P_BYPASS, 0.f);
    }

    // ---- display: analyzer, meters ----
    {
        fresh(0); choice(P_ANALYZER, 1, 3);
        const int n = (int) SR; std::vector<float> l(n), r(n), ol, orr;
        for (int i = 0; i < n; ++i) l[i] = r[i] = 0.5f * std::sin(6.2831853f * 1000.f * i / SR);
        automateCalls = 0; proc(l, r, ol, orr);
        int top = 0; for (int i = 0; i < NSPEC; ++i) if (getNorm(P_SPEC0 + i) > getNorm(P_SPEC0 + top)) top = i;
        const float lo = 20.f * std::pow(1000.f, (float) top / NSPEC), hi = 20.f * std::pow(1000.f, (float) (top + 1) / NSPEC);
        CHECK(lo <= 1000 && hi >= 1000, "analyzer peak at 1 kHz (band %d %.0f-%.0f)", top, lo, hi);
        const float inL = getNorm(P_METER0) * 66 - 60, outL = getNorm(P_METER0 + 2) * 66 - 60;
        CHECK(std::fabs(inL + 6.02f) < 0.5f && std::fabs(outL + 6.02f) < 0.5f, "meters %.1f / %.1f dB", inL, outL);
        CHECK(automateCalls < 2500, "display traffic bounded (%d updates in 1 s)", automateCalls);
        automateCalls = 0; proc(l, r, ol, orr);
        CHECK(automateCalls < 300, "steady signal: few updates (%d)", automateCalls);
    }

    // ---- denormals ----
    {
        fresh(1); std::vector<float> l(40000, 0.f), r(40000, 0.f), ol, orr; l[0] = r[0] = 1e-30f; l[10] = 0.5f;
        proc(l, r, ol, orr);
        bool ok = true; for (int i = 30000; i < 40000; ++i) ok &= std::fabs(ol[i]) < 1e-12f && std::fpclassify(ol[i]) != FP_SUBNORMAL && std::fpclassify(orr[i]) != FP_SUBNORMAL;
        CHECK(ok, "decays away, no denormals");
    }

    // ---- sample rates and HQ ----
    for (float rate : { 44100.f, 48000.f, 88200.f, 96000.f, 176400.f, 192000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, rate); fresh(0);
        band(4, T_BELL, 1000, 6, 1); band(7, T_HSHELF, 10000, -6, 0.707f); run(0.3f);
        const float a = sineGain(1000), b = sineGain(18000);
        choice(P_HQ, 1, 2); run(0.3f);
        const float c = sineGain(1000), d = sineGain(18000);
        CHECK(std::fabs(a - 6) < 0.2f && std::fabs(c - 6) < 0.2f, "%.0f Hz: bell +6 (%+.2f / HQ %+.2f)", rate, a, c);
        CHECK(std::fabs(b + 6) < 1.0f && std::fabs(d + 6) < 1.0f, "%.0f Hz: shelf (%+.2f / HQ %+.2f)", rate, b, d);
        CHECK(fx->initialDelay == 31, "HQ latency reported (%d)", fx->initialDelay);
        choice(P_HQ, 0, 2); run(0.1f);
        CHECK(fx->initialDelay == 0, "zero latency again");
    }
    CHECK(ioChanged >= 2, "host told about latency changes");
    SR = 44100.f; D(effSetSampleRate, 0, 0, nullptr, SR);

    // ---- HQ: latency is exactly what's reported ----
    {
        fresh(0); choice(P_HQ, 1, 2); run(0.2f);
        std::vector<float> l(4000, 0.f), r(4000, 0.f), ol, orr; l[1000] = r[1000] = 1.f;
        proc(l, r, ol, orr);
        int at = 0; for (int i = 0; i < 4000; ++i) if (std::fabs(ol[i]) > std::fabs(ol[at])) at = i;
        CHECK(at == 1000 + fx->initialDelay && std::fabs(ol[at] - 1.f) < 0.1f, "HQ impulse at +%d (%.3f)", at - 1000, ol[at]);
        choice(P_HQ, 0, 2);
    }

    D(effClose);
    std::remove(favFile);
    if (fails == 0) std::printf("ALL TESTS PASSED\n"); else std::printf("%d FAILURES\n", fails);
    return fails ? 1 : 0;
}
