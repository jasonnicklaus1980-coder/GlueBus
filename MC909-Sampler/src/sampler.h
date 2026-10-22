// Sampler core: 16 pads, one-shot and loop playback, pitch by fractional read, choke groups, short envelopes.
// Rendering allocates nothing: voices are a fixed array and the caller owns the output buffers.
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

struct Pad {
    int sample = -1;            // index into the sampler's sample list, -1 = empty
    float semitones = 0.f;      // tuning
    float volume = 1.f;
    bool loop = false;
    bool reverse = false;
    int choke = 0;              // 0 = none; pads with the same non-zero group cut each other off
};

class Sampler {
public:
    static constexpr int kPads = 16, kVoices = 16;

    explicit Sampler(double outRate = 44100.0) : outRate_(outRate) {}
    void setRate(double r) { outRate_ = r; }

    int addSample(Sample s);    // returns the sample index
    Pad& pad(int i) { return pads_[i]; }
    const Sample& sample(int i) const { return samples_[i]; }
    int sampleCount() const { return (int)samples_.size(); }

    void noteOn(int padIndex, float velocity);     // velocity 0..1
    void noteOff(int padIndex);                    // releases a looping voice; one-shots play to their end
    void render(float* outL, float* outR, int frames);
    int activeVoices() const;

private:
    struct Voice {
        bool active = false, releasing = false, loop = false, reverse = false;
        int pad = -1, sample = -1, choke = 0;
        double pos = 0, inc = 1;
        float gain = 0, env = 0, envStep = 0, relStep = 0;
        uint64_t age = 0;
    };
    void startVoice(Voice& v, int padIndex, float velocity);
    void release(Voice& v);

    double outRate_;
    std::vector<Sample> samples_;
    Pad pads_[kPads];
    Voice voices_[kVoices];
    uint64_t clock_ = 0;
};

}  // namespace mc909
