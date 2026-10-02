#pragma once
// Da Maze Muncher sounds: small synthesised voices, no samples (all original).
//   blip     dots (alternating two pitches)            sweep    power record, bug eaten, death
//   tune     short jingles (start, level clear, extra life, bonus), a square-wave lead
//   drone    a wobbling bass while the bugs are scared
//   beat     a kick / snare / hat loop under the game; its tempo rises with the level (BEAT on/off)
#include <cmath>
#include <cstdint>
#include "Game.h"

namespace mm
{
constexpr double kTau = 6.28318530717958647692;
inline float noteHz (int midi) { return 440.f * std::exp2 ((midi - 69) / 12.f); }

struct Synth
{
    double sr = 44100;
    Rng rng;
    // blip
    double blipPh = 0, blipLeft = 0; float blipHz = 0; bool blipAlt = false;
    // sweep
    double swPh = 0, swT = 0, swDur = 0; float swF0 = 0, swF1 = 0, swGain = 0; int swWave = 0;
    // tune
    const int8_t* tune = nullptr; int tuneLen = 0, tuneIdx = 0; double tuneStep = 0.1, tuneT = 0, tunePh = 0;
    // drone
    double drPh = 0, drLfo = 0; float drAmp = 0;
    // beat
    double beatPh = 0; int beatStep = -1; float kickEnv = 0, kickPh = 0, snEnv = 0, hatEnv = 0, hpZ = 0, lpZ = 0;
    float lowCut = 0;

    void prepare (double rate) { sr = rate; }
    void blip() { blipAlt = ! blipAlt; blipHz = blipAlt ? 784.f : 988.f; blipLeft = 0.045; blipPh = 0; }
    void sweep (float f0, float f1, double dur, int wave, float gain) { swF0 = f0; swF1 = f1; swDur = dur; swT = 0; swPh = 0; swWave = wave; swGain = gain; }
    void play (const int8_t* notes, int n, double step) { tune = notes; tuneLen = n; tuneIdx = 0; tuneStep = step; tuneT = 0; }

    void trigger (uint32_t s)
    {
        static const int8_t kStart[] = { 72, 75, 79, 84, 82, 79, 75, 79, 84, 84 };
        static const int8_t kClear[] = { 72, 76, 79, 84, 88, 91, 96, 96 };
        static const int8_t kExtra[] = { 79, 84, 79, 84, 91 };
        static const int8_t kBonus[] = { 88, 91, 96 };
        if (s & (1u << SFX_DOT)) blip();
        if (s & (1u << SFX_POWER)) sweep (220.f, 880.f, 0.25, 1, 0.22f);
        if (s & (1u << SFX_EAT)) sweep (300.f, 2400.f, 0.35, 0, 0.25f);
        if (s & (1u << SFX_DEATH)) { sweep (880.f, 70.f, 1.5, 1, 0.28f); tune = nullptr; }
        if (s & (1u << SFX_START)) play (kStart, 10, 0.16);
        if (s & (1u << SFX_CLEAR)) play (kClear, 8, 0.11);
        if (s & (1u << SFX_EXTRA)) play (kExtra, 5, 0.09);
        if (s & (1u << SFX_BONUS)) play (kBonus, 3, 0.07);
    }

    // one stereo block; scared: drone on; beatOn / bpm: the loop (only while playing)
    void render (float* L, float* R, int n, bool scared, bool beatOn, double bpm, float vol)
    {
        const double dtS = 1.0 / sr;
        for (int i = 0; i < n; ++i)
        {
            float x = 0.f;
            if (blipLeft > 0)
            {
                blipLeft -= dtS; blipPh += blipHz * dtS; if (blipPh >= 1) blipPh -= 1;
                const float env = (float) (blipLeft / 0.045);
                x += (blipPh < 0.5 ? 0.16f : -0.16f) * env;
            }
            if (swT < swDur)
            {
                const double k = swT / swDur; swT += dtS;
                const double f = swF0 * std::pow ((double) swF1 / swF0, k) * (1.0 + 0.04 * std::sin (kTau * 7.0 * swT));
                swPh += f * dtS; swPh -= std::floor (swPh);
                const float w = swWave == 0 ? (float) (4.0 * std::fabs (swPh - 0.5) - 1.0) : (swPh < 0.5 ? 1.f : -1.f);
                x += w * swGain * (float) (1.0 - k);
            }
            if (tune != nullptr)
            {
                const double idxT = tuneT / tuneStep; const int idx = (int) idxT;
                if (idx >= tuneLen) tune = nullptr;
                else
                {
                    const double f = noteHz (tune[idx]);
                    tunePh += f * dtS; tunePh -= std::floor (tunePh);
                    const float env = (float) (1.0 - (idxT - idx) * 0.7);
                    x += (tunePh < 0.25 ? 0.13f : -0.13f) * env * (idx == tuneLen - 1 ? 1.f : 0.9f);
                }
                tuneT += dtS;
            }
            drAmp += ((scared ? 0.14f : 0.f) - drAmp) * 0.0015f;
            if (drAmp > 1e-4f)
            {
                drLfo += 6.0 * dtS; drLfo -= std::floor (drLfo);
                const double f = 110.0 * (1.0 + 0.06 * std::sin (kTau * drLfo));
                drPh += f * dtS; drPh -= std::floor (drPh);
                x += (float) (4.0 * std::fabs (drPh - 0.5) - 1.0) * drAmp * (float) (0.6 + 0.4 * std::sin (kTau * drLfo));
            }
            if (beatOn)
            {
                beatPh += bpm / 60.0 * 4.0 * dtS;                                // 16th notes
                if (beatPh >= 1.0 || beatStep < 0)
                {
                    beatPh -= std::floor (beatPh); beatStep = (beatStep + 1) & 15;
                    if (beatStep == 0 || beatStep == 8 || beatStep == 10) { kickEnv = 1.f; kickPh = 0; }
                    if (beatStep == 4 || beatStep == 12) snEnv = 1.f;
                    if ((beatStep & 1) == 0) hatEnv = (beatStep & 2) ? 0.6f : 1.f;
                }
                const float kf = 45.f + 110.f * kickEnv * kickEnv;
                kickPh += kf * (float) dtS; kickPh -= std::floor (kickPh);
                x += (float) std::sin (kTau * kickPh) * kickEnv * 0.32f;
                kickEnv *= 0.99955f;
                const float nz = (float) (rng.next() >> 8) * (1.f / 8388608.f) - 1.f;
                hpZ += (nz - hpZ) * 0.35f; const float hp = nz - hpZ;
                lpZ += (nz - lpZ) * 0.25f;
                x += hp * hatEnv * 0.05f + lpZ * snEnv * 0.12f;
                hatEnv *= 0.9985f; snEnv *= 0.9993f;
            }
            else { beatStep = -1; kickEnv = snEnv = hatEnv = 0; }
            // gentle DC block + soft clip
            lowCut += (x - lowCut) * 0.0008f;
            float y = (x - lowCut) * vol * 2.2f;
            y = y / (1.f + std::fabs (y));
            L[i] = y; R[i] = y;
        }
    }
};
} // namespace mm
