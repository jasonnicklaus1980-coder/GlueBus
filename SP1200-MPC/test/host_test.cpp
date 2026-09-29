// Offline VST2 host test for SP1200: loads the .so, exercises the dispatcher, parameters, presets and audio,
// and checks the hardware behaviour: 26.04 kHz clock, 12-bit steps, drop-sample tuning, output filters, decay.
#include "vst2.h"
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum { P_INPUT, P_TUNE, P_DECAY, P_CHANNEL, P_SWEEP, P_FLOOR, P_MIX, P_VOLUME, P_MODE, P_BYPASS, P_COUNT };

static int automateCalls = 0, displayCalls = 0;
static intptr_t host(AEffect*, int32_t op, int32_t, intptr_t, void*, float) {
    if (op == 0) ++automateCalls;
    if (op == 42) ++displayCalls;
    return 2400;
}
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static float getNorm(int i) { return fx->getParameter(fx, i); }
static void tune(int st) { setNorm(P_TUNE, (st + 12) / 19.f); }
static void channel(int c) { setNorm(P_CHANNEL, c / 3.f); }
static void mode(int m) { setNorm(P_MODE, (float) m); }

static void makeSine(std::vector<float>& l, std::vector<float>& r, float peak, float hz, int n) {
    l.resize(n); r.resize(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = peak * std::sin(6.2831853f * hz * i / SR);
}
static float peakOf(const std::vector<float>& v, int from, int to = -1) {
    if (to < 0) to = (int) v.size();
    float m = 0; for (int i = from; i < to; ++i) m = std::fmax(m, std::fabs(v[i])); return m;
}
static float rmsOf(const std::vector<float>& v, int from, int to) {
    double s = 0; for (int i = from; i < to; ++i) s += (double) v[i] * v[i]; return (float) std::sqrt(s / (to - from));
}
static float db(float x) { return 20.f * std::log10(x + 1e-12f); }
static void proc(std::vector<float>& il, std::vector<float>& ir, std::vector<float>& ol, std::vector<float>& orr, int block = 512) {
    ol.assign(il.size(), 0); orr.assign(il.size(), 0);
    for (size_t pos = 0; pos < il.size(); pos += block) {
        int m = (int) std::fmin((double) block, (double)(il.size() - pos));
        float* in[2] = { &il[pos], &ir[pos] }; float* out[2] = { &ol[pos], &orr[pos] };
        fx->processReplacing(fx, in, out, m);
    }
}
static void fresh(int preset = 1) { D(effSetProgram, 0, preset); D(effMainsChanged, 0, 1); }   // Clean Out 7-8
static int changesPerSecond(const std::vector<float>& o, int from, int to) {
    int c = 0; for (int i = from + 1; i < to; ++i) if (o[i] != o[i - 1]) ++c;
    return (int) std::lround(c * SR / (to - from));
}
// frequency from positive-going zero crossings (linear interpolation between samples)
static float freqOf(const std::vector<float>& v, int from, int to) {
    double first = -1, last = -1; int n = 0;
    for (int i = from + 1; i < to; ++i)
        if (v[i - 1] < 0 && v[i] >= 0) { double t = i - 1 + v[i - 1] / (v[i - 1] - v[i]); if (first < 0) first = t; last = t; ++n; }
    return n > 1 ? (float) ((n - 1) * SR / (last - first)) : 0.f;
}

int main(int argc, char** argv) {
    void* h = dlopen(argc > 1 ? argv[1] : "build/native/sp1200.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    if (!entry) { std::printf("no VSTPluginMain\n"); return 2; }
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic, "magic");
    CHECK(fx->numInputs == 2 && fx->numOutputs == 2, "io");
    CHECK(fx->flags & effFlagsCanReplacing, "replacing flag");
    CHECK(fx->uniqueID == 0x53503132, "uid %08x", fx->uniqueID);
    CHECK(fx->numParams == P_COUNT && fx->numPrograms == 13, "counts %d/%d", fx->numParams, fx->numPrograms);
    CHECK(D(effGetPlugCategory) == kPlugCategEffect, "category");
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effSetBlockSize, 0, 512); D(effMainsChanged, 0, 1);

    std::printf("Parameters:\n");
    for (int i = 0; i < fx->numParams; ++i) {
        char n[64] = {}, d[64] = {}, u[64] = {};
        D(effGetParamName, i, 0, n); D(effGetParamDisplay, i, 0, d); D(effGetParamLabel, i, 0, u);
        std::printf("  %2d %-10s %-12s %s\n", i, n, d, u);
        CHECK(std::strlen(n) > 0, "param %d name", i);
        setNorm(i, 0.37f); float a = getNorm(i); setNorm(i, a); CHECK(std::fabs(getNorm(i) - a) < 1e-6f, "param %d roundtrip", i);
    }
    std::printf("Programs:\n");
    for (int i = 0; i < fx->numPrograms; ++i) { char n[64] = {}; D(effGetProgramNameIndexed, i, 0, n); std::printf("  %2d %s\n", i, n); CHECK(n[0], "preset name"); }
    automateCalls = displayCalls = 0; D(effSetProgram, 0, 5);
    CHECK(automateCalls == P_COUNT && displayCalls == 1, "preset change notifies host (%d automate, %d display)", automateCalls, displayCalls);
    CHECK(D(effGetProgram) == 5, "program 5");
    { char d[64] = {}; D(effGetParamDisplay, P_TUNE, 0, d); CHECK(!std::strcmp(d, "-5"), "45>33 preset tune display '%s'", d); }

    // tuning slider: every semitone step from -12 to +7 is reachable and displayed
    for (int st = -12; st <= 7; ++st) {
        tune(st); char d[64] = {}, want[16]; D(effGetParamDisplay, P_TUNE, 0, d);
        if (st == 0) std::snprintf(want, sizeof want, "0"); else std::snprintf(want, sizeof want, "%+d", st);
        CHECK(!std::strcmp(d, want), "tune %d displays '%s'", st, d);
    }

    const int N = (int) SR;
    std::vector<float> l, r, ol, orr;

    // 1) DAC clock: on the unfiltered output the staircase steps at 26.04 kHz regardless of host rate
    for (float rate : { 44100.f, 48000.f, 96000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, SR); fresh();
        const int n = (int) SR; l.resize(n); r.resize(n); srand(1);
        for (int i = 0; i < n; ++i) l[i] = r[i] = 0.5f * ((float) rand() / RAND_MAX - 0.5f);
        proc(l, r, ol, orr);
        int cps = changesPerSecond(ol, n / 10, n);
        std::printf("\nDAC steps/s at host %.0f Hz: %d (expect 26040)", SR, cps);
        CHECK(std::abs(cps - 26040) < 150, "DAC clock %d at %.0f", cps, SR);
        // grit mode, tune -12: the sampler runs at half rate (13.02 kHz), the DAC repeats each sample
        tune(-12); mode(0); D(effMainsChanged, 0, 1); proc(l, r, ol, orr);
        cps = changesPerSecond(ol, n / 10, n);
        std::printf("\n  grit -12: %d steps/s (expect 13020)", cps);
        CHECK(std::abs(cps - 13020) < 150, "grit -12 sample rate %d", cps);
    }
    std::printf("\n");
    SR = 44100.f; D(effSetSampleRate, 0, 0, nullptr, SR);

    // 2) 12-bit resolution: a -80 dBFS sine (below 1 LSB = -66 dBFS) collapses to at most two codes
    fresh(); makeSine(l, r, 1e-4f, 440.f, N); proc(l, r, ol, orr);
    {
        std::vector<float> codes;
        for (int i = N / 10; i < N; ++i) { bool seen = false; for (float c : codes) if (c == ol[i]) seen = true; if (!seen) codes.push_back(ol[i]); }
        std::printf("-80 dBFS sine -> %zu distinct output levels\n", codes.size());
        CHECK(codes.size() <= 2, "12-bit quantisation (%zu levels)", codes.size());
        // -40 dBFS sine: ~20 LSB peak -> a staircase with ~41 codes, never finer than 1/2048
        makeSine(l, r, 0.01f, 440.f, N); D(effMainsChanged, 0, 1); proc(l, r, ol, orr);
        codes.clear();
        for (int i = N / 10; i < N; ++i) { bool seen = false; for (float c : codes) if (c == ol[i]) seen = true; if (!seen) codes.push_back(ol[i]); }
        std::printf("-40 dBFS sine -> %zu distinct output levels\n", codes.size());
        CHECK(codes.size() >= 38 && codes.size() <= 44, "12-bit staircase (%zu levels)", codes.size());
    }

    // 3) Unity: tune 0 on Out 7-8 passes a 1 kHz sine at unity (+-0.3 dB), same pitch
    fresh(); makeSine(l, r, 0.25f, 1000.f, N); proc(l, r, ol, orr);
    float g = db(rmsOf(ol, N / 2, N)) - db(rmsOf(l, N / 2, N)), f0 = freqOf(ol, N / 2, N);
    std::printf("Out 7-8, tune 0: gain %.2f dB, freq %.1f Hz\n", g, f0);
    CHECK(std::fabs(g) < 0.3f && std::fabs(f0 - 1000.f) < 2.f, "unity path");

    // 4) Grit (45>33) mode keeps pitch at every tune step
    for (int st : { -12, -5, 7 }) {
        fresh(); tune(st); mode(0); makeSine(l, r, 0.25f, 440.f, N); proc(l, r, ol, orr);
        float f = freqOf(ol, N / 2, N);
        std::printf("grit tune %+d: %.1f Hz\n", st, f);
        CHECK(std::fabs(f - 440.f) < 3.f, "grit mode keeps pitch at %+d (%.1f)", st, f);
    }

    // 5) Pitch mode: equal-tempered tuning accuracy (drop-sample playback at 26.04 kHz)
    for (int st : { -12, -8, -4, -1, 1, 4, 7 }) {
        fresh(); tune(st); mode(1); makeSine(l, r, 0.25f, 440.f, N); proc(l, r, ol, orr);
        const float want = 440.f * std::pow(2.f, st / 12.f);
        // perceived pitch: median of 20 ms zero-crossing estimates (a splice between the two read heads only
        // disturbs the few segments that straddle it)
        std::vector<float> est;
        for (int p = N / 4; p + (int) (0.02f * SR) < N; p += (int) (0.005f * SR)) est.push_back(freqOf(ol, p, p + (int) (0.02f * SR)));
        std::sort(est.begin(), est.end());
        float f = est[est.size() / 2];
        const float cents = 1200.f * std::log2(f / want);
        std::printf("pitch tune %+d: %.1f Hz (want %.1f, %+.1f cents)\n", st, f, want, cents);
        CHECK(std::fabs(cents) < 6.f, "tuning %+d off by %.1f cents", st, cents);
    }

    // 6) Fixed output filters: Out 3-4 (~7.5 kHz) darker than Out 5-6 (~10 kHz) darker than Out 7-8
    auto levelAt = [&](int ch, float hz) {
        fresh(); channel(ch); makeSine(l, r, 0.25f, hz, N); proc(l, r, ol, orr);
        return db(rmsOf(ol, N / 2, N)) - db(rmsOf(l, N / 2, N));
    };
    float o34 = levelAt(1, 10000.f), o56 = levelAt(2, 10000.f), o78 = levelAt(3, 10000.f);
    float o34lo = levelAt(1, 500.f), o34c = levelAt(1, 7500.f), o56c = levelAt(2, 10000.f);
    std::printf("10 kHz: Out 3-4 %.1f dB, Out 5-6 %.1f dB, Out 7-8 %.1f dB; Out 3-4 at 500 Hz %.2f dB\n", o34, o56, o78, o34lo);
    std::printf("at cutoff: Out 3-4 @7.5k %.1f dB, Out 5-6 @10k %.1f dB\n", o34c, o56c);
    CHECK(o34 < o56 - 3.f && o56 < o78 - 1.f, "output filter ordering");
    CHECK(std::fabs(o34lo) < 0.5f, "Out 3-4 passband");
    CHECK(o34c < -1.f && o34c > -9.f && o56c < -1.f && o56c > -9.f, "cutoffs near 7.5 / 10 kHz");

    // 7) Out 1-2 dynamic SSM2044 filter: opens on the hit, closes to the floor trim
    fresh(); channel(0); l.assign(N, 0.f); r.assign(N, 0.f);
    for (int i = N / 4; i < N; ++i) l[i] = r[i] = 0.5f * std::sin(6.2831853f * 4000.f * i / SR);
    proc(l, r, ol, orr);
    float early = rmsOf(ol, N / 4, N / 4 + 44), late = rmsOf(ol, N / 2, N);
    std::printf("Out 1-2 dyn, 4 kHz burst: first 1 ms %.1f dB, settled %.1f dB\n", db(early), db(late));
    CHECK(db(early) - db(late) > 15.f, "dynamic filter sweep");

    // 8) Decay slider: a held tone dies away after a hit; at the top (Off) it sustains
    fresh(); l.assign(N, 0.f); r.assign(N, 0.f);
    for (int i = N / 10; i < N; ++i) l[i] = r[i] = 0.5f * std::sin(6.2831853f * 500.f * i / SR);
    setNorm(P_DECAY, std::log(0.1f / 0.02f) / std::log(4.f / 0.02f));   // 100 ms to -60 dB
    { char d[64] = {}; D(effGetParamDisplay, P_DECAY, 0, d); CHECK(!std::strcmp(d, "100 ms"), "decay display '%s'", d); }
    proc(l, r, ol, orr);
    float dStart = peakOf(ol, N / 10, N / 10 + 441), dLate = peakOf(ol, N / 10 + (int) (0.3f * SR), N);
    std::printf("decay 100 ms: start %.1f dB, after 300 ms %.1f dB\n", db(dStart), db(dLate));
    CHECK(db(dStart) > -8.f && db(dLate) < -60.f, "decay envelope");
    fresh(); proc(l, r, ol, orr);
    CHECK(db(peakOf(ol, N - 4410, N)) > -7.f, "decay off sustains");

    // 9) Input slider drives the ADC into hard clipping at 12-bit full scale
    fresh(); setNorm(P_INPUT, 1.f); makeSine(l, r, 0.9f, 200.f, N); proc(l, r, ol, orr);
    std::printf("input +12 dB into 0.9 sine: output peak %.3f\n", peakOf(ol, N / 2));
    CHECK(peakOf(ol, N / 2) < 1.f, "ADC full-scale clip");

    // 10) Mix 0 is bit-exact dry; bypass is bit-exact dry
    fresh(); setNorm(P_MIX, 0.f); makeSine(l, r, 0.7f, 1000.f, N); proc(l, r, ol, orr);
    bool exact = true; for (int i = 0; i < N; ++i) if (ol[i] != l[i]) { exact = false; break; }
    CHECK(exact, "mix 0 bit-exact");
    fresh(); setNorm(P_BYPASS, 1.f); proc(l, r, ol, orr);
    exact = true; for (int i = 0; i < N; ++i) if (ol[i] != l[i]) { exact = false; break; }
    CHECK(exact, "bypass bit-exact");
    setNorm(P_BYPASS, 0.f);

    // 11) Every preset: finite, bounded output on loud material; in-place processing works; stereo kept apart
    for (int p = 0; p < fx->numPrograms; ++p) {
        D(effSetProgram, 0, p); D(effMainsChanged, 0, 1);
        makeSine(l, r, 0.95f, 80.f, N); for (int i = 0; i < N; ++i) r[i] = 0.9f * std::sin(6.2831853f * 3000.f * i / SR);
        std::vector<float> al = l, ar = r;
        for (int pos = 0; pos < N; pos += 256) { float* io[2] = { al.data() + pos, ar.data() + pos }; int m = (N - pos) < 256 ? (N - pos) : 256; fx->processReplacing(fx, io, io, m); }
        bool ok = true; float mx = 0; for (int i = 0; i < N; ++i) { if (!std::isfinite(al[i]) || !std::isfinite(ar[i])) ok = false; mx = std::fmax(mx, std::fmax(std::fabs(al[i]), std::fabs(ar[i]))); }
        char nm[64] = {}; D(effGetProgramNameIndexed, p, 0, nm);
        std::printf("preset %-16s finite=%d peak=%.2f\n", nm, ok, mx);
        CHECK(ok && mx < 4.f, "preset %d output sane", p);
    }

    // 12) Silence / denormal-range input stays finite
    fresh(); channel(0); l.assign(N, 1e-30f); r.assign(N, 0.f); proc(l, r, ol, orr);
    bool fin = true; for (int i = 0; i < N; ++i) if (!std::isfinite(ol[i])) fin = false;
    CHECK(fin, "denormal input finite");

    // 13) Legacy accumulating process() adds to the output buffer
    fresh(); setNorm(P_MIX, 0.f); l.assign(512, 0.25f); r = l; std::vector<float> a(512, 1.f), b(512, 1.f);
    float* in[2] = { l.data(), r.data() }; float* out[2] = { a.data(), b.data() };
    fx->process(fx, in, out, 512); CHECK(std::fabs(a[100] - 1.25f) < 0.01f, "accumulating process %.3f", a[100]);

    char s[64] = {}; D(effGetEffectName, 0, 0, s); CHECK(!std::strcmp(s, "SP1200"), "effect name");
    D(effClose);
    std::printf("\n%s (%d failure%s)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
