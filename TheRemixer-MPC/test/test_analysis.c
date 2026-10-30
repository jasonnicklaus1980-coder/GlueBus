/* Analysis tests on synthetic audio with known answers: tempo, chords, key, polyphonic notes, and WAV reading.
   Synthetic signals only: real recordings will score lower. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analyze.h"
#include "analysis/chordcore.h"
#include "analysis/keys.h"
#include "tempo.h"
#include "wav_read.h"

static int failures = 0;
static void check(int ok, const char* what) {
    printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static unsigned rs = 99;
static double rnd(void) { rs = rs * 1103515245u + 12345u; return ((rs >> 8) & 0xFFFFFF) / 16777216.0 * 2.0 - 1.0; }

/* One sustained note with a short attack and release, with a few harmonics. */
static void add_note(float* x, size_t n, double sr, int pitch, double t0, double t1, double amp) {
    double f0 = 440.0 * pow(2.0, (pitch - 69) / 12.0);
    size_t a = (size_t)(t0 * sr), b = (size_t)(t1 * sr);
    for (size_t i = a; i < b && i < n; ++i) {
        double tt = (i - a) / sr, dur = (b - a) / sr;
        double env = fmin(1.0, fmin(tt / 0.01, (dur - tt) / 0.03));
        double v = 0;
        for (int h = 1; h <= 4; ++h) v += sin(2 * M_PI * f0 * h * tt) / h;
        x[i] += (float)(amp * env * v);
    }
}

/* Chord: several notes in the same window. */
static void add_chord(float* x, size_t n, double sr, const int* pitches, int np, double t0, double t1) {
    for (int k = 0; k < np; ++k) add_note(x, n, sr, pitches[k], t0, t1, 0.12);
}

/* A click track: a short decaying low thump with a little noise on every beat. */
static float* click_track(double bpm, double seconds, double sr, size_t* n_out) {
    size_t n = (size_t)(seconds * sr);
    float* x = calloc(n, sizeof(float));
    double period = 60.0 / bpm;
    for (double t = 0.0; t < seconds; t += period) {
        size_t s = (size_t)(t * sr);
        for (size_t i = 0; i < (size_t)(0.12 * sr) && s + i < n; ++i) {
            double tt = i / sr;
            x[s + i] += (float)(0.8 * exp(-tt * 40.0) * sin(2 * M_PI * 70.0 * tt) + 0.2 * exp(-tt * 120.0) * rnd());
        }
    }
    *n_out = n;
    return x;
}

/* ---- WAV writer for the round-trip test (24-bit, 16-bit, or float32, stereo) ---- */
static void put_le(FILE* f, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) fputc((int)((v >> (8 * i)) & 0xFF), f);
}
static void write_wav(const char* path, const float* l, const float* r, size_t n, unsigned rate, int bits, int is_float) {
    FILE* f = fopen(path, "wb");
    unsigned bps = bits / 8, block = bps * 2;
    uint32_t data = (uint32_t)(n * block);
    fwrite("RIFF", 1, 4, f); put_le(f, 36 + data, 4); fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4); put_le(f, is_float ? 3 : 1, 2); put_le(f, 2, 2); put_le(f, rate, 4);
    put_le(f, rate * block, 4); put_le(f, block, 2); put_le(f, bits, 2);
    fwrite("data", 1, 4, f); put_le(f, data, 4);
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 2; ++c) {
            float v = c == 0 ? l[i] : r[i];
            if (is_float) { uint32_t u; memcpy(&u, &v, 4); put_le(f, u, 4); }
            else if (bits == 16) put_le(f, (uint32_t)(int32_t)lrintf(v * 32767.f) & 0xFFFF, 2);
            else put_le(f, (uint32_t)(int32_t)lrintf(v * 8388607.f) & 0xFFFFFF, 3);
        }
    }
    fclose(f);
}

