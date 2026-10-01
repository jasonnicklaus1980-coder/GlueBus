#pragma once
// DAC: the SP-1200 output conversion for one voice, at each 26.04 kHz clock.
//
// Documented architecture (SP-1200 clone analyses and the parts lists): one 12-bit multiplying DAC (AD7541 /
// MP7621) is shared by all eight voices. Its output is the reference of an 8-bit multiplying "level" DAC that sets the
// voice's amplitude (volume and decay), and a per-channel sample-and-hold (4051 analog switch + capacitor + TL084
// buffer) keeps the voice's voltage between its slots (8 x 26.04 kHz = 208 kHz multiplex rate).
// The plugin:
//   12-bit ladder value of the code (same ladder, same component errors as the converter, see Quantizer12Bit)
//   x 8-bit level code / 255 (level DAC; 8-bit steps, so a decay falls in audible 8-bit steps at low levels)
//   -> sample-and-hold: settles towards the new value (SETTLING 100 % = complete in its slot) plus a small charge-
//      injection pedestal (COMPONENT VARIATION)
//   -> optional DIGITAL NOISE: a tiny disturbance proportional to how many DAC bits change on this clock (bus and
//      switching activity coupling into the analog ground; behavioural, off by default)
// The held value then forms the zero-order-hold staircase (ReconstructionFilter renders it).
#include "Quantizer12Bit.h"

namespace sp
{
struct DAC
{
    Ladder lad;
    float lvlW[8] {};               // level-DAC bit-weight errors
    float settle = 1.f;             // 0..1 fraction settled per slot
    float pedestal = 0.f;           // LSBs
    float digitalNoise = 0.f;       // full-scale units per changed bit
    float held = 0.f;
    int lastCode = 0;
    void reset() { held = 0.f; lastCode = 0; }
    float levelGain (int code8) const
    {
        code8 = clampi (code8, 0, 255);
        float s = 0.f;
        for (int b = 0; b < 8; ++b) if (code8 & (1 << b)) s += (float) (1 << b) * (1.f + lvlW[b]);
        return s * (1.f / 255.f);
    }
    // fractional codes come from interpolated reads (ALIAS BYPASS) or REPLAY fades
    float ladderValue (float code) const
    {
        const float fl = std::floor (code);
        const int c0 = (int) fl;
        const float f = code - fl;
        const float v0 = lad.value (clampi (c0, -(1 << (lad.bits - 1)), (1 << (lad.bits - 1)) - 1));
        if (f < 1e-6f) return v0;
        const float v1 = lad.value (clampi (c0 + 1, -(1 << (lad.bits - 1)), (1 << (lad.bits - 1)) - 1));
        return v0 + (v1 - v0) * f;
    }
    float clock (float code, int level8)
    {
        const float target = ladderValue (code) * levelGain (level8) + pedestal * (1.f / 2048.f);
        held += (target - held) * settle;
        float out = held;
        if (digitalNoise > 0.f)
        {
            const int c = (int) code;
            const int flips = __builtin_popcount ((unsigned) (c ^ lastCode) & 0xFFFu);
            out += digitalNoise * (float) flips * ((flips & 1) ? 1.f : -1.f);
            lastCode = c;
        }
        return out;
    }
};
} // namespace sp
