/* Turns an analysis report into editable note lists: voiced chords and melody, with transposition and grid
   quantization. Plain C, libc and libm only. Times are seconds, pitches are MIDI note numbers. */
#ifndef REMIX_MIDI_BUILD_H
#define REMIX_MIDI_BUILD_H
#include <stddef.h>
#include "analysis/musicout.h"
#include "analyze.h"

/* Snaps t (seconds) to the nearest multiple of grid_beats at tempo bpm. grid_beats <= 0 returns t unchanged. */
double rm_quantize(double t, double bpm, double grid_beats);

/* Voiced chord notes for every chord segment of the report, shifted by transpose semitones. inversion 0..2.
   Chords whose voicing leaves the MIDI range are skipped. Returns the number written (at most cap). */
size_t rm_chord_notes(const rm_report* r, int transpose, int inversion, vi_note* out, size_t cap);

/* Melody notes from the polyphonic transcription, shifted by transpose semitones. With grid_beats > 0 and a tempo,
   note starts and ends are quantized to the grid, and a note that collapses to zero length is dropped. Notes whose
   pitch leaves 0..127 are dropped. Returns the number written (at most cap). */
size_t rm_melody_notes(const rm_report* r, int transpose, double bpm, double grid_beats, vi_note* out, size_t cap);

#endif
