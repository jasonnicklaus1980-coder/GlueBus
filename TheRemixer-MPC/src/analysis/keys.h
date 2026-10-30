/* Key estimate from a pitch-class histogram (Krumhansl-Schmuckler profiles). Plain C. */
#ifndef KEYS_H
#define KEYS_H

/* hist[12]: duration-weighted pitch-class counts (C = 0). Returns key index: 0..11 = major tonic C..B,
   12..23 = minor tonic C..B. *confidence in 0..1: how far the best key is above the runner-up. */
int vi_estimate_key(const double hist[12], double* confidence);

/* Text name of a key index, e.g. "C major", "A minor" (static storage). */
const char* vi_key_name(int key);

#endif
