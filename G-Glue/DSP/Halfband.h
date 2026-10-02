#pragma once
// 2x polyphase IIR half-band up/down-sampler (two parallel chains of first-order all-pass sections).
// Minimum phase and zero added latency, so the plugin reports 0 samples of latency.
// The coefficients come from the classic elliptic half-band design (Valenzuela / Constantinides form),
// computed at prepare time from the number of coefficients and the transition bandwidth.
#include <cmath>
#include <cstring>

namespace gglue
{
constexpr int kMaxHalfbandCoefs = 12;

inline void designHalfband (double* coef, int numCoefs, double transition)
{
    const double pi = 3.14159265358979323846;
    // transition parameter
    double k = std::tan ((1.0 - transition * 2.0) * pi / 4.0);
    k *= k;
    const double kksqrt = std::pow (1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e2 = e * e, e4 = e2 * e2;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = numCoefs * 2 + 1;
    for (int index = 0; index < numCoefs; ++index)
    {
        const int c = index + 1;
        double num = 0.0, term;
        int i = 0, j = 1;
        do { term = std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * c * pi / order) * j; num += term; j = -j; ++i; }
        while (std::fabs (term) > 1e-100 && i < 100);
        double den = 0.0;
        i = 1; j = -1;
        do { term = std::pow (q, (double) (i * i)) * std::cos (i * 2 * c * pi / order) * j; den += term; j = -j; ++i; }
        while (std::fabs (term) > 1e-100 && i < 100);
        const double ww = num * std::pow (q, 0.25) / (den + 0.5);
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coef[index] = (1.0 - x) / (1.0 + x);
    }
}

// one chain pair: even coefficients on path 0, odd on path 1
struct AllpassPair
{
    float c[kMaxHalfbandCoefs] {};
    float x[kMaxHalfbandCoefs] {}, y[kMaxHalfbandCoefs] {};
    int n = 0;
    void setup (const double* coef, int numCoefs) { n = numCoefs; for (int i = 0; i < n; ++i) c[i] = (float) coef[i]; reset(); }
    void reset() { std::memset (x, 0, sizeof x); std::memset (y, 0, sizeof y); }
    inline void process (float& s0, float& s1)
    {
        for (int i = 0; i < n; i += 2)
        {
            const float t0 = (s0 - y[i]) * c[i] + x[i];
            x[i] = s0; y[i] = t0; s0 = t0;
            if (i + 1 < n)
            {
                const float t1 = (s1 - y[i + 1]) * c[i + 1] + x[i + 1];
                x[i + 1] = s1; y[i + 1] = t1; s1 = t1;
            }
        }
    }
    void flushDenormals()
    {
        for (int i = 0; i < n; ++i) { if (std::fabs (x[i]) < 1e-20f) x[i] = 0.f; if (std::fabs (y[i]) < 1e-20f) y[i] = 0.f; }
    }
    bool finite() const
    {
        for (int i = 0; i < n; ++i) if (! std::isfinite (x[i]) || ! std::isfinite (y[i])) return false;
        return true;
    }
};

struct Upsampler2x
{
    AllpassPair ap;
    void setup (const double* coef, int n) { ap.setup (coef, n); }
    inline void process (float in, float& out0, float& out1) { float e = in, o = in; ap.process (e, o); out0 = e; out1 = o; }
};

struct Downsampler2x
{
    AllpassPair ap;
    void setup (const double* coef, int n) { ap.setup (coef, n); }
    inline float process (float in0, float in1) { float s0 = in1, s1 = in0; ap.process (s0, s1); return 0.5f * (s0 + s1); }
};
} // namespace gglue
