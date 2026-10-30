/* remixer_analyze: analyse one WAV recording and write the results.
   Usage: remixer_analyze [-t semitones] [-i inversion] [-g grid_beats] recording.wav [outdir]
     -t  transpose the chords and melody by this many semitones (default 0)
     -i  chord inversion, 0..2 (default 0)
     -g  quantize the melody to this grid in beats, e.g. 0.25 for 16ths; 0 leaves it free (default 0)
   Prints the report. Writes into outdir (default "."):
     chords.txt  one chord per line: start, end, name (seconds)
     chords.mid  the chords voiced as MIDI, at the detected tempo (120 BPM if the tempo is not trusted)
     melody.mid  the polyphonic melody as MIDI, transposed and quantized as requested
   Analysis runs on the mono mix of the file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analyze.h"
#include "analysis/musicout.h"
#include "midi_build.h"
#include "wav_read.h"

static int write_text_chords(const char* path, const rm_report* r) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    char name[16];
    for (size_t i = 0; i < r->nchords; ++i) {
        cc_state_name(r->chords[i].state, name);
        fprintf(f, "%.3f\t%.3f\t%s\n", rm_chord_start(r, i), rm_chord_end(r, i), name);
    }
    fclose(f);
    return 0;
}

static int write_midi(const char* path, const vi_note* notes, size_t n, double bpm) {
    int rc = vi_write_smf(path, notes, n, bpm);
    if (rc != 0) fprintf(stderr, "cannot write %s\n", path);
    return rc;
}

/* Below this tempo confidence there is no clear beat (a sustained chord progression scores about 0.5; a click track
   about 0.9), so MIDI and the grid use 120 BPM instead. */
#define TRUST_TEMPO_CONF 0.7

int main(int argc, char** argv) {
    int transpose = 0, inversion = 0;
    double grid = 0.0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i += 2) {
        if (i + 1 >= argc) { fprintf(stderr, "option %s needs a value\n", argv[i]); return 2; }
        if (strcmp(argv[i], "-t") == 0) transpose = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-i") == 0) inversion = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-g") == 0) grid = atof(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (i >= argc) {
        fprintf(stderr, "usage: %s [-t semitones] [-i inversion] [-g grid_beats] recording.wav [outdir]\n", argv[0]);
        return 2;
    }
    const char* input = argv[i];
    const char* outdir = i + 1 < argc ? argv[i + 1] : ".";

    size_t n = 0;
    double sr = 0;
    float* mono = rm_wav_read_mono(input, &n, &sr);
    if (!mono) { fprintf(stderr, "cannot read %s as a PCM or float WAV\n", input); return 1; }

    rm_report r;
    if (rm_analyze(mono, n, sr, &r) != 0) { fprintf(stderr, "analysis failed\n"); free(mono); return 1; }
    rm_print(&r, stdout);
    /* the MIDI tempo and the grid use the detected tempo only when the estimate is trustworthy */
    int trust_tempo = r.bpm > 0 && r.bpm_conf >= TRUST_TEMPO_CONF;
    double bpm = trust_tempo ? r.bpm : 120.0;
    if (!trust_tempo) printf("Tempo not trusted (no clear beat): MIDI and grid use 120 BPM\n");

    int rc = 0;
    char path[1024];
    snprintf(path, sizeof path, "%s/chords.txt", outdir);
    if (write_text_chords(path, &r) != 0) { fprintf(stderr, "cannot write %s\n", path); rc = 1; }

    /* one buffer for both lists: a chord voices to at most 8 notes, a melody has one note per poly note */
    size_t cap = r.nchords * 8 > r.nnotes ? r.nchords * 8 : r.nnotes;
    if (cap == 0) cap = 1;
    vi_note* notes = malloc(cap * sizeof(vi_note));
    if (!notes) { rm_report_free(&r); free(mono); return 1; }

    size_t nc = rm_chord_notes(&r, transpose, inversion, notes, cap);
    snprintf(path, sizeof path, "%s/chords.mid", outdir);
    if (write_midi(path, notes, nc, bpm) != 0) rc = 1;

    size_t nm = rm_melody_notes(&r, transpose, bpm, grid, notes, cap);
    snprintf(path, sizeof path, "%s/melody.mid", outdir);
    if (write_midi(path, notes, nm, bpm) != 0) rc = 1;

    if (rc == 0) printf("wrote %s/chords.txt, chords.mid and melody.mid\n", outdir);
    free(notes);
    rm_report_free(&r);
    free(mono);
    return rc;
}
