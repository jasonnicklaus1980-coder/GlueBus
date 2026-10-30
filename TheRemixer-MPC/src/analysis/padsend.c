#include <math.h>
#include <string.h>
#include "padsend.h"

static void fill(ps_MidiEvent* e, int status, int pitch, int data2, int delta) {
    memset(e, 0, sizeof *e);
    e->type = ps_kVstMidiType;
    e->byteSize = (int32_t)sizeof *e;
    e->deltaFrames = delta;
    e->midiData[0] = (char)status;
    e->midiData[1] = (char)pitch;
    e->midiData[2] = (char)data2;
}

size_t ps_block_events(const ps_note* sched, size_t n, long long blockStart, int blockSize, double sr,
                       ps_MidiEvent* out, size_t cap) {
    size_t m = 0;
    long long blockEnd = blockStart + blockSize;
    for (size_t i = 0; i < n; i++) {
        long long on = llround(sched[i].start * sr), off = llround(sched[i].end * sr);
        if (on >= blockStart && on < blockEnd && m < cap)
            fill(&out[m++], 0x90 | (sched[i].channel & 15), sched[i].pitch, sched[i].velocity, (int)(on - blockStart));
        if (off >= blockStart && off < blockEnd && m < cap)
            fill(&out[m++], 0x80 | (sched[i].channel & 15), sched[i].pitch, 0, (int)(off - blockStart));
    }
    return m;
}

size_t ps_all_off(const ps_note* sched, size_t n, ps_MidiEvent* out, size_t cap) {
    size_t m = 0;
    for (size_t i = 0; i < n && m < cap; i++)
        fill(&out[m++], 0x80 | (sched[i].channel & 15), sched[i].pitch, 0, 0);
    return m;
}
