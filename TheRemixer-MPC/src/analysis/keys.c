#include "keys.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const double MAJOR[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
static const double MINOR[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static double correlate(const double* a, const double* b) {
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; i++) { ma += a[i]; mb += b[i]; }
    ma /= 12; mb /= 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; i++) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return (da > 0 && db > 0) ? num / sqrt(da * db) : 0.0;
}

int vi_estimate_key(const double hist[12], double* confidence) {
    double score[24];
    for (int k = 0; k < 12; k++) {
        double maj[12], min[12];
        for (int i = 0; i < 12; i++) {
            maj[i] = MAJOR[(i - k + 12) % 12];      /* profile rotated to tonic k */
            min[i] = MINOR[(i - k + 12) % 12];
        }
        score[k] = correlate(hist, maj);
        score[12 + k] = correlate(hist, min);
    }
    int best = 0;
    for (int i = 1; i < 24; i++) if (score[i] > score[best]) best = i;
    double second = -2.0;
    for (int i = 0; i < 24; i++) if (i != best && score[i] > second) second = score[i];
    double margin = score[best] - second;
    if (confidence) {
        double c = margin / 0.2;                    /* a margin of 0.2 counts as fully confident */
        *confidence = c < 0 ? 0 : (c > 1 ? 1 : c);
    }
    return best;
}

const char* vi_key_name(int key) {
    static char buf[16];
    if (key < 0 || key > 23) return "?";
    snprintf(buf, sizeof buf, "%s %s", NAMES[key % 12], key < 12 ? "major" : "minor");
    return buf;
}
