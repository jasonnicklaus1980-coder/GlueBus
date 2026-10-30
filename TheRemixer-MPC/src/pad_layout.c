#include "pad_layout.h"
#include <stdlib.h>

size_t rm_chord_layout(const rm_report* r, int states[RM_PADS]) {
    size_t k = 0;
    for (size_t i = 0; i < r->nchords; ++i) {
        int s = r->chords[i].state;
        if (s >= 12 * 9) continue;          /* N: no chord, no pad */
        int seen = 0;
        for (size_t j = 0; j < k; ++j) if (states[j] == s) { seen = 1; break; }
        if (!seen && k < RM_PADS) states[k++] = s;
    }
    return k;
}

int rm_chord_pad(const int states[], size_t k, int state) {
    for (size_t j = 0; j < k; ++j) if (states[j] == state) return (int)j;
    return -1;
}

static int cmp_int(const void* a, const void* b) {
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

size_t rm_melody_layout(const rm_report* r, int transpose, int pitches[RM_PADS], size_t* folded) {
    *folded = 0;
    int all[r->nnotes ? r->nnotes : 1];
    size_t n = 0;
    for (size_t i = 0; i < r->nnotes; ++i) {
        int p = r->notes[i].pitch + transpose;
        if (p >= 0 && p <= 127) all[n++] = p;
    }
    qsort(all, n, sizeof all[0], cmp_int);

    /* distinct, ascending */
    size_t m = 0;
    int distinct[r->nnotes ? r->nnotes : 1];
    for (size_t i = 0; i < n; ++i)
        if (m == 0 || distinct[m - 1] != all[i]) distinct[m++] = all[i];

    /* at most 16 pads: keep the lowest 16 pitches; the rest fold onto the nearest pad */
    size_t keep = m < RM_PADS ? m : RM_PADS;
    for (size_t i = 0; i < keep; ++i) pitches[i] = distinct[i];
    *folded = m > keep ? m - keep : 0;
    return keep;
}

int rm_melody_pad(const int pitches[], size_t m, int pitch) {
    if (m == 0) return -1;
    int best = 0, best_d = abs(pitches[0] - pitch);
    for (size_t j = 1; j < m; ++j) {
        int d = abs(pitches[j] - pitch);
        if (d < best_d) { best_d = d; best = (int)j; }
    }
    return best;
}
