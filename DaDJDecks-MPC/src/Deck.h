#pragma once
// Da DJ Decks - one deck: variable-speed playback (4-point Hermite interpolation, pitch like a turntable: tempo and
// key move together), cue, beat loops, nudge, beat-grid sync, 3-band isolator EQ with kills (Linkwitz-Riley 24 dB
// crossovers at 300 Hz and 4 kHz), one-knob DJ filter (low-pass left, high-pass right), gain and channel fader.
// Every jump (cue, nudge, sync, loop wrap) crossfades over 3 ms, and play/stop ramp over 5 ms: no clicks.
// Scratch: while the hand (a Q-Link or the platter on screen) moves, the play head follows it (forwards / backwards,
// pitch following the speed); still = silent, like a held record. Transform: a beat-synced gate (1 ms edges).
// SP-12 mode: playback through an SP-1200-style 26.04 kHz DAC clock with drop-sample reads and 12-bit words.
#include "Wav.h"
#include <atomic>
#include <algorithm>
#include <cmath>

namespace dj
{
constexpr double kSpRate = 26040.0;
inline double q12 (double x) { x = x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x); return std::floor (x * 2048.0) / 2048.0; }
struct BQ
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    void set (int type, double fs, double f, double q)                // 0 LP, 1 HP
    {
        f = std::fmin (std::fmax (f, 10.0), 0.45 * fs);
        const double w = 2 * M_PI * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        if (type == 0) { b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; }
        else           { b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; }
        b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    inline double run (int c, double x)
    {
        const double y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y;
        return y;
    }
    void flush() { for (int c = 0; c < 2; ++c) { if (std::fabs (z1[c]) < 1e-25) z1[c] = 0; if (std::fabs (z2[c]) < 1e-25) z2[c] = 0; } }
};

// Linkwitz-Riley 4th order split: two cascaded Butterworth sections per side
struct Crossover
{
    BQ lp[2], hp[2];
    void set (double fs, double f) { for (int i = 0; i < 2; ++i) { lp[i].set (0, fs, f, 0.70710678); hp[i].set (1, fs, f, 0.70710678); } }
    void reset() { for (int i = 0; i < 2; ++i) { lp[i].reset(); hp[i].reset(); } }
    inline void split (int c, double x, double& lo, double& hi)
    {
        lo = lp[1].run (c, lp[0].run (c, x));
        hi = hp[1].run (c, hp[0].run (c, x));
    }
    void flush() { for (int i = 0; i < 2; ++i) { lp[i].flush(); hp[i].flush(); } }
};

struct DeckControls                   // set from the plugin's parameters every block
{
    bool play = false;
    double pitch = 0.0;               // fraction: +0.08 = +8 %
    double gainDb = 0.0, hiDb = 0.0, midDb = 0.0, lowDb = 0.0;   // EQ at the minimum (-24) = kill
    double filter = 0.0;              // -1 .. +1
    double fader = 0.8;               // 0 .. 1
    bool scratch = false;             // the hand is on the record
    double scratchTarget = 0.0;       // where the hand has moved it (frames)
    double transform = 0.0;           // gate length in beats (0.5 = 1/8 notes ...), 0 = off
    bool sp12 = false;
};

class Deck
{
public:
    Track* track = nullptr;           // owned by the plugin: swapped only on the audio thread
    double pos = 0.0, cue = 0.0;      // frames
    bool playing = false;
    int loopBeats = 0; double loopStart = 0, loopEnd = 0;
    bool alignOnStart = false;        // SYNC pressed while stopped: line the beats up when PLAY is pressed
    float peak = 0.f;                 // post-fader peak of the last block

    void prepare (double rate)
    {
        fs = rate;
        xo1.set (fs, 300.0); xo2.set (fs, 4000.0);
        rampStep = 1.0 / (0.005 * fs); xfLen = std::max (16, (int) (0.003 * fs));
        reset();
    }
    void reset()
    {
        xo1.reset(); xo2.reset(); flt.reset(); ramp = 0; xfLeft = 0; filterMode = 0;
        gLow = gMid = gHigh = gOut = 1.0;
    }
    void setTrack (Track* t)          // new track: stopped at its first beat
    {
        track = t; playing = false; ramp = 0; xfLeft = 0; loopBeats = 0; alignOnStart = false;
        pos = cue = (t != nullptr && t->bpm > 0) ? t->offset : 0.0;
    }
    double beatFrames() const { return track && track->bpm > 0 ? track->rate * 60.0 / track->bpm : 0.0; }
    double beatPhase (double at) const               // 0..1 position within the beat
    {
        const double b = beatFrames(); if (b <= 0) return 0.0;
        double p = dj::fmodd (at - track->offset, b) / b; if (p < 0) p += 1.0; return p;
    }
    double beatIndex() const { const double b = beatFrames(); return b > 0 ? std::floor ((pos - track->offset) / b) : 0.0; }
    double effectiveBpm (double pitch) const { return track && track->bpm > 0 ? track->bpm * (1.0 + pitch) : 0.0; }

