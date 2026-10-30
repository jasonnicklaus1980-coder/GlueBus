#include "chordcore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CC_PI 3.14159265358979323846

const char* cc_quality_name[CC_NQUAL] = {"", "m", "dim", "aug", "sus2", "sus4", "7", "maj7", "m7"};
const int cc_quality_len[CC_NQUAL] = {3, 3, 3, 3, 3, 3, 4, 4, 4};
const int cc_quality_int[CC_NQUAL][4] = {
    {0, 4, 7, 0}, {0, 3, 7, 0}, {0, 3, 6, 0}, {0, 4, 8, 0}, {0, 2, 7, 0},
    {0, 5, 7, 0}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}};
static const char* NOTES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void cc_state_name(int state, char* buf) {
    if (state >= 12 * CC_NQUAL) { strcpy(buf, "N"); return; }
    int root = state / CC_NQUAL, q = state % CC_NQUAL;
    sprintf(buf, "%s%s", NOTES[root], cc_quality_name[q]);
}

/* ---- in-place iterative radix-2 complex FFT ---- */
static void fft(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * CC_PI / len;
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci;
                double xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

/* Periodic Hann window, matching scipy.signal.get_window('hann', N). */
static double hann(int i) { return 0.5 - 0.5 * cos(2.0 * CC_PI * i / CC_NFFT); }

/* chroma for one frame; returns frame energy */
static double frame_chroma(const float* x, const int* bin_pc, const unsigned char* keep, double* chroma) {
    static double re[CC_NFFT], im[CC_NFFT];
    for (int i = 0; i < CC_NFFT; i++) { re[i] = x[i] * hann(i); im[i] = 0.0; }
    fft(re, im, CC_NFFT);
    for (int pc = 0; pc < 12; pc++) chroma[pc] = 0.0;
    for (int k = 0; k < CC_NBINS; k++) {
        if (!keep[k]) continue;
        double p = re[k] * re[k] + im[k] * im[k];
        chroma[bin_pc[k]] += p;
    }
    double energy = 0.0, norm2 = 0.0;
    for (int pc = 0; pc < 12; pc++) { energy += chroma[pc]; norm2 += chroma[pc] * chroma[pc]; }
    double norm = sqrt(norm2);
    if (norm > 0) for (int pc = 0; pc < 12; pc++) chroma[pc] /= norm;
    return energy;
}

long cc_analyze(const float* x, size_t n, double sr, cc_segment* segments, size_t max_segments, size_t* out_count) {
    *out_count = 0;
    if (!x || !segments || !out_count || sr <= 0 || n < CC_NFFT) return -1;

    /* bin -> pitch class, keeping 65..2100 Hz (same as the Python prototype) */
    static int bin_pc[CC_NBINS];
    static unsigned char keep[CC_NBINS];
    for (int k = 0; k < CC_NBINS; k++) {
        double f = k * sr / CC_NFFT;
        keep[k] = (f >= 65.0 && f <= 2100.0);
        if (keep[k]) {
            double midi = 69.0 + 12.0 * log2(f / 440.0);
            long r = lround(midi);
            bin_pc[k] = (int)(((r % 12) + 12) % 12);
        } else bin_pc[k] = 0;
    }

    size_t frames = (n - CC_NFFT) / CC_HOP + 1;
    double* energy = malloc(sizeof(double) * frames);
    double* chroma = malloc(sizeof(double) * 12 * frames);
    if (!energy || !chroma) { free(energy); free(chroma); return -1; }
    double max_e = 0.0;
    for (size_t f = 0; f < frames; f++) {
        double c[12];
        energy[f] = frame_chroma(x + f * CC_HOP, bin_pc, keep, c);
        memcpy(chroma + 12 * f, c, sizeof c);
        if (energy[f] > max_e) max_e = energy[f];
    }

    /* templates: state s = root*9 + quality */
    static double T[12 * CC_NQUAL][12];
    for (int root = 0; root < 12; root++)
        for (int q = 0; q < CC_NQUAL; q++) {
            double v[12] = {0};
            for (int i = 0; i < cc_quality_len[q]; i++) v[(root + cc_quality_int[q][i]) % 12] = 1.0;
            double nrm = 0; for (int i = 0; i < 12; i++) nrm += v[i] * v[i];
            nrm = sqrt(nrm);
            for (int i = 0; i < 12; i++) T[root * CC_NQUAL + q][i] = v[i] / nrm;
        }

    /* emissions, then Viterbi with the same rules as chords.viterbi (switch penalty 6.0) */
    const int S = CC_NSTATES;
    const double penalty = 6.0;
    double* dp = malloc(sizeof(double) * S);
    double* ndp = malloc(sizeof(double) * S);
    unsigned short* bp = malloc(sizeof(unsigned short) * S * frames);   /* backpointers */
    if (!dp || !ndp || !bp) return -1;
    double emis[CC_NSTATES];
    for (size_t t = 0; t < frames; t++) {
        int gate = energy[t] < 0.02 * max_e;
        for (int s = 0; s < S - 1; s++) {
            double dot = 0; for (int i = 0; i < 12; i++) dot += T[s][i] * chroma[12 * t + i];
            emis[s] = gate ? -1.0 : 10.0 * dot;
        }
        emis[S - 1] = gate ? 2.0 : 0.5;
        if (t == 0) { for (int s = 0; s < S; s++) dp[s] = emis[s]; for (int s = 0; s < S; s++) bp[s] = (unsigned short)s; continue; }
        int best = 0; for (int s = 1; s < S; s++) if (dp[s] > dp[best]) best = s;
        double sw = dp[best] - penalty;
        for (int s = 0; s < S; s++) {
            int from = dp[s] >= sw ? s : best;
            bp[t * S + s] = (unsigned short)from;
            ndp[s] = (dp[s] >= sw ? dp[s] : sw) + emis[s];
        }
        double* tmp = dp; dp = ndp; ndp = tmp;
    }
    /* trace back */
    int* path = malloc(sizeof(int) * frames);
    if (!path) return -1;
    int state = 0; for (int s = 1; s < S; s++) if (dp[s] > dp[state]) state = s;
    path[frames - 1] = state;
    for (size_t t = frames - 1; t > 0; t--) { state = bp[t * S + state]; path[t - 1] = state; }

    /* merge equal consecutive states into segments */
    size_t count = 0;
    size_t start = 0;
    for (size_t t = 1; t <= frames; t++) {
        if (t == frames || path[t] != path[start]) {
            if (count >= max_segments) break;
            segments[count].start_frame = start;
            segments[count].end_frame = t - 1;
            segments[count].state = path[start];
            count++;
            start = t;
        }
    }
    *out_count = count;

    free(dp); free(ndp); free(bp); free(path);
    free(energy); free(chroma);
    return (long)frames;
}

/* ---------- streaming analysis ---------- */
#include <stdlib.h>

static int stream_emission(const float* frame, const int* bin_pc, const unsigned char* keep,
                           double* emis, double* max_energy) {
    static double chroma[12];
    double energy = frame_chroma(frame, bin_pc, keep, chroma);
    if (energy > *max_energy) *max_energy = energy;
    int gate = energy < 0.02 * *max_energy;
    static double T[12 * CC_NQUAL][12];
    static int init = 0;
    if (!init) {
        for (int root = 0; root < 12; root++)
            for (int q = 0; q < CC_NQUAL; q++) {
                double v[12] = {0};
                for (int i = 0; i < cc_quality_len[q]; i++) v[(root + cc_quality_int[q][i]) % 12] = 1.0;
                double nrm = 0; for (int i = 0; i < 12; i++) nrm += v[i] * v[i];
                nrm = sqrt(nrm);
                for (int i = 0; i < 12; i++) T[root * CC_NQUAL + q][i] = v[i] / nrm;
            }
        init = 1;
    }
    for (int st = 0; st < 12 * CC_NQUAL; st++) {
        double dot = 0; for (int i = 0; i < 12; i++) dot += T[st][i] * chroma[i];
        emis[st] = gate ? -1.0 : 10.0 * dot;
    }
    emis[12 * CC_NQUAL] = gate ? 2.0 : 0.5;
    return 0;
}

/* per-bin pitch class tables, built once per sample rate */
static int bin_pc_tab[CC_NBINS];
static unsigned char keep_tab[CC_NBINS];
static double tab_sr = 0;
static void build_tables(double sr) {
    if (tab_sr == sr) return;
    for (int k = 0; k < CC_NBINS; k++) {
        double f = k * sr / CC_NFFT;
        keep_tab[k] = (f >= 65.0 && f <= 2100.0);
        if (keep_tab[k]) {
            long r = lround(69.0 + 12.0 * log2(f / 440.0));
            bin_pc_tab[k] = (int)(((r % 12) + 12) % 12);
        } else bin_pc_tab[k] = 0;
    }
    tab_sr = sr;
}

static void stream_commit(cc_stream* s, int final) {
    while (s->nbp > (final ? 0 : CC_STREAM_LAG) && s->nbp > 0) {
        /* trace back from the best state at the newest frame to the oldest uncommitted frame */
        int best = 0;
        for (int st = 1; st < CC_NSTATES; st++) if (s->dp[st] > s->dp[best]) best = st;
        int state = best;
        for (int k = s->nbp - 1; k >= 1; k--) state = s->bp[k][state];
        if (s->ndecided == s->capdecided) {
            size_t nc = s->capdecided ? s->capdecided * 2 : 1024;
            int* nd = realloc(s->decided, sizeof(int) * nc);
            if (!nd) return;
            s->decided = nd; s->capdecided = nc;
        }
        s->decided[s->ndecided++] = state;
        s->committed++;
        /* drop the oldest backpointer frame */
        for (int k = 1; k < s->nbp; k++) memcpy(s->bp[k - 1], s->bp[k], sizeof s->bp[k]);
        s->nbp--;
        if (final && s->nbp == 0) break;
    }
}

int cc_stream_init(cc_stream* s, double sr) {
    memset(s, 0, sizeof *s);
    if (sr <= 0) return -1;
    s->sr = sr;
    s->capend = CC_NFFT * 4;
    s->pend = malloc(sizeof(float) * s->capend);
    if (!s->pend) return -1;
    build_tables(sr);
    return 0;
}

void cc_stream_free(cc_stream* s) {
    free(s->pend); free(s->decided);
    memset(s, 0, sizeof *s);
}

int cc_stream_push(cc_stream* s, const float* x, size_t n) {
    if (s->npend + n > s->capend) {
        size_t nc = s->npend + n + CC_NFFT;
        float* np = realloc(s->pend, sizeof(float) * nc);
        if (!np) return -1;
        s->pend = np; s->capend = nc;
    }
    memcpy(s->pend + s->npend, x, sizeof(float) * n);
    s->npend += n;
    while (s->npend >= CC_NFFT) {
        double emis[CC_NSTATES];
        stream_emission(s->pend, bin_pc_tab, keep_tab, emis, &s->max_energy);
        s->frames_done++;
        if (!s->started) {
            for (int st = 0; st < CC_NSTATES; st++) s->dp[st] = emis[st];
            for (int st = 0; st < CC_NSTATES; st++) s->bp[0][st] = (unsigned short)st;
            s->nbp = 1;
            s->started = 1;
        } else {
            int best = 0;
            for (int st = 1; st < CC_NSTATES; st++) if (s->dp[st] > s->dp[best]) best = st;
            double sw = s->dp[best] - 6.0;
            double ndp[CC_NSTATES];
            unsigned short* row = s->bp[s->nbp < CC_STREAM_LAG + 2 ? s->nbp : CC_STREAM_LAG + 1];
            for (int st = 0; st < CC_NSTATES; st++) {
                int from = s->dp[st] >= sw ? st : best;
                row[st] = (unsigned short)from;
                ndp[st] = (s->dp[st] >= sw ? s->dp[st] : sw) + emis[st];
            }
            for (int st = 0; st < CC_NSTATES; st++) s->dp[st] = ndp[st];
            if (s->nbp < CC_STREAM_LAG + 2) s->nbp++;
        }
        stream_commit(s, 0);
        memmove(s->pend, s->pend + CC_HOP, sizeof(float) * (s->npend - CC_HOP));
        s->npend -= CC_HOP;
    }
    return 0;
}

void cc_stream_flush(cc_stream* s) {
    stream_commit(s, 1);
}
