/* Tempo estimate from a mono buffer: onset envelope, then autocorrelation over the 60 to 200 BPM range.
   Plain C, libc only. */
#ifndef REMIX_TEMPO_H
#define REMIX_TEMPO_H
#include <stddef.h>

/* Returns the tempo in BPM (0 if the buffer is too short or silent). *confidence in 0..1: strength of the
   chosen periodicity relative to the zero-lag energy of the onset envelope. */
double rm_estimate_bpm(const float* x, size_t n, double sr, double* confidence);

#endif
