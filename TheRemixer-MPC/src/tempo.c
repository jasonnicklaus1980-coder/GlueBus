#include "tempo.h"
#include <math.h>
#include <stdlib.h>

#define RM_HOP 512            /* envelope resolution: about 86 frames per second at 44.1 kHz */
#define RM_WIN 1024
#define RM_MIN_BPM 60.0
#define RM_MAX_BPM 200.0

double rm_estimate_bpm(const float* x, size_t n, double sr, double* confidence) {
    if (confidence) *confidence = 0.0;
    if (!x || sr <= 0) return 0.0;
    size_t nf = n / RM_HOP;                     /* envelope frames */
    if (nf < 16) return 0.0;

    /* log energy per hop, then its half-wave rectified rise = onset strength */
    float* env = malloc(nf * sizeof(float));
    float* on = malloc(nf * sizeof(float));
    if (!env || !on) { free(env); free(on); return 0.0; }
    for (size_t t = 0; t < nf; ++t) {
        double e = 0.0;
        const float* p = x + t * RM_HOP;
        size_t m = RM_WIN;
        if (t * RM_HOP + m > n) m = n - t * RM_HOP;
        for (size_t i = 0; i < m; ++i) e += (double)p[i] * p[i];
        env[t] = (float)log(1.0 + 1000.0 * sqrt(e / (double)m));
    }
    double mean = 0.0;
    on[0] = 0.f;
    for (size_t t = 1; t < nf; ++t) {
        double d = env[t] - env[t - 1];
        on[t] = d > 0 ? (float)d : 0.f;
        mean += on[t];
    }
    mean /= (double)(nf - 1);
    for (size_t t = 0; t < nf; ++t) on[t] -= (float)mean;
    free(env);

    /* autocorrelation over the lags that correspond to 60..200 BPM */
    double frames_per_s = sr / RM_HOP;
    int lag_min = (int)floor(60.0 * frames_per_s / RM_MAX_BPM);
    int lag_max = (int)ceil(60.0 * frames_per_s / RM_MIN_BPM);
    if (lag_min < 2) lag_min = 2;
    if (lag_max * 2 + 2 >= (int)nf) lag_max = (int)nf / 2 - 2;   /* 2x lag is used for octave checks */
    if (lag_max <= lag_min + 2) { free(on); return 0.0; }

    int nl = lag_max * 2 + 2;
    double* acf = calloc((size_t)nl, sizeof(double));
    if (!acf) { free(on); return 0.0; }
    double acf0 = 0.0;
    for (size_t t = 0; t < nf; ++t) acf0 += (double)on[t] * on[t];
    if (acf0 <= 1e-12) { free(acf); free(on); return 0.0; }
    for (int l = 0; l < nl; ++l) {
        double s = 0.0;
        for (size_t t = 0; t + (size_t)l < nf; ++t) s += (double)on[t] * on[t + (size_t)l];
        acf[l] = s / acf0;
    }
    free(on);

    /* favour the fundamental period over its multiples: score(L) = acf(L) + 0.5 acf(2L) */
    int best = lag_min;
    double best_score = -1e9;
    for (int l = lag_min; l <= lag_max; ++l) {
        double score = acf[l] + 0.5 * acf[2 * l];
        if (score > best_score) { best_score = score; best = l; }
    }
    /* parabolic refinement of the lag */
    double lag = best;
    if (best > lag_min && best < lag_max) {
        double a = acf[best - 1], b = acf[best], c = acf[best + 1];
        double den = a - 2 * b + c;
        if (fabs(den) > 1e-12) lag = best + 0.5 * (a - c) / den;
    }
    double bpm = 60.0 * frames_per_s / lag;
    if (confidence) {
        double c = acf[best];
        *confidence = c < 0 ? 0.0 : (c > 1 ? 1.0 : c);
    }
    free(acf);
    return bpm;
}
