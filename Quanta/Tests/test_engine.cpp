// Quanta engine tests: plain C++, no framework. Build with the Makefile in this folder.
#include "../DSP/QuantaEngine.h"
#include "../DSP/QuantaPresets.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>

using namespace quanta;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double kPi = 3.14159265358979323846;

// Renders a sine (per channel phase offset) through an engine with the given settings.
static void render(Engine& e, std::vector<float>& L, std::vector<float>& R, double sr, double hz, float amp, float phaseR) {
    for (size_t i = 0; i < L.size(); ++i) {
        L[i] = amp * (float)std::sin(2 * kPi * hz * (double)i / sr);
        R[i] = amp * (float)std::sin(2 * kPi * hz * (double)i / sr + phaseR);
    }
    for (size_t i = 0; i < L.size(); i += 256) {
        size_t n = L.size() - i < 256 ? L.size() - i : 256;
        e.process(&L[i], &R[i], (int)n);
    }
}

static float rms(const std::vector<float>& v, size_t from, size_t to) {
    double s = 0; for (size_t i = from; i < to; ++i) s += (double)v[i] * v[i];
    return (float)std::sqrt(s / (double)(to - from));
}

static void neutral(Engine& e) {
    e.setParam(P_PARTICLE, 0.f); e.setParam(P_TONE, 0.5f); e.setParam(P_DENSITY, 0.f); e.setParam(P_GLUE, 0.f);
    e.setParam(P_WIDTH, 0.5f); e.setParam(P_LOWEND, 0.f); e.setParam(P_AIR, 0.f); e.setParam(P_MIX, 1.f);
    e.setParam(P_OUTPUT, 0.5f); e.setParam(P_MONOBASS, 0.f);
}

static void test_neutral_passthrough(double sr) {
    Engine e; e.prepare(sr); neutral(e);
    const size_t N = (size_t)sr;
    std::vector<float> L(N), R(N), L0(N), R0(N);
    render(e, L, R, sr, 1000.0, 0.3f, 0.f);
    for (size_t i = 0; i < N; ++i) { L0[i] = 0.3f * (float)std::sin(2 * kPi * 1000.0 * (double)i / sr); R0[i] = L0[i]; }
    double maxd = 0;
    for (size_t i = N / 2; i < N; ++i) maxd = std::fmax(maxd, std::fabs(L[i] - L0[i]));
    CHECK(maxd < 1e-3, "neutral settings pass a sine through at %.0f Hz (max error %g)", sr, maxd);
}

static void test_no_nonfinite_and_bounded(double sr) {
    Engine e; e.prepare(sr);
    e.setParam(P_PARTICLE, 1.f); e.setParam(P_DENSITY, 1.f); e.setParam(P_GLUE, 1.f); e.setParam(P_LOWEND, 1.f);
    e.setParam(P_AIR, 1.f); e.setParam(P_TONE, 1.f); e.setParam(P_WIDTH, 1.f); e.setParam(P_OUTPUT, 1.f); e.setParam(P_MIX, 1.f);
    std::srand(7);
    bool finite = true; float maxOut = 0.f;
    std::vector<float> L(256), R(256);
    for (int blk = 0; blk < 4000; ++blk) {
        for (int i = 0; i < 256; ++i) {
            float amp = (blk % 500 < 250) ? 4.0f : 0.001f;  // hot bursts and near-silence
            L[i] = amp * ((float)std::rand() / RAND_MAX * 2.f - 1.f);
            R[i] = amp * ((float)std::rand() / RAND_MAX * 2.f - 1.f);
        }
        e.process(L.data(), R.data(), 256);
        for (int i = 0; i < 256; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) finite = false;
            maxOut = std::fmax(maxOut, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
        }
    }
    CHECK(finite, "no NaN or infinity at %.0f Hz with every control at maximum", sr);
    CHECK(maxOut <= 1.0f, "peak control keeps output under full scale at %.0f Hz (max %.3f)", sr, maxOut);
}

static void test_mono_width(double sr) {
    Engine e; e.prepare(sr); neutral(e);
    e.setParam(P_WIDTH, 0.f);
    const size_t N = (size_t)sr / 2;
    std::vector<float> L(N), R(N);
    render(e, L, R, sr, 700.0, 0.4f, 1.1f);  // out-of-phase content at a phase offset
    double maxd = 0;
    for (size_t i = N / 2; i < N; ++i) maxd = std::fmax(maxd, std::fabs(L[i] - R[i]));  // after the 10 ms width ramp
    CHECK(maxd < 1e-5, "width 0 gives identical channels (max difference %g)", maxd);
}

