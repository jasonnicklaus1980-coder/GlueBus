#include <stdbool.h>
#include "notes.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Analysis at about 12 kHz. Window 1024 (~85 ms) and hop 256 (~21 ms) give a time resolution fine enough
   to place note boundaries within the 60 ms test tolerance. */
#define YIN_WIN 1024
#define YIN_HOP 256
#define YIN_THRESH 0.12
#define LEVEL_WIN 128          /* ~10 ms level window, used to find the gaps between repeated notes */
#define SPLIT_RATIO 0.35       /* a level dip below this fraction of the run's median splits the run */
#define DEC_TAPS 63

/* YIN: fractional period in samples, or 0 if unvoiced. *conf = 1 - cmndf at the chosen lag. */
static double yin_period(const float* x, size_t start, int tau_min, int tau_max, double* conf) {
    static double d[YIN_WIN / 2 + 1];
    static double cmnd[YIN_WIN / 2 + 1];
    const float* a = x + start;
    d[0] = 0;
    for (int tau = 1; tau <= tau_max; tau++) {
        double s = 0;
        for (int j = 0; j < YIN_WIN; j++) { double dd = a[j] - a[j + tau]; s += dd * dd; }
        d[tau] = s;
    }
    cmnd[0] = 1.0;
    double running = 0;
    for (int tau = 1; tau <= tau_max; tau++) {
        running += d[tau];
        cmnd[tau] = running > 0 ? d[tau] * tau / running : 1.0;
    }
    int tau = tau_min;
    while (tau < tau_max) {
        if (cmnd[tau] < YIN_THRESH) {
            while (tau + 1 < tau_max && cmnd[tau + 1] < cmnd[tau]) tau++;
            break;
        }
        tau++;
    }
    if (tau >= tau_max || cmnd[tau] >= YIN_THRESH) { *conf = 0; return 0; }
    double s0 = cmnd[tau - 1], s1 = cmnd[tau], s2 = cmnd[tau + 1];
    double denom = s0 - 2 * s1 + s2;
    double shift = denom != 0 ? 0.5 * (s0 - s2) / denom : 0;
    *conf = 1.0 - s1;
    return tau + shift;
}

static double rms(const float* x, size_t n) {
    double s = 0; for (size_t i = 0; i < n; i++) s += (double)x[i] * x[i];
    return n ? sqrt(s / n) : 0;
}

static int cmp_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}

/* Low-pass (5 kHz, windowed sinc) and decimate by an integer factor. */
static int decimate(const float* x, size_t n, double sr, int factor, float** out_x, size_t* out_n) {
    double fc = 5000.0 / sr;
    double h[DEC_TAPS], sum = 0;
    for (int i = 0; i < DEC_TAPS; i++) {
        int m = i - (DEC_TAPS - 1) / 2;
        double sinc = m == 0 ? 2 * fc : sin(2 * M_PI * fc * m) / (M_PI * m);
        double w = 0.42 - 0.5 * cos(2 * M_PI * i / (DEC_TAPS - 1)) + 0.08 * cos(4 * M_PI * i / (DEC_TAPS - 1));
        h[i] = sinc * w; sum += h[i];
    }
    for (int i = 0; i < DEC_TAPS; i++) h[i] /= sum;
    size_t m = n / factor;
    float* y = malloc(sizeof(float) * (m > 0 ? m : 1));
    if (!y) return -1;
    for (size_t j = 0; j < m; j++) {
        long c = (long)(j * factor) - (DEC_TAPS - 1) / 2;
        double acc = 0;
        for (int i = 0; i < DEC_TAPS; i++) {
            long idx = c + i;
            if (idx >= 0 && (size_t)idx < n) acc += h[i] * x[idx];
        }
        y[j] = (float)acc;
    }
    *out_x = y; *out_n = m;
    return 0;
}

