#pragma once
// Filters shared by the clip channels, the sampler stage and the master effects.
#include "../Utilities/Util.h"

namespace cp
{
// Topology-preserving state-variable filter (Simper / Zavalishin): stable under fast cutoff changes, which
// matters for a filter that is swept live. One instance per channel (L, R).
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f;
    void reset() { ic1 = ic2 = 0.f; }
};
struct SvfCoefs
{
    float a1 = 1.f, a2 = 0.f, a3 = 0.f, k = 1.f;
    void set (float hz, float sr, float res)                        // res 0..1 -> Q 0.5 .. ~12
    {
        hz = clampf (hz, 16.f, sr * 0.45f);
        const float g = std::tan ((float) kPi * hz / sr);
        k = 2.f - 1.9f * clampf (res, 0.f, 1.f);                    // k = 1/Q
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
};
enum FilterType { FT_OFF, FT_LP, FT_HP, FT_BP, FT_COUNT };
inline float svfTick (Svf& s, const SvfCoefs& c, float x, int type)
{
    const float v3 = x - s.ic2;
    const float v1 = c.a1 * s.ic1 + c.a2 * v3;
    const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.f * v1 - s.ic1; s.ic2 = 2.f * v2 - s.ic2;
    switch (type)
    {
        case FT_LP: return v2;
        case FT_HP: return x - c.k * v1 - v2;
        case FT_BP: return v1 * c.k;                                 // unity gain at the centre
        default:    return x;
    }
}

// RBJ biquad (EQ bands, anti-alias filters). Transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void norm (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    void lowpass (double hz, double sr, double q)
    {
        const double w = 2 * kPi * clampd (hz, 10, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double hz, double sr, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * clampd (hz, 10, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void shelf (double hz, double sr, double db, bool high)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * clampd (hz, 10, sr * 0.49) / sr, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        if (high) norm (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                        (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        else norm (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                   (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
};

struct OnePole
{
    float a = 0.f, z = 0.f;
    void set (float hz, float sr) { a = std::exp (-2.f * (float) kPi * clampf (hz, 1.f, sr * 0.49f) / sr); }
    float lp (float x) { z = x + (z - x) * a; return z; }
};

// Smooth soft clip, unity slope at 0, saturates towards +-1.
inline float softClip (float x) { return x / (1.f + std::fabs (x)); }
inline float tanhApprox (float x)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
} // namespace cp
