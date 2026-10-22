#include "sampler.h"
#include "mathlite.h"

#include <algorithm>
#include <cmath>

namespace mc909 {

int Sampler::addSample(Sample s)
{
    samples_.push_back(std::move(s));
    return (int)samples_.size() - 1;
}

bool Sampler::addLayer(int padIndex, const Layer& l)
{
    if (padIndex < 0 || padIndex >= kPads) return false;
    Pad& p = pads_[padIndex];
    if (p.layerCount >= Pad::kLayers) return false;
    p.layers[p.layerCount++] = l;
    return true;
}

// The layer whose velocity range contains the velocity; if none does, the nearest one.
const Layer* Sampler::pickLayer(const Pad& p, float velocity) const
{
    const Layer* best = nullptr; float bestDist = 1e9f;
    for (int i = 0; i < p.layerCount; ++i)
    {
        const Layer& l = p.layers[i];
        if (l.sample < 0 || l.sample >= (int)samples_.size()) continue;
        if (velocity >= l.velLo && velocity <= l.velHi) return &l;
        float d = velocity < l.velLo ? l.velLo - velocity : velocity - l.velHi;
        if (d < bestDist) { bestDist = d; best = &l; }
    }
    return best;
}

void Sampler::startVoice(Voice& v, int padIndex, const Layer& layer, float velocity)
{
    const Pad& p = pads_[padIndex];
    const Sample& s = samples_[layer.sample];
    const double len = (double)s.frames();
    v.active = true;
    v.pad = padIndex; v.sample = layer.sample; v.choke = p.choke;
    v.loop = p.loop; v.reverse = p.reverse;
    v.winStart = floord(layer.start * len);
    v.winEnd = std::max(v.winStart + 1.0, floord(layer.end * len));
    v.inc = expd(p.semitones / 12.0 * 0.6931471805599453) * (double)s.rate / outRate_;
    if (v.reverse) { v.pos = v.winEnd - 1.0; v.inc = -v.inc; } else v.pos = v.winStart;
    v.gain = velocity * p.volume;
    if (s.channels == 1)                                       // pan applies to mono samples
    {
        v.pan = p.pan;
    }
    else v.pan = 0.5f;                                         // stereo samples keep their own balance
    // envelope: at least 1 ms attack to avoid clicks; decay and release as set on the pad
    const double sr = outRate_;
    v.attackStep = (float)(1.0 / std::max(0.001, (double)p.attack) / sr);
    v.decayStep = p.decay > 0.f ? (float)((1.0 - p.sustain) / (p.decay * sr)) : 1.f;
    v.sustain = p.sustain;
    v.relMul = (float)(1.0 - expd(-6.9078 / (std::max(0.001, (double)p.release) * sr)));   // release: 60 dB down
    v.env = 0.f; v.stage = Stage::Attack;
    v.age = ++clock_;
}

void Sampler::release(Voice& v)
{
    if (v.stage != Stage::Release) v.stage = Stage::Release;
}

void Sampler::noteOn(int padIndex, float velocity)
{
    if (padIndex < 0 || padIndex >= kPads) return;
    const Pad& p = pads_[padIndex];
    if (p.mute) return;
    const Layer* layer = pickLayer(p, velocity);
    if (!layer) return;
    if (p.choke)
        for (auto& v : voices_) if (v.active && v.choke == p.choke) release(v);
    Voice* slot = nullptr;
    for (auto& v : voices_) if (!v.active) { slot = &v; break; }
    if (!slot)
    {
        slot = &voices_[0];
        for (auto& v : voices_) if (v.age < slot->age) slot = &v;
    }
    startVoice(*slot, padIndex, *layer, velocity);
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
        const uint16_t ch = s.channels;
        const double ws = v.winStart, we = v.winEnd, span = we - ws;
        // equal-gain pan for mono: centre is unity on both sides
        const float gl = ch == 1 ? std::min(1.f, 2.f * (1.f - v.pan)) : 1.f;
        const float gr = ch == 1 ? std::min(1.f, 2.f * v.pan) : 1.f;
        for (int i = 0; i < frames; ++i)
        {
            if (v.reverse ? v.pos < ws : v.pos >= we)
            {
                if (v.loop) v.pos = v.reverse ? v.pos + span : v.pos - span;
                else { v.active = false; break; }
            }
            // linear interpolation between the two neighbouring frames
            const uint64_t i0 = (uint64_t)v.pos;
            const double f = v.pos - (double)i0;
            const uint64_t i1 = std::min<uint64_t>(i0 + 1, s.frames() - 1);
            float l, r;
            if (ch >= 2)
            {
                l = (float)((1 - f) * s.data[i0 * ch] + f * s.data[i1 * ch]);
                r = (float)((1 - f) * s.data[i0 * ch + 1] + f * s.data[i1 * ch + 1]);
            }
            else l = r = (float)((1 - f) * s.data[i0] + f * s.data[i1]);
            // envelope
            switch (v.stage)
            {
            case Stage::Attack:
                v.env += v.attackStep;
                if (v.env >= 1.f) { v.env = 1.f; v.stage = v.decayStep < 1.f && v.sustain < 1.f ? Stage::Decay : Stage::Sustain; }
                break;
            case Stage::Decay:
                v.env -= v.decayStep;
                if (v.env <= v.sustain) { v.env = v.sustain; v.stage = Stage::Sustain; }
                break;
            case Stage::Sustain: v.env = v.sustain; break;
            case Stage::Release:
                v.env *= 1.f - v.relMul;
                if (v.env < 1e-4f) { v.active = false; }
                break;
            }
            if (!v.active) break;
            const float g = v.gain * v.env;
            outL[i] += l * g * gl;
            outR[i] += r * g * gr;
            v.pos += v.inc;
        }
    }
}

}  // namespace mc909
