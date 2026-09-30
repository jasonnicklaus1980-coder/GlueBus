// Offline VST2 host test for EQ7: dispatcher, parameters, presets, every filter type / slope measured against its
// expected response, the curve display (read-only parameters) against the measured response, glide without clicks,
// and the MPC stepped-control fix.
#include "vst2.h"
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum { B_ON, B_TYPE, B_FREQ, B_GAIN, B_Q, B_SLOPE };
enum { T_PEAK, T_LSHELF, T_HSHELF, T_LCUT, T_HCUT, T_NOTCH, T_BPASS };
static const int P_OUTPUT = 42, P_BYPASS = 43, P_ANALYZER = 44, P_CURVE0 = 45, NCURVE = 64, P_SPEC0 = 109, NSPEC = 32, P_COUNT = 141;
static int bp(int band, int what) { return (band - 1) * 6 + what; }        // bands numbered 1..7 here

static int automateCalls = 0, displayCalls = 0, curveCalls = 0, specCalls = 0; static float lastAutomate[160];
static intptr_t host(AEffect*, int32_t op, int32_t idx, intptr_t, void*, float opt) {
    if (op == 0) { ++automateCalls; if (idx >= 0 && idx < 160) lastAutomate[idx] = opt;
                   if (idx >= P_CURVE0 && idx < P_SPEC0) ++curveCalls; if (idx >= P_SPEC0) ++specCalls; }
    if (op == 42) ++displayCalls;
    return 2400;
}
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static float getNorm(int i) { return fx->getParameter(fx, i); }
static void type(int b, int t) { setNorm(bp(b, B_TYPE), t / 6.f); setNorm(bp(b, B_ON), 1.f); }
static void slope(int b, int s) { setNorm(bp(b, B_SLOPE), s / 2.f); }
static void freq(int b, float hz) { setNorm(bp(b, B_FREQ), std::log(hz / 20.f) / std::log(1000.f)); }
static void gain(int b, float db) { setNorm(bp(b, B_GAIN), (db + 18.f) / 36.f); }
static void q(int b, float qq) { setNorm(bp(b, B_Q), std::log(qq / 0.1f) / std::log(180.f)); }
static float curveDb(int i) { return getNorm(P_CURVE0 + i) * 36.f - 18.f; }
static float curveHz(int i) { return 20.f * std::pow(1000.f, (float) i / (NCURVE - 1)); }

