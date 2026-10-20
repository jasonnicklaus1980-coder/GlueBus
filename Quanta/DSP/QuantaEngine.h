// Quanta: original stereo mix-enhancement engine (shared by every build target).
// No framework, no allocation after prepare(), no locks: parameters are plain floats written by the host thread.
#pragma once

namespace quanta {

// Parameters, in the order the host sees them. Values set through setParam() are 0..1.
enum Param {
    P_PARTICLE = 0,  // overall intensity: adds a little to Density, Glue, Low End and Air, capped
    P_TONE,          // tilt: 0 = darker, 0.5 = neutral, 1 = brighter (up to 3 dB each way)
    P_DENSITY,       // harmonic density: saturation amount with a touch of even harmonics
    P_GLUE,          // cohesion: linked compression with makeup gain
    P_WIDTH,         // stereo width: 0 mono, 0.5 unchanged, 1 wider
    P_LOWEND,        // bass weight (low shelf) and low-mid buildup cut
    P_AIR,           // high-frequency openness (up to 4 dB at 10 kHz)
    P_MIX,           // parallel blend: 0 dry, 1 processed
    P_OUTPUT,        // output trim: -12 .. +12 dB, 0.5 = 0 dB
    P_MONOBASS,      // 1: content below ~120 Hz is made mono
    P_PRESET,        // preset shown on the screen: 0 Custom, 1.. the factory presets (see QuantaPresets.h). Setting it loads the preset
    // read-only meters: the host can show them, they are not saved and cannot be automated
    P_METER_IN,      // input level, -60 .. 0 dB as 0..1
    P_METER_OUT,     // output level, -60 .. 0 dB as 0..1
    P_METER_GR,      // glue reduction, 0 .. -20 dB as 0..1
    P_METER_CORR,    // stereo correlation, -1 .. 1 as 0..1
    P_COUNT
};

// The controls (the first ones above): the only parameters saved with a project or automated.
constexpr int kControlCount = P_METER_IN;

struct ParamInfo {
    const char* name;
    float def;       // default, 0..1
};

extern const ParamInfo kParams[P_COUNT];

class Engine {
public:
    void prepare(double sampleRate);      // call before processing, and whenever the rate changes
    void reset();                         // clears filter, compressor and DC state
    void setParam(int index, float value);  // value 0..1
    void loadPreset(int index);             // sets the sound controls of a factory preset and the Preset parameter
    float getParam(int index) const;

    // Processes n stereo samples in place. Never allocates, never blocks.
    void process(float* left, float* right, int n);

    // Metering, updated by process(): peak absolute values of the last block, and the stereo correlation (-1..1).
    float inputPeak() const { return inPeak_; }
    float outputPeak() const { return outPeak_; }
    float correlation() const { return corr_; }
    float gainReductionDb() const { return grDb_; }  // current glue reduction, 0 or negative
    float gainReductionDbMax() const { return grMin_; }  // deepest reduction in the last block (negative)

    // Particle is calibrated so that it adds at most these amounts on top of the other controls.
    static constexpr float kParticleStep = 0.25f;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float run(float x) {
            float y = (float)(b0 * x) + z1;
            z1 = (float)(b1 * x - a1 * y) + z2;
            z2 = (float)(b2 * x - a2 * y);
            if (z1 > -1e-20f && z1 < 1e-20f) z1 = 0.f;  // denormal guard
            if (z2 > -1e-20f && z2 < 1e-20f) z2 = 0.f;
            return y;
        }
        void reset() { z1 = z2 = 0.f; }
    };

    struct Channel {
        Biquad tilt[2];      // low shelf, high shelf (Tone)
        Biquad lowShelf, lowMid, air;
        float dcX1 = 0.f, dcY1 = 0.f;
        float lowpassState = 0.f;  // for the bass-mono split of the side channel (mid/side path only)
    };

    void recomputeFilters();
    static float softCeiling(float v);  // peak control: linear below 0.8, smooth limit towards 0.97
    void setShelfLow(Biquad& b, double f0, double gainDb) const;
    void setShelfHigh(Biquad& b, double f0, double gainDb) const;
    void setPeak(Biquad& b, double f0, double gainDb, double q) const;

    double sr_ = 48000.0;
    float p_[P_COUNT];
    bool dirty_ = true;

    Channel ch_[2];
    float env_ = 0.f;        // glue detector, dB
    float grDb_ = 0.f;       // glue gain reduction, dB (negative)
    float attack_ = 0.f, release_ = 0.f;
    float sideLpCoef_ = 0.f;
    float dcCoef_ = 0.f;     // DC blocker pole, set for a 3 Hz cutoff at the current rate

    // smoothed controls (one-pole, about 10 ms)
    float mixS_ = 0.f, outS_ = 1.f, widthS_ = 1.f;
    float smoothCoef_ = 0.f;

    // per-block tuning derived from the parameters
    float satDrive_ = 1.f, satAmount_ = 0.f, satBias_ = 0.f;
    float glueThresh_ = -6.f, glueRatio_ = 1.f, makeupDb_ = 0.f;
    float particleTrimDb_ = 0.f;
    float particleGain_ = 1.f;   // the same compensation as a linear gain, applied to the processed signal only

    float inPeak_ = 0.f, outPeak_ = 0.f, corr_ = 0.f, grMin_ = 0.f;
};

}  // namespace quanta
