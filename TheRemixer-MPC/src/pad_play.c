#include "pad_play.h"
#include "analysis/musicout.h"

int rm_pad_from_note(int note) {
    int pad = note - RM_PAD_BASE_NOTE;
    return pad >= 0 && pad < RM_PADS ? pad : -1;
}

int rm_chord_pad_notes(const int states[], size_t k, int pad, int transpose, int inversion, int out[8]) {
    if (pad < 0 || (size_t)pad >= k) return 0;
    int n = vi_voice(states[pad], transpose, inversion, 0, out);
    return n > 0 ? n : 0;
}

int rm_melody_pad_note(const int pitches[], size_t m, int pad) {
    if (pad < 0 || (size_t)pad >= m) return -1;
    return pitches[pad];
}
