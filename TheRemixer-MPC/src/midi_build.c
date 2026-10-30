#include "midi_build.h"
#include <math.h>

#define RM_CHORD_VELOCITY 90
#define RM_MELODY_VELOCITY 100

double rm_quantize(double t, double bpm, double grid_beats) {
    if (grid_beats <= 0 || bpm <= 0) return t;
    double grid = 60.0 / bpm * grid_beats;        /* grid length in seconds */
    return floor(t / grid + 0.5) * grid;
}

size_t rm_chord_notes(const rm_report* r, int transpose, int inversion, vi_note* out, size_t cap) {
    size_t m = 0;
    int pitches[8];
    for (size_t i = 0; i < r->nchords && m < cap; ++i) {
        int k = vi_voice(r->chords[i].state, transpose, inversion, 0, pitches);
        if (k <= 0) continue;     /* N (no chord) or out of range */
        double on = rm_chord_start(r, i), off = rm_chord_end(r, i);
        for (int j = 0; j < k && m < cap; ++j) {
            out[m].start = on;
            out[m].end = off;
            out[m].pitch = pitches[j];
            out[m].velocity = RM_CHORD_VELOCITY;
            ++m;
        }
    }
    return m;
}

size_t rm_melody_notes(const rm_report* r, int transpose, double bpm, double grid_beats, vi_note* out, size_t cap) {
    size_t m = 0;
    for (size_t i = 0; i < r->nnotes && m < cap; ++i) {
        int pitch = r->notes[i].pitch + transpose;
        if (pitch < 0 || pitch > 127) continue;
        double on = rm_quantize(r->notes[i].start, bpm, grid_beats);
        double off = rm_quantize(r->notes[i].end, bpm, grid_beats);
        if (grid_beats > 0 && off <= on) continue;     /* collapsed to nothing on the grid */
        out[m].start = on;
        out[m].end = off;
        out[m].pitch = pitch;
        out[m].velocity = RM_MELODY_VELOCITY;
        ++m;
    }
    return m;
}
