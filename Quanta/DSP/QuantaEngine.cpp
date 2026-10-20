#include "QuantaEngine.h"

#include "QuantaMath.h"
#include "QuantaPresets.h"

namespace quanta {

const ParamInfo kParams[P_COUNT] = {
    {"Intensity", 0.30f}, {"Tone", 0.50f}, {"Density", 0.00f}, {"Glue", 0.00f}, {"Width", 0.50f},
    {"Low End", 0.00f},  {"Air", 0.00f},  {"Mix", 1.00f},     {"Output", 0.50f}, {"Mono Bass", 0.00f},
    {"Preset", 0.00f},  {"Mid", 0.50f},   {"Limit", 0.00f},  {"Bands", 0.00f},
    {"Bypass", 0.f},     {"A/B", 0.f},     {"Undo", 0.f},     {"Redo", 0.f},
    {"Slot", 0.f},       {"Save", 0.f},    {"Load", 0.f},
    {"In Level", 0.f},   {"Out Level", 0.f}, {"Glue GR", 0.f}, {"Correlation", 0.5f},
    {"RMS In", 0.f},     {"RMS Out", 0.f},   {"Loudness", 0.f},
    {"GR Low", 0.f},     {"GR Mid", 0.f},    {"GR High", 0.f},  {"Limit GR", 0.f},
};

namespace {
constexpr double kPi = 3.14159265358979323846;
// 10^(dB/40) for the RBJ filters
inline double dbToA(double gainDb) { return qexp2d(gainDb / 40.0 * 3.3219280948873623); }
constexpr float kCeilingKnee = 0.80f;  // peak control starts here
constexpr float kCeiling = 0.97f;      // and never passes this (about -0.3 dBFS, sample peak, not true peak)
constexpr double kBassMonoHz = 120.0;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float flush(float v) { return (v > -1e-20f && v < 1e-20f) ? 0.f : v; }
}  // namespace

// ---- RBJ cookbook biquads (double precision for the coefficients only)

void Engine::setShelfLow(Biquad& b, double f0, double gainDb) const {
    double A = dbToA(gainDb), w0 = 2.0 * kPi * f0 / sr_;
    double cs = qcos(w0), sn = qsin(w0), alpha = sn / 2.0 * 1.41421356237309504880;  // slope S = 1
    double sA = __builtin_sqrt(A);
    double a0 = (A + 1) + (A - 1) * cs + 2 * sA * alpha;
    b.b0 = A * ((A + 1) - (A - 1) * cs + 2 * sA * alpha) / a0;
    b.b1 = 2 * A * ((A - 1) - (A + 1) * cs) / a0;
    b.b2 = A * ((A + 1) - (A - 1) * cs - 2 * sA * alpha) / a0;
    b.a1 = -2 * ((A - 1) + (A + 1) * cs) / a0;
    b.a2 = ((A + 1) + (A - 1) * cs - 2 * sA * alpha) / a0;
}

void Engine::setShelfHigh(Biquad& b, double f0, double gainDb, double slope) const {
    double A = dbToA(gainDb), w0 = 2.0 * kPi * f0 / sr_;
    double cs = qcos(w0), sn = qsin(w0), alpha = sn / 2.0 * __builtin_sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    double sA = __builtin_sqrt(A);
    double a0 = (A + 1) - (A - 1) * cs + 2 * sA * alpha;
    b.b0 = A * ((A + 1) + (A - 1) * cs + 2 * sA * alpha) / a0;
    b.b1 = -2 * A * ((A - 1) + (A + 1) * cs) / a0;
    b.b2 = A * ((A + 1) + (A - 1) * cs - 2 * sA * alpha) / a0;
    b.a1 = 2 * ((A - 1) - (A + 1) * cs) / a0;
    b.a2 = ((A + 1) - (A - 1) * cs - 2 * sA * alpha) / a0;
}

void Engine::setHighPass(Biquad& b, double f0, double q) const {
    double w0 = 2.0 * kPi * f0 / sr_;
    double cs = qcos(w0), alpha = qsin(w0) / (2.0 * q);
    double a0 = 1 + alpha;
    b.b0 = (1 + cs) / 2.0 / a0;
    b.b1 = -(1 + cs) / a0;
    b.b2 = (1 + cs) / 2.0 / a0;
    b.a1 = -2 * cs / a0;
    b.a2 = (1 - alpha) / a0;
}

void Engine::setPeak(Biquad& b, double f0, double gainDb, double q) const {
    double A = dbToA(gainDb), w0 = 2.0 * kPi * f0 / sr_;
    double cs = qcos(w0), alpha = qsin(w0) / (2.0 * q);
    double a0 = 1 + alpha / A;
    b.b0 = (1 + alpha * A) / a0;
    b.b1 = -2 * cs / a0;
    b.b2 = (1 - alpha * A) / a0;
    b.a1 = -2 * cs / a0;
    b.a2 = (1 - alpha / A) / a0;
}

// ---- parameters and setup

void Engine::prepare(double sampleRate) {
    sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    smoothCoef_ = (float)(1.0 - qexp2d(-1.0 / (0.010 * sr_) * 1.44269504088896340736));       // 10 ms
    limAtt_ = (float)(1.0 - qexp2d(-1.0 / (0.001 * sr_) * 1.44269504088896340736));           // limiter: 1 ms attack
    limRel_ = (float)(1.0 - qexp2d(-1.0 / (0.100 * sr_) * 1.44269504088896340736));           // and 100 ms release
    rmsCoef_ = (float)(1.0 - qexp2d(-1.0 / (0.300 * sr_) * 1.44269504088896340736));          // RMS: 300 ms
    lufsCoef_ = (float)(1.0 - qexp2d(-1.0 / (0.400 * sr_) * 1.44269504088896340736));         // momentary loudness: 400 ms
    for (auto& c : ch_) {                                                                    // K-weighting (approximate)
        setShelfHigh(c.kShelf, 1681.97, 3.99984);
        setHighPass(c.kHigh, 38.14, 0.5003);
        for (auto& b : c.xoLow) setLowPass(b, 200.0, 0.70710678);      // crossovers: two stages each
        for (auto& b : c.xoHigh) setHighPass(b, 3000.0, 0.70710678);
    }
    attack_ = (float)(1.0 - qexp2d(-1.0 / (0.020 * sr_) * 1.44269504088896340736));            // 20 ms: keeps most of the transient
    release_ = (float)(1.0 - qexp2d(-1.0 / (0.250 * sr_) * 1.44269504088896340736));           // 250 ms: no audible breathing at moderate settings
    sideLpCoef_ = (float)(1.0 - qexp2d(-2.0 * kPi * kBassMonoHz / sr_ * 1.44269504088896340736));
    dcCoef_ = (float)qexp2d(-2.0 * kPi * 3.0 / sr_ * 1.44269504088896340736);                 // 3 Hz: removes offsets, leaves audible bass alone
    for (int i = 0; i < P_COUNT; ++i) p_[i] = kParams[i].def;
    mixS_ = p_[P_MIX];
    outS_ = 1.f;
    widthS_ = p_[P_WIDTH] * 2.f;
    dirty_ = true;
    recomputeFilters();
    reset();
}

void Engine::reset() {
    for (auto& c : ch_) {
        for (auto& b : c.tilt) b.reset();
        c.lowShelf.reset(); c.lowMid.reset(); c.air.reset();
        c.kShelf.reset(); c.kHigh.reset();
        c.mid.reset();
        for (auto& b : c.xoLow) b.reset();
        for (auto& b : c.xoHigh) b.reset();
        c.dcX1 = c.dcY1 = 0.f;
        c.lowpassState = 0.f;
    }
    env_ = 0.f;
    grDb_ = 0.f;
    msIn_ = msOut_ = 0.f;
    kMs_[0] = kMs_[1] = 0.f;
    for (int b = 0; b < 3; ++b) { bandEnv_[b] = 0.f; bandGrMin_[b] = 0.f; }
    limGain_ = 1.f; limGrDb_ = 0.f;
    inPeak_ = outPeak_ = 0.f;
    corr_ = 0.f;
}

void Engine::setParam(int index, float value) {
    if (index < 0 || index >= P_COUNT) return;
    p_[index] = clampf(value, 0.f, 1.f);
    dirty_ = true;
}

void Engine::loadPreset(int index) {
    float values[kPresetValues];
    if (!presetValues(index, values)) return;
    for (int k = 0; k < kPresetValues; ++k) if (k != P_PRESET) setParam(k, values[k]);
    setParam(P_PRESET, (float)index / (float)(kPresetCount - 1));
}

float Engine::getParam(int index) const {
    return (index < 0 || index >= P_COUNT) ? 0.f : p_[index];
}

// Derives every filter and stage setting from the parameters. Runs once per block when something changed.
void Engine::recomputeFilters() {
    const float particle = p_[P_PARTICLE];
    // Particle adds a capped share to each stage; the other controls set the base
    const float density = clampf(p_[P_DENSITY] + kParticleStep * particle, 0.f, 1.f);
    const float glue = clampf(p_[P_GLUE] + kParticleStep * particle, 0.f, 1.f);
    const float lowEnd = clampf(p_[P_LOWEND] + kParticleStep * particle, 0.f, 1.f);
    const float air = clampf(p_[P_AIR] + kParticleStep * particle, 0.f, 1.f);
    const float tone = (p_[P_TONE] - 0.5f) * 2.f;  // -1..1

    for (auto& c : ch_) {
        setShelfLow(c.tilt[0], 200.0, -3.0 * tone);             // darker or brighter at the bottom...
        setShelfHigh(c.tilt[1], 5000.0, 3.0 * tone);            // ...and at the top
        setShelfLow(c.lowShelf, 90.0, 4.0 * lowEnd);           // bass weight
        setPeak(c.lowMid, 300.0, -2.0 * lowEnd, 1.0);           // low-mid buildup
        setShelfHigh(c.air, 10000.0, 4.0 * air);                // openness
        setPeak(c.mid, 1000.0, 6.0 * (p_[P_MID] - 0.5), 0.8);   // mid-range: -3 .. +3 dB at 1 kHz
    }
    bandsOn_ = p_[P_BANDS] > 0.f;
    bandRatio_ = 1.f + 2.f * p_[P_BANDS];                        // 1:1 .. 3:1 on each band
    limOn_ = p_[P_LIMIT] > 0.f;
    limDrive_ = qdb2lin(12.f * p_[P_LIMIT]);                     // up to +12 dB into the ceiling

    // Density: saturation drive from 1 to 4 (about 0 to 12 dB), blended in at up to 60%
    satDrive_ = 1.f + 3.f * density;
    satAmount_ = 0.6f * density;
    satBias_ = 0.03f * density;  // a little asymmetry gives even harmonics

    // Glue: threshold from -10 to -20 dB, ratio from 1:1 to 2.5:1, makeup up to 2 dB
    glueThresh_ = -10.f - 10.f * glue;
    glueRatio_ = 1.f + 1.5f * glue;
    makeupDb_ = 2.f * glue;

    // Particle trims the output by up to 1 dB, so it adds density and balance rather than loudness
    particleTrimDb_ = -1.0f * particle;
    particleGain_ = qdb2lin(particleTrimDb_);
    dirty_ = false;
}

// ---- processing

void Engine::process(float* left, float* right, int n) {
    if (dirty_) recomputeFilters();

    const float mixTarget = p_[P_MIX];
    const float widthTarget = p_[P_WIDTH] * 2.f;               // 0..2, 1 is unchanged
    const float outTargetDb = (p_[P_OUTPUT] - 0.5f) * 24.f;  // -12..+12 dB
    const float outTarget = qdb2lin(outTargetDb);
    const bool monoBass = p_[P_MONOBASS] >= 0.5f;

    float inPk = 0.f, outPk = 0.f, grMinBlock = 0.f, limMinBlock = 1.f;
    double sLL = 0.0, sRR = 0.0, sLR = 0.0;

    for (int i = 0; i < n; ++i) {
        const float inL = left[i], inR = right[i];
        const float aL = qabs(inL), aR = qabs(inR);
        if (aL > inPk) inPk = aL;
        if (aR > inPk) inPk = aR;

        // smoothed controls
        mixS_ += (mixTarget - mixS_) * smoothCoef_;
        widthS_ += (widthTarget - widthS_) * smoothCoef_;
        outS_ += (outTarget - outS_) * smoothCoef_;

        // 1. tonal shaping, per channel
        float x[2] = {inL, inR};
        // (Particle's compensation is applied to the processed path below, so Mix 0 is exactly the input)
        for (int c = 0; c < 2; ++c) {
            Channel& ch = ch_[c];
            float y = ch.tilt[0].run(x[c]);
            y = ch.tilt[1].run(y);
            y = ch.lowShelf.run(y);
            y = ch.lowMid.run(y);
            y = ch.mid.run(y);
            y = ch.air.run(y);
            x[c] = y;
        }

        // 2. density: saturation, normalised so small signals keep their level
        if (satAmount_ > 0.f) {
            for (int c = 0; c < 2; ++c) {
                float s = satDrive_ * (x[c] + satBias_);
                float sat = (qtanh(s) - qtanh(satDrive_ * satBias_)) / satDrive_;
                x[c] = x[c] * (1.f - satAmount_) + sat * satAmount_;
            }
        }

        // 3. glue: linked feed-forward compressor with a soft knee
        float peak = qmax(qabs(x[0]), qabs(x[1]));
        float levelDb = 20.f * qlog10(peak + 1e-9f);
        env_ += (levelDb - env_) * (levelDb > env_ ? attack_ : release_);
        float over = env_ - glueThresh_;
        const float knee = 6.f;
        float gr = 0.f;  // dB, zero or negative
        if (glueRatio_ > 1.f) {
            float slope = 1.f / glueRatio_ - 1.f;
            if (2.f * over <= -knee) gr = 0.f;
            else if (2.f * over >= knee) gr = slope * over;
            else { float t = over + knee * 0.5f; gr = slope * t * t / (2.f * knee); }
        }
        grDb_ = gr;
        if (gr < grMinBlock) grMinBlock = gr;
        float gain = qdb2lin(gr + makeupDb_);
        for (int c = 0; c < 2; ++c) x[c] *= gain;

        // 3b. multiband dynamics: split into three bands (exactly summing back to the input), compress each, sum
        if (bandsOn_) {
            float lo[2], mi[2], hi[2];
            for (int c = 0; c < 2; ++c) {
                Channel& ch = ch_[c];
                lo[c] = ch.xoLow[1].run(ch.xoLow[0].run(x[c]));
                float rest = x[c] - lo[c];
                hi[c] = ch.xoHigh[1].run(ch.xoHigh[0].run(rest));
                mi[c] = rest - hi[c];
            }
            for (int b = 0; b < 3; ++b) {
                const float* v = b == 0 ? lo : b == 1 ? mi : hi;
                float lev = qmax(qabs(v[0]), qabs(v[1]));
                float levelDb = 20.f * qlog10(lev + 1e-9f);
                bandEnv_[b] += (levelDb - bandEnv_[b]) * (levelDb > bandEnv_[b] ? attack_ : release_);
                float grb = bandGainDb(bandEnv_[b], -18.f, bandRatio_);
                if (grb < bandGrMin_[b]) bandGrMin_[b] = grb;
                float g = qdb2lin(grb);
                for (int c = 0; c < 2; ++c) {
                    float bv = v[c] * g;
                    if (b == 0) x[c] = bv; else x[c] += bv;
                }
            }
        }

        // 4. stereo width (mid/side), with optional mono bass
        float m = 0.5f * (x[0] + x[1]);
        float s = 0.5f * (x[0] - x[1]);
        if (monoBass) {
            ch_[0].lowpassState += (s - ch_[0].lowpassState) * sideLpCoef_;
            float sLow = ch_[0].lowpassState;
            s = (s - sLow) * widthS_ + 0.f * sLow;
        } else {
            s *= widthS_;
        }
        x[0] = m + s;
        x[1] = m - s;

        // 5. DC protection on the processed path, then Particle's compensation
        for (int c = 0; c < 2; ++c) {
            Channel& ch = ch_[c];
            float y = x[c] - ch.dcX1 + dcCoef_ * ch.dcY1;
            ch.dcX1 = x[c];
            ch.dcY1 = flush(y);
            x[c] = ch.dcY1 * particleGain_;
        }

        // 6. output: dry/wet blend, trim, then the peak control
        float outL = (inL * (1.f - mixS_) + x[0] * mixS_) * outS_;
        float outR = (inR * (1.f - mixS_) + x[1] * mixS_) * outS_;
        // 7. limiter (optional): drive into a ceiling with a 1 ms attack; overshoot is caught by the peak control below
        if (limOn_) {
            outL *= limDrive_;
            outR *= limDrive_;
            float pk = qmax(qabs(outL), qabs(outR));
            float target = pk > limCeil_ ? limCeil_ / pk : 1.f;
            limGain_ += (target - limGain_) * (target < limGain_ ? limAtt_ : limRel_);
            if (limGain_ < limMinBlock) limMinBlock = limGain_;
            outL *= limGain_;
            outR *= limGain_;
        }
        outL = softCeiling(outL);
        outR = softCeiling(outR);
        left[i] = outL;
        right[i] = outR;

        float ao = qmax(qabs(outL), qabs(outR));
        if (ao > outPk) outPk = ao;
        msIn_ = flush(msIn_ + ((inL * inL + inR * inR) * 0.5f - msIn_) * rmsCoef_);
        msOut_ = flush(msOut_ + ((outL * outL + outR * outR) * 0.5f - msOut_) * rmsCoef_);
        for (int c = 0; c < 2; ++c) {   // loudness: K-weighting, then mean square per channel
            Channel& ch = ch_[c];
            float k = ch.kHigh.run(ch.kShelf.run(c == 0 ? outL : outR));
            kMs_[c] = flush(kMs_[c] + (k * k - kMs_[c]) * lufsCoef_);
        }
        sLL += (double)outL * outL;
        sRR += (double)outR * outR;
        sLR += (double)outL * outR;
    }

    inPeak_ = inPk;
    outPeak_ = outPk;
    grMin_ = grMinBlock;
    limGrDb_ = limMinBlock < 1.f ? 20.f * qlog10(limMinBlock) : 0.f;
    double den = __builtin_sqrt(sLL * sRR);
    corr_ = den > 1e-12 ? (float)(sLR / den) : 0.f;
}

float Engine::bandGainDb(float env, float thresh, float ratio) {
    float over = env - thresh;
    const float knee = 6.f;
    if (ratio <= 1.f) return 0.f;
    float slope = 1.f / ratio - 1.f;
    if (2.f * over <= -knee) return 0.f;
    if (2.f * over >= knee) return slope * over;
    float t = over + knee * 0.5f;
    return slope * t * t / (2.f * knee);
}

void Engine::setLowPass(Biquad& b, double f0, double q) const {
    double w0 = 2.0 * kPi * f0 / sr_;
    double cs = qcos(w0), alpha = qsin(w0) / (2.0 * q);
    double a0 = 1 + alpha;
    b.b0 = (1 - cs) / 2.0 / a0;
    b.b1 = (1 - cs) / a0;
    b.b2 = (1 - cs) / 2.0 / a0;
    b.a1 = -2 * cs / a0;
    b.a2 = (1 - alpha) / a0;
}

float Engine::inputRmsDb() const { return 10.f * qlog10(msIn_ + 1e-12f); }
float Engine::outputRmsDb() const { return 10.f * qlog10(msOut_ + 1e-12f); }
float Engine::outputLufs() const { return -0.691f + 10.f * qlog10(kMs_[0] + kMs_[1] + 1e-12f); }

float Engine::softCeiling(float v) {
    float a = qabs(v);
    if (a <= kCeilingKnee) return v;
    float range = kCeiling - kCeilingKnee;
    float y = kCeilingKnee + range * qtanh((a - kCeilingKnee) / range);
    return v < 0.f ? -y : y;
}

}  // namespace quanta
