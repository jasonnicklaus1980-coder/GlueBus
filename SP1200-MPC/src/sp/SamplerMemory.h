#pragma once
// SamplerMemory: the sample RAM, holding converter codes (not floats) at the sampling clock.
// 65536 words = 2.52 s at 26.04 kHz, the length of one SP-1200 sample bank (2.5 s per bank, 10 s total).
#include "Dsp.h"

namespace sp
{
struct SamplerMemory
{
    static constexpr int kLen = 1 << 16, kMask = kLen - 1;
    int16_t m[kLen];
    long w = 0;                                 // words written (absolute index)
    void reset() { std::memset (m, 0, sizeof m); w = 0; }
    void write (int code) { m[w & kMask] = (int16_t) code; ++w; }
    int read (long abs) const { return abs < 0 || abs >= w || w - abs > kLen ? 0 : m[abs & kMask]; }
    int latest() const { return w > 0 ? m[(w - 1) & kMask] : 0; }
};
} // namespace sp
