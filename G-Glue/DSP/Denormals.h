#pragma once
// Scoped flush-to-zero / denormals-are-zero for the audio callback (x86 SSE and 32/64-bit ARM).
#include <cstdint>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
 #define GGLUE_SSE 1
#endif

namespace gglue
{
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals()
    {
#if GGLUE_SSE
        saved = _mm_getcsr(); _mm_setcsr (saved | 0x8040);          // FTZ | DAZ
#elif defined(__aarch64__)
        uint64_t f; asm volatile ("mrs %0, fpcr" : "=r" (f)); saved = f; f |= (1ull << 24); asm volatile ("msr fpcr, %0" :: "r" (f));
#elif defined(__arm__) && defined(__ARM_PCS_VFP)
        uint32_t f; asm volatile ("vmrs %0, fpscr" : "=r" (f)); saved = f; f |= (1u << 24); asm volatile ("vmsr fpscr, %0" :: "r" (f));
#endif
    }
    ~ScopedFlushDenormals()
    {
#if GGLUE_SSE
        _mm_setcsr ((unsigned) saved);
#elif defined(__aarch64__)
        uint64_t f = saved; asm volatile ("msr fpcr, %0" :: "r" (f));
#elif defined(__arm__) && defined(__ARM_PCS_VFP)
        uint32_t f = (uint32_t) saved; asm volatile ("vmsr fpscr, %0" :: "r" (f));
#endif
    }
    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;
private:
    uint64_t saved = 0;
};
} // namespace gglue
