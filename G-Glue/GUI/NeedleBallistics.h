#pragma once
// Analog meter needle: a damped spring-mass system (pure C++, shared by every GUI).
// The needle has inertia and a little overshoot like a moving-coil meter. It is stepped from the GUI timer,
// never from the audio thread.
#include <cmath>

namespace gglue
{
struct NeedleBallistics
{
    double position = 0.0, velocity = 0.0;            // position in the meter's own units (here: dB of GR)
    double naturalHz = 3.2, damping = 0.72;           // ~300 ms rise, about 3 % overshoot

    void reset (double p = 0.0) { position = p; velocity = 0.0; }

    // advance by dt seconds toward target (sub-steps keep it stable at low frame rates)
    double step (double target, double dt)
    {
        if (! std::isfinite (target)) target = 0.0;
        const double w = 2.0 * 3.14159265358979323846 * naturalHz;
        const int sub = dt > 0.004 ? (int) std::ceil (dt / 0.004) : 1;
        const double h = dt / sub;
        for (int i = 0; i < sub; ++i)
        {
            const double acc = w * w * (target - position) - 2.0 * damping * w * velocity;
            velocity += acc * h;
            position += velocity * h;
        }
        if (! std::isfinite (position)) reset (target);
        return position;
    }
};

// GR scale of the G-Glue meter: label value (dB) -> position along the arc (0 = left end, 1 = right end / 0 dB)
struct GrScale
{
    static constexpr int kMarks = 9;
    static constexpr double kDb[kMarks]  { 0, 1, 2, 3, 5, 7, 10, 15, 20 };
    static constexpr double kPos[kMarks] { 1.0, 0.885, 0.775, 0.675, 0.53, 0.415, 0.285, 0.125, 0.0 };
    static double position (double grDb)
    {
        if (grDb <= 0) return 1.0;
        if (grDb >= 20) return 0.0;
        for (int i = 1; i < kMarks; ++i)
            if (grDb <= kDb[i])
            {
                const double t = (grDb - kDb[i - 1]) / (kDb[i] - kDb[i - 1]);
                return kPos[i - 1] + t * (kPos[i] - kPos[i - 1]);
            }
        return 0.0;
    }
};
} // namespace gglue
