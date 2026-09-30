#include "EQEngine.h"
#include <algorithm>
#include <cstring>

namespace rr
{
namespace
{
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
bool sameBand (const BandSettings& a, const BandSettings& b)
{
    return a.on == b.on && a.type == b.type && a.freq == b.freq && a.gainDb == b.gainDb && a.q == b.q && a.slope == b.slope && a.target == b.target;
}
bool sameQuick (const QuickSettings& a, const QuickSettings& b)
{
    return a.amount == b.amount && a.lowEnd == b.lowEnd && a.vocal == b.vocal && a.air == b.air && a.punch == b.punch;
}
bool isFlat (const BandSettings& b) { return ! b.on || (typeUsesGain (b.type) && std::fabs (b.gainDb) < 0.001); }
} // namespace

// ------------------------------------------------------------------------------------------------------------------
double bandResponseDb (const BandSettings& b, double hz, double fs)
{
    if (isFlat (b)) return 0.0;
    const bool cut = typeIsCut (b.type);
    const int n = cut ? sectionsForSlope (b.slope) : 1;
    double db = 0.0;
    for (int s = 0; s < n; ++s)
    {
        const double q = cut && b.slope > 0 ? butterworthQ (b.slope, s) : b.q;
        db += 20.0 * std::log10 (design (b.type, fs, b.freq, q, b.gainDb).magnitude (std::fmin (hz, 0.499 * fs), fs) + 1e-12);
    }
    return db;
}

double pinkLoudnessDb (const BandSettings* bands, int count, double fs)
{
    constexpr int kPts = 48;
    double sum = 0.0;
    for (int i = 0; i < kPts; ++i)
    {
        const double hz = 40.0 * std::pow (12000.0 / 40.0, (double) i / (kPts - 1));   // log-spaced = equal weight per octave
        double db = 0.0;
        for (int b = 0; b < count; ++b) db += bandResponseDb (bands[b], hz, fs);
        sum += std::pow (10.0, db / 10.0);
    }
    return 10.0 * std::log10 (sum / kPts);
}

void quickBands (const QuickSettings& q, BandSettings out[kQuickBands], double fs)
{
    const double a = std::clamp (q.amount, 0.0, 1.0);
    const double L = std::clamp (q.lowEnd, 0.0, 1.0) * a, V = std::clamp (q.vocal, 0.0, 1.0) * a;
    const double A = std::clamp (q.air, 0.0, 1.0) * a, P = std::clamp (q.punch, 0.0, 1.0) * a;
    auto set = [] (BandSettings& b, bool on, FilterType t, double f, double g, double qq, int slope = 1)
    {
        b.on = on; b.type = t; b.freq = f; b.gainDb = g; b.q = qq; b.slope = slope; b.target = Target::Both;
    };
    // LOW-END CONTROL: sub-rumble high-pass (rises 12 -> 32 Hz), low-shelf weight, tightened boom
    set (out[0], L > 1e-4, FilterType::HighPass, 12.0 + 20.0 * L, 0.0, 0.707, 1);
    set (out[1], true, FilterType::LowShelf, 70.0, +2.0 * L, 0.7);
    set (out[2], true, FilterType::Peak, 250.0, -1.5 * L, 1.0);
    // VOCAL CLARITY: less mud, more presence
    set (out[3], true, FilterType::Peak, 320.0, -2.0 * V, 1.1);
    set (out[4], true, FilterType::Peak, 3200.0, +2.5 * V, 0.9);
    // AIR & PRESENCE: open top, with a narrow sibilance guard so it doesn't turn harsh
    set (out[5], true, FilterType::HighShelf, 11000.0, +3.0 * A, 0.7);
    set (out[6], true, FilterType::Peak, 6800.0, -1.2 * A, 3.0);
    // PUNCH: low-mid body and upper-mid attack
    set (out[7], true, FilterType::Peak, 120.0, +1.8 * P, 1.1);
    set (out[8], true, FilterType::Peak, 2000.0, +1.5 * P, 1.3);

    // gain budget: the section's combined boost never exceeds kQuickMaxBoostDb
    double peak = 0.0;
    for (int i = 0; i < 40; ++i)
    {
        const double hz = 30.0 * std::pow (16000.0 / 30.0, i / 39.0);
        double db = 0.0;
        for (int b = 1; b < kQuickBands; ++b) db += bandResponseDb (out[b], hz, fs);
        peak = std::max (peak, db);
    }
    if (peak > kQuickMaxBoostDb)
    {
        const double k = kQuickMaxBoostDb / peak;
        for (int b = 1; b < kQuickBands; ++b) out[b].gainDb *= k;
    }
}

// ------------------------------------------------------------------------------------------------------------------
void Band::prepare (double sampleRate)
{
    fs = sampleRate;
    fadeLen = std::max (16, (int) std::lround (0.01 * fs));   // 10 ms crossfade
    reset();
}

void Band::reset()
{
    cur.resetState(); prev.resetState();
    fading = false; fadePos = 0; primed = false;
}

void Band::design (Filter& f, bool keepState)
{
    const bool wasActive = f.active;
    const bool cut = typeIsCut (live.type);
    f.type = live.type; f.route = live.target; f.mode = liveMode;
    f.active = live.on && ! (typeUsesGain (live.type) && std::fabs (sg) < 0.001);
    const int n = cut ? sectionsForSlope (live.slope) : 1;
    if (! keepState || n != f.nsec || (! wasActive && f.active)) f.resetState();
    f.nsec = n;
    if (! f.active) return;
    for (int s = 0; s < n; ++s)
    {
        const double q = cut && live.slope > 0 ? butterworthQ (live.slope, s) : sq;
        const Coeffs c = rr::design (live.type, fs, sf, q, sg);
        f.sec[s][0].c = c; f.sec[s][1].c = c;
    }
}

void Band::snapToTarget()
{
    live = target; liveMode = targetMode;
    sf = target.freq; sg = target.gainDb; sq = target.q;
    design (cur, false);
    fading = false; primed = true;
}

void Band::tick (int n)
{
    if (! primed) { snapToTarget(); return; }
    const bool structural = target.on != live.on || target.type != live.type || target.slope != live.slope
                         || target.target != live.target || targetMode != liveMode;
    if (structural)
    {
        prev = cur;                                   // the old filter keeps running and fades out
        fading = true; fadePos = 0;
        const bool sameShape = target.type == live.type && target.slope == live.slope;
        live.on = target.on; live.type = target.type; live.slope = target.slope; live.target = target.target; liveMode = targetMode;
        if (! sameShape) { sf = target.freq; sg = target.gainDb; sq = target.q; }
        design (cur, sameShape);
    }
    // glide frequency (in octaves), gain and Q toward their targets, ~20 ms
    const double k = 1.0 - std::exp (-(double) n / (0.02 * fs));
    const double nf = sf * std::pow (target.freq / sf, k), ng = sg + (target.gainDb - sg) * k, nq = sq * std::pow (target.q / sq, k);
    bool moved = false;
    auto settle = [&] (double& v, double nv, double t, bool relative)
    {
        const double tol = relative ? 1e-5 * std::fabs (t) : 1e-5;
        if (std::fabs (nv - t) < tol) nv = t;
        if (nv != v) { v = nv; moved = true; }
    };
    settle (sf, nf, target.freq, true); settle (sg, ng, target.gainDb, false); settle (sq, nq, target.q, true);
    live.freq = sf; live.gainDb = sg; live.q = sq;
    if (moved) design (cur, true);
    if (! fading) { prev.resetState(); }
    for (auto& s : cur.sec) { s[0].flushDenormals(); s[1].flushDenormals(); }
}

// ------------------------------------------------------------------------------------------------------------------
void EQEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    calcEvery = std::max (16, (int) std::lround (0.012 * fs));
    for (auto& b : bands) b.prepare (fs);
    for (auto& b : qbands) b.prepare (fs);
    gSmooth = 1.0 - std::exp (-1.0 / (0.01 * fs));     // 10 ms for the gains
    wetStep = 1.0 / (0.01 * fs);                          // bypass: a linear 10 ms crossfade
    reset();
}