    void jump (double to)                            // crossfaded move of the play head
    {
        if (track == nullptr) return;
        to = std::fmax (0.0, std::fmin (to, (double) track->frames - 4));
        if (playing || ramp > 0) { xfPos = pos; xfLeft = xfLen; }
        pos = to;
    }
    void setLoop (int beats)                         // 0 = off; else loop the current beats, snapped to the grid
    {
        const double b = beatFrames();
        if (beats <= 0 || b <= 0) { loopBeats = 0; return; }
        if (loopBeats == 0) loopStart = track->offset + std::floor ((pos - track->offset) / b) * b;
        loopBeats = beats; loopEnd = loopStart + beats * b;
        if (pos >= loopEnd) jump (loopStart + dj::fmodd (pos - loopStart, beats * b));
    }

    // render n samples of this deck (post EQ, filter, gain and channel fader) into l/r (overwrites)
    void render (const DeckControls& c, double hostRate, float* l, float* r, int n)
    {
        peak = 0.f;
        if (track == nullptr) { for (int i = 0; i < n; ++i) l[i] = r[i] = 0.f; return; }
        if (c.play && ! playing) playing = true;
        if (! c.play) playing = false;
        const double step = track->rate / hostRate * (1.0 + c.pitch);
        const double kScr = 1.0 - std::exp (-1.0 / (0.008 * fs));             // the platter follows the hand (~8 ms)
        const double gateStep = 1.0 / (0.001 * fs), spInc = kSpRate / hostRate;
        const double beatF = beatFrames();
        // EQ / filter / gain targets (smoothed per block)
        auto g = [] (double db) { return db <= -23.9 ? 0.0 : std::pow (10.0, db / 20.0); };
        const double tLow = g (c.lowDb), tMid = g (c.midDb), tHigh = g (c.hiDb);
        const double tOut = std::pow (10.0, c.gainDb / 20.0) * c.fader * c.fader;
        const double k = 1.0 - std::exp (-(double) n / (0.01 * fs));
        gLow += (tLow - gLow) * k; gMid += (tMid - gMid) * k; gHigh += (tHigh - gHigh) * k; gOut += (tOut - gOut) * k;
        if (std::fabs (gOut - tOut) < 1e-6) gOut = tOut;
        updateFilter (c.filter);
        for (int i = 0; i < n; ++i)
        {
            // play / stop ramp
            if (playing) ramp = std::fmin (1.0, ramp + rampStep); else ramp = std::fmax (0.0, ramp - rampStep);
            double sl = 0, sr = 0;
            if (c.scratch)
            {
                // the record moves with the hand; the sound follows its speed, a still record is silent
                const double before = pos, target = std::fmax (0.0, std::fmin (c.scratchTarget, (double) track->frames - 4));
                pos += (target - pos) * kScr;
                if (std::fabs (target - pos) < 0.25) pos = target;                    // the hand has stopped: so has the record
                const double speed = std::fabs (pos - before) / (track->rate / hostRate);   // 1 = normal speed
                scrGain += ((speed > 0.01 ? std::fmin (1.0, speed * 8.0) : 0.0) - scrGain) * 0.02;
                if (scrGain < 1e-5) scrGain = 0.0;
                readSp (c.sp12, spInc, pos, sl, sr);
                sl *= scrGain; sr *= scrGain;
                ramp = playing ? 1.0 : 0.0; xfLeft = 0;
            }
            else if (ramp > 0)
            {
                scrGain = 1.0;
                readSp (c.sp12, spInc, pos, sl, sr);
                if (xfLeft > 0 && ! c.sp12)
                {
                    double ol, orr; read (xfPos, ol, orr);
                    const double t = (double) xfLeft / xfLen;
                    sl = sl + (ol - sl) * t; sr = sr + (orr - sr) * t;
                    xfPos += step; --xfLeft;
                }
                sl *= ramp; sr *= ramp;
                pos += step;
                if (loopBeats > 0 && pos >= loopEnd) jump (pos - (loopEnd - loopStart));
                if (pos >= track->frames - 4) { pos = track->frames - 4; playing = false; ramp = 0; endReached = true; }
            }
            // transform: open for the first half of each step of the beat grid
            double gateT = 1.0;
            if (c.transform > 0)
            {
                // on the track's beat grid; a take without a detectable beat uses 120 BPM from its start
                const double bf = beatF > 0 ? beatF : track->rate * 0.5, off = beatF > 0 ? track->offset : 0.0;
                double ph = (pos - off) / (bf * c.transform); ph -= std::floor (ph);
                gateT = ph < 0.5 ? 1.0 : 0.0;
            }
            gate = gate < gateT ? std::fmin (gateT, gate + gateStep) : std::fmax (gateT, gate - gateStep);
            sl *= gate; sr *= gate;
            double out[2] = { sl, sr };
            for (int ch = 0; ch < 2; ++ch)
            {
                double lo, rest, mid, hi;
                xo1.split (ch, out[ch], lo, rest);
                xo2.split (ch, rest, mid, hi);
                double y = lo * gLow + mid * gMid + hi * gHigh;
                if (filterMode != 0) y = flt.run (ch, y);
                out[ch] = y * gOut;
            }
            l[i] = (float) out[0]; r[i] = (float) out[1];
            const float a = std::fmax (std::fabs (l[i]), std::fabs (r[i])); if (a > peak) peak = a;
        }
        xo1.flush(); xo2.flush(); flt.flush();
    }
    bool takeEndReached() { const bool e = endReached; endReached = false; return e; }

private:
    // SP-12: a new 12-bit word only on each tick of the 26.04 kHz DAC clock, read without interpolation (held between)
    inline void readSp (bool sp, double inc, double p, double& l, double& r)
    {
        if (! sp) { read (p, l, r); return; }
        spPhase += inc;
        if (spPhase >= 1.0 || ! spPrimed)
        {
            spPhase -= std::floor (spPhase); spPrimed = true;
            long i = (long) p; i = i < 0 ? 0 : (i >= track->frames ? track->frames - 1 : i);
            spL = q12 (track->pcm[2 * i] / 32768.0); spR = q12 (track->pcm[2 * i + 1] / 32768.0);
        }
        l = spL; r = spR;
    }
    inline void read (double p, double& l, double& r) const          // 4-point Hermite
    {
        const long i = (long) p; const double t = p - i;
        const int16_t* s = track->pcm;
        const long n = track->frames;
        auto at = [&] (long j, int ch) { j = j < 0 ? 0 : (j >= n ? n - 1 : j); return s[2 * j + ch] * (1.0 / 32768.0); };
        for (int ch = 0; ch < 2; ++ch)
        {
            const double y0 = at (i - 1, ch), y1 = at (i, ch), y2 = at (i + 1, ch), y3 = at (i + 2, ch);
            const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            (ch == 0 ? l : r) = ((c3 * t + c2) * t + c1) * t + y1;
        }
    }
    void updateFilter (double v)
    {
        // centre dead zone = off; left: low-pass 20 kHz -> 60 Hz; right: high-pass 20 Hz -> 8 kHz (both slightly resonant)
        const int mode = v < -0.02 ? -1 : (v > 0.02 ? 1 : 0);
        sFilter += (v - sFilter) * 0.3;
        if (mode != filterMode) { flt.reset(); filterMode = mode; sFilter = v; }
        if (mode < 0) flt.set (0, fs, 20000.0 * std::pow (60.0 / 20000.0, std::fmin (1.0, (-sFilter - 0.02) / 0.98)), 0.9);
        if (mode > 0) flt.set (1, fs, 20.0 * std::pow (8000.0 / 20.0, std::fmin (1.0, (sFilter - 0.02) / 0.98)), 0.9);
    }

    double fs = 44100.0;
    Crossover xo1, xo2;
    BQ flt; int filterMode = 0; double sFilter = 0;
    double gLow = 1, gMid = 1, gHigh = 1, gOut = 1;
    double ramp = 0, rampStep = 0.005;
    double xfPos = 0; int xfLeft = 0, xfLen = 132;
    bool endReached = false;
    double scrGain = 1.0, gate = 1.0;
    double spPhase = 0, spL = 0, spR = 0; bool spPrimed = false;
};
} // namespace dj
