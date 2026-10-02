#pragma once
// Real-time time stretch. VST2 on MPC offers no host time-stretching, so the plugin does its own:
// granular overlap-add. An "anchor" walks through the sample at the TIME rate; every hop a new grain starts at
// the anchor and reads the sample at the PITCH rate under a Hann window. With 2 grains (hop = half a grain) or
// 4 grains (quality High) the windows sum to a constant, and at time = pitch = 1 the output equals the input.
// Cost: 2 or 4 interpolated reads per sample, no FFT, no allocation.
//
// Grain length per stretch mode (a trade-off between smearing and flamming):
//   Drums 22 ms (keeps hits tight)   Loops 45 ms   Vocals 50 ms   Instruments 90 ms (smooth sustained tones)
#include "Util.h"

namespace cp
{
enum StretchType { ST_DRUMS, ST_INSTRUMENTS, ST_VOCALS, ST_LOOPS, ST_COUNT };

inline double grainMs (int type)
{
    static const double k[] { 22.0, 90.0, 50.0, 45.0 };
    return k[clampi (type, 0, ST_COUNT - 1)];
}

struct HannTable
{
    static constexpr int N = 1024;
    float w[N + 1];
    HannTable() { for (int i = 0; i <= N; ++i) w[i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N)); }
    float at (double phase) const                                          // phase 0..1
    {
        const double x = phase * N; int i = (int) x;
        if (i < 0) return 0.f;
        if (i >= N) return 0.f;
        const float u = (float) (x - i);
        return w[i] + (w[i + 1] - w[i]) * u;
    }
};
inline const HannTable& hann() { static const HannTable t; return t; }

struct Grain { double start = 0.0; double age = 0.0; bool on = false; };

// The grain set of one voice. `grains` is 2 or 4; `len` is the grain length in output samples.
struct GrainSet
{
    static constexpr int kMax = 4;
    Grain g[kMax];
    double sinceSpawn = 1e9;
    int next = 0;
    void reset() { for (auto& x : g) x = Grain(); sinceSpawn = 1e9; next = 0; }
    // start a grain now if a hop has passed; the caller supplies the anchor position
    bool due (double len, int grains) const { return sinceSpawn >= len / grains; }
    void spawn (double anchor)
    {
        g[next].start = anchor; g[next].age = 0.0; g[next].on = true;
        next = (next + 1) % kMax;
        sinceSpawn = 0.0;
    }
};
} // namespace cp
