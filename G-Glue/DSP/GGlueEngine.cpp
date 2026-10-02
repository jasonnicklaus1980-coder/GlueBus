#include "GGlueEngine.h"
#include "Denormals.h"
#include <algorithm>
#include <cmath>

namespace gglue
{
namespace
{
constexpr float kLn10Over20 = 0.11512925464970228f;   // ln(10) / 20
inline float dbToGain (float db) { return std::exp (db * kLn10Over20); }
inline float gainToDb (float g) { return 8.685889638065037f * std::log (g); }   // 20 / ln(10)

// fast odd rational tanh approximation (exact at 0, slope 1, saturates to +-1)
inline float softTanh (float x)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// knee width follows the ratio: gentle ratios get a wide knee
inline float kneeFor (int ratioIndex) { return ratioIndex == 0 ? 8.f : (ratioIndex == 1 ? 6.f : 4.f); }

constexpr float kAnalogDrive = 0.35f;                  // analog stage input scaling (lower = cleaner)

// output protection: transparent below -0.5 dBFS, then a smooth knee that never exceeds 1.0
constexpr float kProtectStart = 0.9440609f;            // -0.5 dBFS
inline float protect (float x)
{
    const float a = std::fabs (x);
    if (a <= kProtectStart) return x;
    const float room = 1.f - kProtectStart;
    const float y = kProtectStart + room * softTanh ((a - kProtectStart) / room);
    return x < 0.f ? -y : y;
}

// coefficients for the oversampling filters: stage 1 (base <-> 2x) is the steep one
double gCoefStage1[kMaxHalfbandCoefs], gCoefStage2[kMaxHalfbandCoefs];
constexpr int kStage1Coefs = 12, kStage2Coefs = 6;
constexpr double kStage1Transition = 0.04, kStage2Transition = 0.2;
struct CoefInit { CoefInit() { designHalfband (gCoefStage1, kStage1Coefs, kStage1Transition); designHalfband (gCoefStage2, kStage2Coefs, kStage2Transition); } };
const CoefInit gCoefInit;
} // namespace

void Engine::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate > 1000.0 ? sampleRate : 44100.0;
    numCh = numChannels < 1 ? 1 : (numChannels > 2 ? 2 : numChannels);
    os = config.oversampling >= 4 ? 4 : (config.oversampling >= 2 ? 2 : 1);
    osRate = fs * os;
    for (auto& c : ch)
    {
        c.up1.setup (gCoefStage1, kStage1Coefs); c.down1.setup (gCoefStage1, kStage1Coefs);
        c.up2.setup (gCoefStage2, kStage2Coefs); c.down2.setup (gCoefStage2, kStage2Coefs);
        c.analogDc.setup (8.0, osRate);
    }
    for (Smoother* s : { &sThreshold, &sSlope, &sKnee, &sMakeupDb, &sMix, &sInputDb, &sOutputDb })
        s->setup (20.0, fs);
    sAnalog.setup (15.0, fs);
    sBypass.setup (15.0, fs);
    rmsCoef = (float) std::exp (-1.0 / (0.010 * osRate));            // 10 ms RMS window
    meterFallPerSample = (float) (24.0 / fs);                         // 24 dB/s
    coefDirty = true;
    updateCoefficients();
    reset();
}

void Engine::reset()
{
    for (auto& c : ch) c.reset();
    rmsState = 0.f; grFast = grSlow = 0.f; blockMaxGr = 0.f;
    const int ri = (int) params[kRatio];
    sThreshold.reset (params[kThreshold]);
    sSlope.reset (1.f - 1.f / kRatios[ri]);
    sKnee.reset (kneeFor (ri));
    sMakeupDb.reset (params[kMakeup]);
    sMix.reset (params[kMix] * 0.01f);
    sInputDb.reset (params[kInput]);
    sOutputDb.reset (params[kOutput]);
    sAnalog.reset (params[kAnalog]);
    sBypass.reset (params[kBypass]);
    inputGain = dbToGain (params[kInput]); outputGain = dbToGain (params[kOutput]); makeupGain = dbToGain (params[kMakeup]);
    inMeter = outMeter = -120.f;
    inputMeterDb.store (-120.f); outputMeterDb.store (-120.f); gainReductionDb.store (0.f);
}

void Engine::setParameter (int index, float plain)
{
    if (index < 0 || index >= kNumParams) return;
    plain = clampPlain (index, plain);
    if (params[(size_t) index] == plain) return;
    params[(size_t) index] = plain;
    switch (index)
    {
        case kThreshold: sThreshold.target = plain; break;
        case kMakeup:    sMakeupDb.target = plain; break;
        case kMix:       sMix.target = plain * 0.01f; break;
        case kInput:     sInputDb.target = plain; break;
        case kOutput:    sOutputDb.target = plain; break;
        case kAnalog:    sAnalog.target = plain; break;
        case kBypass:    sBypass.target = plain; break;
        case kRatio:     sSlope.target = 1.f - 1.f / kRatios[(int) plain]; sKnee.target = kneeFor ((int) plain); break;
        default:         coefDirty = true; break;               // attack, release, sidechain filter
    }
}

