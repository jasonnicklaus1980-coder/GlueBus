#include "poly.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PN_FFT 8192
#define PN_HOP 1024
#define PN_BINS (PN_FFT / 2 + 1)
#define PN_LO 36
#define PN_HI 96
#define PN_NP (PN_HI - PN_LO + 1)
#define PN_HARM 8
#define PN_MAXVOICES 6

static void fft_inplace(double* re, double* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                double ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

static double midi_hz(double m) { return 440.0 * pow(2.0, (m - 69.0) / 12.0); }

/* keeps the picked salience per frame and pitch (used for note strength and tracking) */
static void picked_note(float* sal, size_t f, int pi, double v) {
    sal[f * PN_NP + pi] = (float)v;
}

typedef struct { int nframes; float* sal; int* voices; int* nvoice; } frames_t;

long vi_poly_notes(const float* x, size_t n, double sr, double min_ms, vi_poly_note* out, size_t max_notes) {
    if (!x || !out || sr <= 0 || n < PN_FFT) return -1;
    size_t nf = (n - PN_FFT) / PN_HOP + 1;
    /* precompute, per pitch and harmonic, the spectral bin (nearest) */
    static int bin_of[PN_NP][PN_HARM];
    static double wh[PN_HARM];
    for (int h = 0; h < PN_HARM; h++) wh[h] = 1.0 / sqrt((double)(h + 1));
    for (int pi = 0; pi < PN_NP; pi++) {
        double f0 = midi_hz(PN_LO + pi);
        for (int h = 0; h < PN_HARM; h++) {
            double f = f0 * (h + 1);
            int b = (int)lround(f * PN_FFT / sr);
            bin_of[pi][h] = (b > 0 && b < PN_BINS && f < sr * 0.45) ? b : -1;
        }
    }
    float* sal = malloc(sizeof(float) * nf * PN_NP);
    unsigned char* on = malloc(nf * PN_NP);
    double* re = malloc(sizeof(double) * PN_FFT), *im = malloc(sizeof(double) * PN_FFT);
    double* mag = malloc(sizeof(double) * PN_BINS);
    if (!sal || !on || !re || !im || !mag) { free(sal); free(on); free(re); free(im); free(mag); return -1; }
    for (size_t f = 0; f < nf; f++) {
        const float* fr = x + f * PN_HOP;
        for (int i = 0; i < PN_FFT; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / PN_FFT);
            re[i] = fr[i] * w; im[i] = 0;
        }
        fft_inplace(re, im, PN_FFT);
        double peak = 1e-12;
        for (int k = 0; k < PN_BINS; k++) { mag[k] = sqrt(re[k] * re[k] + im[k] * im[k]); if (mag[k] > peak) peak = mag[k]; }
        for (int k = 0; k < PN_BINS; k++) mag[k] = log(1.0 + 100.0 * mag[k] / peak);   /* log compression */
        for (int pi = 0; pi < PN_NP; pi++) on[f * PN_NP + pi] = 0;
        /* iterative harmonic cancellation: pick the strongest pitch, remove its partials from the spectrum, repeat */
        double first = 0;
        for (int pass = 0; pass < PN_MAXVOICES; pass++) {
            int best = -1; double bv = 0;
            for (int pi = 0; pi < PN_NP; pi++) {
                if (on[f * PN_NP + pi] != 0) continue;
                double s2 = 0, norm = 0;
                for (int h = 0; h < PN_HARM; h++) {
                    if (bin_of[pi][h] < 0) continue;
                    s2 += wh[h] * mag[bin_of[pi][h]];
                    norm += wh[h];
                }
                double sc = norm > 0 ? s2 / norm : 0;
                if (sc > bv) { bv = sc; best = pi; }
            }
            if (best < 0) break;
            if (pass == 0) first = bv;
            if (bv < 0.5 * first || bv < 0.08) break;
            /* a real note is a local maximum of salience over pitch */
            on[f * PN_NP + best] = 2;
            int lm = 1;
            for (int d = -1; d <= 1; d += 2) {
                int q = best + d;
                if (q < 0 || q >= PN_NP) continue;
                double sq = 0, nq = 0;
                for (int h = 0; h < PN_HARM; h++) {
                    if (bin_of[q][h] < 0) continue;
                    sq += wh[h] * mag[bin_of[q][h]];
                    nq += wh[h];
                }
                if (nq > 0 && sq / nq > bv) lm = 0;
            }
            if (lm) { on[f * PN_NP + best] = 1; picked_note(sal, f, best, bv); }
            /* remove this pitch's partials from the residual spectrum */
            for (int h = 0; h < PN_HARM; h++) {
                int b = bin_of[best][h];
                if (b < 0) continue;
                for (int k = b - 2; k <= b + 2; k++) if (k >= 0 && k < PN_BINS) mag[k] *= 0.25;
            }
        }
    }
    /* tracking: a pitch is active in a frame if picked; keep runs of >= min frames */
    double frame_s = (double)PN_HOP / sr;
    double half_s = (double)PN_FFT / 2.0 / sr;      /* frame times are window centres */
    size_t min_frames = (size_t)ceil(min_ms / 1000.0 / frame_s);
    if (min_frames < 2) min_frames = 2;
    size_t count = 0;
    for (int pi = 0; pi < PN_NP; pi++) {
        size_t f = 0;
        while (f < nf) {
            if (on[f * PN_NP + pi] != 1) { f++; continue; }
            size_t g = f; double ssum = 0;
            while (g < nf && on[g * PN_NP + pi] == 1) { ssum += sal[g * PN_NP + pi]; g++; }
            if (g - f >= min_frames && count < max_notes) {
                out[count].start = f * frame_s + half_s; out[count].end = g * frame_s + half_s;
                out[count].pitch = PN_LO + pi; out[count].strength = ssum / (g - f); count++;
            }
            f = g;
        }
    }
    free(sal); free(on); free(re); free(im); free(mag);
    return (long)count;
}
