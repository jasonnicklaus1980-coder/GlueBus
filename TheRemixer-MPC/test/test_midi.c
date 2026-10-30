/* MIDI building tests: quantization, chord voicing, transposition, the written SMF file (parsed back), and the pad
   schedule (sample-accurate note events). Uses hand-made reports with known contents. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis/padsend.h"
#include "analysis/musicout.h"
#include "midi_build.h"

static int failures = 0;
static void check(int ok, const char* what) {
    printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
static int near(double a, double b) { return fabs(a - b) < 1e-9; }

/* A report with two chord segments (C major, then A minor) and a few melody notes. */
static void make_report(rm_report* r) {
    memset(r, 0, sizeof *r);
    r->sr = 44100;
    r->nchords = 2;
    r->chords = malloc(2 * sizeof(cc_segment));
    r->chords[0].start_frame = 0;  r->chords[0].end_frame = 9;   r->chords[0].state = 0 * 9 + 0;   /* C maj */
    r->chords[1].start_frame = 10; r->chords[1].end_frame = 19;  r->chords[1].state = 9 * 9 + 1;   /* A m */
    r->nnotes = 4;
    r->notes = malloc(4 * sizeof(vi_poly_note));
    r->notes[0] = (vi_poly_note){ 0.26, 0.49, 60, 1.0 };   /* grid 1/4 beat at 120 BPM = 0.125 s */
    r->notes[1] = (vi_poly_note){ 0.01, 0.02, 62, 1.0 };   /* collapses to nothing on the grid */
    r->notes[2] = (vi_poly_note){ 0.50, 1.00, 0, 1.0 };    /* transposed by -2 it leaves the MIDI range */
    r->notes[3] = (vi_poly_note){ 1.00, 1.50, 72, 1.0 };
}

/* Minimal SMF reader: returns note-on count and note-off count; fills pitches of note-ons in order. */
static int read_smf(const char* path, int* pitches, int cap, int* ons, int* offs, int* format) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* d = malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return 0; }
    fclose(f);
    int ok = 0;
    if (n < 14 || memcmp(d, "MThd", 4) != 0) goto done;
    *format = (d[8] << 8) | d[9];
    long p = 14;
    if (memcmp(d + p, "MTrk", 4) != 0) goto done;
    p += 8;
    long end = p + ((long)d[p - 4] << 24 | (long)d[p - 3] << 16 | (long)d[p - 2] << 8 | d[p - 1]);
    if (end > n) goto done;
    int status = 0;
    *ons = *offs = 0;
    while (p < end) {
        unsigned long delta = 0;
        unsigned char b;
        do { b = d[p++]; delta = (delta << 7) | (b & 0x7F); } while (b & 0x80);
        if (d[p] & 0x80) status = d[p++];          /* running status otherwise */
        if (status == 0xFF) {                       /* meta */
            p++;
            unsigned long len = 0;
            do { b = d[p++]; len = (len << 7) | (b & 0x7F); } while (b & 0x80);
            p += len;
        } else if (status == 0xF0 || status == 0xF7) {
            unsigned long len = 0;
            do { b = d[p++]; len = (len << 7) | (b & 0x7F); } while (b & 0x80);
            p += len;
        } else {
            int kind = status & 0xF0;
            int a = d[p++];
            int v = (kind == 0xC0 || kind == 0xD0) ? 0 : d[p++];
            if (kind == 0x90 && v > 0) { if (*ons < cap) pitches[*ons] = a; ++*ons; }
            else if (kind == 0x80 || (kind == 0x90 && v == 0)) ++*offs;
        }
    }
    ok = 1;
done:
    free(d);
    return ok;
}

