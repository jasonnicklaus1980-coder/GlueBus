#pragma once
// Small filters used by the engine.
#include <cmath>

namespace gglue
{
// 2nd-order Butterworth high-pass (RBJ form, transposed direct form II), used in the sidechain.
struct HighPass2
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    bool enabled = false;
    void setup (double hz, double fs)
    {
        enabled = hz > 0.0;
        if (! enabled) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        const double w = 2.0 * 3.14159265358979323846 * hz / fs, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;
        b0 = (float) ((1.0 + cw) / 2.0 / a0); b1 = (float) (-(1.0 + cw) / a0); b2 = b0;
        a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - alpha) / a0);
    }
    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        if (! enabled) return x;
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void flushDenormals() { if (std::fabs (z1) < 1e-20f) z1 = 0; if (std::fabs (z2) < 1e-20f) z2 = 0; }
    bool finite() const { return std::isfinite (z1) && std::isfinite (z2); }
};

// 1st-order DC blocker
struct DcBlocker
{
    float r = 0.9995f, x1 = 0, y1 = 0;
    void setup (double hz, double fs) { r = (float) std::exp (-2.0 * 3.14159265358979323846 * hz / fs); }
    void reset() { x1 = y1 = 0; }
    inline float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void flushDenormals() { if (std::fabs (y1) < 1e-20f) y1 = 0; if (std::fabs (x1) < 1e-20f) x1 = 0; }
    bool finite() const { return std::isfinite (x1) && std::isfinite (y1); }
};

// one-pole parameter smoother (per base-rate sample)
struct Smoother
{
    float current = 0, target = 0, coef = 0.999f;
    void setup (double ms, double fs) { coef = (float) std::exp (-1.0 / (ms * 0.001 * fs)); }
    void reset (float v) { current = target = v; }
    inline float next()
    {
        current = target + (current - target) * coef;
        if (std::fabs (current - target) < 1e-6f) current = target;
        return current;
    }
    bool smoothing() const { return current != target; }
};
// linear ramp for on/off switches (bypass, analog): reaches the target exactly after `ms`, no endless tail
struct Ramp
{
    float current = 0, target = 0, step = 0.001f;
    void setup (double ms, double fs) { step = (float) (1.0 / (ms * 0.001 * fs)); }
    void reset (float v) { current = target = v; }
    inline float next()
    {
        if (current < target) current = current + step >= target ? target : current + step;
        else if (current > target) current = current - step <= target ? target : current - step;
        return current;
    }
    bool smoothing() const { return current != target; }
};
} // namespace gglue
