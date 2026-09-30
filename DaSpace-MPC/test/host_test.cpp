// Offline VST2 host test for Da Space. Measures the reverbs like an acoustician would: impulse responses, RT60 from
// the Schroeder backward-integrated energy decay (T20), pre-delay onset, damping (HF decays faster), low cut,
// shimmer (energy an octave up), freeze, ducking, width / decorrelation, stability of every algorithm and preset at
// extreme settings, denormals, sample rates, mix 0 = bit-exact dry.
#include "vst2.h"
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum { P_PRESET, P_PREV, P_NEXT, P_ALGO, P_MIX, P_PRE, P_SIZE, P_DECAY, P_DAMP, P_LOWCUT, P_DIFF, P_MOD, P_WIDTH,
       P_SHIMMER, P_COLOR, P_FREEZE, P_DUCK, P_OUT, P_INMETER, P_WETMETER, P_INFO, P_COUNT };
static const char* kAlgo[] = { "Room", "Hall", "Plate", "Classic", "Ice", "Meta", "Reflex" };

static intptr_t host(AEffect*, int32_t, int32_t, intptr_t, void*, float) { return 2400; }
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static std::string display(int i) { char d[128] = {}; D(effGetParamDisplay, i, 0, d); return d; }
static void pct(int i, float p, float hi = 100.f) { setNorm(i, p / hi); }
static void logp(int i, float v, float lo, float hi) { setNorm(i, std::log(v / lo) / std::log(hi / lo)); }
static void algo(int a) { setNorm(P_ALGO, a / 6.f); }

