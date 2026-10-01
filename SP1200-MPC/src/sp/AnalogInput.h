#pragma once
// AnalogInput: the SP-1200 sampling input before the converter (one channel).
//
//   coupling capacitor (high-pass, ~5 Hz: assumed value)
//   -> input buffer / gain stage (op-amp, NE5534 class per the parts list): linear until its output nears the rails,
//      then a rounded clip. HEADROOM = rail level above converter full scale (assumed 8 dB: unknown in the hardware,
//      typical of +-15 V op-amps driving a +-5 V converter). Input-referred noise is injected here, so it is filtered
//      and sampled like the signal.
//   -> anti-alias filter: op-amp (TL084) low-pass. Yeh et al. (ICMC 2007) measured the SP-12's op-amp anti-alias
//      filter attenuating above ~15 kHz; the SP-1200 shares the design family, its exact corner and order are not
//      published. Modelled as a 4th-order Butterworth (two Sallen-Key sections) at 15 kHz: it is leaky between
//      13.02 kHz (Nyquist of 26.04 kHz) and ~20 kHz, so some aliasing gets into the samples, as on the hardware.
//
// Output units: 1.0 = converter full scale.
#include "Dsp.h"

namespace sp
{
struct AnalogInput
{
    double fs = 44100.0;
    int os = 1;                                 // oversampling of the nonlinear gain stage: 1, 2 or 4
    OnePole coupling;
    LowPass4 aa;
    Halfband up1, up2, dn1, dn2;
    float aaHz = -1.f;

    // settings (set per block)
    float headroom = 2.512f;                    // rails above full scale (linear), 8 dB
    float knee = 0.3f;                          // 0 = hard clip at the rails, 1 = very soft
    float amount = 1.f;                         // ANALOG: scales the nonlinearity (0 = ideal linear)
    float asym = 0.f;                           // from HardwareVariation
    bool ampOn = true, filterOn = true;

    void prepare (double rate, int oversample)
    {
        fs = rate; os = oversample; coupling.lowpass (5.0, fs); aaHz = -1.f;
        up1.reset(); up2.reset(); dn1.reset(); dn2.reset(); aa.reset(); coupling.z = 0.f;
    }
    void setFilter (float hz)
    {
        if (hz == aaHz) return;
        aaHz = hz;
        aa.set (std::fmin (hz, (float) (fs * 0.45)), fs);
    }
    // rail clip with a soft knee, normalised to the rails
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
        if (! ampOn || amount < 1e-3f) return x;
        const float g = amount / headroom;
        float u = x * g;
        u += asym * u * u;                                         // slight even-order bend from component mismatch
        return rail (u, knee * 0.5f) / g;
    }
    float process (float x, float noise)
    {
        x = coupling.hp (x) + noise;
        float y;
        if (os == 1 || ! ampOn || amount < 1e-3f) y = stage (x);
        else if (os == 2) { float a, b; up1.up (x, a, b); y = dn1.down (stage (a), stage (b)); }
        else
        {
            float a, b, a0, a1, b0, b1;
            up1.up (x, a, b); up2.up (a, a0, a1); up2.up (b, b0, b1);
            y = dn1.down (dn2.down (stage (a0), stage (a1)), dn2.down (stage (b0), stage (b1)));
        }
        return filterOn ? aa.tick (y) : y;
    }
};
} // namespace sp
