// Small math helpers so the engine needs no libm: the MPC's plugin loader refuses shared objects that depend on it.
// Accuracy is far better than the audio needs (about 1e-5 relative); the test suite checks the results.
#pragma once
#include <cstdint>

namespace quanta {

inline float qabs(float x) { return x < 0.f ? -x : x; }
inline float qmax(float a, float b) { return a > b ? a : b; }

// 2^x for float
inline float qexp2(float x) {
    if (x > 126.f) x = 126.f;
    if (x < -126.f) return 0.f;
    int fl = (int)(x < 0.f ? x - 1.f : x);
    float f = x - (float)fl;
    float p = 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)(fl + 127) << 23;
    return v.f * p;
}

// log2 for x > 0
inline float qlog2(float x) {
    union { float f; uint32_t u; } v;
    v.f = x;
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;   // mantissa in [1, 2)
    float m = v.f;
    float t = (m - 1.f) / (m + 1.f), t2 = t * t;
    float ln = 2.f * t * (1.f + t2 * (1.f / 3.f + t2 * (1.f / 5.f + t2 * (1.f / 7.f + t2 * (1.f / 9.f)))));
    return (float)e + ln * 1.4426950f;
}

inline float qlog10(float x) { return qlog2(x) * 0.30103f; }

// 10^(db/20) for a level in dB
inline float qdb2lin(float db) { return qexp2(db * (3.3219281f / 20.f)); }

inline float qtanh(float x) {
    if (x > 9.f) return 1.f;
    if (x < -9.f) return -1.f;
    float e = qexp2(2.f * x * 1.4426950f);
    return (e - 1.f) / (e + 1.f);
}

// double precision helpers for filter design (run when parameters change, never per sample)
inline double qexp2d(double x) {
    int fl = (int)(x < 0.0 ? x - 1.0 : x);
    double f = x - (double)fl;
    double p = 1.0, term = 1.0, ln2 = 0.69314718055994530942;
    for (int k = 1; k <= 12; ++k) { term *= f * ln2 / (double)k; p += term; }
    union { double d; uint32_t w[2]; } v;   // 2^fl from its exponent bits (little-endian words); no 64-bit integers
    v.w[0] = 0;
    v.w[1] = (uint32_t)(fl + 1023) << 20;
    return v.d * p;
}

inline double qsin(double x) {
    const double twoPi = 6.28318530717958647692, pi = 3.14159265358979323846;
    x -= twoPi * (double)(int)(x / twoPi);   // 32-bit conversion: 64-bit ones pull in libgcc
    if (x > pi) x -= twoPi;
    if (x < -pi) x += twoPi;
    double x2 = x * x, term = x, s = x;
    for (int k = 1; k <= 8; ++k) { term *= -x2 / (double)((2 * k) * (2 * k + 1)); s += term; }
    return s;
}

inline double qcos(double x) { return qsin(x + 1.57079632679489661923); }

}  // namespace quanta