static long extract_at_rate(const float* x, size_t n, double sr, double fmin, double fmax,
                            double min_ms, vi_line_note* out, size_t max_notes) {
    int tau_min = (int)floor(sr / fmax), tau_max = (int)ceil(sr / fmin);
    if (tau_max > YIN_WIN / 2) tau_max = YIN_WIN / 2;
    if (tau_min < 2) tau_min = 2;
    if (n < (size_t)(YIN_WIN + tau_max)) return 0;

    size_t frames = (n - YIN_WIN - tau_max) / YIN_HOP + 1;
    int* pitch = malloc(sizeof(int) * frames);
    double* conf = malloc(sizeof(double) * frames);
    double* level = malloc(sizeof(double) * frames);
    int* clean = malloc(sizeof(int) * frames);
    double* scratch = malloc(sizeof(double) * frames);
    if (!pitch || !conf || !level || !clean || !scratch) {
        free(pitch); free(conf); free(level); free(clean); free(scratch);
        return -1;
    }
    double total_level = rms(x, n);
    for (size_t f = 0; f < frames; f++) {
        size_t s = f * YIN_HOP;
        level[f] = rms(x + s + YIN_WIN / 2 - LEVEL_WIN / 2, LEVEL_WIN);          /* short level at the frame centre */
        pitch[f] = -1; conf[f] = 0;
        if (rms(x + s, YIN_WIN) < 0.05 * total_level) continue;                   /* silence or gap */
        double c = 0;
        double p = yin_period(x, s, tau_min, tau_max, &c);
        if (p > 0) { pitch[f] = (int)lround(69 + 12 * log2((sr / p) / 440.0)); conf[f] = c; }
    }
    /* median of three over pitch, keeping unvoiced frames unvoiced */
    for (size_t f = 0; f < frames; f++) {
        if (f == 0 || f + 1 >= frames) { clean[f] = pitch[f]; continue; }
        int a = pitch[f - 1], b = pitch[f], c2 = pitch[f + 1];
        int lo = a < b ? a : b, hi = a < b ? b : a;
        int med = c2 < lo ? lo : (c2 > hi ? hi : c2);
        clean[f] = (b == -1) ? -1 : med;
    }

    double frame_s = (double)YIN_HOP / sr, min_s = min_ms / 1000.0;
    size_t count = 0, f = 0;
    while (f < frames) {
        if (clean[f] < 0) { f++; continue; }
        size_t g = f;
        while (g < frames && clean[g] == clean[f]) g++;
        /* split a run at level dips between repeated notes */
        size_t m = g - f, k = 0;
        for (size_t i = f; i < g; i++) scratch[k++] = level[i];
        qsort(scratch, m, sizeof(double), cmp_double);
        double med = scratch[m / 2];
        size_t piece = f;
        for (size_t i = f + 1; i + 1 < g; i++) {
            bool dip = level[i] < SPLIT_RATIO * med && level[i] <= level[i - 1] && level[i] <= level[i + 1];
            if (dip && i - piece >= 1) {
                double dur = (i - piece) * frame_s;
                if (dur >= min_s && count < max_notes) {
                    double csum = 0; for (size_t j = piece; j < i; j++) csum += conf[j];
                    out[count].start = piece * frame_s; out[count].end = i * frame_s;
                    out[count].pitch = clean[piece]; out[count].confidence = csum / (i - piece); count++;
                }
                piece = i;
            }
        }
        double dur = (g - piece) * frame_s;
        if (dur >= min_s && count < max_notes) {
            double csum = 0; for (size_t j = piece; j < g; j++) csum += conf[j];
            out[count].start = piece * frame_s; out[count].end = g * frame_s;
            out[count].pitch = clean[piece]; out[count].confidence = csum / (g - piece); count++;
        }
        f = g;
    }
    free(pitch); free(conf); free(level); free(clean); free(scratch);
    return (long)count;
}

long vi_extract_notes(const float* x, size_t n, double sr, double fmin, double fmax,
                      double min_ms, vi_line_note* out, size_t max_notes) {
    if (!x || !out || sr <= 0 || fmin <= 0 || fmax <= fmin || n < YIN_WIN) return -1;
    int factor = (int)floor(sr / 12000.0);
    if (factor < 2) return extract_at_rate(x, n, sr, fmin, fmax, min_ms, out, max_notes);
    float* y = NULL; size_t m = 0;
    if (decimate(x, n, sr, factor, &y, &m) != 0) return -1;
    double sr2 = sr / factor;
    long k = (m >= YIN_WIN) ? extract_at_rate(y, m, sr2, fmin, fmax, min_ms, out, max_notes) : 0;
    free(y);
    return k;
}
