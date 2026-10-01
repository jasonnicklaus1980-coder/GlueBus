#pragma once
// Analyzer: spectrum (input and processed) and a 4-tap oscilloscope, for checking what each stage does.
//   Spectrum: 1024-point FFT, Hann window, of the mono input (aligned with the output's latency) and of the output,
//   every ~93 ms; 32 log-spaced bands from 40 Hz to 20 kHz; dB with a fast-attack / slow-release display.
//   Scope: one tap at a time (INPUT, POST-ADC = the converter's held codes, POST-DAC = the staircase before the
//   analog stages, OUTPUT), triggered on a rising zero crossing, 64 points across the chosen time window.
// Runs on the audio thread at a low rate (fixed buffers, no allocation).
#include "Dsp.h"

namespace sp
{
struct Analyzer
{
    static constexpr int N = 1024, kBands = 32, kScope = 64, kHist = 4096;
    float inBuf[N] {}, outBuf[N] {};
    int fill = 0, turn = 0;
    float bandIn[kBands] {}, bandOut[kBands] {};
    float tapHist[kHist] {}; int tapW = 0;
    float scope[kScope] {};
    double fs = 44100.0;
    float re[N], im[N], win[N];
    int bandLo[kBands] {}, bandHi[kBands] {};
    void prepare (double rate)
    {
        fs = rate;
        for (int i = 0; i < N; ++i) win[i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / (N - 1)));
        for (int b = 0; b < kBands; ++b)
        {
            const double f0 = 40.0 * std::pow (500.0, (double) b / kBands), f1 = 40.0 * std::pow (500.0, (double) (b + 1) / kBands);
            bandLo[b] = clampi ((int) std::floor (f0 * N / fs), 1, N / 2 - 1);
            bandHi[b] = clampi ((int) std::ceil (f1 * N / fs), bandLo[b] + 1, N / 2);
        }
        for (int b = 0; b < kBands; ++b) bandIn[b] = bandOut[b] = -120.f;
        fill = 0;
    }
    void fft()
    {
        for (int i = 1, j = 0; i < N; ++i)
        {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const double a = -2 * kPi / len; const float wr = (float) std::cos (a), wi = (float) std::sin (a);
            for (int i = 0; i < N; i += len)
            {
                float cr = 1.f, ci = 0.f;
                for (int k = 0; k < len / 2; ++k)
                {
                    const int p = i + k, q = p + len / 2;
                    const float xr = re[q] * cr - im[q] * ci, xi = re[q] * ci + im[q] * cr;
                    re[q] = re[p] - xr; im[q] = im[p] - xi; re[p] += xr; im[p] += xi;
                    const float t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
                }
            }
        }
    }
    void spectrum (const float* x, float* bands)
    {
        for (int i = 0; i < N; ++i) { re[i] = x[i] * win[i]; im[i] = 0.f; }
        fft();
        for (int b = 0; b < kBands; ++b)
        {
            float pk = 0.f;
            for (int k = bandLo[b]; k < bandHi[b]; ++k) pk = std::fmax (pk, re[k] * re[k] + im[k] * im[k]);
            const float db = 10.f * std::log10 (pk * (4.f / ((float) N * N)) + 1e-14f) + 6.f;     // ~0 dB for a full-scale sine
            bands[b] = db > bands[b] ? db : std::fmax (db, bands[b] - 6.f);
        }
    }
    // feed one host sample; returns true when new spectra are ready
    bool feed (float in, float out, float tap)
    {
        tapHist[tapW] = tap; tapW = (tapW + 1) % kHist;
        inBuf[fill] = in; outBuf[fill] = out;
        if (++fill < N) return false;
        fill = 0;
        if ((turn ^= 1) != 0) spectrum (inBuf, bandIn); else spectrum (outBuf, bandOut);   // one FFT per frame
        return true;
    }
    // capture the scope trace over `ms` milliseconds ending ~now, triggered on a rising zero crossing
    void captureScope (float ms)
    {
        const int span = clampi ((int) (ms * 0.001 * fs), kScope, kHist / 2);
        int start = (tapW - span - 1 + 2 * kHist) % kHist;
        for (int s = 0; s < kHist / 2 - span; ++s)                               // search backwards for a trigger
        {
            const int i = (tapW - span - 1 - s + 2 * kHist) % kHist, j = (i + kHist - 1) % kHist;
            if (tapHist[j] < 0.f && tapHist[i] >= 0.f) { start = i; break; }
        }
        for (int p = 0; p < kScope; ++p) scope[p] = tapHist[(start + p * span / kScope) % kHist];
    }
};
} // namespace sp
