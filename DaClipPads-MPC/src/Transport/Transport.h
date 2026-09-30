#pragma once
// Musical clock. Every launch, stop, follow action and scene change is scheduled on one beat timeline
// (quarter notes). Each block the host is asked for its time info (audioMasterGetTime):
//  - SYNC on and the host reports a tempo: that tempo is used.
//  - SYNC on and the host is playing with a valid position: the timeline is locked to the host position and bar
//    lines follow the host's bar start and time signature. When the host position jumps (sequence loop,
//    locate), everything already scheduled keeps its distance to "now".
//  - Otherwise the plugin's own clock runs at MASTER TEMPO, so pads stay quantised with the sequencer stopped.
#include "../Utilities/Util.h"
#include "../vst2.h"

namespace cp
{
enum Quant { Q_NONE, Q_4, Q_8, Q_8T, Q_16, Q_16T, Q_BAR, Q_2BAR, Q_4BAR, Q_COUNT };

struct Transport
{
    double sr = 44100.0;
    double tempo = 120.0;
    double beat = 0.0;                 // beat position at the current sample
    double beatsPerBar = 4.0;
    double barOffset = 0.0;            // a bar line falls on barOffset + k * beatsPerBar
    bool hostTempo = false, hostPos = false, hostPlaying = false, prevHostPlaying = false;
    bool startedEdge = false, stoppedEdge = false;

    double bps() const { return tempo / (60.0 * sr); }                       // beats per sample
    double framesPerBeat() const { return 60.0 * sr / tempo; }
    void advance (int n) { beat += n * bps(); }

    // Returns how far the timeline jumped (beats) so the caller can move scheduled events with it.
    double beginBlock (const VstTimeInfo* ti, bool sync, double masterTempo)
    {
        const int fl = ti != nullptr ? ti->flags : 0;
        hostTempo = sync && (fl & kVstTempoValid) && ti->tempo > 1.0;
        tempo = hostTempo ? clampd (ti->tempo, 20.0, 999.0) : clampd (masterTempo, 20.0, 999.0);
        prevHostPlaying = hostPlaying;
        hostPlaying = (fl & kVstTransportPlaying) != 0;
        startedEdge = hostPlaying && ! prevHostPlaying;
        stoppedEdge = ! hostPlaying && prevHostPlaying;
        if ((fl & kVstTimeSigValid) && ti->timeSigNumerator > 0 && ti->timeSigDenominator > 0)
            beatsPerBar = clampd (ti->timeSigNumerator * 4.0 / ti->timeSigDenominator, 1.0, 16.0);
        hostPos = sync && hostPlaying && (fl & kVstPpqPosValid);
        double jump = 0.0;
        if (hostPos)
        {
            jump = ti->ppqPos - beat;
            if (std::fabs (jump) < 1e-3) jump = 0.0;                                   // rounding: stay continuous
            beat += jump;
            barOffset = (fl & kVstBarsValid) ? wrapTo (ti->barStartPos, beatsPerBar) : 0.0;
        }
        return jump;
    }
    static double wrapTo (double x, double m) { double r = fmodd (x, m); return r < 0 ? r + m : r; }

    static const char* name (int q)
    {
        static const char* const k[] { "None", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1 Bar", "2 Bars", "4 Bars" };
        return k[clampi (q, 0, Q_COUNT - 1)];
    }
    double grid (int q) const
    {
        switch (q)
        {
            case Q_4: return 1.0; case Q_8: return 0.5; case Q_8T: return 1.0 / 3.0; case Q_16: return 0.25;
            case Q_16T: return 1.0 / 6.0; case Q_BAR: return beatsPerBar; case Q_2BAR: return 2 * beatsPerBar;
            case Q_4BAR: return 4 * beatsPerBar; default: return 0.0;
        }
    }
    // Grid point k for quantisation q. Swing (0.5 = straight .. 0.75) delays the odd 1/8 or 1/16 steps.
    double point (int q, long k, double swing) const
    {
        const double g = grid (q), off = q >= Q_BAR ? barOffset : 0.0;
        double p = off + k * g;
        if ((q == Q_8 || q == Q_16) && (k & 1)) p += (swing - 0.5) * 2.0 * g;
        return p;
    }
    // When a launch at beat b should happen. A press up to `late` beats after a grid point starts right away,
    // already in step (returns that grid point, which is <= b); otherwise the next grid point.
    double launchBeat (double b, int q, double swing, double late) const
    {
        if (q <= Q_NONE || q >= Q_COUNT) return b;
        const double g = grid (q), off = q >= Q_BAR ? barOffset : 0.0;
        const long k = (long) std::floor ((b - off) / g);
        double prev = -1e18, next = 1e18;
        for (long j = k - 1; j <= k + 1; ++j)
        {
            const double p = point (q, j, swing);
            if (p <= b + 1e-9) prev = std::fmax (prev, p); else next = std::fmin (next, p);
        }
        if (b - prev <= late) return prev;
        return next;
    }
    double beatInBar() const { return wrapTo (beat - barOffset, beatsPerBar); }
    long barNumber() const { return (long) std::floor ((beat - barOffset) / beatsPerBar); }
};
} // namespace cp
