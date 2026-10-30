/* Polyphonic note transcription (harmonic salience per pitch, peak picking, note tracking), plain C.
   Works on a mono buffer. Notes are returned as (start, end, MIDI pitch). */
#ifndef POLY_H
#define POLY_H
#include <stddef.h>

typedef struct { double start, end; int pitch; double strength; } vi_poly_note;

/* Returns the number of notes written (at most max_notes), or -1 on bad input.
   min_ms: shortest note kept. Pitches searched: MIDI 36..96. */
long vi_poly_notes(const float* x, size_t n, double sr, double min_ms, vi_poly_note* out, size_t max_notes);

#endif