static void test_mix_zero_is_dry(double sr) {
    Engine e; e.prepare(sr); neutral(e);
    e.setParam(P_MIX, 0.f); e.setParam(P_DENSITY, 1.f); e.setParam(P_GLUE, 1.f); e.setParam(P_AIR, 1.f);
    const size_t N = (size_t)sr / 4;
    std::vector<float> L(N), R(N);
    render(e, L, R, sr, 300.0, 0.25f, 0.f);
    double maxd = 0;
    for (size_t i = N / 2; i < N; ++i) {
        float dry = 0.25f * (float)std::sin(2 * kPi * 300.0 * (double)i / sr);
        maxd = std::fmax(maxd, std::fabs(L[i] - dry));
    }
    CHECK(maxd < 1e-3, "mix 0 passes the dry signal (max error %g)", maxd);
}

static void test_tone_and_air_move_the_spectrum(double sr) {
    auto levelAt = [&](float tone, float air, double hz) {
        Engine e; e.prepare(sr); neutral(e);
        e.setParam(P_TONE, tone); e.setParam(P_AIR, air);
        const size_t N = (size_t)sr / 2;
        std::vector<float> L(N), R(N);
        render(e, L, R, sr, hz, 0.2f, 0.f);
        return rms(L, N / 2, N);
    };
    float lowDark = levelAt(0.f, 0.f, 150.0), lowBright = levelAt(1.f, 0.f, 150.0);
    float highDark = levelAt(0.f, 0.f, 6000.0), highBright = levelAt(1.f, 0.f, 6000.0);
    CHECK(lowDark > lowBright, "Tone 0 is darker at 150 Hz (%.4f vs %.4f)", lowDark, lowBright);
    CHECK(highBright > highDark, "Tone 1 is brighter at 6 kHz (%.4f vs %.4f)", highBright, highDark);
    float airOff = levelAt(0.5f, 0.f, 12000.0), airOn = levelAt(0.5f, 1.f, 12000.0);
    CHECK(airOn > airOff * 1.2f, "Air adds high-frequency level at 12 kHz (%.4f vs %.4f)", airOn, airOff);
}

static void test_glue_reduces_loud_material(double sr) {
    auto levelWith = [&](float glue) {
        Engine e; e.prepare(sr); neutral(e);
        e.setParam(P_GLUE, glue);
        const size_t N = (size_t)sr;
        std::vector<float> L(N), R(N);
        render(e, L, R, sr, 110.0, 0.25f, 0.f);
        return rms(L, N / 2, N);
    };
    float dry = levelWith(0.f), glued = levelWith(1.f);
    CHECK(glued < dry * 0.95f && glued > dry * 0.6f, "Glue 1 gently reduces a tone at -12 dBFS (%.3f vs %.3f)", glued, dry);
}

static void test_dc_removed(double sr) {
    Engine e; e.prepare(sr); neutral(e);
    e.setParam(P_DENSITY, 0.5f);
    const size_t N = (size_t)sr;
    std::vector<float> L(N), R(N);
    for (size_t i = 0; i < N; ++i) { L[i] = 0.2f + 0.1f * (float)std::sin(2 * kPi * 440.0 * (double)i / sr); R[i] = L[i]; }
    for (size_t i = 0; i < N; i += 256) e.process(&L[i], &R[i], (int)(N - i < 256 ? N - i : 256));
    double mean = 0; for (size_t i = N / 2; i < N; ++i) mean += L[i];
    mean /= (double)(N / 2);
    CHECK(std::fabs(mean) < 0.01, "DC offset is removed after settling (mean %.5f)", mean);
}

static void test_parameter_smoothing(double sr) {
    Engine e; e.prepare(sr); neutral(e);
    const size_t N = (size_t)sr / 2;
    std::vector<float> L(N, 0.25f), R(N, 0.25f);
    float maxStep = 0.f, prev = 0.f;
    for (size_t i = 0; i < N; i += 256) {
        if (i == N / 2) e.setParam(P_OUTPUT, 1.f);  // a full-scale step in output trim
        size_t n = N - i < 256 ? N - i : 256;
        e.process(&L[i], &R[i], (int)n);
        for (size_t k = 0; k < n; ++k) { if (i + k > 0) maxStep = std::fmax(maxStep, std::fabs(L[i + k] - prev)); prev = L[i + k]; }
    }
    CHECK(maxStep < 0.05f, "output trim changes smoothly (largest sample step %.4f)", maxStep);
}

static void test_meters_and_correlation(double sr) {
    Engine e; e.prepare(sr); neutral(e);
    const size_t N = (size_t)sr / 4;
    std::vector<float> L(N), R(N);
    render(e, L, R, sr, 500.0, 0.5f, 0.f);
    CHECK(e.inputPeak() > 0.45f && e.inputPeak() < 0.55f, "input peak meter reads the input (%.3f)", e.inputPeak());
    CHECK(e.correlation() > 0.99f, "identical channels correlate fully (%.3f)", e.correlation());
}

