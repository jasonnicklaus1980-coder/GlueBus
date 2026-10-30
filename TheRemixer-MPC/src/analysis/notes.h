/* Monophonic note extraction (bass or lead line) to MIDI notes, plain C. Uses the YIN pitch estimator. */
#ifndef NOTES_H
#define NOTES_H
#include <stddef.h>

typedef struct { double start, end; int pitch; double confidence; } vi_line_note;

/* Extracts notes from a mono buffer. Returns the number of notes written (at most max_notes), or -1 on bad input.
   fmin/fmax bound the search (Hz). A note is a run of frames with the same rounded MIDI pitch, at least min_ms long. */
long vi_extract_notes(const float* x, size_t n, double sr, double fmin, double fmax,
                      double min_ms, vi_line_note* out, size_t max_notes);

#endif
