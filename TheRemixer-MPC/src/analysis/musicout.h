/* MIDI file writing, chord voicing and pad sheets, in plain C. */
#ifndef MUSICOUT_H
#define MUSICOUT_H
#include <stddef.h>

typedef struct { double start, end; int pitch, velocity; } vi_note;

/* Writes a format-0 Standard MIDI File at the given tempo. Returns 0 on success. */
int vi_write_smf(const char* path, const vi_note* notes, size_t count, double bpm);

/* Voicing of a chord state (0..107) with transposition (semitones), inversion (0..2) and open spread.
   Writes up to 8 notes into out and returns the count, or -1 if a note leaves the MIDI range. */
int vi_voice(int state, int transpose, int inversion, int spread, int* out);

#endif
