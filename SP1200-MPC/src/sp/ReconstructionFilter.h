#pragma once
// ReconstructionFilter: from DAC steps to the analog output signal (one channel).
//
// 1. Zero-order hold, rendered at the host rate. The hardware's held voltage is a staircase: its spectrum is the
//    sampled audio times sinc(f / 26.04 kHz), with images at 26.04 kHz +- f, 52.08 kHz +- f, ... and no low-pass of
//    its own. The SP's "crunch" is those images. RECON:
//      ANALOG ZOH   (hardware) each step is a band-limited step (BLEP: integrated windowed sinc, +-24 samples) placed
//                   at its exact sub-sample time. Every image below the host's Nyquist is kept, as on the hardware;
//                   images above it are removed, as any recording interface would. No new alias appears.
//      RAW STEPS    steps snapped to the host's sample grid (ECO): images above the host Nyquist fold back and the
//                   step timing jitters. Harsher than the hardware: the ALIAS "Enhanced" sound.
//      IDEAL        (non-hardware) ANALOG ZOH followed by an 8th-order low-pass at 0.45 x the sample clock: shows what
//                   the SP would sound like with a proper reconstruction filter.
// 2. Output channel filter (owners' measurements; the topology is the op-amp filter family the parts list implies):
//      Out 1-2  SSM2044 dynamic VCF (SSMModel)
//      Out 3-4  fixed low-pass ~7.5 kHz        Out 5-6  fixed low-pass ~10 kHz      Out 7-8  unfiltered
//    The fixed filters' order is not documented: two Sallen-Key sections (4th-order Butterworth) are assumed.
#include "Dsp.h"

namespace sp
{
struct BlepTable
{
    static constexpr int W = 24, kPhases = 64;          // +-24 samples (48 taps)
    float t[2 * W * kPhases + 1];
    // residual r(x) = bandlimited step(x) - unit step(x), x in [-W, W] host samples. Kaiser beta 8 (~80 dB stopband);
    // cutoff 0.885 x the host Nyquist: flat to ~17 kHz at 44.1 kHz, ~80 dB down from ~21.8 kHz, so a ZOH image
    // just above the host Nyquist can't fold back into the audio band.
    void build (double cutoff = 0.885)                  // fraction of the host Nyquist
    {
        const int N = 2 * W * kPhases;
        static double h[2 * W * kPhases + 1];
        double acc = 0;
        auto i0 = [] (double v) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (v / (2 * j)) * (v / (2 * j)); s += t; } return s; };
        for (int i = 0; i <= N; ++i)
        {
            const double x = (double) i / kPhases - W;
            const double wx = x / W;
            const double win = i0 (8.0 * std::sqrt (std::fmax (0.0, 1 - wx * wx))) / i0 (8.0);
            const double a = kPi * cutoff * x;
            h[i] = (std::fabs (a) < 1e-12 ? 1.0 : std::sin (a) / a) * win;
            acc += h[i];
        }
        double run = 0;
        for (int i = 0; i <= N; ++i)
        {
            run += h[i] / acc;
            const double x = (double) i / kPhases - W;
            t[i] = (float) (run - (x >= 0 ? 1.0 : 0.0));
        }
    }
    float at (double x) const
    {
        if (x <= -W || x >= W) return 0.f;
        const double p = (x + W) * kPhases; const int i = (int) p; const float u = (float) (p - i);
        return t[i] + (t[i + 1] - t[i]) * u;
    }
};

// Renders one channel's staircase with a fixed latency of W samples.
struct ZohRenderer
{
    static constexpr int W = BlepTable::W, kRing = 128;
    const BlepTable* blep = nullptr;
    float corr[kRing] {};
    float level = 0.f;                                  // current held value
    long n = 0;                                         // host sample being formed
    bool bandlimited = true;
    void reset() { std::memset (corr, 0, sizeof corr); level = 0.f; n = 0; }
    // a step to `value` at time t (host samples, n - 1 < t <= n)
    void step (double t, float value)
    {
        const float h = value - level;
        level = value;
        if (! bandlimited || h == 0.f) return;
        // all taps share one sub-sample phase: find it once, then walk the table in whole-sample strides
        const long k0 = (long) std::ceil (t - W);
        const double base = ((double) k0 - t + W) * BlepTable::kPhases;
        const int i0 = (int) base; const float u = (float) (base - i0);
        const float* tb = blep->t;
        const int last = 2 * W * BlepTable::kPhases;
        for (int j = 0, i = i0; i < last; ++j, i += BlepTable::kPhases)
            corr[(k0 + j) & (kRing - 1)] += h * (tb[i] + (tb[i + 1] - tb[i]) * u);
    }
    // finish sample n: returns the staircase at time n - W (band-limited)
    float naive[kRing] {};
    float end()
    {
        naive[n & (kRing - 1)] = level;
        const long k = n - W;
        const float y = naive[k & (kRing - 1)] + corr[k & (kRing - 1)];
        corr[k & (kRing - 1)] = 0.f;
        ++n;
        return y;
    }
};

struct OutputFilter
{
    LowPass4 lp;
    float hz = -1.f;
    void set (float f, double fs) { if (f == hz) return; hz = f; lp.set (std::fmin (f, (float) (fs * 0.45)), fs); }
    void reset() { lp.reset(); }
    float tick (float x) { return lp.tick (x); }
};
} // namespace sp
