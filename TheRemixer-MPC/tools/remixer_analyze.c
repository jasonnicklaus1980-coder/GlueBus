/* remixer_analyze: analyse one WAV recording and write the results.
   Usage: remixer_analyze recording.wav [outdir]
   Prints the report. Writes outdir/chords.txt (one chord per line) and outdir/melody.mid (the polyphonic notes at the
   detected tempo, 120 BPM if none was found). Analysis runs on the mono mix of the file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analyze.h"
#include "analysis/musicout.h"
#include "wav_read.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s recording.wav [outdir]\n", argv[0]);
        return 2;
    }
    const char* outdir = argc > 2 ? argv[2] : ".";

    size_t n = 0;
    double sr = 0;
    float* mono = rm_wav_read_mono(argv[1], &n, &sr);
    if (!mono) { fprintf(stderr, "cannot read %s as a PCM or float WAV\n", argv[1]); return 1; }

    rm_report r;
    if (rm_analyze(mono, n, sr, &r) != 0) { fprintf(stderr, "analysis failed\n"); free(mono); return 1; }
    rm_print(&r, stdout);

    char path[1024];
    snprintf(path, sizeof path, "%s/chords.txt", outdir);
    FILE* f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); rm_report_free(&r); free(mono); return 1; }
    char name[16];
    for (size_t i = 0; i < r.nchords; ++i) {
        cc_state_name(r.chords[i].state, name);
        fprintf(f, "%.3f\t%.3f\t%s\n", rm_chord_start(&r, i), rm_chord_end(&r, i), name);
    }
    fclose(f);

    vi_note* notes = malloc((r.nnotes ? r.nnotes : 1) * sizeof(vi_note));
    if (!notes) { rm_report_free(&r); free(mono); return 1; }
    for (size_t i = 0; i < r.nnotes; ++i) {
        notes[i].start = r.notes[i].start;
        notes[i].end = r.notes[i].end;
        notes[i].pitch = r.notes[i].pitch;
        notes[i].velocity = 100;
    }
    snprintf(path, sizeof path, "%s/melody.mid", outdir);
    int rc = vi_write_smf(path, notes, r.nnotes, r.bpm > 0 ? r.bpm : 120.0);
    if (rc != 0) fprintf(stderr, "cannot write %s\n", path);
    else printf("wrote %s and %s/chords.txt\n", path, outdir);

    free(notes);
    rm_report_free(&r);
    free(mono);
    return rc == 0 ? 0 : 1;
}