void Engine::updateCoefficients()
{
    if (! coefDirty) return;
    coefDirty = false;
    auto coefFor = [this] (double seconds) { return (float) std::exp (-1.0 / (seconds * osRate)); };
    attackCoef = coefFor (kAttackMs[(int) params[kAttack]] * 0.001);
    const int ri = (int) params[kRelease];
    autoRelease = ri == kReleaseAuto;
    releaseCoef = coefFor (autoRelease ? 0.6 : kReleaseSec[ri]);
    // AUTO: a fast stage (100 ms) recovers from short peaks; a slow stage follows the static curve with a 0.5 s attack,
    // so it only builds up under sustained compression, and releases over 1.2 s. The deeper of the two is used,
    // so the release time follows the programme.
    autoFastRelease = coefFor (0.1);
    autoSlowAttack = coefFor (0.5);
    autoSlowRelease = coefFor (1.2);
    const double hz = kScFilterHz[(int) params[kScFilter]];
    for (auto& c : ch) c.scHpf.setup (hz, osRate);
}

// one oversampled sample pair (l, r hold the input after input gain; they come back processed)
inline float Engine::processOversampled (float& l, float& r, bool stereo, int sub)
{
    (void) sub;
    // ---- sidechain + linked detector
    const float sl = ch[0].scHpf.process (l);
    const float sr = stereo ? ch[1].scHpf.process (r) : sl;
    const float pk = std::max (std::fabs (sl), std::fabs (sr));
    const float ms = stereo ? 0.5f * (sl * sl + sr * sr) : sl * sl;
    rmsState = ms + (rmsState - ms) * rmsCoef;
    float level;
    switch (config.detector)
    {
        case Detector::Peak: level = gainToDb (pk + 1e-9f); break;
        case Detector::Rms:  level = gainToDb (std::sqrt (rmsState) + 1e-9f) + 3.0103f; break;   // +3 dB: a sine reads its peak
        default:             level = 0.5f * (gainToDb (pk + 1e-9f) + gainToDb (std::sqrt (rmsState) + 1e-9f) + 3.0103f); break;
    }

    // ---- soft-knee gain computer (dB, <= 0)
    const float over = level - sThreshold.current, knee = sKnee.current, slope = sSlope.current;
    float target;
    if (2.f * over <= -knee) target = 0.f;
    else if (2.f * over >= knee) target = -slope * over;
    else { const float t = over + 0.5f * knee; target = -slope * t * t / (2.f * knee); }

    // ---- ballistics (decoupled, in dB)
    if (target < grFast) grFast = target + (grFast - target) * attackCoef;
    else grFast = target + (grFast - target) * (autoRelease ? autoFastRelease : releaseCoef);
    float gr = grFast;
    if (autoRelease)
    {
        grSlow = target + (grSlow - target) * (target < grSlow ? autoSlowAttack : autoSlowRelease);
        gr = std::min (grFast, grSlow);
    }
    if (gr < blockMaxGr) blockMaxGr = gr;

    // ---- gain, analog stage, parallel mix, output
    const float g = dbToGain (gr) * makeupGain;
    const float mix = sMix.current, analog = sAnalog.current;
    const float dryL = l, dryR = r;
    float wl = l * g, wr = r * g;
    if (analog > 0.f)
    {
        // asymmetric soft saturation: mostly 2nd harmonic, a little more bias the harder it compresses.
        // Only the added harmonics go through the DC blocker, so the clean path stays untouched.
        const float bias = 0.12f + 0.06f * std::min (1.f, -gr * 0.1f);
        const float tb = softTanh (bias), norm = 1.f / (kAnalogDrive * (1.f - tb * tb));
        const float al = (softTanh (kAnalogDrive * wl + bias) - tb) * norm;
        wl += analog * ch[0].analogDc.process (al - wl);
        if (stereo)
        {
            const float ar = (softTanh (kAnalogDrive * wr + bias) - tb) * norm;
            wr += analog * ch[1].analogDc.process (ar - wr);
        }
    }
    l = protect ((dryL + mix * (wl - dryL)) * outputGain);
    if (stereo) r = protect ((dryR + mix * (wr - dryR)) * outputGain);
    return gr;
}