static std::vector<float> L, R;
static void process(std::vector<float>& l, std::vector<float>& r) {
    for (size_t pos = 0; pos < l.size(); pos += 256) {
        const int m = (int) std::min<size_t>(256, l.size() - pos);
        float* io[2] = { &l[pos], &r[pos] };
        fx->processReplacing(fx, io, io, m);                    // in place, like MPC
    }
}
static void silence(float sec) { std::vector<float> l((size_t) (sec * SR), 0.f), r = l; process(l, r); L = l; R = r; }
static void impulse(float sec) { std::vector<float> l((size_t) (sec * SR), 0.f), r = l; l[0] = r[0] = 1.f; process(l, r); L = l; R = r; }
static void noise(float sec, float amp = 0.3f) {
    std::vector<float> l((size_t) (sec * SR)), r(l.size()); unsigned s = 11;
    for (size_t i = 0; i < l.size(); ++i) { s = s * 1664525u + 1013904223u; l[i] = amp * ((float) (s >> 9) / 4194304.f - 1.f); s = s * 1664525u + 1013904223u; r[i] = amp * ((float) (s >> 9) / 4194304.f - 1.f); }
    process(l, r); L = l; R = r;
}
static void sine(float sec, float hz, float amp) {
    std::vector<float> l((size_t) (sec * SR)); for (size_t i = 0; i < l.size(); ++i) l[i] = amp * std::sin(2 * M_PI * hz * i / SR);
    std::vector<float> r = l; process(l, r); L = l; R = r;
}
static double energy(const std::vector<float>& v, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b && i < v.size(); ++i) e += (double) v[i] * v[i]; return e; }
static double db(double x) { return 10 * std::log10(x + 1e-30); }
// RT60 via Schroeder backward integration, T20 (-5 .. -25 dB) x 3
static double rt60() {
    const size_t n = L.size(); std::vector<double> edc(n + 1, 0.0);
    for (size_t i = n; i-- > 0;) edc[i] = edc[i + 1] + (double) L[i] * L[i] + (double) R[i] * R[i];
    double t5 = -1, t25 = -1;
    for (size_t i = 0; i < n; ++i) {
        const double d = db(edc[i] / edc[0]);
        if (t5 < 0 && d <= -5) t5 = i / SR;
        if (t25 < 0 && d <= -25) { t25 = i / SR; break; }
    }
    return (t5 < 0 || t25 < 0) ? -1 : 3 * (t25 - t5);
}
static double goertzel(const std::vector<float>& v, size_t a, size_t b, double hz) {
    const double w = 2 * M_PI * hz / SR, c = 2 * std::cos(w); double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double s0 = v[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static void clean(int a, float decay) {                     // measurement setup: 100 % wet, flat, no modulation
    algo(a); pct(P_MIX, 100); pct(P_PRE, 0, 250); pct(P_SIZE, 60); logp(P_DECAY, decay, 0.2f, 20); logp(P_DAMP, 20000, 1000, 20000);
    logp(P_LOWCUT, 20, 20, 1000); pct(P_DIFF, 75); pct(P_MOD, 0); pct(P_WIDTH, 100, 150); pct(P_SHIMMER, 0); setNorm(P_COLOR, 0.5f);
    setNorm(P_FREEZE, 0); pct(P_DUCK, 0); setNorm(P_OUT, 24.f / 36.f);
    silence(0.1f); D(effMainsChanged, 0, 1); silence(0.05f);
}

int main(int argc, char** argv) {
    void* h = dlopen(argc > 1 ? argv[1] : "build/native/daspace.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic && fx->uniqueID == 0x44535043, "plugin");
    CHECK(fx->numParams == P_COUNT && fx->numPrograms == 34, "counts %d / %d", fx->numParams, fx->numPrograms);
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effMainsChanged, 0, 1);
    for (int i = 0; i < P_COUNT; ++i) { char n[64] = {}; D(effGetParamName, i, 0, n); CHECK(std::strlen(n) > 0 && !display(i).empty(), "param %d", i); }
    bool highSky = false;
    for (int i = 0; i < fx->numPrograms; ++i) { char n[64] = {}; D(effGetProgramNameIndexed, i, 0, n); if (std::string(n) == "High Sky") highSky = true; }
    CHECK(highSky, "High Sky preset");

    // ---- mix 0: dry passes bit-exact ----
    {
        clean(1, 2); pct(P_MIX, 0); silence(0.2f);
        std::vector<float> l(20000), r(20000); unsigned s = 3;
        for (int i = 0; i < 20000; ++i) { s = s * 1664525u + 1013904223u; l[i] = (float) ((int) (s >> 9) - (1 << 22)) / (1 << 22); r[i] = 0.5f * l[i]; }
        std::vector<float> l0 = l, r0 = r; process(l, r);
        CHECK(l == l0 && r == r0, "mix 0 %% is bit-exact dry");
    }

    // ---- RT60 of every algorithm follows the Decay control ----
    std::printf("RT60 (Decay 2.0 s / 6.0 s):\n");
    for (int a = 0; a < 7; ++a) {
        clean(a, 2.0f); impulse(8.f); const double t2 = rt60();
        clean(a, 6.0f); impulse(20.f); const double t6 = rt60();
        std::printf("  %-8s %.2f s   %.2f s\n", kAlgo[a], t2, t6);
        if (a == 6) { CHECK(t2 > 0.1 && t2 < 0.8, "Reflex is short (%.2f s)", t2); continue; }       // Reflex: early reflections, short tail
        CHECK(t2 > 1.4 && t2 < 2.7, "%s RT60 %.2f s for 2.0 s", kAlgo[a], t2);
        CHECK(t6 > 4.2 && t6 < 8.0, "%s RT60 %.2f s for 6.0 s", kAlgo[a], t6);
    }

    // ---- pre-delay: nothing before it, reverb right after ----
    {
        clean(1, 2); pct(P_PRE, 100, 250); impulse(0.4f);
        size_t first = 0; for (size_t i = 0; i < L.size(); ++i) if (std::fabs(L[i]) > 1e-4f || std::fabs(R[i]) > 1e-4f) { first = i; break; }
        CHECK(first >= (size_t) (0.099 * SR) && first < (size_t) (0.140 * SR), "pre-delay 100 ms: first reverb at %.1f ms", first * 1000.0 / SR);
    }
    // ---- damping: the highs die first ----
    {
        auto hfRatio = [](float damp) { clean(1, 3); logp(P_DAMP, damp, 1000, 20000); impulse(1.5f);
            const size_t a = (size_t) (0.8 * SR), b = (size_t) (1.2 * SR);
            return db(goertzel(L, a, b, 6000) / goertzel(L, a, b, 500)); };
        const double open = hfRatio(20000), damped = hfRatio(2500);
        CHECK(open - damped > 12, "damping 2.5 kHz: 6 kHz vs 500 Hz in the tail %.1f dB lower", open - damped);
    }
    // ---- low cut keeps the bass out of the reverb ----
    {
        clean(1, 2); sine(2.f, 60, 0.3f); const double full = energy(L, (size_t) SR, 2 * (size_t) SR);
        clean(1, 2); logp(P_LOWCUT, 800, 20, 1000); sine(2.f, 60, 0.3f); const double cut = energy(L, (size_t) SR, 2 * (size_t) SR);
        CHECK(db(full) - db(cut) > 20, "low cut 800 Hz removes 60 Hz from the reverb (%.1f dB)", db(full) - db(cut));
    }
    // ---- shimmer puts energy an octave up ----
    for (int a : { 4, 5, 1 }) {
        clean(a, 4); sine(1.f, 440, 0.2f); silence(2.f); const double plain = goertzel(L, 0, L.size(), 880) / goertzel(L, 0, L.size(), 440);
        clean(a, 4); pct(P_SHIMMER, 80); sine(1.f, 440, 0.2f); silence(2.f); const double shim = goertzel(L, 0, L.size(), 880) / goertzel(L, 0, L.size(), 440);
        CHECK(db(shim) - db(plain) > 10, "%s shimmer: octave up +%.1f dB", kAlgo[a], db(shim) - db(plain));
    }
    // ---- freeze holds the tail ----
    for (int a : { 1, 2, 3, 5 }) {
        clean(a, 1.5f); noise(1.f); setNorm(P_FREEZE, 1); silence(1.f); const double early = energy(L, (size_t) (0.5 * SR), L.size());
        silence(4.f); const double late = energy(L, (size_t) (3.5 * SR), L.size());
        CHECK(std::fabs(db(late) - db(early)) < 3, "%s freeze holds (%.1f dB change)", kAlgo[a], db(late) - db(early));
        noise(0.5f, 0.3f); silence(0.5f); const double after = energy(L, 0, L.size());
        CHECK(std::fabs(db(after / (0.5 * SR)) - db(early / (0.5 * SR))) < 3, "%s frozen: new input doesn't pile in", kAlgo[a]);
        setNorm(P_FREEZE, 0); silence(8.f); CHECK(energy(L, (size_t) (7 * SR), L.size()) < 1e-6, "%s unfreeze: tail dies", kAlgo[a]);
    }
    // ---- ducking: the reverb dips while the dry signal plays ----
    {
        clean(1, 3); sine(2.f, 300, 0.3f); const double open = energy(L, (size_t) SR, 2 * (size_t) SR);
        clean(1, 3); pct(P_DUCK, 100); sine(2.f, 300, 0.3f); const double ducked = energy(L, (size_t) SR, 2 * (size_t) SR);
        CHECK(db(open) - db(ducked) > 10, "ducking 100 %%: %.1f dB under the dry signal", db(open) - db(ducked));
    }
    // ---- width: 0 = mono, 100 % = decorrelated tail ----
    {
        clean(1, 2); pct(P_WIDTH, 0, 150); impulse(1.f); bool mono = true; for (size_t i = 0; i < L.size(); ++i) mono &= std::fabs(L[i] - R[i]) < 1e-6f;
        CHECK(mono, "width 0 = mono reverb");
        clean(1, 2); impulse(1.f);
        double lr = 0, ll = 0, rr = 0; for (size_t i = (size_t) (0.1 * SR); i < L.size(); ++i) { lr += L[i] * R[i]; ll += L[i] * L[i]; rr += R[i] * R[i]; }
        const double corr = lr / std::sqrt(ll * rr);
        CHECK(std::fabs(corr) < 0.5, "stereo tail correlation %.2f", corr);
    }
    // ---- stability: every algorithm flat out (size, decay, modulation, shimmer at max) ----
    for (int a = 0; a < 7; ++a) {
        clean(a, 20); pct(P_SIZE, 100); pct(P_MOD, 100); pct(P_SHIMMER, 100); pct(P_DIFF, 100);
        noise(1.f, 0.5f); silence(12.f);
        float pk = 0; bool finite = true; for (size_t i = 0; i < L.size(); ++i) { pk = std::max(pk, std::fabs(L[i])); finite &= std::isfinite(L[i]); }
        const double early = energy(L, (size_t) SR, 3 * (size_t) SR), late = energy(L, 10 * (size_t) SR, 12 * (size_t) SR);
        CHECK(finite && pk < 12.f && late < early, "%s at extremes stays stable (peak %.2f, tail %+.1f dB over 9 s)", kAlgo[a], pk, db(late) - db(early));
    }
    // ---- every preset: stable, and it makes reverb ----
    for (int i = 0; i < fx->numPrograms; ++i) {
        D(effSetProgram, 0, i); D(effMainsChanged, 0, 1); noise(0.5f, 0.3f); silence(3.f);
        char n[64] = {}; D(effGetProgramName, 0, 0, n);
        float pk = 0; bool finite = true; for (float x : L) { pk = std::max(pk, std::fabs(x)); finite &= std::isfinite(x); }
        CHECK(finite && pk < 2.f && pk > 1e-4f, "preset %s: tail peak %.3f", n, pk);
    }
    D(effSetProgram, 0, 28);
    { char n[64] = {}; D(effGetProgramName, 0, 0, n);
      CHECK(std::string(n) == "High Sky" && display(P_ALGO) == "Meta" && display(P_PRE) == "80 ms" && display(P_SHIMMER) == "35 %" && display(P_LOWCUT) == "400 Hz",
            "High Sky settings (%s: %s, %s, %s, %s)", n, display(P_ALGO).c_str(), display(P_PRE).c_str(), display(P_SHIMMER).c_str(), display(P_LOWCUT).c_str()); }
    // ---- algorithm switch: no bang ----
    {
        clean(1, 3); noise(1.f, 0.3f);
        float natural = 0; for (size_t i = 1; i < L.size(); ++i) natural = std::max(natural, std::fabs(L[i] - L[i - 1]));   // the tail's own steps
        std::vector<float> l((size_t) SR, 0.f), r = l; float prev = L.back(), jump = 0;
        algo(2); process(l, r); for (float x : l) { jump = std::max(jump, std::fabs(x - prev)); prev = x; }
        CHECK(jump <= natural, "switching algorithm mid-tail: biggest step %.3f (tail itself %.3f)", jump, natural);
    }
    // ---- denormals ----
    { clean(5, 20); impulse(0.1f); silence(30.f); bool ok = true; for (float x : L) ok &= std::fpclassify(x) != FP_SUBNORMAL; CHECK(ok, "no denormals in a 20 s tail"); }
    // ---- sample rates ----
    for (float rate : { 48000.f, 96000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, rate); clean(1, 2); impulse(8.f);
        const double t = rt60(); CHECK(t > 1.4 && t < 2.7, "%.0f Hz: Hall RT60 %.2f s", rate, t);
    }
    D(effClose);
    if (fails == 0) std::printf("ALL TESTS PASSED\n"); else std::printf("%d FAILURES\n", fails);
    return fails ? 1 : 0;
}
