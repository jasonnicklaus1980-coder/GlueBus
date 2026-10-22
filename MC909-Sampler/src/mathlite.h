// Small math helpers so the MPC build needs only libc: no libm symbols (pow, exp, floor, ldexp).
// Accuracy is about 1e-15 relative for the range used here (pitch ratios and envelope coefficients).
#pragma once

#include <cstdint>
#include <cstring>

namespace mc909 {

// 2^n as a double, for n in [-1022, 1023]
inline double pow2i(int n)
{
    const uint64_t bits = (uint64_t)(n + 1023) << 52;
    double d;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

// e^x, by splitting x = k*ln2 + r with |r| <= ln2/2 and a Taylor series for r
inline double expd(double x)
{
    const double ln2 = 0.6931471805599453;
    if (x > 700.0) x = 700.0;
    if (x < -700.0) x = -700.0;
    const int k = x >= 0 ? (int)(x / ln2 + 0.5) : -(int)(-x / ln2 + 0.5);
    const double r = x - k * ln2;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 16; ++i) { term *= r / i; sum += term; }
    return sum * pow2i(k);
}

// v * 2^e
inline double ldexpd(double v, int e)
{
    while (e > 1000) { v *= pow2i(1000); e -= 1000; }
    while (e < -1000) { v *= pow2i(-1000); e += 1000; }
    return v * pow2i(e);
}

// largest integer not above x (for the non-negative values used here, and the negative ones too)
inline double floord(double x)
{
    const long long i = (long long)x;
    return x < (double)i ? (double)(i - 1) : (double)i;
}

}  // namespace mc909
