#pragma once
// HardwareVariation: component tolerances of one "unit". At 0 % every value is the idealised reference; COMPONENT
// VARIATION scales seeded random offsets (UNIT picks the seed), so a setting is repeatable. Left and right are two
// voices of the same machine and get their own offsets, so at 0 % the plugin never widens the stereo image.
//
// Ranges are typical part tolerances, not measurements of a particular SP-1200:
//   resistors / capacitors 5 %   -> filter corners +-5 %
//   12-bit DAC (AD7541 class)    -> bit-weight errors giving up to ~0.5 LSB integral non-linearity
//   8-bit level DAC              -> bit-weight errors up to ~0.5 LSB
//   SSM2044                      -> cutoff trim spread +-15 %, input offset +-3 mV (even-order distortion when driven)
//   op-amp stages                -> gain +-0.2 dB, slight asymmetry, noise +-1.5 dB, S/H pedestal +-0.3 LSB
#include "Dsp.h"

namespace sp
{
struct VoiceTolerances
{
    float aaCut = 1.f, outCut = 1.f, ssmCut = 1.f;     // multipliers on corner frequencies
    float ssmOffset = 0.f;                              // SSM2044 input offset, in units of its nominal input level
    float gain = 1.f, asym = 0.f, noise = 1.f, pedestal = 0.f, ssmFeed = 1.f;
    float dacW[16] {};                                  // relative bit-weight errors of the 12-bit DAC (bit 0 = LSB)
    float lvlW[8] {};                                   // relative bit-weight errors of the 8-bit level DAC
};

struct HardwareVariation
{
    VoiceTolerances v[2];
    float amount = -1.f; int unit = -1;
    bool update (float amt, int u)                      // returns true when the values changed
    {
        if (amt == amount && u == unit) return false;
        amount = amt; unit = u;
        Rng r; r.seed (0x51200u + 7919u * (uint32_t) u);
        for (int c = 0; c < 2; ++c)
        {
            VoiceTolerances& t = v[c];
            t.aaCut = 1.f + 0.05f * amt * r.bi();
            t.outCut = 1.f + 0.05f * amt * r.bi();
            t.ssmCut = 1.f + 0.15f * amt * r.bi();
            t.ssmOffset = 0.15f * amt * r.bi();
            t.gain = dbToGain (0.2f * amt * r.bi());
            t.asym = 0.03f * amt * r.bi();
            t.noise = dbToGain (1.5f * amt * r.bi());
            t.pedestal = 0.3f * amt * r.bi();
            t.ssmFeed = 1.f + 0.5f * amt * r.bi();
            // bit-weight errors: absolute error of bit b in LSBs is e_b; scaled so the MSB error is <= 0.5 LSB
            for (int b = 0; b < 16; ++b)
            {
                const float lsbErr = 0.5f * amt * r.bi() * std::sqrt ((float) (b + 1) / 12.f);
                t.dacW[b] = lsbErr / (float) (1 << b);
            }
            for (int b = 0; b < 8; ++b) t.lvlW[b] = 0.5f * amt * r.bi() / (float) (1 << b);
        }
        return true;
    }
};
} // namespace sp
