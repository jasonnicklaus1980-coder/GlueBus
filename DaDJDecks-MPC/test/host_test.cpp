// Offline VST2 host test for Da DJ Decks. Writes test WAVs (16/24-bit, float, 44.1/48 kHz, known tempos and beat
// positions) to a folder, points the plugin at it (DJ_FOLDER) and drives it like MPC: library scan, loading,
// BPM + beat grid detection, play / cue / pitch / sync / nudge / loops, EQ kills, filter, crossfader, master,
// MPC input pass-through, end of track, project restore, bad files.
#include "vst2.h"
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

enum { D_TRACK, D_LOAD, D_PLAY, D_CUE, D_PITCH, D_RANGE, D_SYNC, D_NUDGE_DN, D_NUDGE_UP, D_LOOP, D_GAIN, D_HIGH, D_MID, D_LOW,
       D_FILTER, D_FADER, D_LOADED, D_TIME, D_REMAIN, D_BPM, D_PLATTER, D_PROGRESS, D_VU, D_STATUS, D_BEAT, D_COUNT };
static int dp(int d, int w) { return d * D_COUNT + w; }
enum { M_XFADE = 2 * D_COUNT, M_CURVE, M_MASTER, M_INPUT, M_RESCAN, M_VU_L, M_VU_R, M_LIBRARY, P_COUNT };

static intptr_t host(AEffect*, int32_t, int32_t, intptr_t, void*, float) { return 2400; }
static AEffect* fx;
static const float SR = 44100.f;
static intptr_t D(int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher(fx, op, idx, val, p, opt); }
static void setNorm(int i, float n) { fx->setParameter(fx, i, n); }
static float getNorm(int i) { return fx->getParameter(fx, i); }
static std::string display(int i) { char d[128] = {}; D(effGetParamDisplay, i, 0, d); return d; }
static void press(int i) { setNorm(i, 1.f); }
static float timeOf(int d) { return getNorm(dp(d, D_TIME)) * 3600.f; }
static float bpmOf(int d) { return getNorm(dp(d, D_BPM)) * 400.f; }

// ---- audio through the plugin ----
static std::vector<float> lastL, lastR;
static void run(float seconds, float inputLevel = 0.f) {
    const int n = (int) (seconds * SR); lastL.assign(n, 0.f); lastR.assign(n, 0.f);
    std::vector<float> il(n), ir(n);
    for (int i = 0; i < n; ++i) il[i] = ir[i] = inputLevel * std::sin(0.03f * i);
    for (int pos = 0; pos < n; pos += 512) {
        const int m = std::min(512, n - pos);
        float* in[2] = { &il[pos], &ir[pos] }; float* out[2] = { &lastL[pos], &lastR[pos] };
        fx->processReplacing(fx, in, out, m);
    }
}
static double rms(const std::vector<float>& v) { double s = 0; for (float x : v) s += (double) x * x; return std::sqrt(s / std::max<size_t>(1, v.size())); }
static double dbv(double x) { return 20 * std::log10(x + 1e-12); }
static bool waitFor(int param, const std::string& want, float seconds = 5.f) {
    for (int i = 0; i < (int) (seconds * 100); ++i) {
        if (display(param).find(want) != std::string::npos) return true;
        run(0.01f); usleep(10000);
    }
    return false;
}

