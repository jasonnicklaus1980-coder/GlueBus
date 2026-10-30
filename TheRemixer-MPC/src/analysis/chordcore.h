// Chord analysis core in plain C (no libraries beyond libc/libm), for the MPC plugin.
// Offline: analyse a buffer that has already been recorded. Same method as the Python prototype.
#ifndef CHORDCORE_H
#define CHORDCORE_H
#include <stddef.h>

#define CC_NFFT 8192
#define CC_HOP 2048
#define CC_NBINS (CC_NFFT / 2 + 1)
#define CC_NQUAL 9
#define CC_NSTATES (12 * CC_NQUAL + 1)   /* 108 chords + "N" (no chord) */

typedef struct {
    size_t start_frame, end_frame;   /* inclusive frame indices */
    int state;                       /* 0..107 chord, 108 = N */
} cc_segment;

/* Returns number of frames analysed; fills segments (caller provides room for max_segments).
   Returns -1 on bad input. */
long cc_analyze(const float* x, size_t n, double sr, cc_segment* segments, size_t max_segments, size_t* out_count);

/* Name of a state, e.g. "C#m7". Writes into buf (at least 8 bytes). */
void cc_state_name(int state, char* buf);

/* Interval sets for the qualities, index 0..8: maj, m, dim, aug, sus2, sus4, 7, maj7, m7. */
extern const int cc_quality_len[CC_NQUAL];
extern const int cc_quality_int[CC_NQUAL][4];
extern const char* cc_quality_name[CC_NQUAL];

/* ---- streaming (live) analysis ----
   Audio is pushed in blocks of any size while it is recorded. Decisions come out with a fixed lag of
   CC_STREAM_LAG frames (about 0.2 s), using the same frame method and Viterbi rules as cc_analyze. */
#define CC_STREAM_LAG 4
typedef struct {
    double sr;
    float* pend;              /* samples not yet consumed by a frame */
    size_t npend, capend;
    double dp[CC_NSTATES];
    int started;
    unsigned short bp[CC_STREAM_LAG + 2][CC_NSTATES];   /* backpointers for uncommitted frames */
    int nbp;
    double max_energy;
    size_t frames_done;       /* frames analysed */
    size_t committed;         /* frames decided */
    int* decided;             /* state per committed frame (grows) */
    size_t ndecided, capdecided;
} cc_stream;

int cc_stream_init(cc_stream* s, double sr);          /* 0 on success */
void cc_stream_free(cc_stream* s);
/* Adds samples. Returns 0 on success, -1 on out of memory. */
int cc_stream_push(cc_stream* s, const float* x, size_t n);
/* Decides the frames still pending at the end of the take. */
void cc_stream_flush(cc_stream* s);

#endif