int main(void) {
    printf("quantization\n");
    check(near(rm_quantize(0.26, 120, 0.25), 0.25), "0.26 s snaps to the 1/4-beat grid at 120 BPM");
    check(near(rm_quantize(0.49, 120, 0.25), 0.5), "0.49 s snaps up to 0.5 s");
    check(near(rm_quantize(0.26, 120, 0), 0.26), "no grid leaves the time unchanged");

    rm_report r;
    make_report(&r);

    printf("chord voicing\n");
    vi_note chords[32];
    size_t nc = rm_chord_notes(&r, 0, 0, chords, 32);
    check(nc == 6, "two triads give six notes");
    check(chords[0].pitch == 48 && chords[1].pitch == 52 && chords[2].pitch == 55,
          "C major root position is 48, 52, 55");
    check(chords[3].pitch == 57 && chords[4].pitch == 60 && chords[5].pitch == 64,
          "A minor root position is 57, 60, 64");
    check(near(chords[0].end, rm_chord_end(&r, 0)) && near(chords[3].start, rm_chord_start(&r, 1)),
          "chord notes span their chord segment");
    size_t ni = rm_chord_notes(&r, 0, 1, chords, 32);
    check(ni == 6 && chords[0].pitch == 52 && chords[1].pitch == 55 && chords[2].pitch == 60,
          "first inversion moves the root up an octave");
    size_t nt = rm_chord_notes(&r, 12, 0, chords, 32);
    check(nt == 6 && chords[0].pitch == 60 && chords[2].pitch == 67, "transpose +12 shifts the chord an octave");
    size_t nc2 = rm_chord_notes(&r, 100, 0, chords, 32);
    check(nc2 == 0, "chords that leave the MIDI range are skipped");

    printf("melody\n");
    vi_note mel[8];
    size_t nm = rm_melody_notes(&r, 0, 120, 0.25, mel, 8);
    check(nm == 3, "quantized melody drops only the note that collapses on the grid");
    check(nm >= 1 && near(mel[0].start, 0.25) && near(mel[0].end, 0.5) && mel[0].pitch == 60,
          "first note quantized to 0.25..0.5 s");
    size_t nm2 = rm_melody_notes(&r, -2, 120, 0, mel, 8);
    check(nm2 == 3 && mel[0].pitch == 58, "transpose -2 shifts the melody; the note at pitch 0 is dropped");

    printf("MIDI file round trip\n");
    if (system("mkdir -p build") != 0) { printf("FAIL: cannot create build/\n"); return 1; }
    const char* path = "build/test_chords.mid";
    nc = rm_chord_notes(&r, 0, 0, chords, 32);
    check(vi_write_smf(path, chords, nc, 120.0) == 0, "chord MIDI file written");
    int pitches[16], ons = 0, offs = 0, format = -1;
    check(read_smf(path, pitches, 16, &ons, &offs, &format), "file parses as SMF");
    check(format == 0, "format 0");
    check(ons == 6 && offs == 6, "six note-ons and six note-offs");
    check(ons == 6 && pitches[0] == 48 && pitches[1] == 52 && pitches[2] == 55 && pitches[3] == 57 &&
          pitches[4] == 60 && pitches[5] == 64, "note-on pitches in file match the voicing");

    printf("pad schedule\n");
    ps_note sched[16];
    size_t ns = 0;
    for (size_t i = 0; i < nc && ns < 16; ++i) {
        sched[ns].start = chords[i].start;
        sched[ns].end = chords[i].end;
        sched[ns].pitch = chords[i].pitch;
        sched[ns].channel = 0;
        sched[ns].velocity = chords[i].velocity;
        ++ns;
    }
    const double sr = 44100.0;
    const int block = 512;
    int pad_ons = 0;
    long long first_on = -1;
    ps_MidiEvent ev[64];
    for (long long b = 0; b < (long long)(3.0 * sr); b += block) {
        size_t m = ps_block_events(sched, ns, b, block, sr, ev, 64);
        for (size_t k = 0; k < m; ++k) {
            if ((ev[k].midiData[0] & 0xF0) == 0x90) {
                if (first_on < 0) first_on = b + ev[k].deltaFrames;
                ++pad_ons;
            }
        }
    }
    check(pad_ons == 6, "pad schedule sends six note-ons across the blocks");
    check(first_on == llround(rm_chord_start(&r, 0) * sr), "first note-on lands on its sample");
    size_t offs_all = ps_all_off(sched, ns, ev, 64);
    check(offs_all == ns, "stop sends a note-off for every scheduled note");

    rm_report_free(&r);
    printf("\n%s\n", failures ? "MIDI TESTS FAILED" : "midi tests passed");
    return failures ? 1 : 0;
}
