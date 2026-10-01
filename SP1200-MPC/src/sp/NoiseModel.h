#pragma once
// NoiseModel: the noise sources of the signal path, each injected where it arises (one channel).
//
//   source               injected at                        default level (unweighted rms re converter full scale)
//   input amp noise      AnalogInput, before the AA filter  -100 dB   (mostly below 1 LSB: sampled, then quantised away)
//   converter noise      ADC comparator, in LSB             0.1 LSB   (with the offset trim, silence stays one code)
//   S/H + DAC buffer     DAC output, held with the step     -97 dB
//   SSM2044 noise        SSM input (Out 1-2)                -92 dB    (filtered by the closing VCF)
//   filter op-amps       before the Out 3-6 filters         -98 dB
//   output stage hiss    AnalogOutput                       -91 dB    (HISS; NOISE COLOR tilts it)
//   hum / ground         AnalogOutput (and -12 dB at input) off       (HUM: 50/60 Hz + harmonics; GROUND:
//                                                                      rectifier buzz pulses at 2x mains)
//   digital noise        DAC, per changed bit               off       (behavioural, see DAC.h)
// Calibration: with everything at its default the idle output measures about -89 dB unweighted / ~-91 dBA, in line
// with the SP-1200's published 90 dB (A-weighted) signal-to-noise figure. The sources are uncorrelated between the
// two channels and generated continuously (no stored noise, so nothing ever repeats).
#include "Dsp.h"

namespace sp
{
struct NoiseLevels
{
    float inputAmp = 0, sh = 0, ssm = 0, filters = 0, hiss = 0, hum = 0, ground = 0;   // linear rms / peak
    float color = 0;                                                                   // -1 .. +1
    double humHz = 60.0;
};

struct NoiseModel
{
    double fs = 44100.0;
    Rng rng;
    Pink pink;
    Biquad tiltLo, tiltHi;
    float lastColor = -9.f;
    double humPh = 0.0;
    void prepare (double rate, uint32_t seed) { fs = rate; rng.seed (seed); lastColor = -9.f; humPh = 0.0; }
    float white() { return rng.gauss(); }
    // output hiss with NOISE COLOR: -1 = dark (pink-ish), 0 = white, +1 = bright
    float hiss (float color)
    {
        if (color != lastColor)
        {
            lastColor = color;
            tiltLo.shelf (300.0, fs, 6.0 * color, false);  tiltLo.reset();
            tiltHi.shelf (3000.0, fs, 6.0 * color, true);  tiltHi.reset();
        }
        float w = white();
        if (color < 0.f) w = w + (pink.tick (w) - w) * (-color);           // blend towards pink
        return color == 0.f ? w : tiltHi.tick (tiltLo.tick (w));
    }
    // mains hum (sine + decaying harmonics) and ground buzz (narrow pulses at twice the mains, rich in harmonics)
    void mains (double hz, float& hum, float& ground)
    {
        humPh += hz / fs; if (humPh >= 1.0) humPh -= 1.0;
        const double w = 2 * kPi * humPh;
        hum = (float) (std::sin (w) + 0.5 * std::sin (2 * w) + 0.35 * std::sin (3 * w) + 0.2 * std::sin (4 * w) + 0.12 * std::sin (5 * w)) * 0.55f;
        const float r = (float) std::fabs (std::sin (w));                    // full-wave rectifier
        ground = (r > 0.96f ? (r - 0.96f) * 25.f : 0.f) - 0.04f;             // charging pulses, mean ~0
    }
};
} // namespace sp
