/* Turns a schedule of pad notes into VST MIDI events, one audio block at a time. Pure code: no host calls.
   The event layout is the VST 2.4 VstMidiEvent (32 bytes). */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { ps_kVstMidiType = 1 };

typedef struct {
    int32_t type;                        /* ps_kVstMidiType */
    int32_t byteSize;                    /* sizeof(ps_MidiEvent) */
    int32_t deltaFrames;                 /* offset inside the audio block */
    int32_t flags;
    int32_t noteLength, noteOffset;
    char midiData[4];                    /* status, data1, data2, 0 */
    char detune, noteOffVelocity, reserved1, reserved2;
} ps_MidiEvent;

/* One pad hit: start and end in seconds from the start of playback. channel is 0..15. */
typedef struct { double start, end; int pitch, channel, velocity; } ps_note;

/* Writes the note-on and note-off events that fall inside [blockStart, blockStart + blockSize) of the schedule.
   blockStart is in samples from the start of playback, at rate sr. Returns the number of events written (at most cap). */
size_t ps_block_events(const ps_note* sched, size_t n, long long blockStart, int blockSize, double sr,
                       ps_MidiEvent* out, size_t cap);

/* Writes a note-off for every note in the schedule (used when playback stops, so nothing hangs).
   Returns the number written (at most cap). */
size_t ps_all_off(const ps_note* sched, size_t n, ps_MidiEvent* out, size_t cap);

#ifdef __cplusplus
}
#endif