void EQEngine::reset()
{
    for (auto& b : bands) b.reset();
    for (auto& b : qbands) b.reset();
    cacheValid = false; firstBlock = true; tickPos = 0; sinceCalc = 0;
}

void EQEngine::snapToTargets()
{
    cacheValid = false;
    control (0);
    for (auto& b : bands) b.snapToTarget();
    for (auto& b : qbands) b.snapToTarget();
    quickCompDb = targetQuickComp; autoDb = targetAuto;
    gIn = tIn; gOut = tOut; wet = bypass ? 0.0 : 1.0;
    firstBlock = false;
}

void EQEngine::control (int n)
{
    // loudness estimates and the Quick filters are recomputed only when something changed, at most every ~12 ms
    const bool changed = ! cacheValid || autoGain != lastAutoGain || ! sameQuick (quick, lastQuick)
                      || ! std::equal (user.begin(), user.end(), lastUser.begin(), sameBand);
    sinceCalc += n;
    if (changed && (sinceCalc >= calcEvery || ! cacheValid))
    {
        BandSettings q[kQuickBands];
        quickBands (quick, q, fs);
        for (int i = 0; i < kQuickBands; ++i) qbands[(size_t) i].setTarget (q[i], mode);
        targetQuickComp = -pinkLoudnessDb (q, kQuickBands, fs);                           // the Quick section is level-matched
        targetAuto = autoGain ? -pinkLoudnessDb (user.data(), kUserBands, fs) : 0.0;
        lastUser = user; lastQuick = quick; lastAutoGain = autoGain; cacheValid = true; sinceCalc = 0;
    }
    for (int i = 0; i < kUserBands; ++i) bands[(size_t) i].setTarget (user[(size_t) i], mode);
    for (auto& b : qbands) b.setMode (mode);
    if (n > 0)
    {
        for (auto& b : bands) b.tick (n);
        for (auto& b : qbands) b.tick (n);
        const double k = 1.0 - std::exp (-(double) n / (0.05 * fs));                       // loudness targets glide over 50 ms
        quickCompDb += (targetQuickComp - quickCompDb) * k; if (std::fabs (quickCompDb - targetQuickComp) < 1e-6) quickCompDb = targetQuickComp;
        autoDb += (targetAuto - autoDb) * k;                if (std::fabs (autoDb - targetAuto) < 1e-6) autoDb = targetAuto;
    }
    tIn = dbToGain (inGainDb);
    tOut = dbToGain (outGainDb + quickCompDb + autoDb);
}

