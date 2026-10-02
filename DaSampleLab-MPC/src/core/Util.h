#pragma once
// Small shared helpers: maths without new glibc symbols, relaxed atomics for values shared between threads,
// double-buffered display text, a fast random generator and a monotonic clock.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <time.h>

namespace cp
{
// fmod without libm's fmod (its newest ARM symbol version needs glibc 2.38; MPC OS has 2.34)
inline double fmodd (double a, double b) { return a - b * std::trunc (a / b); }
inline double wrap01 (double x) { return x - std::floor (x); }
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double clampd (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline int clampi (int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float dbToGain (float db) { return db <= -59.9f ? 0.f : std::pow (10.f, db * 0.05f); }
inline float gainToDb (float g) { return g <= 1e-6f ? -120.f : 20.f * std::log10 (g); }
constexpr double kPi = 3.14159265358979323846;

// A value written by one thread and read by others. Relaxed ordering: every field stands on its own.
template <typename T> struct Rel
{
    std::atomic<T> a;
    Rel (T v = T()) : a (v) {}
    Rel (const Rel& o) : a (o.a.load (std::memory_order_relaxed)) {}
    Rel& operator= (const Rel& o) { a.store (o.a.load (std::memory_order_relaxed), std::memory_order_relaxed); return *this; }
    Rel& operator= (T v) { a.store (v, std::memory_order_relaxed); return *this; }
    operator T() const { return a.load (std::memory_order_relaxed); }
};

// Text shown on the screen: written into the idle half, published by flipping the index.
struct Text
{
    char buf[2][160] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    bool publish (const char* s)
    {
        if (std::strcmp (get(), s) == 0) return false;
        const int idle = 1 - live.load();
        std::snprintf (buf[idle], sizeof buf[idle], "%s", s);
        live.store (idle);
        return true;
    }
};

struct Rng
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) { s = v ? v : 0x9e3779b9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (next() >> 8) * (1.f / 16777216.f); }                 // [0, 1)
    float bi() { return uni() * 2.f - 1.f; }                                  // [-1, 1)
    int below (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
};

inline double nowSeconds()
{
    timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Integer parse without atoi/strtol: current glibc headers map those to __isoc23_strtol (GLIBC_2.38),
// which MPC OS (glibc 2.34) doesn't have.
inline int toInt (const char* s)
{
    while (*s == ' ') ++s;
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    long v = 0;
    while (*s >= '0' && *s <= '9' && v < 100000000L) v = v * 10 + (*s++ - '0');
    return (int) (neg ? -v : v);
}

inline void copyStr (void* dst, const char* src, size_t max)
{
    if (dst == nullptr) return;
    std::strncpy ((char*) dst, src, max - 1);
    ((char*) dst)[max - 1] = 0;
}

// "C1" = MIDI note 36 (Akai / MPC naming: middle C 60 = C3)
inline void noteName (int n, char* out, size_t max)
{
    static const char* const k[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (n < 0 || n > 127) { std::snprintf (out, max, "Off"); return; }
    std::snprintf (out, max, "%s%d (%d)", k[n % 12], n / 12 - 2, n);
}
} // namespace cp
