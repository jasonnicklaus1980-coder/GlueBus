#pragma once
// G-Glue bus compressor DSP engine. No dependency on any GUI or plugin framework.
//
// Signal flow (per channel, stereo-linked gain):
//   input gain -> [up-sample 1x/2x/4x] -> sidechain HPF -> linked peak/RMS detector -> soft-knee gain computer
//   -> attack / release (or program-dependent AUTO release) -> makeup -> optional analog stage
//   -> parallel mix with the dry signal -> output gain -> output protection -> [down-sample] -> bypass crossfade
//
// Real-time rules: prepare() is the only call that may allocate (it doesn't, currently); process() never allocates,
// never locks and never touches a GUI. Meter values are published through atomics for any thread to read.
#include "../Parameters/GGlueParameters.h"
#include "Filters.h"
#include "Halfband.h"
#include <atomic>

namespace gglue
{
enum class Detector { Peak, Rms, Blend };

struct EngineConfig
{
    int oversampling = 2;                 // 1, 2 or 4
    Detector detector = Detector::Blend;  // Blend = average of the peak and RMS levels (in dB)
};

class Engine
{
public:
    void configure (const EngineConfig& c) { config = c; }
    const EngineConfig& getConfig() const { return config; }

    void prepare (double sampleRate, int numChannels);    // call before processing and on any rate change
    void reset();                                          // clears all state (filters, envelopes, meters)

    void setParameter (int index, float plain);            // plain units, see GGlueParameters.h; audio thread
    void setParameters (const ParamValues& v) { for (int i = 0; i < kNumParams; ++i) setParameter (i, v[(size_t) i]); }
    float getParameter (int index) const { return params[(size_t) index]; }

    // in-place, 1 or 2 channels (extra channels are left untouched)
    void process (float* const* channels, int numChannels, int numSamples);

    int getLatencySamples() const { return 0; }
    double getSampleRate() const { return fs; }

    // meters (any thread): input / output peak in dBFS with a 24 dB/s fall, gain reduction in dB (>= 0)
    std::atomic<float> inputMeterDb { -120.f }, outputMeterDb { -120.f }, gainReductionDb { 0.f };

private:
    struct Channel
    {
        Upsampler2x up1, up2;
        Downsampler2x down1, down2;
        HighPass2 scHpf;
        DcBlocker analogDc;
        void reset() { up1.ap.reset(); up2.ap.reset(); down1.ap.reset(); down2.ap.reset(); scHpf.reset(); analogDc.reset(); }
    };

    void updateCoefficients();
    float processOversampled (float& l, float& r, bool stereo, int sub);
    void sanitizeState();

    EngineConfig config;
    double fs = 44100.0, osRate = 88200.0;
    int os = 2, numCh = 2;
    ParamValues params = defaultValues();
    bool coefDirty = true;

    Channel ch[2];

    // smoothed controls (base rate)
    Smoother sThreshold, sSlope, sKnee, sMakeupDb, sMix, sInputDb, sOutputDb;
    Ramp sAnalog, sBypass;                                // 15 ms linear crossfades
    float inputGain = 1.f, outputGain = 1.f, makeupGain = 1.f;

    // detector / envelope state (oversampled rate)
    float rmsState = 0.f, rmsCoef = 0.f;
    float grFast = 0.f, grSlow = 0.f;                     // dB, <= 0
    float attackCoef = 0.f, releaseCoef = 0.f;
    float autoFastRelease = 0.f, autoSlowAttack = 0.f, autoSlowRelease = 0.f;
    bool autoRelease = true;
    float blockMaxGr = 0.f;

    // meters
    float inMeter = -120.f, outMeter = -120.f, meterFallPerSample = 0.f;
};
} // namespace gglue
