/* Reads a RIFF WAV file (PCM 16/24/32-bit, IEEE float 32-bit, or WAVE_FORMAT_EXTENSIBLE) and mixes it to mono.
   Plain C, libc only. */
#ifndef REMIX_WAV_READ_H
#define REMIX_WAV_READ_H
#include <stddef.h>

/* Returns a malloc'd mono buffer (caller frees), with *n frames and *sr sample rate. NULL on error. */
float* rm_wav_read_mono(const char* path, size_t* n, double* sr);

#endif
