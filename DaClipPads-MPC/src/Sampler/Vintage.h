#pragma once
// SAMPLER stage: the audible behaviour of early digital samplers, built from documented DSP steps. It does not
// model any particular product's circuitry; the presets are named after the sound they aim for.
//
//   input -> ANTI-ALIAS (4th-order low-pass at 0.45 x SAMPLE RATE, blended by ANTI-ALIAS)
//         -> SAMPLE-RATE CONVERSION (sample-and-hold at SAMPLE RATE: aliasing when the filter is off)
//         -> BIT-DEPTH QUANTISATION (BIT DEPTH) -> QUANTISATION ERROR (QUANTIZE: how much of the error is kept)
//         -> DAC CHARACTER (zero-order hold steps, then a gentle reconstruction low-pass)
//         -> ANALOG SATURATION (asymmetric soft clip, SATURATION) + NOISE (hiss) + CRACKLE -> OUTPUT
// NOISE and CRACKLE ride on the signal (an envelope follower opens them), the way noise is part of a sampled
// record: with nothing playing the stage is silent.
#include "../Effects/Filters.h"

namespace cp
{
struct VintageParams
{
    float bits = 12.f, rate = 26040.f, aa = 0.f, quantize = 1.f, sat = 0.25f, noise = 0.f, crackle = 0.f, outDb = 0.f;
};

struct VintagePreset { const char* name; VintageParams p; };
inline const VintagePreset* vintagePresets (int& count)
{
    static const VintagePreset k[] {
        { "Clean",    { 24.f, 48000.f, 1.00f, 0.0f, 0.00f, 0.00f, 0.00f, 0.f } },
        { "12-Bit",   { 12.f, 32000.f, 1.00f, 1.0f, 0.10f, 0.00f, 0.00f, 0.f } },
        { "SP Style", { 12.f, 26040.f, 0.00f, 1.0f, 0.25f, 0.06f, 0.00f, 0.f } },
        { "MPC Style",{ 12.f, 40000.f, 0.70f, 1.0f, 0.35f, 0.03f, 0.00f, 0.f } },
        { "Vinyl",    { 16.f, 44100.f, 1.00f, 0.5f, 0.30f, 0.20f, 0.45f, 0.f } },
        { "Dusty",    { 12.f, 22050.f, 0.80f, 1.0f, 0.40f, 0.30f, 0.20f, 0.f } },
        { "Crushed",  {  6.f,  8000.f, 0.00f, 1.0f, 0.60f, 0.08f, 0.00f, -3.f } },
    };
    count = (int) (sizeof k / sizeof k[0]);
    return k;
}

struct Vintage
{
    float sr = 44100.f;
    Biquad aa1[2], aa2[2];
    OnePole dac[2], hissLp;
    double phase = 1.0;
    float held[2] {};
    float crackEnv = 0.f, crackSign = 1.f, follow = 0.f;
    Rng rng;
    float lastRate = -1.f;

    void prepare (float rate) { sr = rate; lastRate = -1.f; reset(); }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { aa1[c].reset(); aa2[c].reset(); dac[c].z = 0.f; held[c] = 0.f; }
        hissLp.z = 0.f; phase = 1.0; crackEnv = 0.f; follow = 0.f;
    }
    void process (float* L, float* R, int n, const VintageParams& p)
    {
        const float target = clampf (p.rate, 2000.f, sr);
        if (target != lastRate)
        {
            lastRate = target;
            const double fc = std::fmin (0.45 * target, 0.45 * sr);
            for (int c = 0; c < 2; ++c) { aa1[c].lowpass (fc, sr, 0.5412); aa2[c].lowpass (fc, sr, 1.3066); }   // Butterworth 4
            for (int c = 0; c < 2; ++c) dac[c].set ((float) std::fmin (0.5 * target, 0.47 * sr), sr);
            hissLp.set (6000.f, sr);
        }
        const float aa = clampf (p.aa, 0.f, 1.f), q = clampf (p.quantize, 0.f, 1.f);
        const float steps = std::pow (2.f, clampf (p.bits, 2.f, 24.f) - 1.f);
        const bool bypassRate = target >= sr - 1.f;
        const double inc = target / sr;
        const float drive = 1.f + 4.f * clampf (p.sat, 0.f, 1.f), bias = 0.12f * clampf (p.sat, 0.f, 1.f);
        const float satNorm = 1.f / tanhApprox (drive), biasOff = tanhApprox (bias);
        const float hiss = 0.03f * clampf (p.noise, 0.f, 1.f) * clampf (p.noise, 0.f, 1.f);
        const float crackRate = 40.f * clampf (p.crackle, 0.f, 1.f) / sr;
        const float crackDecay = std::exp (-1.f / (0.0006f * sr));
        const float out = dbToGain (p.outDb);
        const float atk = 1.f - std::exp (-1.f / (0.002f * sr)), rel = 1.f - std::exp (-1.f / (0.25f * sr));
        float* ch[2] { L, R };
        for (int i = 0; i < n; ++i)
        {
            bool tick = true;
            if (! bypassRate) { phase += inc; tick = phase >= 1.0; if (tick) phase -= 1.0; }
            const float in = std::fmax (std::fabs (L[i]), std::fabs (R[i]));
            follow += (in - follow) * (in > follow ? atk : rel);
            const float gate = std::fmin (1.f, follow * 20.f);                 // fully open from about -26 dBFS
            float noise = 0.f;
            if (hiss > 0.f) noise = hissLp.lp (rng.bi()) * hiss * 3.f * gate;
            if (crackRate > 0.f && gate > 0.05f && rng.uni() < crackRate) { crackEnv = (0.05f + 0.25f * rng.uni()) * gate; crackSign = rng.uni() < 0.5f ? -1.f : 1.f; }
            const float crack = crackEnv * crackSign; crackEnv *= crackDecay;
            for (int c = 0; c < 2; ++c)
            {
                float x = ch[c][i];
                if (aa > 0.f) { const float f = aa2[c].tick (aa1[c].tick (x)); x += (f - x) * aa; }
                if (tick)
                {
                    const float quant = std::floor (x * steps + 0.5f) / steps;
                    held[c] = x + (quant - x) * q;                                   // QUANTIZE: keep this much of the error
                }
                float y = bypassRate ? held[c] : dac[c].lp (held[c]);
                if (p.sat > 0.f) y = (tanhApprox (y * drive + bias) - biasOff) * satNorm;
                ch[c][i] = (y + noise + crack) * out;
            }
        }
    }
};
} // namespace cp
