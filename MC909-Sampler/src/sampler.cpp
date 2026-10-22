#include "sampler.h"

#include <cmath>

namespace mc909 {

int Sampler::addSample(Sample s)
{
    samples_.push_back(std::move(s));
    return (int)samples_.size() - 1;
}

void Sampler::startVoice(Voice& v, int padIndex, float velocity)
{
    const Pad& p = pads_[padIndex];
    const Sample& s = samples_[p.sample];
    v.active = true; v.releasing = false;
    v.pad = padIndex; v.sample = p.sample; v.choke = p.choke;
    v.loop = p.loop; v.reverse = p.reverse;
    v.inc = std::pow(2.0, p.semitones / 12.0) * (double)s.rate / outRate_;
    v.pos = p.reverse ? (double)s.frames() - 1.0 : 0.0;
    if (p.reverse) v.inc = -v.inc;
    v.gain = velocity * p.volume;
    v.env = 0.f;
    v.envStep = (float)(1.0 / (0.001 * outRate_));     // 1 ms attack
    v.relStep = (float)(1.0 - std::exp(-6.9078 / (0.010 * outRate_)));   // 10 ms release, 60 dB down (no click)
    v.age = ++clock_;
}

void Sampler::release(Voice& v)
{
    v.releasing = true;
}

void Sampler::noteOn(int padIndex, float velocity)
{
    if (padIndex < 0 || padIndex >= kPads) return;
    const Pad& p = pads_[padIndex];
    if (p.sample < 0 || p.sample >= (int)samples_.size()) return;
    const int choke = p.choke;
    if (choke)
        for (auto& v : voices_) if (v.active && v.choke == choke) release(v);
    // a free voice, else the oldest one
    Voice* slot = nullptr;
    for (auto& v : voices_) if (!v.active) { slot = &v; break; }
    if (!slot)
    {
        slot = &voices_[0];
        for (auto& v : voices_) if (v.age < slot->age) slot = &v;
    }
    startVoice(*slot, padIndex, velocity);
}

void Sampler::noteOff(int padIndex)
{
    for (auto& v : voices_) if (v.active && v.pad == padIndex && v.loop) release(v);
}

int Sampler::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices_) if (v.active) ++n;
    return n;
}

void Sampler::render(float* outL, float* outR, int frames)
{
    for (int i = 0; i < frames; ++i) { outL[i] = 0.f; outR[i] = 0.f; }
    for (auto& v : voices_)
    {
        if (!v.active) continue;
        const Sample& s = samples_[v.sample];
        const double len = (double)s.frames();
        for (int i = 0; i < frames; ++i)
        {
            if (v.reverse ? v.pos < 0.0 : v.pos >= len)
            {
                if (v.loop) v.pos = v.reverse ? v.pos + len : v.pos - len;   // wrap back into the sample
                else { v.active = false; break; }
            }
            // linear interpolation between the two neighbouring frames
            const uint64_t i0 = (uint64_t)v.pos;
            const double f = v.pos - (double)i0;
            const uint64_t i1 = (i0 + 1 < (uint64_t)len) ? i0 + 1 : i0;
            const uint16_t ch = s.channels;
            float l, r;
            if (ch >= 2)
            {
                l = (float)((1 - f) * s.data[i0 * ch] + f * s.data[i1 * ch]);
                r = (float)((1 - f) * s.data[i0 * ch + 1] + f * s.data[i1 * ch + 1]);
            }
            else
            {
                l = r = (float)((1 - f) * s.data[i0] + f * s.data[i1]);
            }
            if (v.env < 1.f) v.env += v.envStep;
            if (v.env > 1.f) v.env = 1.f;
            const float g = v.gain * v.env;
            outL[i] += l * g;
            outR[i] += r * g;
            v.pos += v.inc;
            if (v.releasing)
            {
                v.gain -= v.gain * v.relStep;
                if (v.gain < 1e-5f) { v.active = false; break; }
            }
        }
    }
}

}  // namespace mc909
