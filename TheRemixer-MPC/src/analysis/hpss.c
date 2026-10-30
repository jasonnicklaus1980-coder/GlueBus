#include "hpss.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HP_NFFT 2048
#define HP_HOP 1024              /* 50% overlap, Hann analysis and synthesis */
#define HP_BINS (HP_NFFT / 2 + 1)
#define HP_KERNEL 17             /* median length, in frames (time) and bins (frequency) */
#define HP_POWER 2.0

static void fft(double* re, double* im, int n) {
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

/* in-place median of v[0..m-1] (insertion sort; m is small) */
static float median_of(float* v, int m) {
    for (int i = 1; i < m; i++) {
        float t = v[i]; int j = i;
        while (j > 0 && v[j - 1] > t) { v[j] = v[j - 1]; j--; }
        v[j] = t;
    }
    return v[m / 2];
}

static int frame_count(size_t n) {
    /* frames cover the signal with zero padding of HP_NFFT/2 at both ends */
    size_t padded = n + HP_NFFT;
    return (int)((padded - HP_NFFT) / HP_HOP + 1);
}

int vi_hpss(const float* x, size_t n, double sr, float* out) {
    (void)sr;
    if (!x || !out || n < HP_NFFT) return -1;
    int F = frame_count(n);
    float* mag = malloc(sizeof(float) * (size_t)F * HP_BINS);        /* magnitudes, first pass */
    float* H = malloc(sizeof(float) * (size_t)F * HP_BINS);
    float* P = malloc(sizeof(float) * (size_t)F * HP_BINS);
    double* re = malloc(sizeof(double) * HP_NFFT), *im = malloc(sizeof(double) * HP_NFFT);
    float* win = malloc(sizeof(float) * HP_NFFT);
    float* acc = calloc(n + HP_NFFT, sizeof(float));
    float* wsum = calloc(n + HP_NFFT, sizeof(float));
    float* tmp = malloc(sizeof(float) * HP_KERNEL);
    if (!mag || !H || !P || !re || !im || !win || !acc || !wsum || !tmp) {
        free(mag); free(H); free(P); free(re); free(im); free(win); free(acc); free(wsum); free(tmp);
        return -1;
    }
    for (int i = 0; i < HP_NFFT; i++) win[i] = (float)(0.5 - 0.5 * cos(2 * M_PI * i / HP_NFFT));
    /* pass 1: magnitudes. frame f starts at sample f*HOP - NFFT/2 (zero outside the signal) */
    for (int f = 0; f < F; f++) {
        long start = (long)f * HP_HOP - HP_NFFT / 2;
        for (int i = 0; i < HP_NFFT; i++) {
            long idx = start + i;
            re[i] = (idx >= 0 && (size_t)idx < n) ? x[idx] * win[i] : 0.0;
            im[i] = 0;
        }
        fft(re, im, HP_NFFT);
        for (int k = 0; k < HP_BINS; k++) mag[(size_t)f * HP_BINS + k] = (float)sqrt(re[k] * re[k] + im[k] * im[k]);
    }
    /* median filters: along time (sustained) -> H, along frequency (drum hits) -> P */
    for (int f = 0; f < F; f++) {
        for (int k = 0; k < HP_BINS; k++) {
            int lo = f - HP_KERNEL / 2, hi = f + HP_KERNEL / 2;
            int m = 0;
            for (int t = lo; t <= hi; t++) if (t >= 0 && t < F) tmp[m++] = mag[(size_t)t * HP_BINS + k];
            H[(size_t)f * HP_BINS + k] = median_of(tmp, m);
        }
    }
    for (int f = 0; f < F; f++) {
        for (int k = 0; k < HP_BINS; k++) {
            int lo = k - HP_KERNEL / 2, hi = k + HP_KERNEL / 2;
            int m = 0;
            for (int q = lo; q <= hi; q++) if (q >= 0 && q < HP_BINS) tmp[m++] = mag[(size_t)f * HP_BINS + q];
            P[(size_t)f * HP_BINS + k] = median_of(tmp, m);
        }
    }
    /* pass 2: masked spectrum -> inverse FFT -> overlap-add with the synthesis window */
    for (int f = 0; f < F; f++) {
        long start = (long)f * HP_HOP - HP_NFFT / 2;
        for (int i = 0; i < HP_NFFT; i++) {
            long idx = start + i;
            re[i] = (idx >= 0 && (size_t)idx < n) ? x[idx] * win[i] : 0.0;
            im[i] = 0;
        }
        fft(re, im, HP_NFFT);
        for (int k = 0; k < HP_BINS; k++) {
            double hp = pow(H[(size_t)f * HP_BINS + k], HP_POWER), pp = pow(P[(size_t)f * HP_BINS + k], HP_POWER);
            double mask = hp / (hp + pp + 1e-12);
            re[k] *= mask; im[k] *= mask;
            if (k > 0 && k < HP_NFFT / 2) { re[HP_NFFT - k] = re[k]; im[HP_NFFT - k] = -im[k]; }
        }
        /* inverse FFT: conjugate, forward FFT, conjugate, scale */
        for (int i = 0; i < HP_NFFT; i++) im[i] = -im[i];
        fft(re, im, HP_NFFT);
        for (int i = 0; i < HP_NFFT; i++) {
            long idx = start + i;
            if (idx < 0 || (size_t)idx >= n) continue;
            double y = re[i] / HP_NFFT;
            acc[idx] += (float)(y * win[i]);
            wsum[idx] += win[i] * win[i];
        }
    }
    for (size_t i = 0; i < n; i++) out[i] = wsum[i] > 1e-6f ? acc[i] / wsum[i] : 0.f;
    free(mag); free(H); free(P); free(re); free(im); free(win); free(acc); free(wsum); free(tmp);
    return 0;
}
