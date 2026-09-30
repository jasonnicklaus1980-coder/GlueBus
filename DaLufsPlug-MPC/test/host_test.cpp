// Offline VST2 host test for Da Lufs Plug (loads the plugin like MPC does, no MPC needed).
// Covers: pass-through, BS.1770 / EBU Tech 3341 loudness cases (momentary, short-term, gated integrated),
// Tech 3342 loudness range, true peak (inter-sample), platform presets and status lines, reset, pause, history,
// sample rates.
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

enum { P_PLATFORM, P_TARGET, P_CEIL, P_PREV, P_NEXT, P_RESET, P_PAUSE, P_M, P_S, P_I, P_LRA, P_TP, P_MAXM, P_MAXS, P_TIME,
       P_GAIN, P_TPHEAD, P_TPOVER, P_STATUS, P_TPSTATUS, P_REL, P_HIST0, P_COUNT = P_HIST0 + 60 };
static const int NPROG = 17;

static int automateCalls = 0;
static intptr_t host(AEffect*, int32_t op, int32_t, intptr_t, void*, float) { if (op == 0) ++automateCalls; return 2400; }
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static float getNorm(int i) { return fx->getParameter(fx, i); }
static std::string display(int i) { char d[64] = {}; D(effGetParamDisplay, i, 0, d); return d; }
static float lufs(int i) { return getNorm(i) * 75.f - 70.f; }                    // M, S, I, max: -70 .. +5
static float tpDb() { return getNorm(P_TP) * 82.f - 70.f; }                       // -70 .. +12
static void press(int i) { setNorm(i, 1.f); }

static void feed(std::vector<float>& l, std::vector<float>& r, int block = 512) {
    std::vector<float> ol(l.size()), orr(r.size());
    for (size_t pos = 0; pos < l.size(); pos += block) {
        int m = (int) std::min((size_t) block, l.size() - pos);
        float* in[2] = { &l[pos], &r[pos] }; float* out[2] = { &ol[pos], &orr[pos] };
        fx->processReplacing(fx, in, out, m);
    }
}
static void sine(float dbfs, float seconds, float hz = 997.f, bool stereo = true) {
    const size_t n = (size_t) (seconds * SR); std::vector<float> l(n), r(n);
    const float a = std::pow(10.f, dbfs / 20.f);
    for (size_t i = 0; i < n; ++i) { l[i] = a * std::sin(6.2831853f * hz * i / SR); r[i] = stereo ? l[i] : 0.f; }
    feed(l, r);
}
static void fresh(int platform = 0) { D(effSetProgram, 0, platform); press(P_RESET); std::vector<float> z(64, 0.f), z2(64, 0.f); feed(z, z2); }

