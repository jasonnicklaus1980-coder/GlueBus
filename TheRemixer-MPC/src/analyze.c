#include "analyze.h"
#include "analysis/keys.h"
#include "tempo.h"
#include <stdlib.h>
#include <string.h>

#define RM_MAX_NOTES 65536
/* A chord whose audio fits it less well than this is shown as N (no chord). Provisional: measured on synthetic chords
   (0.99), white noise (0.61) and sound effects (0.70 to 0.86). Set from real recordings before relying on it. */
#define RM_CHORD_MIN_FIT 0.85
#define RM_STATE_NONE (12 * 9)

int rm_analyze(const float* mono, size_t n, double sr, rm_report* r) {
    memset(r, 0, sizeof *r);
    if (!mono || sr <= 0 || n == 0) return -1;
    r->sr = sr;
    r->frames = n;
    r->seconds = (double)n / sr;

    r->bpm = rm_estimate_bpm(mono, n, sr, &r->bpm_conf);

    /* polyphonic notes, then the key from their duration-weighted pitch classes */
    r->notes = malloc(sizeof(vi_poly_note) * RM_MAX_NOTES);
    if (!r->notes) return -1;
    long nn = vi_poly_notes(mono, n, sr, 60.0, r->notes, RM_MAX_NOTES);
    r->nnotes = nn > 0 ? (size_t)nn : 0;

    double hist[12] = {0};
    for (size_t i = 0; i < r->nnotes; ++i) {
        int pc = ((r->notes[i].pitch % 12) + 12) % 12;
        hist[pc] += (r->notes[i].end - r->notes[i].start) * r->notes[i].strength;
    }
    r->key = vi_estimate_key(hist, &r->key_conf);

    /* chords: needs at least one analysis frame */
    if (n >= CC_NFFT) {
        size_t cap = n / CC_HOP + 16;
        r->chords = malloc(sizeof(cc_segment) * cap);
        if (!r->chords) return -1;
        size_t count = 0;
        if (cc_analyze(mono, n, sr, r->chords, cap, &count) < 0) count = 0;
        r->nchords = count;
        r->chord_fit = malloc((count ? count : 1) * sizeof(double));
        if (!r->chord_fit) return -1;
        for (size_t i = 0; i < count; ++i) {
            r->chord_fit[i] = cc_segment_fit(mono, n, sr, &r->chords[i]);
            if (r->chord_fit[i] < RM_CHORD_MIN_FIT) r->chords[i].state = RM_STATE_NONE;
        }
    }
    return 0;
}

double rm_chord_start(const rm_report* r, size_t i) {
    return (double)r->chords[i].start_frame * CC_HOP / r->sr;
}
double rm_chord_end(const rm_report* r, size_t i) {
    return (double)(r->chords[i].end_frame + 1) * CC_HOP / r->sr;
}

void rm_report_free(rm_report* r) {
    free(r->chords);
    free(r->chord_fit);
    free(r->notes);
    memset(r, 0, sizeof *r);
}

void rm_print(const rm_report* r, FILE* f) {
    fprintf(f, "Duration: %.2f s (%zu frames at %.0f Hz)\n", r->seconds, r->frames, r->sr);
    if (r->bpm > 0)
        fprintf(f, "Tempo:    %.1f BPM (confidence %.2f)\n", r->bpm, r->bpm_conf);
    else
        fprintf(f, "Tempo:    not found\n");
    fprintf(f, "Key:      %s (confidence %.2f)\n", vi_key_name(r->key), r->key_conf);
    fprintf(f, "Notes:    %zu\n", r->nnotes);
    fprintf(f, "Chords:   %zu segments\n", r->nchords);
    char name[16];
    for (size_t i = 0; i < r->nchords; ++i) {
        cc_state_name(r->chords[i].state, name);
        /* segments count analysis frames of CC_HOP samples each */
        fprintf(f, "  %7.2f - %7.2f  %-6s fit %.2f\n", rm_chord_start(r, i), rm_chord_end(r, i), name, r->chord_fit[i]);
    }
}
