/* What a pad sends when it is pressed: a chord pad plays its whole voiced chord, a melody pad plays its note.
   Plain C, libc only. The pad layout comes from pad_layout.h. */
#ifndef REMIX_PAD_PLAY_H
#define REMIX_PAD_PLAY_H
#include <stddef.h>
#include "pad_layout.h"

/* Pad index (0..15) for an incoming MIDI note on bank A, or -1 if the note is outside the 16 pads of the bank. */
int rm_pad_from_note(int note);

/* Notes a chord pad sends: the voicing of its chord, transposed and inverted as requested. Writes up to 8 notes into
   out and returns the count. Returns 0 if the pad has no chord (pad >= k) or the voicing leaves the MIDI range. */
int rm_chord_pad_notes(const int states[], size_t k, int pad, int transpose, int inversion, int out[8]);

/* Note a melody pad sends: the pitch on that pad, or -1 if the pad is empty. */
int rm_melody_pad_note(const int pitches[], size_t m, int pad);

#endif