static void test_rates_and_cpu(void) {
    const double rates[] = {44100.0, 48000.0, 96000.0};
    for (double sr : rates) {
        test_neutral_passthrough(sr);
        test_no_nonfinite_and_bounded(sr);
        test_dc_removed(sr);
    }
    // benchmark: every control on, 10 seconds of stereo at 48 kHz
    Engine e; e.prepare(48000.0);
    for (int i = 0; i < P_COUNT; ++i) e.setParam(i, 0.6f);
    const int N = 48000 * 10;
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i) { L[i] = 0.3f * (float)std::sin(0.05 * i); R[i] = 0.3f * (float)std::sin(0.047 * i); }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i += 256) e.process(&L[i], &R[i], 256);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("benchmark: 10 s stereo at 48 kHz in %.1f ms (%.2f%% of one core, host machine)\n", secs * 1000.0, secs / 10.0 * 100.0);
    CHECK(secs < 10.0, "processes faster than real time on this machine");
}

static void test_presets(double sr) {
    CHECK(kPresetCount == 13, "13 presets: Custom and 12 factory presets (%d)", kPresetCount);
    for (int i = 0; i < kPresetCount; ++i) {
        for (int j = 0; j < i; ++j) CHECK(std::strcmp(presetName(i), presetName(j)) != 0, "duplicate preset name %s", presetName(i));
        CHECK(std::strlen(presetName(i)) <= 23 && presetName(i)[0], "preset name fits the screen (%s)", presetName(i));
    }
    float v[P_PRESET];
    CHECK(!presetValues(0, v), "Custom has no values to load");
    for (int i = 1; i < kPresetCount; ++i) {
        CHECK(presetValues(i, v), "%s has values", presetName(i));
        bool inRange = true;
        for (int k = 0; k < P_PRESET; ++k) if (!(v[k] >= 0.f && v[k] <= 1.f)) inRange = false;
        CHECK(inRange, "%s: every value is 0..1", presetName(i));
        Engine e; e.prepare(sr); e.loadPreset(i);
        for (int k = 0; k < P_PRESET; ++k) CHECK(std::fabs(e.getParam(k) - v[k]) < 1e-6f, "%s sets control %d", presetName(i), k);
        CHECK(std::fabs(e.getParam(P_PRESET) - (float)i / (float)(kPresetCount - 1)) < 1e-6f, "%s shows its own number", presetName(i));
        // a preset renders cleanly on a loud, dense input
        const size_t N = (size_t)sr;
        std::vector<float> L(N), R(N);
        std::srand(11);
        for (size_t k = 0; k < N; ++k) {
            float tone = 0.4f * (float)std::sin(2 * kPi * 110.0 * (double)k / sr);
            float noise = 0.3f * ((float)std::rand() / RAND_MAX * 2.f - 1.f);
            L[k] = tone + noise; R[k] = tone - noise;
        }
        float peak = 0.f; bool finite = true;
        for (size_t k = 0; k < N; k += 256) e.process(&L[k], &R[k], (int)(N - k < 256 ? N - k : 256));
        for (size_t k = 0; k < N; ++k) { if (!std::isfinite(L[k]) || !std::isfinite(R[k])) finite = false; peak = std::fmax(peak, std::fmax(std::fabs(L[k]), std::fabs(R[k]))); }
        CHECK(finite && peak <= 1.0f, "%s stays finite and under full scale (peak %.3f)", presetName(i), peak);
    }
    // character: relative settings that the names promise
    auto idx = [](const char* name) { for (int i = 0; i < kPresetCount; ++i) if (!std::strcmp(presetName(i), name)) return i; return -1; };
    float a[P_PRESET], b[P_PRESET];
    presetValues(idx("Warm and Dense"), a); presetValues(idx("Clean and Open"), b);
    CHECK(a[P_DENSITY] > b[P_DENSITY] && b[P_AIR] > a[P_AIR], "Warm and Dense is denser; Clean and Open is airier");
    presetValues(idx("Gentle Master"), a); presetValues(idx("Hip-Hop Mix Bus"), b);
    CHECK(a[P_PARTICLE] < b[P_PARTICLE] && a[P_GLUE] < b[P_GLUE], "Gentle Master is gentler than Hip-Hop Mix Bus");
}

int main() {
    test_neutral_passthrough(48000.0);
    test_no_nonfinite_and_bounded(48000.0);
    test_mono_width(48000.0);
    test_mix_zero_is_dry(48000.0);
    test_tone_and_air_move_the_spectrum(48000.0);
    test_glue_reduces_loud_material(48000.0);
    test_dc_removed(48000.0);
    test_parameter_smoothing(48000.0);
    test_meters_and_correlation(48000.0);
    test_rates_and_cpu();
    test_presets(48000.0);
    test_presets(44100.0);
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
