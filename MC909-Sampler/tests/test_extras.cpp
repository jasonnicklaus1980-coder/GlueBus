// Pad mapping, waveform editing, pitch accuracy and a CPU figure for the sampler.
#include "../src/padmap.h"
#include "../src/sampler.h"
#include "../src/waveedit.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace mc909;
static int checks = 0, fails = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Sample tone(double hz, double rate, uint64_t frames)
{
    Sample s; s.channels = 1; s.rate = (uint32_t)rate; s.data.resize(frames);
    for (uint64_t i = 0; i < frames; ++i) s.data[i] = (float)(0.5 * std::sin(2 * 3.14159265358979 * hz * i / rate));
    return s;
}
// Frequency from upward zero crossings over a span of output.
static double crossingHz(const float* x, int n, double rate)
{
    int c = 0, first = -1, last = -1;
    for (int i = 1; i < n; ++i) if (x[i - 1] < 0 && x[i] >= 0) { if (first < 0) first = i; last = i; ++c; }
    if (c < 2) return 0;
    return (c - 1) * rate / (last - first);
}

int main()
{
    // pad mapping: the MPC-style default and the chromatic map
    CHECK(padForNote(36) == 0 && padForNote(51) == 15 && padForNote(35) == -1 && padForNote(52) == -1, "the MPC-style map covers notes 36 to 51 as pads 0 to 15");
    CHECK(padForNote(60, PadMap::Chromatic, 60) == 0 && padForNote(75, PadMap::Chromatic, 60) == 15 && padForNote(76, PadMap::Chromatic, 60) == -1,
          "the chromatic map starts at its base note");

    // waveform editing: trim copies a window; the source is unchanged
    Sample src = tone(100, 44100, 1000);
    Sample cut = trimmed(src, 100, 300);
    CHECK(cut.frames() == 200 && src.frames() == 1000, "trim copies a window and leaves the source at full length");
    CHECK(std::fabs(cut.data[0] - src.data[100]) < 1e-9f, "a trimmed copy starts at the requested frame");
    CHECK(trimmed(src, 900, 5000).frames() == 100, "trim clamps to the end of the sample");

    // normalise: peak becomes the target, the source keeps its level
    Sample quiet = src; for (float& v : quiet.data) v *= 0.1f;
    Sample loud = normalized(quiet, 1.f);
    float m = 0; for (float v : loud.data) m = std::max(m, std::fabs(v));
    CHECK(std::fabs(m - 1.f) < 1e-4f, "normalise brings the peak to 1 (%.4f)", m);
    float qm = 0; for (float v : quiet.data) qm = std::max(qm, std::fabs(v));
    CHECK(qm < 0.06f, "the source of a normalised copy is unchanged");

    // waveform peaks: one min/max pair per bin, covering the sample
    auto pk = waveformPeaks(src, 10);
    bool negative = false; for (const auto& q : pk) if (q.min < -0.4f) negative = true;
    CHECK(pk.size() == 10 && pk[0].max > 0.4f && negative, "waveform peaks cover the sample (bin 0 max %.3f, some bin reaches negative)", pk[0].max);

    // pitch: +12 semitones doubles the frequency; -12 halves it; 0 keeps it (within the frequency read by zero crossings)
    const double rate = 44100.0;
    for (double st : {0.0, 12.0, -12.0})
    {
        Sampler s(rate);
        Sample t = tone(440, rate, 44100 * 2);
        s.addLayer(0, Layer{s.addSample(t)});
        s.pad(0).loop = true; s.pad(0).semitones = (float)st;
        s.noteOn(0, 1.f);
        std::vector<float> L(20000), R(20000);
        s.render(L.data(), R.data(), 20000);
        const double want = 440 * std::pow(2.0, st / 12.0);
        const double got = crossingHz(L.data() + 2000, 18000, rate);
        CHECK(std::fabs(got - want) / want < 0.01, "pitch %+.0f semitones: %.1f Hz read, %.1f Hz expected", st, got, want);
    }

    // CPU: 16 voices, stereo, 10 seconds at 44.1 kHz (reported, not asserted)
    {
        Sampler s(rate);
        for (int p = 0; p < 16; ++p) { s.addLayer(p, Layer{s.addSample(tone(200 + 40 * p, rate, 44100 * 2))}); s.pad(p).loop = true; s.noteOn(p, 0.8f); }
        const int block = 512, blocks = (int)(10 * rate / block);
        std::vector<float> L(block), R(block);
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) s.render(L.data(), R.data(), block);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("cpu: 16 looping voices, 10 s stereo at 44.1 kHz rendered in %.1f ms on this machine\n", sec * 1000);
        CHECK(sec < 10.0, "16 voices render faster than real time on this machine");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
