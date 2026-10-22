// Sampler core: 16 pads, each with up to four velocity layers, start/end points, one-shot, loop and reverse playback,
// tuning by fractional read, an ADSR envelope, pan, mute and choke groups. Rendering allocates nothing: voices are a
// fixed array and the caller owns the output buffers.
#pragma once
#include <cstdint>
#include <vector>

namespace mc909 {

struct Sample {
    uint16_t channels = 1;
    uint32_t rate = 44100;
    std::vector<float> data;    // interleaved
    uint64_t frames() const { return channels ? data.size() / channels : 0; }
};

struct Layer {
    int sample = -1;            // index into the sampler's sample list
    float velLo = 0.f, velHi = 1.f;   // velocity range this layer answers to (0..1)
    float start = 0.f, end = 1.f;     // playback window, as fractions of the sample
};

struct Pad {
    static constexpr int kLayers = 4;
    Layer layers[kLayers];
    int layerCount = 0;
    float semitones = 0.f;      // tuning
    float volume = 1.f;
    float pan = 0.5f;           // 0 left, 0.5 centre, 1 right (mono samples)
    bool mute = false;
    bool loop = false;
    bool reverse = false;
    int choke = 0;              // 0 = none; pads with the same non-zero group cut each other off
    float attack = 0.001f, decay = 0.f, sustain = 1.f, release = 0.01f;   // seconds, and sustain level 0..1
};

class Sampler {
public:
    static constexpr int kPads = 16, kVoices = 16;

    explicit Sampler(double outRate = 44100.0) : outRate_(outRate) {}

    int addSample(Sample s);    // returns the sample index
    bool addLayer(int padIndex, const Layer& l);   // false when the pad is full
    Pad& pad(int i) { return pads_[i]; }
    const Sample& sample(int i) const { return samples_[i]; }
    int sampleCount() const { return (int)samples_.size(); }

    void noteOn(int padIndex, float velocity);     // velocity 0..1
    void noteOff(int padIndex);                    // releases looping voices; one-shots play to their end
    void render(float* outL, float* outR, int frames);
    int activeVoices() const;

private:
    enum class Stage { Attack, Decay, Sustain, Release };
    struct Voice {
        bool active = false, loop = false, reverse = false;
        int pad = -1, sample = -1, choke = 0;
        double pos = 0, inc = 1, winStart = 0, winEnd = 0;
        float gain = 0, pan = 0.5f, env = 0, attackStep = 1, decayStep = 0, sustain = 1, relMul = 0;
        Stage stage = Stage::Attack;
        uint64_t age = 0;
    };
    void startVoice(Voice& v, int padIndex, const Layer& layer, float velocity);
    void release(Voice& v);
    const Layer* pickLayer(const Pad& p, float velocity) const;

    double outRate_;
    std::vector<Sample> samples_;
    Pad pads_[kPads];
    Voice voices_[kVoices];
    uint64_t clock_ = 0;
};

}  // namespace mc909
