#pragma once
// Quantizer12Bit: the converter's ladder and the successive-approximation (SAR) search.
//
// Documented: the SP-12 / SP-1200 sample with a successive-approximation converter built around the same 12-bit
// multiplying DAC that plays the samples back (AD7541 / MP7621 class per the parts lists; Yeh et al. 2007 describe
// the SP-12 path as "12-bit successive approximation quantizer"). So the plugin models the DAC as a binary-weighted
// ladder and runs the actual SAR search against it:
//   - with ideal bit weights this is an exact 12-bit quantizer (two's complement, -2048 .. +2047);
//   - with COMPONENT VARIATION each bit weight has a small error, which gives the converter's real kind of
//     non-linearity: steps of uneven size at the major carries (code 0 / -1, +-1024 ...), and the same errors on
//     record and playback because both use the one ladder;
//   - OFFSET is the "sample offset" trimmer (one of the 7 trimmers reported on the main board): 0.5 LSB centres the
//     codes on zero (mid-tread, silence gives one stable code); 0 gives truncation (mid-riser). Which way E-mu
//     trimmed it is not documented; mid-tread is the default because a trimmed unit records silence as one code.
//   - BITS < 12 stops the search early (lower bits stay 0); BITS > 12 is a non-hardware extension.
#include "Dsp.h"

namespace sp
{
struct Ladder
{
    int bits = 12;
    float w[16];                 // bit weights in LSBs of a 12-bit scale (ideal: 2^b * 2^(12-bits) ... see build)
    float zero = 2048.f;         // ladder level of code 0 (offset-binary mid-scale)
    float lsb = 1.f / 2048.f;    // full scale = 1.0
    void build (int nbits, const float* relErr)
    {
        bits = clampi (nbits, 2, 16);
        // offset binary, total span 4096 "12-bit LSB units" whatever the resolution
        const float unit = 4096.f / (float) (1 << bits);
        float z = 0.f;
        for (int b = 0; b < bits; ++b)
        {
            w[b] = unit * (float) (1 << b) * (1.f + (relErr ? relErr[b] : 0.f));
            if (b == bits - 1) z = w[b];                                       // MSB = mid-scale
        }
        zero = z;
    }
    // ladder output for an offset-binary code
    float level (uint32_t ob) const
    {
        float s = 0.f;
        for (int b = 0; b < bits; ++b) if (ob & (1u << b)) s += w[b];
        return s;
    }
    // analog value (full scale 1.0) of a two's-complement code
    float value (int code) const
    {
        const uint32_t ob = (uint32_t) (code + (1 << (bits - 1)));
        return (level (ob) - zero) * lsb;
    }
};

struct Quantizer12Bit
{
    Ladder lad;
    float offset = 0.5f;          // trimmer, in LSBs
    // SAR conversion: binary search against the ladder, clipping at the ends like a real converter
    int convert (float v) const
    {
        const float target = v * 2048.f + lad.zero + offset * (4096.f / (float) (1 << lad.bits));
        uint32_t ob = 0; float lvl = 0.f;
        for (int b = lad.bits - 1; b >= 0; --b)                               // the comparator, MSB first
        {
            const float trial = lvl + lad.w[b];
            if (target >= trial) { ob |= 1u << b; lvl = trial; }
        }
        return (int) ob - (1 << (lad.bits - 1));
    }
};
} // namespace sp
