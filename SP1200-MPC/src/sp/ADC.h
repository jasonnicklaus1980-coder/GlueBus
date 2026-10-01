#pragma once
// ADC: track-and-hold + SAR converter (one channel).
//
// The real converter samples the continuous analog signal at exact instants of its own 26.04 kHz clock. The plugin
// only has the analog signal at the host's sample instants, so the track-and-hold value at the converter's instant is
// reconstructed by band-limited interpolation of the host-rate signal (the host signal is band-limited to the host's
// Nyquist, so this reconstruction is exact up to the kernel's accuracy):
//   ECO        nearest host sample (the converter instants snap to the host grid: adds timing jitter, non-hardware)
//   NORMAL     4-point cubic Hermite
//   ACCURATE   16-tap Kaiser-windowed sinc (flat to ~15 kHz at 44.1 kHz)
//   REFERENCE  32-tap Kaiser-windowed sinc (flat to ~19 kHz)
// Nothing here filters the signal: whatever the anti-alias stage let through above 13.02 kHz folds back, exactly as
// it does when the hardware samples it. Converter (comparator / reference) noise is added before the SAR search,
// in LSBs, so it only shows where the signal sits near a code boundary.
#include "Quantizer12Bit.h"

namespace sp
{
struct SincTable
{
    static constexpr int kPhases = 256;
    int taps = 8;
    float t[(kPhases + 1) * 32];
    void build (int ntaps, double beta)
    {
        taps = ntaps;
        const int half = taps / 2;
        auto i0 = [] (double v) { double s = 1, t = 1; for (int j = 1; j < 25; ++j) { t *= (v / (2 * j)) * (v / (2 * j)); s += t; } return s; };
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            double sum = 0;
            for (int k = 0; k < taps; ++k)
            {
                const double x = (k - half + 1) - frac;                        // tap position relative to the point
                const double wx = x / half;
                double win;
                if (std::fabs (wx) >= 1.0) win = 0.0;
                else win = i0 (beta * std::sqrt (1 - wx * wx)) / i0 (beta);
                const double s = std::fabs (x) < 1e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                t[p * 32 + k] = (float) (s * win); sum += s * win;
            }
            for (int k = 0; k < taps; ++k) t[p * 32 + k] = (float) (t[p * 32 + k] / sum);   // unity DC gain
        }
    }
};

struct ADC
{
    static constexpr int kRing = 64;
    float ring[kRing] {};
    long n = 0;                                 // samples written
    int quality = 1;                            // 0 eco, 1 normal, 2 accurate, 3 reference
    const SincTable* sinc16 = nullptr; const SincTable* sinc32 = nullptr;
    Quantizer12Bit q;
    float noiseLsb = 0.f;                       // converter noise, rms LSB
    void reset() { std::memset (ring, 0, sizeof ring); n = 0; }
    void push (float a) { ring[n & (kRing - 1)] = a; ++n; }
    float at (long i) const { return ring[i & (kRing - 1)]; }
    // track-and-hold value at continuous time t (host samples; t <= n - 1 - 16 so every kernel has its taps)
    float hold (double t) const
    {
        const long i = (long) std::floor (t);
        const float f = (float) (t - (double) i);
        switch (quality)
        {
            case 0: return at (f < 0.5f ? i : i + 1);
            case 1:
            {
                const float xm = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
                const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            }
            default:
            {
                const SincTable& T = quality == 2 ? *sinc16 : *sinc32;
                const float ph = f * SincTable::kPhases; const int p = (int) ph; const float u = ph - (float) p;
                const float* a = T.t + p * 32; const float* b = a + 32;
                const int half = T.taps / 2;
                float s = 0.f;
                for (int k = 0; k < T.taps; ++k) s += (a[k] + (b[k] - a[k]) * u) * at (i + k - half + 1);
                return s;
            }
        }
    }
    // one conversion
    int convert (float held, Rng& rng) const
    {
        const float v = noiseLsb > 0.f ? held + rng.gauss() * noiseLsb * (1.f / 2048.f) : held;
        return q.convert (v);
    }
};
} // namespace sp