void EQEngine::process (double* l, double* r, int n)
{
    if (firstBlock) snapToTargets();
    const bool stereo = r != nullptr;
    const double wetTarget = bypass ? 0.0 : 1.0;
    if (bypass && wet == 0.0) { control (n); return; }     // fully bypassed: untouched audio (bit-exact)
    for (int i = 0; i < n; ++i)
    {
        if (tickPos == 0) control (16);
        tickPos = (tickPos + 1) & 15;
        gIn += (tIn - gIn) * gSmooth;   if (std::fabs (gIn - tIn) < 1e-10) gIn = tIn;
        gOut += (tOut - gOut) * gSmooth; if (std::fabs (gOut - tOut) < 1e-10) gOut = tOut;
        if (wet != wetTarget) wet = wetTarget > wet ? std::fmin (wetTarget, wet + wetStep) : std::fmax (wetTarget, wet - wetStep);   // linear 10 ms

        const double dl = l[i], dr = stereo ? r[i] : 0.0;
        double x = dl * gIn, y = stereo ? dr * gIn : 0.0;
        for (auto& b : bands) b.process (x, y, stereo);
        for (auto& b : qbands) b.process (x, y, stereo);
        x *= gOut; y *= gOut;
        if (wet == 1.0) { l[i] = x; if (stereo) r[i] = y; }
        else { l[i] = dl + (x - dl) * wet; if (stereo) r[i] = dr + (y - dr) * wet; }
    }
}
} // namespace rr
