#pragma once
// AnalogOutput: the output amplifier (one channel). The parts list names NE5534 low-noise op-amps; the plugin treats
// the output stage as one op-amp gain block:
//   DRIVE raises the level into the stage (and lowers it after, so the loudness stays put and only the colour
//   changes) -> rail saturation with a soft knee (OUTPUT HEADROOM above converter full scale: assumed 12 dB, not
//   documented), small asymmetry from component mismatch -> output coupling capacitor (high-pass ~3 Hz, assumed).
// The nonlinearity is oversampled by the quality setting. Noise (hiss, hum, ground) is injected here by the engine,
// after the filters, where an output stage's own noise appears.
#include "Dsp.h"

namespace sp
{
struct AnalogOutput
{
    double fs = 44100.0;
    int os = 1;
    OnePole coupling;
    Halfband up1, up2, dn1, dn2;
    float headroom = 3.98f;         // 12 dB
    float knee = 0.35f;
    float amount = 1.f;             // ANALOG
    float asym = 0.f;
    bool ampOn = true;
    void prepare (double rate, int oversample)
    {
        fs = rate; os = oversample; coupling.lowpass (3.0, fs); coupling.z = 0.f;
        up1.reset(); up2.reset(); dn1.reset(); dn2.reset();
    }
    static float rail (float u, float k)
    {
        const float a = std::fabs (u), lo = 1.f - k;
        if (a <= lo) return u;
        if (k <= 1e-4f || a >= 1.f + k) return u > 0 ? 1.f : -1.f;
        const float d = a - lo, y = a - d * d / (4.f * k);
        return u > 0 ? y : -y;
    }
    float stage (float x) const
    {
        const float g = amount / headroom;
        float u = x * g;
        u += asym * u * u;
        return rail (u, knee * 0.5f) / g;
    }
    // input already oversampled (os samples, from SSMModel::processOS): stage at that rate, then down to the host rate
    float processOS (const float* in, float drive)
    {
        const bool on = ampOn && amount >= 1e-3f;
        float v[4];
        for (int i = 0; i < os; ++i) v[i] = on ? stage (in[i] * drive) / drive : in[i];
        float y;
        if (os == 1) y = v[0];
        else if (os == 2) y = dn1.down (v[0], v[1]);
        else y = dn1.down (dn2.down (v[0], v[1]), dn2.down (v[2], v[3]));
        return coupling.hp (y);
    }
    float process (float x, float drive)
    {
        float y = x;
        if (ampOn && amount >= 1e-3f)
        {
            const float xd = x * drive;
            if (os == 1) y = stage (xd);
            else if (os == 2) { float a, b; up1.up (xd, a, b); y = dn1.down (stage (a), stage (b)); }
            else
            {
                float a, b, a0, a1, b0, b1;
                up1.up (xd, a, b); up2.up (a, a0, a1); up2.up (b, b0, b1);
                y = dn1.down (dn2.down (stage (a0), stage (a1)), dn2.down (stage (b0), stage (b1)));
            }
            y /= drive;
        }
        return coupling.hp (y);
    }
};
} // namespace sp
