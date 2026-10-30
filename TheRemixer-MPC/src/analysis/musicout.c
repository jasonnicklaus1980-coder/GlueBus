#include "musicout.h"
#include "chordcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int put_vlq(unsigned char* buf, unsigned long v) {
    unsigned char tmp[5]; int n = 0;
    tmp[n++] = (unsigned char)(v & 0x7F); v >>= 7;
    while (v) { tmp[n++] = (unsigned char)((v & 0x7F) | 0x80); v >>= 7; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

typedef struct { unsigned long tick; int order; unsigned char data[3]; } vi_event;

static int cmp_event(const void* a, const void* b) {
    const vi_event* x = a; const vi_event* y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;           /* note-offs (0) before note-ons (1) at the same tick */
}

int vi_write_smf(const char* path, const vi_note* notes, size_t count, double bpm) {
    const unsigned ppq = 480;
    vi_event* ev = malloc(sizeof(vi_event) * (2 * count + 1));
    if (!ev || bpm <= 0) { free(ev); return -1; }
    size_t m = 0;
    for (size_t i = 0; i < count; i++) {
        unsigned long on = (unsigned long)(notes[i].start * bpm / 60.0 * ppq + 0.5);
        unsigned long off = (unsigned long)(notes[i].end * bpm / 60.0 * ppq + 0.5);
        if (off <= on) off = on + 1;
        ev[m].tick = on;  ev[m].order = 1; ev[m].data[0] = 0x90; ev[m].data[1] = (unsigned char)notes[i].pitch; ev[m].data[2] = (unsigned char)notes[i].velocity; m++;
        ev[m].tick = off; ev[m].order = 0; ev[m].data[0] = 0x80; ev[m].data[1] = (unsigned char)notes[i].pitch; ev[m].data[2] = 0; m++;
    }
    qsort(ev, m, sizeof(vi_event), cmp_event);

    /* track body */
    size_t cap = 64 + m * 8;
    unsigned char* body = malloc(cap);
    if (!body) { free(ev); return -1; }
    size_t len = 0;
    unsigned long us = (unsigned long)(60000000.0 / bpm + 0.5);
    len += put_vlq(body + len, 0); body[len++] = 0xFF; body[len++] = 0x51; body[len++] = 0x03;
    body[len++] = (unsigned char)(us >> 16); body[len++] = (unsigned char)(us >> 8); body[len++] = (unsigned char)us;
    unsigned long last = 0;
    for (size_t i = 0; i < m; i++) {
        len += put_vlq(body + len, ev[i].tick - last);
        body[len++] = ev[i].data[0]; body[len++] = ev[i].data[1]; body[len++] = ev[i].data[2];
        last = ev[i].tick;
    }
    len += put_vlq(body + len, 0); body[len++] = 0xFF; body[len++] = 0x2F; body[len++] = 0x00;

    FILE* f = fopen(path, "wb");
    if (!f) { free(body); free(ev); return -1; }
    unsigned char hdr[14] = {'M','T','h','d', 0,0,0,6, 0,0, 0,1, (unsigned char)(ppq >> 8), (unsigned char)ppq};
    unsigned char trk[8] = {'M','T','r','k', (unsigned char)(len >> 24), (unsigned char)(len >> 16), (unsigned char)(len >> 8), (unsigned char)len};
    int ok = fwrite(hdr, 1, 14, f) == 14 && fwrite(trk, 1, 8, f) == 8 && fwrite(body, 1, len, f) == len;
    fclose(f);
    free(body); free(ev);
    return ok ? 0 : -1;
}

int vi_voice(int state, int transpose, int inversion, int spread, int* out) {
    if (state < 0 || state >= 12 * CC_NQUAL) return -1;
    int root = state / CC_NQUAL, q = state % CC_NQUAL;
    int n = cc_quality_len[q];
    int notes[8]; int k = 0;
    for (int i = 0; i < n; i++) notes[k++] = 48 + root + cc_quality_int[q][i];
    /* sort ascending */
    for (int i = 1; i < k; i++) for (int j = i; j > 0 && notes[j - 1] > notes[j]; j--) { int t = notes[j]; notes[j] = notes[j - 1]; notes[j - 1] = t; }
    for (int r = 0; r < (inversion % k + k) % k; r++) {
        int lowest = notes[0];
        for (int i = 1; i < k; i++) notes[i - 1] = notes[i];
        notes[k - 1] = lowest + 12;
    }
    if (spread && k >= 3) {
        if (1 < k) notes[1] += 12;
        if (3 < k) notes[3] += 12;
        for (int i = 1; i < k; i++) for (int j = i; j > 0 && notes[j - 1] > notes[j]; j--) { int t = notes[j]; notes[j] = notes[j - 1]; notes[j - 1] = t; }
    }
    for (int i = 0; i < k; i++) {
        int v = notes[i] + transpose;
        if (v < 0 || v > 127) return -1;
        out[i] = v;
    }
    return k;
}
