/* Pad layout tests: chord pads in order of first appearance, melody pads sorted by pitch, folding past 16,
   and pad-to-note numbers. Uses hand-made reports with known contents. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pad_layout.h"

static int failures = 0;
static void check(int ok, const char* what) {
    printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

/* Chord states: root * 9 + quality. C maj = 0, A m = 82, F maj = 45, G maj = 63. */
static void chords_report(rm_report* r, const int* states, size_t n) {
    memset(r, 0, sizeof *r);
    r->sr = 44100;
    r->nchords = n;
    r->chords = malloc(n * sizeof(cc_segment));
    for (size_t i = 0; i < n; ++i) {
        r->chords[i].start_frame = i * 10;
        r->chords[i].end_frame = i * 10 + 9;
        r->chords[i].state = states[i];
    }
}

static void notes_report(rm_report* r, const int* pitches, size_t n) {
    memset(r, 0, sizeof *r);
    r->sr = 44100;
    r->nnotes = n;
    r->notes = malloc((n ? n : 1) * sizeof(vi_poly_note));
    for (size_t i = 0; i < n; ++i) r->notes[i] = (vi_poly_note){ i * 0.1, i * 0.1 + 0.05, pitches[i], 1.0 };
}

int main(void) {
    printf("chord pads\n");
    {
        const int seq[] = { 0, 82, 0, 45, 63, 0, 82, 108 };        /* C Am C F G C Am N */
        rm_report r;
        chords_report(&r, seq, sizeof seq / sizeof seq[0]);
        int states[RM_PADS];
        size_t k = rm_chord_layout(&r, states);
        check(k == 4, "four distinct chords, N has no pad");
        check(states[0] == 0 && states[1] == 82 && states[2] == 45 && states[3] == 63,
              "pads follow the order of first appearance: C, Am, F, G");
        check(rm_chord_pad(states, k, 45) == 2, "F maj is on pad 2");
        check(rm_chord_pad(states, k, 0) == 0, "repeated C maj stays on pad 0");
        check(rm_chord_pad(states, k, 5) == -1, "a chord not in the progression has no pad");
        check(rm_pad_note(0) == 36 && rm_pad_note(3) == 39, "pad 1 sends 36 and pad 4 sends 39");
        rm_report_free(&r);
    }
    {
        int seq[20];
        for (int i = 0; i < 20; ++i) seq[i] = i * 5 % 108;        /* 20 distinct chord states */
        rm_report r;
        chords_report(&r, seq, 20);
        int states[RM_PADS];
        size_t k = rm_chord_layout(&r, states);
        check(k == 16, "more than 16 distinct chords: the first 16 get pads");
        check(rm_chord_pad(states, k, seq[19]) == -1, "a chord past the 16th has no pad");
        rm_report_free(&r);
    }

    printf("melody pads\n");
    {
        const int pitches[] = { 60, 62, 64, 65, 67, 60, 64 };
        rm_report r;
        notes_report(&r, pitches, 7);
        int layout[RM_PADS];
        size_t folded = 99;
        size_t m = rm_melody_layout(&r, -12, layout, &folded);
        check(m == 5 && folded == 0, "five distinct pitches after -12, nothing folded");
        check(layout[0] == 48 && layout[1] == 50 && layout[2] == 52 && layout[3] == 53 && layout[4] == 55,
              "pads sorted from the lowest pitch: 48, 50, 52, 53, 55");
        check(rm_melody_pad(layout, m, 52) == 2, "pitch 52 is on pad 2");
        check(rm_melody_pad(layout, m, 51) == 1, "pitch 51 is between two pads and goes to the lower one (50)");
        check(rm_melody_pad(layout, m, 30) == 0, "a pitch below the layout goes to the lowest pad");
        check(rm_melody_pad(layout, 0, 52) == -1, "an empty layout has no pad");
        rm_report_free(&r);
    }
    {
        int pitches[20];
        for (int i = 0; i < 20; ++i) pitches[i] = 40 + i;          /* 20 distinct pitches: 40..59 */
        rm_report r;
        notes_report(&r, pitches, 20);
        int layout[RM_PADS];
        size_t folded = 0;
        size_t m = rm_melody_layout(&r, 0, layout, &folded);
        check(m == 16 && folded == 4, "20 distinct pitches: 16 pads, 4 folded");
        check(layout[0] == 40 && layout[15] == 55, "the lowest 16 pitches have the pads");
        check(rm_melody_pad(layout, m, 75) == 15, "a pitch above the layout folds onto the top pad");
        rm_report_free(&r);
    }
    {
        const int pitches[] = { 0, 60 };
        rm_report r;
        notes_report(&r, pitches, 2);
        int layout[RM_PADS];
        size_t folded = 0;
        size_t m = rm_melody_layout(&r, -2, layout, &folded);
        check(m == 1 && layout[0] == 60 - 2, "a note that transposes out of range is left off the layout");
        rm_report_free(&r);
    }

    printf("\n%s\n", failures ? "PAD LAYOUT TESTS FAILED" : "pad layout tests passed");
    return failures ? 1 : 0;
}