int main(int argc, char** argv) {
    void* h = dlopen(argc > 1 ? argv[1] : "build/native/dalufsplug.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic, "magic");
    CHECK(fx->uniqueID == 0x444c5047, "uid %08x", fx->uniqueID);
    CHECK(fx->numParams == P_COUNT && fx->numPrograms == NPROG, "counts %d/%d", fx->numParams, fx->numPrograms);
    CHECK(fx->initialDelay == 0, "no latency");
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effMainsChanged, 0, 1);

    std::printf("Platforms:\n");
    for (int i = 0; i < NPROG; ++i) {
        char n[64] = {}; D(effGetProgramNameIndexed, i, 0, n); D(effSetProgram, 0, i);
        std::printf("  %2d %-20s target %s LUFS, ceiling %s dBTP\n", i, n, display(P_TARGET).c_str(), display(P_CEIL).c_str());
        CHECK(std::strlen(n) > 0, "program %d name", i);
    }
    for (int i = 0; i < P_COUNT; ++i) { char n[64] = {}; D(effGetParamName, i, 0, n); CHECK(std::strlen(n) > 0 && !display(i).empty(), "param %d", i); }

    // ---- pass-through is bit-exact ----
    {
        fresh(); std::vector<float> l(9000), r(9000), ol(9000), orr(9000); unsigned s = 5;
        for (int i = 0; i < 9000; ++i) { s = s * 1664525u + 1013904223u; l[i] = (float) ((int) (s >> 9) - (1 << 22)) / (1 << 22); r[i] = 0.3f * l[i]; }
        float* in[2] = { l.data(), r.data() }; float* out[2] = { ol.data(), orr.data() };
        fx->processReplacing(fx, in, out, 9000);
        CHECK(ol == l && orr == r, "audio passes through untouched");
    }

    // ---- EBU Tech 3341 case 1/2: stereo 1 kHz sine at -23 / -33 dBFS reads -23 / -33 LUFS on M, S, I ----
    for (float lvl : { -23.f, -33.f }) {
        fresh(); sine(lvl, 12.f);
        std::printf("%.0f dBFS: M %.2f S %.2f I %.2f LRA %.2f TP %.2f time %s\n", lvl, lufs(P_M), lufs(P_S), lufs(P_I), getNorm(P_LRA) * 40, tpDb(), display(P_TIME).c_str());
        CHECK(std::fabs(lufs(P_M) - lvl) < 0.1f && std::fabs(lufs(P_S) - lvl) < 0.1f && std::fabs(lufs(P_I) - lvl) < 0.1f, "levels at %.0f", lvl);
        CHECK(getNorm(P_LRA) * 40 < 0.2f, "steady tone LRA ~0");
        CHECK(std::fabs(tpDb() - lvl) < 0.1f, "true peak of a plain sine (%.2f)", tpDb());
        CHECK(display(P_TIME) == "00:00:12", "time %s", display(P_TIME).c_str());
        CHECK(std::fabs(lufs(P_MAXM) - lvl) < 0.1f && std::fabs(lufs(P_MAXS) - lvl) < 0.1f, "maxima");
    }
    // ---- Tech 3341 case 3-style gating: -36 / -23 / -36 dBFS (10 / 20 / 10 s) integrates to -23 ----
    {
        fresh(); sine(-36, 10); sine(-23, 20); sine(-36, 10);
        CHECK(std::fabs(lufs(P_I) + 23) < 0.1f, "relative gate: integrated %.2f", lufs(P_I));
        CHECK(std::fabs(lufs(P_MAXM) + 23) < 0.1f && std::fabs(lufs(P_M) + 36) < 0.1f, "max momentary / momentary");
        fresh(); sine(-23, 10); std::vector<float> z((size_t) (10 * SR), 0.f), z2 = z; feed(z, z2);
        CHECK(std::fabs(lufs(P_I) + 23) < 0.1f && lufs(P_M) < -69.f, "silence gated out (I %.2f)", lufs(P_I));
    }
    // ---- one channel only: -3.01 LU (997 Hz, 0 dBFS in one channel = -3.01 LUFS) ----
    { fresh(); sine(0, 5, 997.f, false); CHECK(std::fabs(lufs(P_I) + 3.01f) < 0.1f, "mono 0 dBFS = %.2f LUFS", lufs(P_I)); }
    // ---- EBU Tech 3342 case 1: 20 s at -20 then 20 s at -30 dBFS -> LRA 10 LU ----
    {
        fresh(); sine(-20, 20); sine(-30, 20);
        CHECK(std::fabs(getNorm(P_LRA) * 40 - 10) < 1.f, "LRA %.2f LU (want 10)", getNorm(P_LRA) * 40);
        fresh(); sine(-20, 20); sine(-15, 20);
        CHECK(std::fabs(getNorm(P_LRA) * 40 - 5) < 1.f, "LRA %.2f LU (want 5)", getNorm(P_LRA) * 40);
    }
    // ---- true peak: sine at fs/4 with 45 degree phase: samples at 0.707, true peak 0 dBTP ----
    for (float rate : { 44100.f, 48000.f, 96000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, SR); fresh();
        const size_t n = (size_t) SR; std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.98f * std::sin(1.5707963f * i + 0.7853982f);     // true peak -0.18 dBTP
        feed(l, r);
        CHECK(std::fabs(tpDb() + 0.18f) < 0.4f, "%.0f Hz: inter-sample true peak %.2f dBTP (sample peak -3.2)", rate, tpDb());
    }
    SR = 44100.f; D(effSetSampleRate, 0, 0, nullptr, SR);
    for (float rate : { 48000.f, 88200.f, 192000.f }) {
        SR = rate; D(effSetSampleRate, 0, 0, nullptr, SR); fresh(); sine(-20, 5);
        CHECK(std::fabs(lufs(P_I) + 20) < 0.1f, "%.0f Hz: integrated %.2f", rate, lufs(P_I));
    }
    SR = 44100.f; D(effSetSampleRate, 0, 0, nullptr, SR);

    // ---- platforms, status lines ----
    {
        struct Case { int platform; float level; const char* status; };
        const Case cases[] = { { 0, -20, "Turned up 6.0 dB" }, { 0, -10, "Turned down 4.0 dB" }, { 0, -14, "On target" },
                               { 4, -20, "Plays 6.0 dB quieter" }, { 4, -8, "Turned down 6.0 dB" }, { 3, -20, "Turned up 4.0 dB" },
                               { 2, -16, "Turned down 3.0 dB" }, { 7, -16, "Turned down 2.0 dB" },
                               { 9, -10, "Not normalized: plays as is" }, { 10, -11.5f, "On target" }, { 10, -8, "2.5 LU above target" },
                               { 13, -26, "3.0 LU below target" } };
        for (const Case& c : cases) {
            fresh(c.platform); sine(c.level, 4);
            char n[64] = {}; D(effGetProgramName, 0, 0, n);
            CHECK(display(P_STATUS) == c.status, "%s at %.0f: '%s' (want '%s')", n, c.level, display(P_STATUS).c_str(), c.status);
        }
        fresh(0); sine(-14, 4);
        CHECK(std::fabs(getNorm(P_GAIN) * 80 - 40) < 0.1f, "gain to target 0 on target");
        CHECK(display(P_TPSTATUS) == "Peaks OK, 13.0 dB headroom", "tp status '%s'", display(P_TPSTATUS).c_str());
        fresh(0); sine(-6, 4);                                      // Spotify: a master louder than -14 gets the -2 dBTP ceiling
        CHECK(display(P_TPSTATUS) == "Peaks OK, 4.0 dB headroom", "spotify ok '%s'", display(P_TPSTATUS).c_str());
        fresh(0); sine(-1.5f, 4);
        CHECK(display(P_TPSTATUS) == "Loud master: peaks 0.5 over -2" && getNorm(P_TPOVER) == 1.f, "spotify loud master '%s'", display(P_TPSTATUS).c_str());
        fresh(5); sine(-1, 4);                                      // Amazon: ceiling -2
        CHECK(display(P_TPSTATUS) == "Peaks over ceiling by 1.0 dB" && getNorm(P_TPOVER) == 1.f, "tp over '%s'", display(P_TPSTATUS).c_str());
        D(effSetProgram, 0, 3); CHECK(display(P_TARGET) == "-16.0" && display(P_CEIL) == "-1.0", "Apple Music target");
        setNorm(P_TARGET, (-12.f + 30.f) / 25.f); D(effSetProgram, 0, 16);
        CHECK(display(P_TARGET) == "-12.0", "Custom keeps your target (%s)", display(P_TARGET).c_str());
        D(effSetProgram, 0, 0); press(P_NEXT); char n[64] = {}; D(effGetProgramName, 0, 0, n);
        CHECK(std::strcmp(n, "Spotify Loud") == 0 && getNorm(P_NEXT) == 0.f, "next platform (%s)", n);
        press(P_PREV); press(P_PREV); D(effGetProgramName, 0, 0, n);
        CHECK(std::strcmp(n, "Custom") == 0, "prev wraps (%s)", n);
        setNorm(P_PLATFORM, 4.f / (NPROG - 1)); D(effGetProgramName, 0, 0, n);
        CHECK(std::strcmp(n, "YouTube / YT Music") == 0, "platform parameter (%s)", n);
    }

    // ---- reset, pause, history, meter bar ----
    {
        fresh(0); sine(-18, 8);
        const float rel = getNorm(P_REL) * 36 - 18;
        CHECK(std::fabs(rel + 4) < 0.2f, "meter bar vs target %.2f LU", rel);
        const float last = getNorm(P_HIST0 + 59) * 24.2f - 12.2f, first = getNorm(P_HIST0);
        CHECK(std::fabs(last + 4) < 0.3f && first == 0.f, "history: newest %.2f LU, oldest blank", last);
        setNorm(P_PAUSE, 1.f); sine(-10, 3);
        CHECK(display(P_TIME) == "00:00:08" && std::fabs(lufs(P_I) + 18) < 0.1f && display(P_STATUS) == "Paused", "pause");
        setNorm(P_PAUSE, 0.f); press(P_RESET); sine(-30, 0.05f);
        CHECK(lufs(P_I) < -69.f && display(P_TIME) == "00:00:00" && display(P_STATUS) == "Measuring...", "reset (%s)", display(P_STATUS).c_str());
        automateCalls = 0; sine(-20, 10);
        CHECK(automateCalls < 2500, "display traffic %d calls in 10 s", automateCalls);
    }

    D(effClose);
    if (fails == 0) std::printf("ALL TESTS PASSED\n"); else std::printf("%d FAILURES\n", fails);
    return fails ? 1 : 0;
}
