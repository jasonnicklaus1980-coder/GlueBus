#pragma once
// RadioReady EQ - the DSP engine: 8 user bands + the QUICK RADIO READY section, 64-bit, click-free.
// Framework-free (no JUCE) so it can be unit-tested and reused.
#include "Biquad.h"
#include <array>

namespace rr
{
enum class StereoMode { LeftRight = 0, MidSide = 1 };
enum class Target { Both = 0, LeftOrMid = 1, RightOrSide = 2 };

struct BandSettings
{
    bool on = false;
    FilterType type = FilterType::Peak;
    double freq = 1000.0, gainDb = 0.0, q = 0.707;
    int slope = 1;                 // cuts: 0 = 12, 1 = 24, 2 = 48 dB/oct
    Target target = Target::Both;
};

struct QuickSettings
{
    double amount = 0.0;           // 0..1 intensity of the whole section
    double lowEnd = 0.0, vocal = 0.0, air = 0.0, punch = 0.0;   // 0..1 each
};

constexpr int kUserBands = 8;
constexpr int kQuickBands = 9;
constexpr double kQuickMaxBoostDb = 4.0;   // the Quick section never boosts more than this anywhere

// ---- response helpers (also used by the GUI curve, auto gain and the preset designer) ----
double bandResponseDb (const BandSettings& b, double hz, double fs);
// average level change on pink-noise-like material (equal energy per octave, 40 Hz .. 12 kHz), in dB
double pinkLoudnessDb (const BandSettings* bands, int count, double fs);
// the Quick section's filters for given settings (gain budget applied)
void quickBands (const QuickSettings& q, BandSettings out[kQuickBands], double fs);

// One band: up to 4 sections per channel, smoothed frequency / gain / Q, crossfade on discrete changes
class Band
{
public:
    void prepare (double fs);
    void reset();
    void setTarget (const BandSettings& s, StereoMode mode) { target = s; targetMode = mode; }
    void setMode (StereoMode mode) { targetMode = mode; }
    void snapToTarget();                    // jump straight to the target (after reset / preset load in tests)
    void tick (int samplesInTick);          // control-rate update (every 16 samples)
    inline void process (double& l, double& r, bool stereo);
    bool isIdentity() const { return ! fading && ! cur.active; }

private:
    struct Filter
    {
        bool active = false;               // false: identity (off or flat)
        FilterType type = FilterType::Peak;
        int nsec = 0;
        Target route = Target::Both;
        StereoMode mode = StereoMode::LeftRight;
        Section sec[4][2];
        inline void apply (double& l, double& r, bool stereo);
        void resetState() { for (auto& s : sec) { s[0].reset(); s[1].reset(); } }
    };
    void design (Filter& f, bool keepState);

    double fs = 44100.0;
    BandSettings target;  StereoMode targetMode = StereoMode::LeftRight;
    double sf = 1000.0, sg = 0.0, sq = 0.707;    // smoothed
    BandSettings live;    StereoMode liveMode = StereoMode::LeftRight;   // what cur was designed for
    Filter cur, prev;
    bool fading = false; int fadePos = 0, fadeLen = 441;
    bool primed = false;
};

class EQEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    // set targets (any time between process() calls, e.g. at the start of each block)
    void setUserBand (int i, const BandSettings& s) { user[(size_t) i] = s; }
    void setStereoMode (StereoMode m) { mode = m; }
    void setQuick (const QuickSettings& q) { quick = q; }
    void setGains (double inDb, double outDb) { inGainDb = inDb; outGainDb = outDb; }
    void setAutoGain (bool on) { autoGain = on; }
    void setBypass (bool b) { bypass = b; }
    void snapToTargets();                  // no glide: for tests / first block

    // process in place; r may be nullptr for mono
    void process (double* l, double* r, int n);

    // introspection (GUI / tests)
    double quickCompensationDb() const { return quickCompDb; }
    double autoGainDb() const { return autoDb; }
    double sampleRate() const { return fs; }

private:
    void control (int n);
    double fs = 44100.0;
    std::array<BandSettings, kUserBands> user {};
    QuickSettings quick;
    StereoMode mode = StereoMode::LeftRight;
    double inGainDb = 0, outGainDb = 0; bool autoGain = false, bypass = false;

    std::array<Band, kUserBands> bands;
    std::array<Band, kQuickBands> qbands;
    double quickCompDb = 0, autoDb = 0;
    double gIn = 1, gOut = 1, tIn = 1, tOut = 1, wet = 1, gSmooth = 0.001, wetStep = 0.002;
    int tickPos = 0;
    // cache for the (relatively costly) loudness estimates
    std::array<BandSettings, kUserBands> lastUser {}; QuickSettings lastQuick; bool cacheValid = false, lastAutoGain = false;
    int sinceCalc = 0, calcEvery = 529;
    double targetQuickComp = 0, targetAuto = 0;
    bool firstBlock = true;
};

// ---- inline hot path ----
inline void Band::Filter::apply (double& l, double& r, bool stereo)
{
    if (! active) return;
    if (! stereo)
    {
        if (route == Target::RightOrSide && mode == StereoMode::MidSide) return;   // a mono signal has no side
        for (int s = 0; s < nsec; ++s) l = sec[s][0].process (l);
        return;
    }
    if (route == Target::Both)
    {
        for (int s = 0; s < nsec; ++s) { l = sec[s][0].process (l); r = sec[s][1].process (r); }
        return;
    }
    if (mode == StereoMode::LeftRight)
    {
        if (route == Target::LeftOrMid) for (int s = 0; s < nsec; ++s) l = sec[s][0].process (l);
        else                            for (int s = 0; s < nsec; ++s) r = sec[s][1].process (r);
        return;
    }
    double m = 0.5 * (l + r), sd = 0.5 * (l - r);
    if (route == Target::LeftOrMid) for (int s = 0; s < nsec; ++s) m = sec[s][0].process (m);
    else                            for (int s = 0; s < nsec; ++s) sd = sec[s][1].process (sd);
    l = m + sd; r = m - sd;
}

inline void Band::process (double& l, double& r, bool stereo)
{
    if (! fading) { cur.apply (l, r, stereo); return; }
    double l2 = l, r2 = r;
    cur.apply (l, r, stereo);
    prev.apply (l2, r2, stereo);
    const double t = (double) ++fadePos / fadeLen;
    l = l2 + (l - l2) * t; r = r2 + (r - r2) * t;
    if (fadePos >= fadeLen) fading = false;
}
} // namespace rr
