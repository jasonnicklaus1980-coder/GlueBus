/* Pad layout: which chord or melody pitch sits on which of the 16 pads of a bank. Plain C, libc only.
   Pads are numbered 0..15 (the MPC's pad 1..16). The MIDI note a pad sends is RM_PAD_BASE_NOTE + pad.
   Chords and melody are laid out separately: put them on different banks so they never share a pad. */
#ifndef REMIX_PAD_LAYOUT_H
#define REMIX_PAD_LAYOUT_H
#include <stddef.h>
#include "analyze.h"

#define RM_PADS 16
/* Assumed: the MPC's default bank sends notes 36..51 from pad 1..16. Check on the device before relying on it. */
#define RM_PAD_BASE_NOTE 36

/* Chord layout: each distinct chord (chord state) of the report gets its own pad, in order of first appearance.
   Fills states[0..k-1] with the chord state on each pad and returns k (at most 16). Chords beyond the 16th distinct
   one are not on a pad: rm_chord_pad() returns -1 for them. */
size_t rm_chord_layout(const rm_report* r, int states[RM_PADS]);

/* Pad index (0..k-1) for a chord state in a layout of k pads, or -1 if the state has no pad. */
int rm_chord_pad(const int states[], size_t k, int state);

/* Melody layout: each distinct pitch (after transposition by transpose semitones) gets a pad, sorted from the lowest
   pitch. Fills pitches[0..m-1] and returns m (at most 16). Melody notes whose pitch is not on a pad are folded to the
   nearest pad pitch by rm_melody_pad(); *folded counts how many distinct pitches had to be folded. */
size_t rm_melody_layout(const rm_report* r, int transpose, int pitches[RM_PADS], size_t* folded);

/* Pad index for a pitch: the exact pad, or the nearest one when the pitch is not on a pad. -1 if the layout is empty. */
int rm_melody_pad(const int pitches[], size_t m, int pitch);

/* MIDI note a pad sends. */
static inline int rm_pad_note(int pad) { return RM_PAD_BASE_NOTE + pad; }

#endif