// ---- test WAVs: a kick on every beat, a hat on the off-beats, first beat at `first` seconds ----
static void writeWav(const std::string& path, float bpm, float seconds, int rate, int bits, int channels, bool isFloat, float first) {
    const long n = (long) (seconds * rate);
    std::vector<double> x(n, 0.0);
    unsigned seed = 7;
    const double beat = 60.0 / bpm;
    for (double t0 = first; t0 < seconds; t0 += beat) {
        const long s0 = (long) (t0 * rate);
        for (long i = 0; i < (long) (0.25 * rate) && s0 + i < n; ++i) {
            const double t = (double) i / rate;
            x[s0 + i] += 0.8 * std::exp(-t * 18) * std::sin(2 * M_PI * (55 + 90 * std::exp(-t * 40)) * t);
        }
        const long h0 = (long) ((t0 + beat / 2) * rate);
        for (long i = 0; i < (long) (0.04 * rate) && h0 + i < n; ++i) {
            seed = seed * 1664525u + 1013904223u;
            x[h0 + i] += 0.15 * std::exp(-(double) i / rate * 90) * ((double) (seed >> 8) / 8388608.0 - 1.0);
        }
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    const int bps = bits / 8; const uint32_t dataBytes = (uint32_t) (n * channels * bps);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(isFloat ? 3 : 1); w16((uint16_t) channels); w32((uint32_t) rate); w32((uint32_t) (rate * channels * bps)); w16((uint16_t) (channels * bps)); w16((uint16_t) bits);
    std::fwrite("data", 1, 4, f); w32(dataBytes);
    for (long i = 0; i < n; ++i)
        for (int c = 0; c < channels; ++c) {
            const double v = std::max(-1.0, std::min(1.0, x[i]));
            if (isFloat) { float fl = (float) v; std::fwrite(&fl, 4, 1, f); }
            else if (bits == 16) { int16_t s = (int16_t) std::lround(v * 32767); std::fwrite(&s, 2, 1, f); }
            else { int32_t s = (int32_t) std::lround(v * 8388607); unsigned char b[3] = { (unsigned char) s, (unsigned char) (s >> 8), (unsigned char) (s >> 16) }; std::fwrite(b, 1, 3, f); }
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    const std::string dir = "build/native/djtest";
    mkdir(dir.c_str(), 0755); mkdir((dir + "/sub").c_str(), 0755);
    std::remove((dir + "/e_new 110.wav").c_str());                          // left over from an earlier run
    writeWav(dir + "/a_track 120.wav", 120, 30, 44100, 16, 2, false, 0.25f);
    writeWav(dir + "/b_track 95.wav", 95, 20, 48000, 24, 1, false, 0.10f);
    writeWav(dir + "/c float 128.wav", 128, 12, 44100, 32, 2, true, 0.0f);
    writeWav(dir + "/sub/d_sub 100.wav", 100, 10, 44100, 16, 2, false, 0.3f);
    { FILE* f = std::fopen((dir + "/bad.wav").c_str(), "wb"); std::fputs("this is not audio", f); std::fclose(f); }
    { FILE* f = std::fopen((dir + "/notes.txt").c_str(), "wb"); std::fputs("x", f); std::fclose(f); }
    setenv("DJ_FOLDER", dir.c_str(), 1);

    void* h = dlopen(argc > 1 ? argv[1] : "build/native/dadjdecks.so", RTLD_NOW);
    if (!h) { std::printf("dlopen: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*)(audioMasterCallback)) dlsym(h, "VSTPluginMain");
    fx = entry(host);
    CHECK(fx && fx->magic == kEffectMagic && fx->uniqueID == 0x444a444b, "plugin");
    CHECK(fx->numParams == P_COUNT, "params %d", fx->numParams);
    D(effOpen); D(effSetSampleRate, 0, 0, nullptr, SR); D(effMainsChanged, 0, 1);
    for (int i = 0; i < P_COUNT; ++i) { char n[64] = {}; D(effGetParamName, i, 0, n); CHECK(std::strlen(n) > 0 && !display(i).empty(), "param %d", i); }

    // ---- library ----
    CHECK(waitFor(M_LIBRARY, "5 tracks"), "library scan: '%s'", display(M_LIBRARY).c_str());
    auto select = [](int d, int idx) { setNorm(dp(d, D_TRACK), (idx + 0.5f) / 5.f); };
    const char* order[] = { "a_track 120", "b_track 95", "bad", "c float 128", "sub/d_sub 100" };
    for (int i = 0; i < 5; ++i) { select(0, i); CHECK(display(dp(0, D_TRACK)) == std::to_string(i + 1) + ". " + order[i], "entry %d: %s", i, display(dp(0, D_TRACK)).c_str()); }

    // ---- MPC input passes straight through while nothing is loaded ----
    {
        const int n = 4096; std::vector<float> l(n), r(n), ol(n), orr(n);
        for (int i = 0; i < n; ++i) { l[i] = std::sin(i * 0.01f) * 0.5f; r[i] = -l[i]; }
        float* in[2] = { l.data(), r.data() }; float* out[2] = { ol.data(), orr.data() };
        fx->processReplacing(fx, in, out, n);
        CHECK(ol == l && orr == r, "MPC input passes through untouched");
    }

    // ---- loading + beat detection ----
    select(0, 0); press(dp(0, D_LOAD));
    CHECK(waitFor(dp(0, D_STATUS), "a_track 120"), "deck A loads: '%s'", display(dp(0, D_STATUS)).c_str());
    select(1, 1); press(dp(1, D_LOAD));
    CHECK(waitFor(dp(1, D_STATUS), "b_track 95"), "deck B loads: '%s'", display(dp(1, D_STATUS)).c_str());
    run(0.1f);
    std::printf("Deck A: %s BPM, Deck B: %s BPM\n", display(dp(0, D_BPM)).c_str(), display(dp(1, D_BPM)).c_str());
    CHECK(std::fabs(bpmOf(0) - 120) < 0.15f && std::fabs(bpmOf(1) - 95) < 0.15f, "BPM detection %.2f / %.2f", bpmOf(0), bpmOf(1));
    CHECK(std::fabs(timeOf(0) - 0.25f) < 0.03f, "deck A cued at its first beat (%.3f s)", timeOf(0));
    CHECK(std::fabs(timeOf(1) - 0.10f) < 0.03f, "deck B cued at its first beat (%.3f s)", timeOf(1));
    CHECK(getNorm(dp(0, D_LOADED)) * 256 > 0.5f, "loaded track remembered for the project");
    { std::vector<float> z; run(0.5f); CHECK(rms(lastL) == 0.0, "stopped decks are silent"); }

    // ---- play, pitch ----
    press(dp(0, D_PLAY)); run(2.0f);
    CHECK(rms(lastL) > 0.01, "deck A plays (%.1f dB)", dbv(rms(lastL)));
    CHECK(std::fabs(timeOf(0) - 2.25f) < 0.06f, "time advances (%.2f)", timeOf(0));
    CHECK(getNorm(dp(0, D_PLATTER)) > 0.f && getNorm(dp(0, D_PROGRESS)) > 0.05f, "platter spins, progress moves");
    setNorm(dp(0, D_PLAY), 0.f); run(0.5f);
    const float t0 = timeOf(0);
    setNorm(dp(0, D_RANGE), 0.f); setNorm(dp(0, D_PITCH), 1.f); setNorm(dp(0, D_PLAY), 1.f); run(5.0f); setNorm(dp(0, D_PLAY), 0.f);
    CHECK(std::fabs(timeOf(0) - t0 - 5.4f) < 0.06f, "+8 %% pitch plays 8 %% faster (%.2f s in 5 s)", timeOf(0) - t0);
    CHECK(display(dp(0, D_BPM)) == "129.6" && display(dp(0, D_PITCH)) == "+8.00 %", "BPM follows pitch (%s, %s)", display(dp(0, D_BPM)).c_str(), display(dp(0, D_PITCH)).c_str());
    setNorm(dp(0, D_PITCH), 0.5f);                                           // back to 0 %

    // ---- cue ----
    press(dp(0, D_CUE)); run(0.1f);                                         // stopped: sets the cue here
    const float cueAt = timeOf(0);
    setNorm(dp(0, D_PLAY), 1.f); run(3.0f); press(dp(0, D_CUE)); run(0.1f);
    CHECK(getNorm(dp(0, D_PLAY)) == 0.f && std::fabs(timeOf(0) - cueAt) < 0.02f, "CUE while playing returns to the cue point and stops (%.2f vs %.2f)", timeOf(0), cueAt);

    // ---- sync: B (95) to A (120): tempo and beats ----
    setNorm(dp(0, D_PLAY), 1.f); run(1.3f);
    press(dp(1, D_SYNC)); run(0.05f);
    CHECK(display(dp(1, D_BPM)) == "120.0" && display(dp(1, D_RANGE)) == "+-50 %", "sync tempo: %s BPM, range %s", display(dp(1, D_BPM)).c_str(), display(dp(1, D_RANGE)).c_str());
    setNorm(dp(1, D_PLAY), 1.f); run(4.0f);
    {
        const float a = getNorm(dp(0, D_BEAT)) * 4, b = getNorm(dp(1, D_BEAT)) * 4;          // beats within the bar
        double diff = std::fmod(a - b, 1.0); if (diff < 0) diff += 1; if (diff > 0.5) diff -= 1;
        CHECK(std::fabs(diff) < 0.06, "beats line up after SYNC (off by %.3f beat)", diff);
    }
    // nudge
    { const float before = timeOf(1); setNorm(dp(1, D_PLAY), 0.f); run(0.05f); const float b0 = timeOf(1);
      press(dp(1, D_NUDGE_UP)); press(dp(1, D_NUDGE_UP)); run(0.05f);
      CHECK(std::fabs(timeOf(1) - b0 - 0.04f) < 0.005f, "nudge + moves 20 ms per press (%.3f)", timeOf(1) - b0); (void) before; }

    // ---- loop ----
    setNorm(dp(0, D_LOOP), 1.f / 5.f); run(0.1f);                           // 1 beat
    { float lo = 1e9, hi = -1e9; for (int i = 0; i < 60; ++i) { run(0.05f); lo = std::min(lo, timeOf(0)); hi = std::max(hi, timeOf(0)); }
      CHECK(hi - lo <= 0.52f && hi - lo > 0.4f, "1-beat loop stays inside 0.5 s (%.2f .. %.2f)", lo, hi); }
    setNorm(dp(0, D_LOOP), 0.f); run(1.0f);
    { const float a = timeOf(0); run(1.0f); CHECK(timeOf(0) - a > 0.9f, "loop off: plays on"); }

    // ---- crossfader, EQ kill, filter, master ----
    setNorm(dp(1, D_PLAY), 0.f); setNorm(M_XFADE, 0.f); run(1.0f);                          // A only, full left
    const double full = rms(lastL);
    setNorm(dp(0, D_LOW), 0.f); run(1.0f); const double noLow = rms(lastL);
    CHECK(dbv(full) - dbv(noLow) > 6, "low kill takes the kick out (%.1f -> %.1f dB)", dbv(full), dbv(noLow));
    CHECK(display(dp(0, D_LOW)) == "Kill", "kill display");
    setNorm(dp(0, D_LOW), 0.8f); setNorm(dp(0, D_FILTER), 1.f); run(1.0f);
    CHECK(dbv(full) - dbv(rms(lastL)) > 10, "high-pass filter at 100 %% thins it out (%.1f dB)", dbv(rms(lastL)));
    setNorm(dp(0, D_FILTER), 0.5f); setNorm(M_XFADE, 1.f); run(0.2f); run(1.0f);
    CHECK(dbv(rms(lastL)) < -100, "crossfader on B mutes A (%.1f dB)", dbv(rms(lastL)));
    setNorm(M_XFADE, 0.5f); setNorm(M_CURVE, 1.f); run(1.0f);
    CHECK(std::fabs(dbv(rms(lastL)) - dbv(full)) < 1.5, "centre, cut curve: A at full level");
    setNorm(M_MASTER, 0.f); run(0.2f); run(0.5f); CHECK(dbv(rms(lastL)) < -100, "master off");
    setNorm(M_MASTER, 60.f / 66.f);
    { run(0.2f, 0.5f); } // input + decks together
    setNorm(M_INPUT, 0.f); setNorm(dp(0, D_PLAY), 0.f); run(0.3f); run(0.2f, 0.5f);
    CHECK(rms(lastL) == 0.0, "MPC IN at 0 mutes the input");
    setNorm(M_INPUT, 1.f);

    // ---- bad file, end of track, project restore ----
    select(1, 2); press(dp(1, D_LOAD));
    CHECK(waitFor(dp(1, D_STATUS), "Can't load: not a WAV file"), "bad file: '%s'", display(dp(1, D_STATUS)).c_str());
    select(1, 3); press(dp(1, D_LOAD));
    CHECK(waitFor(dp(1, D_STATUS), "c float 128"), "float WAV loads");
    setNorm(dp(1, D_PITCH), 0.5f); run(0.05f); CHECK(std::fabs(bpmOf(1) - 128) < 0.15f, "float track %.2f BPM", bpmOf(1));
    setNorm(dp(1, D_PITCH), 0.5f); setNorm(dp(1, D_PLAY), 1.f); run(13.f);
    CHECK(getNorm(dp(1, D_PLAY)) == 0.f, "stops at the end of the track");
    setNorm(dp(0, D_LOADED), 5.f / 256.f);                                 // restore: "#5" = sub/d_sub 100
    CHECK(waitFor(dp(0, D_STATUS), "sub/d_sub 100"), "project restore reloads the saved track: '%s'", display(dp(0, D_STATUS)).c_str());
    run(0.05f); CHECK(std::fabs(bpmOf(0) - 100) < 0.15f && getNorm(dp(0, D_PLAY)) == 0.f, "restored track stopped, %.2f BPM", bpmOf(0));

    // ---- rescan picks up new files ----
    writeWav(dir + "/e_new 110.wav", 110, 6, 44100, 16, 2, false, 0.f);
    press(M_RESCAN);
    CHECK(waitFor(M_LIBRARY, "6 tracks"), "rescan: '%s'", display(M_LIBRARY).c_str());

    // ---- sample rate change keeps tempo ----
    D(effSetSampleRate, 0, 0, nullptr, 48000.f);
    select(0, 0); press(dp(0, D_LOAD)); waitFor(dp(0, D_STATUS), "a_track 120"); run(0.05f);
    { const float a = timeOf(0); setNorm(dp(0, D_PLAY), 1.f); run(2.0f * 48000.f / SR); CHECK(std::fabs(timeOf(0) - a - 2.0f) < 0.06f, "48 kHz host: real-time playback (%.2f)", timeOf(0) - a); }

    D(effClose);
    if (fails == 0) std::printf("ALL TESTS PASSED\n"); else std::printf("%d FAILURES\n", fails);
    return fails ? 1 : 0;
}
