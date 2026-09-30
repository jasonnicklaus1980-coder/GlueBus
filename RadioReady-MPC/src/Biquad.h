#pragma once
// RadioReady EQ - biquad filter designs (RBJ "Audio EQ Cookbook") and a 64-bit transposed direct form II section.
// Framework-free so the DSP can be unit-tested on its own.
#include <cmath>
#include <complex>

namespace rr
{
enum class FilterType { Peak = 0, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass };
constexpr int kNumFilterTypes = 7;

inline bool typeUsesGain (FilterType t) { return t == FilterType::Peak || t == FilterType::LowShelf || t == FilterType::HighShelf; }
inline bool typeIsCut (FilterType t)    { return t == FilterType::HighPass || t == FilterType::LowPass; }

struct Coeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    // |H(e^jw)| at frequency hz for sample rate fs
    double magnitude (double hz, double fs) const
    {
        const double w = 2.0 * M_PI * hz / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }
};

// RBJ designs. q is the filter Q (for shelves: the shelf's Q, 0.707 = Butterworth-like corner)
inline Coeffs design (FilterType type, double fs, double f, double q, double gainDb)
{
    f = std::fmin (std::fmax (f, 5.0), 0.49 * fs);
    q = std::fmax (q, 0.025);
    const double w = 2.0 * M_PI * f / fs, cw = std::cos (w), sw = std::sin (w);
    const double A = std::pow (10.0, gainDb / 40.0), al = sw / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case FilterType::Peak:
            b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case FilterType::LowShelf:
        {
            const double s = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;       a1 = -2 * ((A - 1) + (A + 1) * cw);     a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case FilterType::HighShelf:
        {
            const double s = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;       a1 = 2 * ((A - 1) - (A + 1) * cw);       a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        case FilterType::HighPass: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FilterType::LowPass:  b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FilterType::Notch:    b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FilterType::BandPass: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    }
    Coeffs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

// One biquad section, transposed direct form II, 64-bit state
struct Section
{
    Coeffs c;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    inline double process (double x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void flushDenormals()
    {
        if (std::fabs (z1) < 1e-30) z1 = 0;
        if (std::fabs (z2) < 1e-30) z2 = 0;
    }
};

// Butterworth section Qs for the cut slopes: 12 dB/oct = 1 section (uses the band's Q), 24 = 2, 48 = 4
inline int sectionsForSlope (int slopeIndex) { return slopeIndex <= 0 ? 1 : (slopeIndex == 1 ? 2 : 4); }
inline double butterworthQ (int slopeIndex, int section)
{
    static const double q24[2] { 0.54119610, 1.30656296 };
    static const double q48[4] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
    return slopeIndex == 1 ? q24[section & 1] : q48[section & 3];
}
} // namespace rr
