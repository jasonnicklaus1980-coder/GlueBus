// Offline VST2 host test for ASR10: loads the .so, exercises the dispatcher, parameters, presets and audio, and
// checks the modelled hardware: 30k / 44.1k converter bandwidth, 16-bit resolution, interpolated tuning (semitones
// and cents), Rate mode, the four OTTO filter modes, plus the MPC slider / click fixes carried over from SP1200.
#include "vst2.h"
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum { P_INPUT, P_TUNE, P_FINE, P_FC1, P_FC2, P_MIX, P_VOLUME, P_RATE, P_FMODE, P_TMODE, P_BYPASS, P_DATA, P_EDIT, P_COUNT };

static int automateCalls = 0, displayCalls = 0; static float lastAutomate[64]; static int automated[64];
static intptr_t host(AEffect*, int32_t op, int32_t idx, intptr_t, void*, float opt) {
    if (op == 0) { ++automateCalls; if (idx >= 0 && idx < 64) { lastAutomate[idx] = opt; ++automated[idx]; } }
    if (op == 42) ++displayCalls;
    return 2400;
}
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static float getNorm(int i) { return fx->getParameter(fx, i); }
static void tune(int st) { setNorm(P_TUNE, (st + 12) / 24.f); }
static void fine(int ct) { setNorm(P_FINE, (ct + 50) / 100.f); }
static void rate30(bool on) { setNorm(P_RATE, on ? 0.f : 1.f); }
static void tmode(int m) { setNorm(P_TMODE, (float) m); }
static void fmode(int m) { setNorm(P_FMODE, m / 3.f); }
static void logHz(int p, float hz, float lo, float hi) { setNorm(p, std::log(hz / lo) / std::log(hi / lo)); }
static void fc1(float hz) { logHz(P_FC1, hz, 100.f, 20000.f); }
static void fc2(float hz) { logHz(P_FC2, hz, 20.f, 20000.f); }

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
static void fresh(int preset = 0) { D(effSetProgram, 0, preset); D(effMainsChanged, 0, 1); }   // Init 44.1k
static float freqOf(const std::vector<float>& v, int from, int to) {
    double first = -1, last = -1; int n = 0;
    for (int i = from + 1; i < to; ++i)
        if (v[i - 1] < 0 && v[i] >= 0) { double t = i - 1 + v[i - 1] / (v[i - 1] - v[i]); if (first < 0) first = t; last = t; ++n; }
    return n > 1 ? (float) ((n - 1) * SR / (last - first)) : 0.f;
}
static float medianPitch(const std::vector<float>& v, int from, int to) {
    std::vector<float> est; const int w = (int) (0.02f * SR), hop = (int) (0.005f * SR);
    for (int p = from; p + w < to; p += hop) est.push_back(freqOf(v, p, p + w));
    std::sort(est.begin(), est.end()); return est[est.size() / 2];
}

