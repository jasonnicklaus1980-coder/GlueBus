#pragma once
// MIDI input. MPC sends the pads of a plugin track to its instrument as MIDI notes; the plugin receives them
// through VST2 effProcessEvents, sample-accurately (deltaFrames). There is no direct pad API for plugins, so the
// mapping is plain MIDI and fully configurable:
//  - each clip has a NOTE (default C1 = 36 for clip 1, C#1 = 37 for clip 2 ... D#2 = 51 for clip 16; that is
//    MPC's default pad layout for bank A), set on the SETTINGS page or with LEARN;
//  - the slices of the selected clip play from SLICE NOTE upwards (default C3 = 60, 32 notes);
//  - MIDI CHANNEL filters (Omni by default); VELOCITY scales the level by VEL SENS.
#include "../vst2.h"
#include "../Utilities/Util.h"

namespace cp
{
struct MidiNote { int32_t frame; uint8_t note, vel; bool on; };

// Filled by effProcessEvents (just before process on the audio thread), read by process.
struct MidiQueue
{
    static constexpr int kMax = 256;
    MidiNote ev[kMax]; int count = 0;
    void clear() { count = 0; }
    void add (const VstEvents* e, int channel)                   // channel 0 = omni, else 1..16
    {
        if (e == nullptr) return;
        for (int i = 0; i < e->numEvents && count < kMax; ++i)
        {
            const VstEvent* v = e->events[i];
            if (v == nullptr || v->type != kVstMidiType) continue;
            const VstMidiEvent* m = (const VstMidiEvent*) v;
            const uint8_t st = (uint8_t) m->midiData[0], d1 = (uint8_t) m->midiData[1] & 0x7f, d2 = (uint8_t) m->midiData[2] & 0x7f;
            if (channel > 0 && (st & 0x0f) != channel - 1) continue;
            const uint8_t type = st & 0xf0;
            if (type != 0x90 && type != 0x80) continue;
            MidiNote n { m->deltaFrames < 0 ? 0 : m->deltaFrames, d1, d2, type == 0x90 && d2 > 0 };
            // keep them sorted by time (hosts usually send them sorted already)
            int j = count++;
            while (j > 0 && ev[j - 1].frame > n.frame) { ev[j] = ev[j - 1]; --j; }
            ev[j] = n;
        }
    }
};
} // namespace cp
