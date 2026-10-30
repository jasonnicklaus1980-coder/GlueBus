/* Analysis of one recording: tempo, key, chords and the polyphonic melody. Plain C, libc and libm only.
   Everything here is measured from the audio. Nothing is generated or guessed. */
#ifndef REMIX_ANALYZE_H
#define REMIX_ANALYZE_H
#include <stddef.h>
#include <stdio.h>
#include "analysis/chordcore.h"
#include "analysis/poly.h"

typedef struct {
    double sr;
    size_t frames;
    double seconds;
    double bpm, bpm_conf;
    int key;                     /* 0..11 major tonic C..B, 12..23 minor; see vi_key_name() */
    double key_conf;
    cc_segment* chords;          /* chord segments from the recording */
    size_t nchords;
    vi_poly_note* notes;         /* polyphonic note transcription */
    size_t nnotes;
} rm_report;

/* Analyses a mono buffer. Returns 0 on success. Call rm_report_free() afterwards. */
int rm_analyze(const float* mono, size_t n, double sr, rm_report* r);
void rm_report_free(rm_report* r);

/* Start and end of chord segment i, in seconds. Segments count analysis frames of CC_HOP samples. */
double rm_chord_start(const rm_report* r, size_t i);
double rm_chord_end(const rm_report* r, size_t i);

/* Human-readable summary. */
void rm_print(const rm_report* r, FILE* f);

#endif
