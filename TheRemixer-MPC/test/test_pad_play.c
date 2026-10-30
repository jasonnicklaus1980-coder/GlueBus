/* Pad play tests: what each pad sends when pressed. */
#include <stdio.h>
#include "pad_play.h"

static int failures = 0;
static void check(int ok, const char* what) {
    printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main(void) {
    printf("pad play\n");
    const int states[4] = { 0, 82, 45, 63 };        /* C, Am, F, G */
    int out[8];

    int n = rm_chord_pad_notes(states, 4, 0, 0, 0, out);
    check(n == 3 && out[0] == 48 && out[1] == 52 && out[2] == 55, "C pad sends C major voicing 48, 52, 55");
    n = rm_chord_pad_notes(states, 4, 1, 0, 0, out);
    check(n == 3 && out[0] == 57 && out[1] == 60 && out[2] == 64, "Am pad sends A minor voicing 57, 60, 64");
    n = rm_chord_pad_notes(states, 4, 1, 0, 1, out);
    check(n == 3 && out[0] == 60 && out[2] == 69, "inversion 1 moves the root of Am up an octave");
    n = rm_chord_pad_notes(states, 4, 0, 12, 0, out);
    check(n == 3 && out[0] == 60, "transpose +12 shifts the chord up an octave");
    check(rm_chord_pad_notes(states, 4, 7, 0, 0, out) == 0, "a pad with no chord sends nothing");
    check(rm_chord_pad_notes(states, 4, 0, 100, 0, out) == 0, "a voicing that leaves the MIDI range sends nothing");

    printf("pad mapping\n");
    check(rm_pad_from_note(36) == 0 && rm_pad_from_note(51) == 15, "notes 36..51 map to pads 0..15");
    check(rm_pad_from_note(35) == -1 && rm_pad_from_note(52) == -1, "notes outside the bank map to no pad");

    int pitches[3] = { 60, 62, 64 };
    check(rm_melody_pad_note(pitches, 3, 1) == 62, "melody pad 2 sends 62");
    check(rm_melody_pad_note(pitches, 3, 5) == -1, "an empty melody pad sends nothing");

    printf("\n%s\n", failures ? "PAD PLAY TESTS FAILED" : "pad play tests passed");
    return failures ? 1 : 0;
}