int main(int argc, char** argv) {
    void* h = dlopen(argc > 1 ? argv[1] : "build/native/asr10.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    if (!entry) { std::printf("no VSTPluginMain\n"); return 2; }
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic, "magic");
    CHECK(fx->numInputs == 2 && fx->numOutputs == 2, "io");
    CHECK(fx->flags & effFlagsCanReplacing, "replacing flag");
    CHECK(fx->uniqueID == 0x41535231, "uid %08x", fx->uniqueID);
    CHECK(fx->numParams == P_COUNT && fx->numPrograms == 36, "counts %d/%d", fx->numParams, fx->numPrograms);
    CHECK(D(effGetPlugCategory) == kPlugCategEffect, "category");
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effSetBlockSize, 0, 512); D(effMainsChanged, 0, 1);

    std::printf("Parameters:\n");
    for (int i = 0; i < fx->numParams; ++i) {
        char n[64] = {}, d[64] = {}, u[64] = {};
        D(effGetParamName, i, 0, n); D(effGetParamDisplay, i, 0, d); D(effGetParamLabel, i, 0, u);
        std::printf("  %2d %-12s %-10s %s\n", i, n, d, u);
        CHECK(std::strlen(n) > 0, "param %d name", i);
        setNorm(i, 0.37f); float a = getNorm(i); setNorm(i, a); CHECK(std::fabs(getNorm(i) - a) < 1e-6f, "param %d roundtrip", i);
    }
    std::printf("Programs:\n");
    for (int i = 0; i < fx->numPrograms; ++i) { char n[64] = {}; D(effGetProgramNameIndexed, i, 0, n); std::printf("  %2d %s\n", i, n); CHECK(n[0], "preset name"); }
    automateCalls = displayCalls = 0; D(effSetProgram, 0, 4);
    CHECK(automateCalls == P_COUNT && displayCalls == 1, "preset change notifies host (%d automate, %d display)", automateCalls, displayCalls);
    CHECK(D(effGetProgram) == 4, "program 4");
    { char d[64] = {}; D(effGetParamDisplay, P_TUNE, 0, d); CHECK(!std::strcmp(d, "-3"), "Pitch Down -3 display '%s'", d); }
    { char d[64] = {}; D(effGetParamDisplay, P_RATE, 0, d); CHECK(!std::strcmp(d, "30 kHz"), "30k display '%s'", d); }
    fresh();
    { char d[64] = {}; D(effGetParamDisplay, P_FC1, 0, d); CHECK(!std::strcmp(d, "Open"), "FC1 open display '%s'", d); }
    { char d[64] = {}; D(effGetParamDisplay, P_FC2, 0, d); CHECK(!std::strcmp(d, "Off"), "FC2 off display '%s'", d); }
    fmode(2); { char d[64] = {}; fc2(20000.f); D(effGetParamDisplay, P_FC2, 0, d); CHECK(!std::strcmp(d, "Open"), "FC2 open in LP mode '%s'", d); }

    const int N = (int) SR;
    std::vector<float> l, r, ol, orr;
    auto gainAt = [&](float hz, float amp = 0.25f) {
        makeSine(l, r, amp, hz, N); D(effMainsChanged, 0, 1); proc(l, r, ol, orr);
        return db(rmsOf(ol, N / 2, N)) - db(rmsOf(l, N / 2, N));
    };

    // 1) Init (44.1k, filters open, tune 0): transparent apart from 16-bit rounding and a 2-sample delay
    fresh(); float g1k = gainAt(1000.f), g15k = gainAt(15000.f);
    std::printf("\n44.1k: 1 kHz %.2f dB, 15 kHz %.2f dB\n", g1k, g15k);
    CHECK(std::fabs(g1k) < 0.1f && std::fabs(g15k) < 0.5f, "44.1k path flat");

    // 2) 30k mode: the sigma-delta converter band-limits at ~13.4 kHz, so highs roll off and nothing aliases
    fresh(); rate30(true);
    float a1k = gainAt(1000.f), a10k = gainAt(10000.f), a15k = gainAt(15000.f), a18k = gainAt(18000.f);
    std::printf("30k: 1 kHz %.2f dB, 10 kHz %.2f dB, 15 kHz %.1f dB, 18 kHz %.1f dB\n", a1k, a10k, a15k, a18k);
    CHECK(std::fabs(a1k) < 0.3f, "30k passband");
    CHECK(a10k > -3.f, "30k keeps 10 kHz");
    CHECK(a15k < -20.f && a18k < -30.f, "30k band limit");
    {   // alias check: 18 kHz into 30k would fold to 11.76 kHz without the decimation filter
        makeSine(l, r, 0.5f, 18000.f, N); D(effMainsChanged, 0, 1); proc(l, r, ol, orr);
        const double w = 6.283185307179586 * 11762.0 / SR, c = 2 * std::cos(w); double s1 = 0, s2 = 0;
        for (int i = N / 2; i < N; ++i) { const double s0 = ol[i] + c * s1 - s2; s2 = s1; s1 = s0; }
        const float alias = (float) std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (N / 4.f);
        std::printf("30k: 18 kHz input, alias at 11.76 kHz: %.1f dB\n", db(alias / 0.5f));
        CHECK(db(alias / 0.5f) < -40.f, "no alias in 30k");
    }

    // 3) 16-bit resolution: -100 dBFS sine (below 1 LSB = -90 dBFS) collapses to at most three codes
    fresh(); makeSine(l, r, 1e-5f, 440.f, N); proc(l, r, ol, orr);
    {
        std::vector<float> codes;
        for (int i = N / 10; i < N; ++i) if (std::find(codes.begin(), codes.end(), ol[i]) == codes.end()) codes.push_back(ol[i]);
        std::printf("-100 dBFS sine -> %zu output levels\n", codes.size());
        CHECK(codes.size() <= 3, "16-bit quantisation (%zu levels)", codes.size());
    }

    // 4) Pitch mode tuning (linear-interpolated playback): semitones and cents, both sampling rates
    for (bool r30 : { true, false })
        for (int st : { -12, -5, -1, 3, 7, 12 }) {
            fresh(); rate30(r30); tune(st); tmode(0); makeSine(l, r, 0.25f, 440.f, N); proc(l, r, ol, orr);
            const float want = 440.f * std::pow(2.f, st / 12.f), f = medianPitch(ol, N / 4, N);
            const float cents = 1200.f * std::log2(f / want);
            std::printf("%s pitch %+3d: %.1f Hz (want %.1f, %+.1f cents)\n", r30 ? "30k " : "44k", st, f, want, cents);
            CHECK(std::fabs(cents) < 6.f, "tuning %+d off by %.1f cents", st, cents);
        }
    for (int ct : { -37, 25 }) {
        fresh(); tune(0); fine(ct); makeSine(l, r, 0.25f, 440.f, N); proc(l, r, ol, orr);
        const float want = 440.f * std::pow(2.f, ct / 1200.f), f = medianPitch(ol, N / 4, N);
        std::printf("fine %+d ct: %.2f Hz (want %.2f, %+.1f cents)\n", ct, f, want, 1200.f * std::log2(f / want));
        CHECK(std::fabs(1200.f * std::log2(f / want)) < 3.f, "fine tune %+d", ct);
    }

    // 5) Rate mode keeps the pitch and lowers the effective sampling rate (band limit follows)
    fresh(); rate30(true); tmode(1); tune(-12);
    makeSine(l, r, 0.25f, 440.f, N); proc(l, r, ol, orr);
    float fr = freqOf(ol, N / 2, N); std::printf("Rate mode -12 (30k -> 14.9k): 440 Hz in -> %.1f Hz\n", fr);
    CHECK(std::fabs(fr - 440.f) < 2.f, "rate mode keeps pitch");
    float r5k = gainAt(5000.f), r8k = gainAt(8000.f);
    std::printf("Rate mode -12: 5 kHz %.1f dB, 8 kHz %.1f dB (band limit ~6.7 kHz)\n", r5k, r8k);
    // 5 kHz: linear interpolation at a 14.9 kHz rate droops sinc^2(5/14.9) = -3.4 dB, as on the hardware
    CHECK(r5k > -4.5f && r8k < -15.f, "rate mode band limit");

    // 6) OTTO filter modes (one-pole stages, no resonance)
    fresh(); fc1(1000.f);                                           // LP2/HP2, FC2 off: 2 poles at 1 kHz
    float lp2_4k = gainAt(4000.f), lp2_250 = gainAt(250.f);
    fresh(); fmode(1); fc1(1000.f);                                 // LP3/HP1, FC2 off: 3 poles at 1 kHz
    float lp3_4k = gainAt(4000.f);
    fresh(); fmode(3); fc1(1000.f); fc2(1000.f);                    // LP3/LP1: 4 poles at 1 kHz (24 dB/oct)
    float lp4_4k = gainAt(4000.f), lp4_8k = gainAt(8000.f);
    std::printf("FC1 1 kHz @ 4 kHz: 2-pole %.1f dB, 3-pole %.1f dB, 4-pole %.1f dB; 250 Hz %.2f dB\n", lp2_4k, lp3_4k, lp4_4k, lp2_250);
    CHECK(lp2_4k < -9.f && lp3_4k < lp2_4k - 4.f && lp4_4k < lp3_4k - 4.f, "filter slopes 12/18/24 dB");
    CHECK(lp2_250 > -1.f, "passband below FC1");
    CHECK(lp4_8k < lp4_4k - 15.f, "4-pole ~24 dB/oct (%.1f)", lp4_8k - lp4_4k);
    fresh(); fc2(500.f);                                            // LP2/HP2, FC1 open: 2 high-pass poles at 500 Hz
    float hp100 = gainAt(100.f), hp5k = gainAt(5000.f);
    std::printf("FC2 high-pass 500 Hz: 100 Hz %.1f dB, 5 kHz %.2f dB\n", hp100, hp5k);
    CHECK(hp100 < -20.f && hp5k > -1.f, "high-pass");   // the chip's high-pass is x - lowpass: ~0.35 dB dip per pole
    fresh(); fmode(2); fc1(20000.f); fc2(2000.f);                   // LP2/LP2 with FC1 open: 2 poles on FC2
    float lpfc2 = gainAt(8000.f); std::printf("LP2/LP2 FC2 2 kHz @ 8 kHz: %.1f dB\n", lpfc2);
    CHECK(lpfc2 < -15.f, "FC2 low-pass in LP modes");
    {   // no resonance: sweep around the cutoff, nothing rises above the passband
        fresh(); fmode(3); fc1(1000.f); fc2(1000.f); float mx = -100;
        for (float hz : { 500.f, 700.f, 900.f, 1000.f, 1200.f }) mx = std::fmax(mx, gainAt(hz));
        std::printf("4-pole, max gain near cutoff: %.2f dB\n", mx);
        CHECK(mx < 0.1f, "no resonant peak");
    }

    // 7) Input slider drives the ADC into hard clipping at 16-bit full scale
    fresh(); setNorm(P_INPUT, 1.f); makeSine(l, r, 0.9f, 200.f, N); proc(l, r, ol, orr);
    std::printf("input +12 dB into 0.9 sine: output peak %.4f\n", peakOf(ol, N / 2));
    CHECK(peakOf(ol, N / 2) <= 1.f, "ADC full-scale clip");

    // 8) Mix 0 and Bypass are bit-exact dry
    fresh(); setNorm(P_MIX, 0.f); makeSine(l, r, 0.7f, 1000.f, N); proc(l, r, ol, orr);
    bool exact = true; for (int i = 0; i < N; ++i) if (ol[i] != l[i]) { exact = false; break; }
    CHECK(exact, "mix 0 bit-exact");
    fresh(); setNorm(P_BYPASS, 1.f); proc(l, r, ol, orr);
    exact = true; for (int i = 0; i < N; ++i) if (ol[i] != l[i]) { exact = false; break; }
    CHECK(exact, "bypass bit-exact");
    setNorm(P_BYPASS, 0.f);

    // 9) Every preset: finite, bounded, in-place processing, at 44.1 and 48 kHz host rates
    for (float rate : { 44100.f, 48000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, SR);
        for (int p = 0; p < fx->numPrograms; ++p) {
            D(effSetProgram, 0, p); D(effMainsChanged, 0, 1);
            makeSine(l, r, 0.95f, 80.f, N); for (int i = 0; i < N; ++i) r[i] = 0.9f * std::sin(6.2831853f * 3000.f * i / SR);
            std::vector<float> al = l, ar = r;
            for (int pos = 0; pos < N; pos += 256) { float* io[2] = { al.data() + pos, ar.data() + pos }; int m = (N - pos) < 256 ? (N - pos) : 256; fx->processReplacing(fx, io, io, m); }
            bool ok = true; float mx = 0; for (int i = 0; i < N; ++i) { if (!std::isfinite(al[i]) || !std::isfinite(ar[i])) ok = false; mx = std::fmax(mx, std::fmax(std::fabs(al[i]), std::fabs(ar[i]))); }
            char nm[64] = {}; D(effGetProgramNameIndexed, p, 0, nm);
            if (rate == 44100.f) std::printf("preset %-16s finite=%d peak=%.2f\n", nm, ok, mx);
            CHECK(ok && mx < 4.f, "preset %d output sane at %.0f", p, rate);
        }
    }
    SR = 44100.f; D(effSetSampleRate, 0, 0, nullptr, SR);

    // 10) Silence / denormal-range input stays finite
    fresh(); rate30(true); fmode(3); fc1(300.f); fc2(300.f); l.assign(N, 1e-30f); r.assign(N, 0.f); proc(l, r, ol, orr);
    bool fin = true; for (int i = 0; i < N; ++i) if (!std::isfinite(ol[i])) fin = false;
    CHECK(fin, "denormal input finite");

    // 11) Legacy accumulating process() adds to the output buffer
    fresh(); setNorm(P_MIX, 0.f); l.assign(512, 0.25f); r = l; std::vector<float> a(512, 1.f), b(512, 1.f);
    float* in[2] = { l.data(), r.data() }; float* out[2] = { a.data(), b.data() };
    fx->process(fx, in, out, 512); CHECK(std::fabs(a[100] - 1.25f) < 0.01f, "accumulating process %.3f", a[100]);

    // 12) Slow Q-Link turns (host reads, adds 1/127, writes back) walk through every step of the stepped sliders
    for (int p : { P_TUNE, P_FINE }) {
        setNorm(p, 0.f); int changes = 0; char last[64] = {}; D(effGetParamDisplay, p, 0, last);
        for (int k = 0; k < 127; ++k) {
            setNorm(p, std::fmin(1.f, getNorm(p) + 1.f / 127.f));
            char d[64] = {}; D(effGetParamDisplay, p, 0, d);
            if (std::strcmp(d, last)) { ++changes; std::strcpy(last, d); }
        }
        std::printf("slow Q-Link turn, param %d: %d steps, ends at '%s'\n", p, changes, last);
        CHECK(changes == (p == P_TUNE ? 24 : 100) || (p == P_FINE && changes >= 90), "Q-Link walks steps of param %d (%d)", p, changes);
    }

    // 13) Moving controls mid-sound doesn't click
    auto maxJump = [](const std::vector<float>& v, int from, int to) {
        float m = 0; for (int i = from + 1; i < to; ++i) m = std::fmax(m, std::fabs(v[i] - v[i - 1])); return m;
    };
    auto switchRun = [&](int param, float from, float to, int tm, float fc1Hz = 0.f) {
        fresh(); rate30(true); tmode(tm); if (fc1Hz > 0.f) fc1(fc1Hz);
        setNorm(param, from); makeSine(l, r, 0.5f, 200.5f, N);   // switch near a peak
        ol.assign(N, 0); orr.assign(N, 0);
        const int sw = (N / 2 / 256) * 256;
        for (int pos = 0; pos < N; pos += 256) {
            if (pos == sw) setNorm(param, to);
            float* in2[2] = { &l[pos], &r[pos] }; float* out2[2] = { &ol[pos], &orr[pos] };
            fx->processReplacing(fx, in2, out2, std::min(256, N - pos));
        }
        return std::make_pair(maxJump(ol, N / 4, sw - 1), maxJump(ol, sw - 1, sw + 4410));
    };
    {
        auto j = switchRun(P_TUNE, 12 / 24.f, 8 / 24.f, 0);
        std::printf("Pitch mode tune 0 -> -4: steady max step %.3f, at switch %.3f\n", j.first, j.second);
        CHECK(j.second < 2.f * j.first + 0.02f, "tune-0 switch click (%.3f vs %.3f)", j.second, j.first);
        // mode LP2/HP2 -> LP3/HP1 with FC1 at 2 kHz re-purposes pole 3 (high-pass -> low-pass on FC1)
        j = switchRun(P_FMODE, 0.f, 1 / 3.f, 0, 2000.f);
        std::printf("Filter mode change: steady max step %.3f, at switch %.3f\n", j.first, j.second);
        CHECK(j.second < 2.f * j.first + 0.02f, "filter mode switch click (%.3f vs %.3f)", j.second, j.first);
    }

    // 14) ASR-10 style editing: Edit buttons pick a parameter, the one Data Entry slider moves it
    {
        auto edit = [](int k) { setNorm(P_EDIT, k / 6.f); };           // 0 Input, 1 Tune, 2 Fine, 3 Filter 1 ...
        auto disp = [](int p) { static char d[64]; std::memset(d, 0, sizeof d); D(effGetParamDisplay, p, 0, d); return d; };
        fresh(); edit(1); setNorm(P_DATA, 18 / 24.f);
        std::printf("\nData Entry on Tune: display '%s'", disp(P_DATA)); std::printf(", tune '%s'\n", disp(P_TUNE));
        CHECK(!std::strcmp(disp(P_TUNE), "+6") && !std::strcmp(disp(P_DATA), "TUNE +6"), "data entry moves Tune");
        CHECK(std::fabs(getNorm(P_DATA) - getNorm(P_TUNE)) < 1e-6f, "data entry reads the selected parameter");
        CHECK(std::fabs(lastAutomate[P_TUNE] - getNorm(P_TUNE)) < 1e-6f, "host told about the real parameter");
        fc1(6000.f); automated[P_DATA] = 0; displayCalls = 0; edit(3);
        std::printf("Edit -> Filter 1: display '%s', host told %d time(s), Data Entry now %.3f (Filter 1 %.3f)\n",
                    disp(P_DATA), automated[P_DATA], lastAutomate[P_DATA], getNorm(P_FC1));
        CHECK(!std::strcmp(disp(P_DATA), "FILTER 1 6.0 kHz"), "display follows the selection");
        CHECK(automated[P_DATA] == 1 && std::fabs(lastAutomate[P_DATA] - getNorm(P_FC1)) < 1e-6f && displayCalls >= 1,
              "slider jumps to the newly selected value");
        automated[P_DATA] = 0; fc1(2000.f);                         // a Q-Link moves Filter 1 directly
        CHECK(automated[P_DATA] == 1 && std::fabs(lastAutomate[P_DATA] - getNorm(P_FC1)) < 1e-6f, "Data Entry follows a Q-Link");
        automated[P_DATA] = 0; setNorm(P_MIX, 0.3f);                // a parameter that isn't selected
        CHECK(automated[P_DATA] == 0, "unselected parameter doesn't move Data Entry");
        edit(1); setNorm(P_DATA, 0.f); int steps = 0; char last[64]; std::strcpy(last, disp(P_TUNE));
        for (int k = 0; k < 127; ++k) {
            setNorm(P_DATA, std::fmin(1.f, getNorm(P_DATA) + 1.f / 127.f));
            if (std::strcmp(disp(P_TUNE), last)) { ++steps; std::strcpy(last, disp(P_TUNE)); }
        }
        std::printf("slow Q-Link on Data Entry (Tune): %d steps, ends at '%s'\n", steps, last);
        CHECK(steps == 24, "data entry steps through every semitone (%d)", steps);
        edit(4); D(effSetProgram, 0, 3);
        CHECK(!std::strncmp(disp(P_DATA), "FILTER 2", 8), "presets keep the Edit selection ('%s')", disp(P_DATA));
        for (int k = 0; k < 7; ++k) { edit(k); std::printf("  edit %d: '%s'\n", k, disp(P_DATA)); CHECK(std::strlen(disp(P_DATA)) < 24, "display fits"); }
    }

    char s[64] = {}; D(effGetEffectName, 0, 0, s); CHECK(!std::strcmp(s, "ASR10"), "effect name");
    D(effClose);
    std::printf("\n%s (%d failure%s)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