void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    if (numChannels <= 0 || numSamples <= 0 || channels == nullptr) return;
    ScopedFlushDenormals noDenormals;
    updateCoefficients();
    const bool stereo = numChannels >= 2 && numCh >= 2;
    float* L = channels[0];
    float* R = stereo ? channels[1] : nullptr;
    blockMaxGr = 0.f;
    float inPeak = 0.f, outPeak = 0.f;
    // Bypass keeps the whole chain running (filters and envelopes stay warm, so switching back is seamless) and
    // outputs the untouched input; a 15 ms linear crossfade covers the switch.
    const bool fullyBypassed = sBypass.current == 1.f && sBypass.target == 1.f;

    for (int n = 0; n < numSamples; ++n)
    {
        // per-sample smoothing of every continuous control
        if (sInputDb.smoothing()) inputGain = dbToGain (sInputDb.next());
        if (sOutputDb.smoothing()) outputGain = dbToGain (sOutputDb.next());
        if (sMakeupDb.smoothing()) makeupGain = dbToGain (sMakeupDb.next());
        sThreshold.next(); sSlope.next(); sKnee.next(); sMix.next(); sAnalog.next();
        const float bypass = sBypass.next();

        float rawL = L[n], rawR = stereo ? R[n] : 0.f;
        if (! std::isfinite (rawL)) rawL = 0.f;                 // NaN / inf never reach the filters
        if (! std::isfinite (rawR)) rawR = 0.f;
        const float xl = rawL * inputGain, xr = rawR * inputGain;
        inPeak = std::max (inPeak, std::max (std::fabs (xl), std::fabs (xr)));
        float yl, yr = 0.f;
        if (os == 1)
        {
            yl = xl; yr = xr;
            processOversampled (yl, yr, stereo, 0);
        }
        else if (os == 2)
        {
            float l0, l1, r0 = 0, r1 = 0;
            ch[0].up1.process (xl, l0, l1);
            if (stereo) ch[1].up1.process (xr, r0, r1);
            processOversampled (l0, r0, stereo, 0);
            processOversampled (l1, r1, stereo, 1);
            yl = ch[0].down1.process (l0, l1);
            if (stereo) yr = ch[1].down1.process (r0, r1);
        }
        else
        {
            float a[2], b[2] = { 0, 0 }, l4[4], r4[4] = { 0, 0, 0, 0 };
            ch[0].up1.process (xl, a[0], a[1]);
            if (stereo) ch[1].up1.process (xr, b[0], b[1]);
            for (int k = 0; k < 2; ++k)
            {
                ch[0].up2.process (a[k], l4[2 * k], l4[2 * k + 1]);
                if (stereo) ch[1].up2.process (b[k], r4[2 * k], r4[2 * k + 1]);
            }
            for (int k = 0; k < 4; ++k) processOversampled (l4[k], r4[k], stereo, k);
            for (int k = 0; k < 2; ++k)
            {
                a[k] = ch[0].down2.process (l4[2 * k], l4[2 * k + 1]);
                if (stereo) b[k] = ch[1].down2.process (r4[2 * k], r4[2 * k + 1]);
            }
            yl = ch[0].down1.process (a[0], a[1]);
            if (stereo) yr = ch[1].down1.process (b[0], b[1]);
        }
        // final safety: the down-sampler can ring a hair above the protection ceiling
        yl = std::min (1.f, std::max (-1.f, yl));
        yr = std::min (1.f, std::max (-1.f, yr));
        if (bypass >= 1.f) { yl = rawL; yr = rawR; }                       // exact pass-through
        else if (bypass > 0.f) { yl += bypass * (rawL - yl); yr += bypass * (rawR - yr); }
        L[n] = yl;
        if (stereo) R[n] = yr;
        outPeak = std::max (outPeak, std::max (std::fabs (yl), std::fabs (yr)));
    }

    sanitizeState();

    // meters
    const float fall = meterFallPerSample * (float) numSamples;
    const float inDb = inPeak > 1e-6f ? gainToDb (inPeak) : -120.f, outDb = outPeak > 1e-6f ? gainToDb (outPeak) : -120.f;
    inMeter = std::max (inDb, inMeter - fall); outMeter = std::max (outDb, outMeter - fall);
    inputMeterDb.store (inMeter, std::memory_order_relaxed);
    outputMeterDb.store (outMeter, std::memory_order_relaxed);
    gainReductionDb.store (fullyBypassed ? 0.f : -blockMaxGr, std::memory_order_relaxed);
}

void Engine::sanitizeState()
{
    bool ok = std::isfinite (rmsState) && std::isfinite (grFast) && std::isfinite (grSlow);
    for (auto& c : ch)
    {
        ok = ok && c.up1.ap.finite() && c.up2.ap.finite() && c.down1.ap.finite() && c.down2.ap.finite() && c.scHpf.finite() && c.analogDc.finite();
        c.up1.ap.flushDenormals(); c.up2.ap.flushDenormals(); c.down1.ap.flushDenormals(); c.down2.ap.flushDenormals();
        c.scHpf.flushDenormals(); c.analogDc.flushDenormals();
    }
    if (rmsState < 1e-30f) rmsState = 0.f;
    if (grFast > -1e-9f) grFast = 0.f;
    if (grSlow > -1e-9f) grSlow = 0.f;
    if (! ok)
    {
        for (auto& c : ch) c.reset();
        rmsState = 0.f; grFast = grSlow = 0.f;
    }
}
} // namespace gglue
