#pragma once
// Shared DSP building blocks for the SP-1200 model: maths helpers, noise sources, biquads, a halfband oversampler.
// Header-only, no allocation, no libstdc++ (the plugin links only libc/libm).
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sp
{
constexpr double kPi = 3.14159265358979323846;
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double clampd (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline int clampi (int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float flush (float x) { return std::fabs (x) < 1e-20f ? 0.f : x; }
inline float dbToGain (float db) { return std::exp2 (db * 0.16609640f); }
inline float gainToDb (float g) { return g <= 1e-12f ? -240.f : 20.f * std::log10 (g); }
// fmod without libm's fmod (newer ARM glibc symbol than MPC OS has)
inline double fmodd (double a, double b) { return a - b * std::trunc (a / b); }

// tanh, rational approximation: error < 2e-3, exact +-1 beyond |x| = 3, monotonic, cheap on ARM
inline float tanhA (float x)
{
    if (x >= 3.f) return 1.f;
    if (x <= -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
// derivative of tanhA (for semi-implicit solvers)
inline float tanhAd (float x)
{
    if (x >= 3.f || x <= -3.f) return 0.f;
    const float x2 = x * x, d = 27.f + 9.f * x2;
    return (729.f - 162.f * x2 + 9.f * x2 * x2) / (d * d);
}

// ---------------------------------------------------------------------------------------------- noise
// xorshift32; gaussian from 4 uniforms (Irwin-Hall, variance-normalised): never repeats a stored buffer
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) { s = v ? v : 0x9e3779b9u; for (int i = 0; i < 8; ++i) next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.f / 16777216.f); }       // [0, 1)
    float bi() { return uni() * 2.f - 1.f; }
    float gauss() { return (uni() + uni() + uni() + uni() - 2.f) * 1.7320508f; }   // unit variance
};

// Paul Kellet's pink filter (-3 dB/oct within +-0.05 dB, 10 Hz .. fs/2)
struct Pink
{
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float tick (float w)
    {
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f;
        return p * 0.11f;                                                        // ~unit level
    }
};

// ---------------------------------------------------------------------------------------------- filters
struct OnePole
{
    float a = 0.f, z = 0.f;
    void lowpass (double hz, double fs) { a = (float) std::exp (-2.0 * kPi * clampd (hz, 0.1, fs * 0.49) / fs); }
    float lp (float x) { z = x + (z - x) * a; return z; }
    float hp (float x) { return x - lp (x); }
};

// RBJ / bilinear biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float tick (float x) { const float y = b0 * x + z1; z1 = flush (b1 * x - a1 * y + z2); z2 = flush (b2 * x - a2 * y); return y; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    // 2-pole low-pass with exact (pre-warped) corner and the given Q: the bilinear map of an analog Sallen-Key section
    void lowpass (double hz, double fs, double q)
    {
        const double w = 2 * kPi * clampd (hz, 10, fs * 0.4999) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double hz, double fs, double q)
    {
        const double w = 2 * kPi * clampd (hz, 1, fs * 0.4999) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    // high-shelf / low-shelf (noise colour tilt)
    void shelf (double hz, double fs, double db, bool high)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * clampd (hz, 10, fs * 0.49) / fs, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        if (high) set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                       (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        else set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                  (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
};

// 4th-order Butterworth low-pass as two cascaded Sallen-Key sections (Q 0.5412, 1.3066)
struct LowPass4
{
    Biquad s1, s2;
    void set (double hz, double fs) { s1.lowpass (hz, fs, 0.54119610); s2.lowpass (hz, fs, 1.30656296); }
    void reset() { s1.reset(); s2.reset(); }
    float tick (float x) { return s2.tick (s1.tick (x)); }
};

// ---------------------------------------------------------------------------------------------- oversampling
// Halfband FIR (23 taps, Kaiser): 2x up / 2x down, used only around the NONLINEAR analog stages. The vintage
// digital sampler stage is never oversampled (that would remove its aliasing).
struct Halfband
{
    static constexpr int N = 23, C = 11;                                         // taps, centre
    static const float* coefs()
    {
        static float h[N];
        static bool done = false;
        if (! done)
        {
            double sum = 0;
            for (int i = 0; i < N; ++i)
            {
                const int k = i - C;
                double sinc = k == 0 ? 0.5 : std::sin (kPi * k / 2.0) / (kPi * k);
                const double x = (double) k / (C + 1), beta = 7.0;
                // Kaiser window via a short Bessel series
                auto i0 = [] (double v) { double s = 1, t = 1; for (int j = 1; j < 20; ++j) { t *= (v / (2 * j)) * (v / (2 * j)); s += t; } return s; };
                const double w = i0 (beta * std::sqrt (std::fmax (0.0, 1 - x * x))) / i0 (beta);
                h[i] = (float) (sinc * w); sum += h[i];
            }
            for (int i = 0; i < N; ++i) h[i] = (float) (h[i] / sum);
            done = true;
        }
        return h;
    }
    // histories written twice (at i and i + N) so every read is a straight run without wrap-around
    float zu[2 * N] {}, zd[2 * N] {};
    int pu = 0, pd = 0;
    void reset() { std::memset (zu, 0, sizeof zu); std::memset (zd, 0, sizeof zd); pu = pd = 0; }
    // one input sample -> two output samples (zero-stuffed: even outputs use even taps, odd outputs odd taps)
    void up (float x, float& y0, float& y1)
    {
        const float* h = coefs();
        pu = pu == 0 ? N - 1 : pu - 1;
        zu[pu] = zu[pu + N] = x;
        const float* z = zu + pu;                                                // z[j] = x[n - j]
        // even taps h[2j], j = 0..11, are symmetric (h[2j] = h[22 - 2j]): fold z[j] with z[11 - j]
        float a = 0;
        for (int j = 0; j < 6; ++j) a += h[2 * j] * (z[j] + z[11 - j]);
        const float b = h[C] * z[C / 2];                                         // odd taps: only the centre is non-zero
        y0 = 2.f * a; y1 = 2.f * b;
    }
    // two input samples -> one output sample
    float down (float x0, float x1)
    {
        const float* h = coefs();
        pd = pd == 0 ? N - 1 : pd - 1; zd[pd] = zd[pd + N] = x0;
        pd = pd == 0 ? N - 1 : pd - 1; zd[pd] = zd[pd + N] = x1;
        const float* z = zd + pd;                                                // z[0] = newest
        float y = h[C] * z[C];
        for (int k = 0; k < C; k += 2) y += h[k] * (z[k] + z[N - 1 - k]);      // symmetric taps folded
        return y;
    }
};
} // namespace sp
