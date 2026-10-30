/* Harmonic-percussive separation in plain C (median filtering of the STFT, Fitzgerald 2010).
   Keeps the sustained (melodic) part of a mono buffer and removes drum hits. */
#ifndef HPSS_H
#define HPSS_H
#include <stddef.h>

/* out must hold n floats. Returns 0 on success, -1 on bad input or out of memory. */
int vi_hpss(const float* x, size_t n, double sr, float* out);

#endif