int main(void) {
    const double sr = 44100.0;
    printf("tempo\n");
    {
        size_t n;
        float* x = click_track(120.0, 16.0, sr, &n);
        double conf = 0;
        double bpm = rm_estimate_bpm(x, n, sr, &conf);
        printf("     120 BPM click track: estimated %.2f (confidence %.2f)\n", bpm, conf);
        check(fabs(bpm - 120.0) < 1.0, "120 BPM click track within 1 BPM");
        free(x);
    }
    {
        size_t n;
        float* x = click_track(96.0, 16.0, sr, &n);
        double bpm = rm_estimate_bpm(x, n, sr, NULL);
        printf("     96 BPM click track: estimated %.2f\n", bpm);
        check(fabs(bpm - 96.0) < 1.0, "96 BPM click track within 1 BPM");
        free(x);
    }
    {
        size_t n = (size_t)(2.0 * sr);
        float* x = calloc(n, sizeof(float));
        check(rm_estimate_bpm(x, n, sr, NULL) == 0.0, "silence gives no tempo");
        free(x);
    }

    printf("chords\n");
    {
        /* C, Am, F, G, two seconds each */
        const int prog[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };
        const char* want[4] = { "C", "Am", "F", "G" };
        size_t n = (size_t)(8.0 * sr);
        float* x = calloc(n, sizeof(float));
        for (int b = 0; b < 4; ++b) add_chord(x, n, sr, prog[b], 3, b * 2.0, (b + 1) * 2.0);
        rm_report r;
        rm_analyze(x, n, sr, &r);
        int ok = 1;
        for (int b = 0; b < 4; ++b) {
            double mid = b * 2.0 + 1.0;
            char got[16] = "none";
            for (size_t s = 0; s < r.nchords; ++s) {
                double s0 = rm_chord_start(&r, s), s1 = rm_chord_end(&r, s);
                if (mid >= s0 && mid < s1) cc_state_name(r.chords[s].state, got);
            }
            printf("     block %d: want %-3s got %s\n", b + 1, want[b], got);
            if (strcmp(got, want[b]) != 0) ok = 0;
        }
        check(ok, "C Am F G progression recognised block by block");
        rm_report_free(&r);
        free(x);
    }

    printf("key and notes\n");
    {
        /* C major scale, 0.4 s per note, then the C major triad held */
        const int scale[8] = { 60, 62, 64, 65, 67, 69, 71, 72 };
        size_t n = (size_t)(6.0 * sr);
        float* x = calloc(n, sizeof(float));
        for (int i = 0; i < 8; ++i) add_note(x, n, sr, scale[i], i * 0.4, i * 0.4 + 0.4, 0.2);
        const int triad[3] = { 60, 64, 67 };
        add_chord(x, n, sr, triad, 3, 3.5, 5.5);
        rm_report r;
        rm_analyze(x, n, sr, &r);
        printf("     detected key: %s (confidence %.2f)\n", vi_key_name(r.key), r.key_conf);
        check(r.key == 0, "C major scale and triad read as C major");

        int found[3] = { 0, 0, 0 };
        for (size_t i = 0; i < r.nnotes; ++i) {
            double mid = (r.notes[i].start + r.notes[i].end) / 2;
            if (mid > 3.5 && mid < 5.5)
                for (int k = 0; k < 3; ++k) if (r.notes[i].pitch == triad[k]) found[k] = 1;
        }
        check(found[0] && found[1] && found[2], "C E G triad found as polyphonic notes");
        rm_report_free(&r);
        free(x);
    }

    printf("WAV reading\n");
    {
        size_t n = (size_t)(0.5 * sr);
        float* l = malloc(n * sizeof(float));
        float* rr = malloc(n * sizeof(float));
        for (size_t i = 0; i < n; ++i) {
            l[i] = (float)(0.5 * sin(2 * M_PI * 440.0 * i / sr));
            rr[i] = (float)(0.25 * sin(2 * M_PI * 660.0 * i / sr));
        }
        const char* path = "build/test_wav.wav";
        if (system("mkdir -p build") != 0) { printf("FAIL: cannot create build/\n"); return 1; }
        int formats[3][3] = { { 24, 0, 0 }, { 16, 0, 0 }, { 32, 1, 0 } };
        const char* names[3] = { "24-bit PCM", "16-bit PCM", "32-bit float" };
        for (int k = 0; k < 3; ++k) {
            write_wav(path, l, rr, n, 44100, formats[k][0], formats[k][1]);
            size_t got_n = 0; double got_sr = 0;
            float* m = rm_wav_read_mono(path, &got_n, &got_sr);
            int ok = m != NULL && got_n == n && got_sr == 44100.0;
            double worst = 0;
            for (size_t i = 0; ok && i < n; ++i) {
                double want = (l[i] + rr[i]) / 2.0;
                double e = fabs(m[i] - want);
                if (e > worst) worst = e;
            }
            int tol_ok = worst < (formats[k][0] == 16 ? 2e-4 : 1e-5);
            printf("     %s: %zu frames, worst error %.2e\n", names[k], got_n, worst);
            check(ok && tol_ok, names[k]);
            free(m);
        }
        FILE* bad = fopen("build/test_bad.wav", "wb");
        fputs("not a wav file at all, just text padding here", bad);
        fclose(bad);
        size_t bn; double bsr;
        check(rm_wav_read_mono("build/test_bad.wav", &bn, &bsr) == NULL, "a non-WAV file is rejected");
        check(rm_wav_read_mono("build/does_not_exist.wav", &bn, &bsr) == NULL, "a missing file is rejected");
        free(l); free(rr);
    }

    printf("\n%s\n", failures ? "ANALYSIS TESTS FAILED" : "analysis tests passed");
    return failures ? 1 : 0;
}