static void makeSine(std::vector<float>& l, std::vector<float>& r, float peak, float hz, int n) {
    l.resize(n); r.resize(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = peak * std::sin(6.2831853f * hz * i / SR);
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
static void allOff() { for (int b = 1; b <= 7; ++b) setNorm(bp(b, B_ON), 0.f); }
static float specDb(int i) { return getNorm(P_SPEC0 + i) * 84.f - 84.f; }
static float specLo(int i) { return 20.f * std::pow(1000.f, (float) i / NSPEC); }
static void fresh(int preset = 0) { D(effSetProgram, 0, preset); D(effMainsChanged, 0, 1); }

int main(int argc, char** argv) {
    void* h = dlopen(argc > 1 ? argv[1] : "build/native/eq7.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    if (!entry) { std::printf("no VSTPluginMain\n"); return 2; }
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic, "magic");
    CHECK(fx->numInputs == 2 && fx->numOutputs == 2, "io");
    CHECK(fx->uniqueID == 0x45513750, "uid %08x", fx->uniqueID);
    CHECK(fx->numParams == P_COUNT && fx->numPrograms == 42, "counts %d/%d", fx->numParams, fx->numPrograms);
    CHECK(D(effGetPlugCategory) == kPlugCategEffect, "category");
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effSetBlockSize, 0, 512); D(effMainsChanged, 0, 1);

    std::printf("Parameters (bands 1 and 7, output, first curve points):\n");
    for (int i = 0; i < P_COUNT; ++i) {
        char n[64] = {}, d[64] = {}, u[64] = {};
        D(effGetParamName, i, 0, n); D(effGetParamDisplay, i, 0, d); D(effGetParamLabel, i, 0, u);
        if (i < 6 || (i >= 36 && i < 47) || i == P_SPEC0) std::printf("  %2d %-12s %-12s %s\n", i, n, d, u);
        CHECK(std::strlen(n) > 0, "param %d name", i);
        if (i < P_CURVE0) { setNorm(i, 0.37f); float a = getNorm(i); setNorm(i, a); CHECK(std::fabs(getNorm(i) - a) < 1e-6f, "param %d roundtrip", i); }
        else { float a = getNorm(i); setNorm(i, 0.9f); CHECK(getNorm(i) == a, "curve point %d is read-only", i); CHECK(D(effCanBeAutomated, i) == 0, "curve %d not automatable", i); }
    }
    std::printf("Programs:");
    for (int i = 0; i < fx->numPrograms; ++i) { char n[64] = {}; D(effGetProgramNameIndexed, i, 0, n); std::printf("%s%s", i % 7 ? ", " : "\n  ", n); CHECK(n[0], "preset name"); }
    std::printf("\n");
    automateCalls = displayCalls = 0; D(effSetProgram, 0, 3);
    CHECK(automateCalls == P_CURVE0 && displayCalls == 1, "preset notifies host (%d automate, %d display)", automateCalls, displayCalls);

    const int N = (int) SR;
    std::vector<float> l, r, ol, orr;
    auto gainAt = [&](float hz, float amp = 0.1f) {
        makeSine(l, r, amp, hz, N); D(effMainsChanged, 0, 1); proc(l, r, ol, orr);
        return db(rmsOf(ol, N / 2, N)) - db(rmsOf(l, N / 2, N));
    };

    // 1) Flat preset is bit-transparent
    fresh(); makeSine(l, r, 0.7f, 1000.f, N); proc(l, r, ol, orr);
    bool exact = true; for (int i = 0; i < N; ++i) if (ol[i] != l[i]) { exact = false; break; }
    CHECK(exact, "flat EQ bit-exact");

    // 2) Filter types
    fresh(); allOff(); type(4, T_PEAK); freq(4, 1000.f); gain(4, 12.f); q(4, 1.f);
    float pk = gainAt(1000.f), pk100 = gainAt(100.f);
    std::printf("\nPeak +12 dB @1k: 1 kHz %.2f, 100 Hz %.2f\n", pk, pk100);
    CHECK(std::fabs(pk - 12.f) < 0.3f && std::fabs(pk100) < 0.5f, "peak");
    fresh(); allOff(); type(1, T_LSHELF); freq(1, 100.f); gain(1, 6.f); q(1, 0.707f);
    float ls30 = gainAt(30.f), ls5k = gainAt(5000.f);
    std::printf("Low shelf +6 @100: 30 Hz %.2f, 5 kHz %.2f\n", ls30, ls5k);
    CHECK(ls30 > 5.f && ls30 < 6.5f && std::fabs(ls5k) < 0.2f, "low shelf");
    fresh(); allOff(); type(7, T_HSHELF); freq(7, 5000.f); gain(7, -6.f); q(7, 0.707f);
    float hs16k = gainAt(16000.f), hs200 = gainAt(200.f);
    std::printf("High shelf -6 @5k: 16 kHz %.2f, 200 Hz %.2f\n", hs16k, hs200);
    CHECK(hs16k < -5.f && hs16k > -6.5f && std::fabs(hs200) < 0.2f, "high shelf");
    float lc[3];
    for (int s = 0; s < 3; ++s) { fresh(); allOff(); type(1, T_LCUT); freq(1, 200.f); slope(1, s); q(1, 0.707f); lc[s] = gainAt(100.f); }
    std::printf("Low cut 200 Hz, one octave below: 12 dB %.1f, 24 dB %.1f, 48 dB %.1f\n", lc[0], lc[1], lc[2]);
    CHECK(std::fabs(lc[0] + 12.3f) < 2.f && std::fabs(lc[1] + 24.1f) < 2.f && std::fabs(lc[2] + 48.2f) < 3.f, "low cut slopes");
    fresh(); allOff(); type(1, T_LCUT); freq(1, 200.f); slope(1, 1);
    float lcPass = gainAt(2000.f); CHECK(std::fabs(lcPass) < 0.1f, "low cut passband %.2f", lcPass);
    fresh(); allOff(); type(7, T_HCUT); freq(7, 1000.f); slope(7, 1);
    float hc2k = gainAt(2000.f), hc200 = gainAt(200.f);
    std::printf("High cut 1k 24 dB: 2 kHz %.1f, 200 Hz %.2f\n", hc2k, hc200);
    CHECK(std::fabs(hc2k + 24.1f) < 2.f && std::fabs(hc200) < 0.1f, "high cut");
    fresh(); allOff(); type(3, T_NOTCH); freq(3, 1000.f); q(3, 4.f);
    float n1k = gainAt(1000.f), n500 = gainAt(500.f);
    std::printf("Notch 1k Q4: 1 kHz %.1f, 500 Hz %.2f\n", n1k, n500);
    CHECK(n1k < -30.f && n500 > -0.5f, "notch");
    fresh(); allOff(); type(4, T_BPASS); freq(4, 1000.f); q(4, 1.f);
    float bp1k = gainAt(1000.f), bp100 = gainAt(100.f);
    std::printf("Band pass 1k: 1 kHz %.2f, 100 Hz %.1f\n", bp1k, bp100);
    CHECK(std::fabs(bp1k) < 0.3f && bp100 < -15.f, "band pass");
    fresh(); allOff(); setNorm(P_OUTPUT, (6.f + 18.f) / 36.f);
    float out6 = gainAt(1000.f); CHECK(std::fabs(out6 - 6.f) < 0.05f, "output gain %.2f", out6);

    // 3) Curve display matches the measured response, and is sent to the host once per change
    fresh(1);   // Kick Punch
    l.assign(4096, 0.f); r = l; curveCalls = 0; proc(l, r, ol, orr);
    std::printf("\nKick Punch: host told about %d curve points\n", curveCalls);
    CHECK(curveCalls > 5, "curve sent after a preset change");
    float worst = 0;
    for (int i = 3; i < NCURVE; i += 6) {
        const float hz = curveHz(i), want = curveDb(i), got = gainAt(hz, 0.05f);
        std::printf("  %7.0f Hz: curve %+6.2f dB, measured %+6.2f dB\n", hz, want, got);
        worst = std::fmax(worst, std::fabs(want - got));
    }
    CHECK(worst < 0.3f, "curve matches measured response (worst %.2f dB)", worst);
    curveCalls = 0; l.assign(N, 0.f); r = l; proc(l, r, ol, orr);
    CHECK(curveCalls == 0, "no curve traffic when nothing changes (%d)", curveCalls);
    gain(2, 9.f); curveCalls = 0; proc(l, r, ol, orr);
    std::printf("After moving band 2 gain: %d curve points re-sent\n", curveCalls);
    CHECK(curveCalls > 0 && curveCalls <= NCURVE, "curve follows a band change");
    CHECK(std::fabs(lastAutomate[P_CURVE0 + 7] - getNorm(P_CURVE0 + 7)) < 1e-6f, "sent value = stored value");

    // 4) Sweeping a band while audio plays doesn't click
    {
        fresh(); allOff(); type(4, T_PEAK); gain(4, 12.f); q(4, 2.f); freq(4, 200.f);
        makeSine(l, r, 0.3f, 1000.f, N); ol.assign(N, 0); orr.assign(N, 0);
        auto maxJump = [&](int a, int b) { float m = 0; for (int i = a + 1; i < b; ++i) m = std::fmax(m, std::fabs(ol[i] - ol[i - 1])); return m; };
        for (int pos = 0; pos < N; pos += 256) {
            if (pos >= N / 2 && pos < N / 2 + 256 * 40) freq(4, 200.f * std::pow(25.f, (pos - N / 2) / (256.f * 40)));   // 200 Hz -> 5 kHz
            if (pos == N / 2 + 256 * 20) gain(4, -12.f);
            float* in[2] = { &l[pos], &r[pos] }; float* out[2] = { &ol[pos], &orr[pos] };
            fx->processReplacing(fx, in, out, std::min(256, N - pos));
        }
        const float steadyAfter = 0.3f * 3.98f * 6.2831853f * 1000.f / SR;   // largest step of a +12 dB 1 kHz sine
        const float sweep = maxJump(N / 2, N / 2 + 256 * 60);
        std::printf("\nSweeping a +12 dB peak 200 Hz -> 5 kHz and flipping to -12 dB: max step %.3f (a +12 dB sine alone: %.3f)\n", sweep, steadyAfter);
        CHECK(sweep < 1.3f * steadyAfter, "no clicks while sweeping");
    }

    // 5) Every preset: finite, bounded, in place, at 44.1 and 48 kHz
    for (float rate : { 44100.f, 48000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, SR);
        for (int p = 0; p < fx->numPrograms; ++p) {
            D(effSetProgram, 0, p); D(effMainsChanged, 0, 1);
            makeSine(l, r, 0.5f, 80.f, N); for (int i = 0; i < N; ++i) { l[i] += 0.3f * std::sin(6.2831853f * 3000.f * i / SR); r[i] = 0.5f * std::sin(6.2831853f * 9000.f * i / SR); }
            std::vector<float> al = l, ar = r;
            for (int pos = 0; pos < N; pos += 256) { float* io[2] = { al.data() + pos, ar.data() + pos }; int m = (N - pos) < 256 ? (N - pos) : 256; fx->processReplacing(fx, io, io, m); }
            bool ok = true; float mx = 0; for (int i = 0; i < N; ++i) { if (!std::isfinite(al[i]) || !std::isfinite(ar[i])) ok = false; mx = std::fmax(mx, std::fmax(std::fabs(al[i]), std::fabs(ar[i]))); }
            CHECK(ok && mx < 4.f, "preset %d output sane at %.0f (peak %.2f)", p, rate, mx);
        }
    }
    SR = 44100.f; D(effSetSampleRate, 0, 0, nullptr, SR);
    std::printf("all %d presets finite and bounded at 44.1 / 48 kHz\n", fx->numPrograms);

    // 6) Denormal input, bypass, accumulating process
    fresh(1); l.assign(N, 1e-30f); r.assign(N, 0.f); proc(l, r, ol, orr);
    bool fin = true; for (int i = 0; i < N; ++i) if (!std::isfinite(ol[i])) fin = false;
    CHECK(fin, "denormal input finite");
    fresh(1); setNorm(P_BYPASS, 1.f); makeSine(l, r, 0.5f, 100.f, N); proc(l, r, ol, orr);
    exact = true; for (int i = 0; i < N; ++i) if (ol[i] != l[i]) { exact = false; break; }
    CHECK(exact, "bypass bit-exact"); setNorm(P_BYPASS, 0.f);
    fresh(); l.assign(512, 0.25f); r = l; std::vector<float> a(512, 1.f), b(512, 1.f);
    float* in[2] = { l.data(), r.data() }; float* out[2] = { a.data(), b.data() };
    fx->process(fx, in, out, 512); CHECK(std::fabs(a[100] - 1.25f) < 1e-6f, "accumulating process %.3f", a[100]);

    // 7) Slow Q-Link turns step through every Type and Slope
    for (int prm : { bp(3, B_TYPE), bp(3, B_SLOPE) }) {
        setNorm(prm, 0.f); int changes = 0; char last[64] = {}; D(effGetParamDisplay, prm, 0, last);
        for (int k = 0; k < 127; ++k) {
            setNorm(prm, std::fmin(1.f, getNorm(prm) + 1.f / 127.f));
            char d[64] = {}; D(effGetParamDisplay, prm, 0, d);
            if (std::strcmp(d, last)) { ++changes; std::strcpy(last, d); }
        }
        std::printf("slow Q-Link turn on param %d: %d steps, ends at '%s'\n", prm, changes, last);
        CHECK(changes == (prm == bp(3, B_TYPE) ? 6 : 2), "Q-Link walks every option (%d)", changes);
    }

    // 8) Band power buttons: a band switched off leaves the signal alone and flattens its part of the curve
    fresh(); allOff(); type(4, T_PEAK); freq(4, 1000.f); gain(4, 12.f);
    float onG = gainAt(1000.f); setNorm(bp(4, B_ON), 0.f); float offG = gainAt(1000.f);
    std::printf("\nBand 4 +12 dB: on %.2f dB, switched off %.2f dB, curve at 1 kHz now %+.2f\n", onG, offG, curveDb(38));
    CHECK(std::fabs(onG - 12.f) < 0.3f && std::fabs(offG) < 0.01f && std::fabs(curveDb(38)) < 0.1f, "band power button");

    // 9) Analyzer: a -6 dBFS 1 kHz tone lights the band holding 1 kHz at about -6 dB; far bands stay low
    fresh(); setNorm(P_ANALYZER, 1.f); makeSine(l, r, 0.5f, 1000.f, N); specCalls = 0; proc(l, r, ol, orr);
    int best = 0; for (int i = 1; i < NSPEC; ++i) if (specDb(i) > specDb(best)) best = i;
    std::printf("Analyzer, 1 kHz at -6 dBFS: loudest band %d (%.0f-%.0f Hz) at %.1f dB; 100 Hz band %.1f dB; %d updates sent in 1 s\n",
                best, specLo(best), specLo(best + 1), specDb(best), specDb(13), specCalls);
    CHECK(specLo(best) <= 1000.f && specLo(best + 1) > 1000.f, "analyzer finds 1 kHz");
    CHECK(std::fabs(specDb(best) + 6.f) < 1.5f, "analyzer level %.1f dB", specDb(best));
    CHECK(specDb(13) < -50.f, "analyzer quiet away from the tone");
    CHECK(specCalls > 0 && specCalls < 32 * 16, "analyzer update traffic %d", specCalls);
    proc(l, r, ol, orr); proc(l, r, ol, orr);          // bars lit by the tone's onset fall at 30 dB/s: let them settle
    specCalls = 0; proc(l, r, ol, orr);
    std::printf("steady tone (4th second): %d analyzer updates\n", specCalls);
    CHECK(specCalls < 32, "steady signal sends (almost) nothing");
    setNorm(P_ANALYZER, 0.f); specCalls = 0; proc(l, r, ol, orr);
    CHECK(specDb(best) <= -83.9f && specCalls >= 1 && specCalls <= NSPEC, "analyzer off clears once (%d)", specCalls);
    specCalls = 0; proc(l, r, ol, orr);
    CHECK(specCalls == 0, "analyzer off sends nothing");
    setNorm(P_ANALYZER, 1.f);

    char s[64] = {}; D(effGetEffectName, 0, 0, s); CHECK(!std::strcmp(s, "EQ7"), "effect name");
    D(effClose);
    std::printf("\n%s (%d failure%s)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
