#pragma once
// Model: the SP-1200 signal path, in hardware order, for a stereo pair of voices.
//
//   input -> AnalogInput (coupling, op-amp gain stage + rails, input noise, anti-alias filter)
//         -> ADC (track-and-hold at the sampling clock, converter noise, SAR search on the 12-bit ladder)
//         -> SamplerMemory (12-bit words)
//         -> PlaybackEngine at the 26.04 kHz DAC clock (GRIT / PITCH / REPLAY, drop-sample reads)
//         -> DAC (12-bit ladder x 8-bit level DAC with decay, S/H settling + pedestal, S/H noise, digital noise)
//         -> ReconstructionFilter (zero-order-hold staircase rendered band-limited; output channel filter)
//            Out 1-2: SSMModel (SSM2044, Z80 AR envelope)  Out 3-4 / 5-6: fixed op-amp low-pass  Out 7-8: none
//         -> AnalogOutput (drive, op-amp rails, coupling) + hiss / hum / ground -> output level -> mix
//
// Fixed latency: 16 samples for the converter's band-limited interpolation + 24 for the band-limited steps = 40 host
// samples, 0.9 ms at 44.1 kHz (reported to the host; the dry path is delayed to match, so MIX never comb-filters).
#include "ADC.h"
#include "AliasingEngine.h"
#include "AnalogInput.h"
#include "AnalogOutput.h"
#include "Analyzer.h"
#include "DAC.h"
#include "HardwareVariation.h"
#include "NoiseModel.h"
#include "PitchEngine.h"
#include "PlaybackEngine.h"
#include "ReconstructionFilter.h"
#include "SSMModel.h"

namespace sp
{
constexpr int kAdcDelay = 16, kLatency = kAdcDelay + BlepTable::W;
constexpr double kSpRate = 26040.0, kSp12Rate = 27500.0;
constexpr float kDecayOff = 4.f;

struct Settings
{
    float inputDb = 0, driveDb = 0, pitch = 0, srate = 26040, bits = 12, ssmDb = 0, analog = 1, hissDb = 0, outDb = 0, mix = 1;
    int channel = 2, mode = PM_GRIT; float decay = kDecayOff, sweep = 0.012f, floorHz = 250.f; bool bypass = false;
    int machine = MA_SP1200, quality = Q_ACCURATE; bool hwRange = true;
    float headroomDb = 8, knee = 0.3f, aaHz = 15000; int qmode = 0; float convDb = 0; int alias = AL_AUTHENTIC, recon = RC_ANALOG;
    int level8 = 255; float ssmRes = 0; bool ssmAll = false, analogOn = true, digitalOn = true, ssmOn = true, ampsOn = true, filtersOn = true;
    float outHeadDb = 12, settle = 1.f;
    float humDb = -60, groundDb = -60, digDb = -60, anaDb = 0, color = 0, noiseDb = 0; bool hum50 = false;
    float variation = 0; int unit = 1;
    bool analyzer = false; int tap = 2; float scopeMs = 2; bool freeze = false;
};

struct Model
{
    double fs = 44100.0;
    SincTable sinc16, sinc32;
    BlepTable blep;
    AnalogInput ain[2]; ADC adc[2]; SamplerMemory mem[2]; DAC dac[2]; ZohRenderer zoh[2];
    LowPass4 ideal[2]; OutputFilter ofilt[2]; SSMModel ssm[2]; AnalogOutput aout[2]; NoiseModel noise[2];
    PlaybackEngine play; HardwareVariation var; Analyzer an;
    Rng rng;

    // clocks (in the converter's delayed time domain)
    double adcPhase = 0.0, dacPhase = 0.0;
    long n = 0;
    // hit detector / envelopes
    float envFast = 0, envSlow = 0, decayGain = 1, dynEnv = 0; int holdoff = 0;
    int trigRing[64] {};                                  // hit flags, to delay the SSM envelope by the step latency
    // dry delay
    float dry[2][64] {};
    // per-block derived values
    AliasPlan plan; int prepared = -1; float lastAa = -1, lastOut = -1; double lastF = -1;
    float lastAdcValue[2] {};
    bool anaReady = false;

    void prepare (double rate)
    {
        fs = rate;
        sinc16.build (16, 6.0); sinc32.build (32, 8.0); blep.build();
        for (int c = 0; c < 2; ++c)
        {
            adc[c].sinc16 = &sinc16; adc[c].sinc32 = &sinc32; adc[c].reset(); mem[c].reset(); dac[c].reset();
            zoh[c].blep = &blep; zoh[c].reset(); ideal[c].set (0.45 * kSpRate, fs); ideal[c].reset();
            ofilt[c].hz = -1.f; ofilt[c].reset();
            noise[c].prepare (fs, 0x1234567u + 977u * c);
        }
        an.prepare (fs);
        play.reset();
        adcPhase = dacPhase = 0.0; n = 0;
        envFast = envSlow = 0; decayGain = 1; dynEnv = 0; holdoff = 0;
        std::memset (trigRing, 0, sizeof trigRing); std::memset (dry, 0, sizeof dry);
        prepared = -1; lastAa = lastOut = -1; lastF = -1; var.amount = -1.f;
        rng.seed (0xC0FFEEu);
    }

    // per-block setup from the settings
    void configure (const Settings& s)
    {
        plan = planFor (s.alias, s.recon, s.quality, s.machine);
        if (plan.oversample != prepared)
        {
            for (int c = 0; c < 2; ++c) { ain[c].prepare (fs, plan.oversample); ssm[c].prepare (fs, plan.oversample); aout[c].prepare (fs, plan.oversample); }
            prepared = plan.oversample; lastAa = -1;
        }
        const bool varChanged = var.update (s.variation, s.unit);
        if (varChanged) { lastAa = -1.f; lastOut = -1.f; }
        const int bits = clampi ((int) std::lround (s.bits), 4, 16);
        for (int c = 0; c < 2; ++c)
        {
            const VoiceTolerances& t = var.v[c];
            adc[c].quality = plan.adcQuality;
            adc[c].q.offset = s.qmode == 0 ? 0.5f : 0.f;
            adc[c].noiseLsb = 0.1f * dbToGain (s.convDb) * (s.convDb <= -59.9f ? 0.f : 1.f) * dbToGain (s.noiseDb);
            if (varChanged || adc[c].q.lad.bits != bits)
            {
                adc[c].q.lad.build (bits, bits == 12 ? t.dacW : nullptr);
                dac[c].lad = adc[c].q.lad;                                         // one ladder for record and play
                for (int b = 0; b < 8; ++b) dac[c].lvlW[b] = t.lvlW[b];
            }
            dac[c].settle = clampf (s.settle, 0.05f, 1.f);
            dac[c].pedestal = t.pedestal;
            dac[c].digitalNoise = s.digDb <= -59.9f ? 0.f : dbToGain (s.digDb - 84.f) * dbToGain (s.noiseDb);
            zoh[c].bandlimited = plan.blep;
            AnalogInput& ai = ain[c];
            ai.headroom = dbToGain (s.headroomDb); ai.knee = s.knee; ai.amount = s.analog; ai.asym = t.asym;
            ai.ampOn = s.analogOn && s.ampsOn; ai.filterOn = s.analogOn && s.filtersOn && ! plan.aaOff && s.aaHz < 21900.f;
            AnalogOutput& ao = aout[c];
            ao.headroom = dbToGain (s.outHeadDb); ao.amount = s.analog; ao.asym = t.asym * 0.7f; ao.ampOn = s.analogOn && s.ampsOn;
            SSMModel& sm = ssm[c];
            sm.level = 0.193f * dbToGain (s.ssmDb);                              // +-10 mV at full scale, see SSMModel.h
            sm.offset = t.ssmOffset * 0.193f;
            sm.res = 3.6f * clampf (s.ssmRes, 0.f, 1.f);
            sm.setAmount (s.analog);
            sm.feed = 0.0056f * t.ssmFeed;                                       // -45 dBFS per octave of CV movement
        }
        const float aa = s.aaHz;
        if (aa != lastAa) { for (int c = 0; c < 2; ++c) ain[c].setFilter (aa * var.v[c].aaCut); lastAa = aa; }
        const float of = s.channel == 1 ? 7500.f : (s.channel == 2 ? 10000.f : 0.f);
        if (of != lastOut && of > 0.f) { for (int c = 0; c < 2; ++c) ofilt[c].set (of * var.v[c].outCut, fs); }
        lastOut = of;
    }

    // dry path delayed by the latency
    float dryAt (int c, long k) const { return dry[c][k & 63]; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int nframes, const Settings& s)
    {
        configure (s);
        const double F = s.machine == MA_SP12 ? kSp12Rate : clampd (s.srate, 4000.0, 48000.0);
        if (F != lastF) { for (int c = 0; c < 2; ++c) ideal[c].set (std::fmin (0.45 * F, 0.45 * fs), fs); lastF = F; }
        const int semis = PitchEngine::clampSemis (s.pitch, s.hwRange);
        const bool retune = semis != 0;
        const double ratio = PitchEngine::ratio (semis);
        const double adcRate = (s.mode == PM_GRIT ? ratio : 1.0) * F;
        const double adcInc = adcRate / fs, dacInc = F / fs;
        const float inG = dbToGain (s.inputDb), outG = dbToGain (s.outDb);
        const float drive = dbToGain (s.driveDb + plan.driveBoostDb);
        const bool decayOn = s.decay < kDecayOff * 0.98f;
        const float decayK = decayOn ? (float) std::exp (-6.9078 / (s.decay * F)) : 1.f;     // per DAC clock
        const float sweepK = (float) std::exp (-1.0 / (s.sweep * fs));
        const float fastK = (float) std::exp (-1.0 / (0.004 * fs)), slowK = (float) std::exp (-1.0 / (0.08 * fs));
        const int holdN = (int) (0.04 * fs);
        const float dynTop = 12000.f, dynSpan = std::log2 (dynTop / s.floorHz);
        float ssmFloorOct[2], ssmAllOct[2];                                      // SSM cutoffs in octaves (log2 Hz)
        for (int c = 0; c < 2; ++c) { ssmFloorOct[c] = std::log2 (s.floorHz * var.v[c].ssmCut); ssmAllOct[c] = std::log2 (16000.f * var.v[c].ssmCut); }
        const float nl = dbToGain (s.noiseDb) * (s.noiseDb <= -59.9f ? 0.f : 1.f);
        auto lvl = [&] (float db, float base) { return db <= -59.9f ? 0.f : dbToGain (db + base) * nl; };
        NoiseLevels L;
        L.inputAmp = lvl (s.anaDb, -100.f); L.sh = lvl (s.anaDb, -97.f); L.ssm = lvl (s.anaDb, -92.f); L.filters = lvl (s.anaDb, -98.f);
        L.hiss = lvl (s.hissDb, -91.f); L.hum = lvl (s.humDb, -84.f); L.ground = lvl (s.groundDb, -84.f); L.color = s.color;
        const double humHz = s.hum50 ? 50.0 : 60.0;
        const int preroll = (int) (0.002 * F);

        for (int i = 0; i < nframes; ++i)
        {
            const float xin[2] { inL[i], inR[i] };
            dry[0][n & 63] = xin[0]; dry[1][n & 63] = xin[1];
            float hum[2] { 0.f, 0.f }, gnd[2] { 0.f, 0.f };
            if (L.hum > 0.f || L.ground > 0.f) for (int c = 0; c < 2; ++c) noise[c].mains (humHz, hum[c], gnd[c]);

            // ---------------- analog input -> track-and-hold history
            for (int c = 0; c < 2; ++c)
            {
                const float x = xin[c] * inG;
                float a;
                if (s.analogOn)
                {
                    const float nz = (L.inputAmp > 0.f ? noise[c].white() * L.inputAmp * var.v[c].noise : 0.f) + (hum[c] * L.hum + gnd[c] * L.ground) * 0.25f;
                    a = ain[c].process (x, nz) * var.v[c].gain;
                }
                else a = x;
                adc[c].push (a);
            }
            // hit detector on the analog signal at the converter's (delayed) time
            const long tA = n - kAdcDelay;                                     // converter time for this sample
            {
                const float lev = std::fmax (std::fabs (adc[0].at (tA)), std::fabs (adc[1].at (tA)));
                envFast = flush (lev > envFast ? lev : envFast * fastK);
                envSlow = flush (slowK * envSlow + (1.f - slowK) * lev);
                bool hit = false;
                if (holdoff > 0) --holdoff;
                else if (envFast > 0.01f && envFast > 2.f * envSlow) { hit = true; holdoff = holdN; }
                if (hit) { decayGain = 1.f; if (s.mode == PM_REPLAY) play.trigger (mem[0].w - preroll); }
                trigRing[n & 63] = hit ? 1 : 0;
            }
            // ---------------- converter and DAC clocks, in time order within (tA - 1, tA]
            float postAdc[2] { lastAdcValue[0], lastAdcValue[1] };
            if (s.digitalOn)
            {
                adcPhase += adcInc; dacPhase += dacInc;
                for (int guard = 0; guard < 8; ++guard)
                {
                    const bool aDue = adcPhase >= 1.0, dDue = dacPhase >= 1.0;
                    if (! aDue && ! dDue) break;
                    // time of each pending event: tA - (phase - 1) / inc
                    const double ta = aDue ? (double) tA - (adcPhase - 1.0) / adcInc : 1e30;
                    const double td = dDue ? (double) tA - (dacPhase - 1.0) / dacInc : 1e30;
                    if (ta <= td)
                    {
                        adcPhase -= 1.0;
                        for (int c = 0; c < 2; ++c)
                        {
                            const int code = adc[c].convert (adc[c].hold (ta), rng);
                            mem[c].write (code);
                            lastAdcValue[c] = adc[c].q.lad.value (code);
                        }
                    }
                    else
                    {
                        dacPhase -= 1.0;
                        float codes[2];
                        play.tick (mem, s.mode, ratio, retune, plan.interpReads, codes);
                        decayGain = decayOn ? flush (decayGain * decayK) : 1.f;
                        const int level8 = clampi ((int) std::lround (s.level8 * decayGain), 0, 255);
                        for (int c = 0; c < 2; ++c)
                        {
                            float v = dac[c].clock (codes[c], level8);
                            if (s.analogOn && L.sh > 0.f) v += noise[c].white() * L.sh * var.v[c].noise;
                            zoh[c].step (td + kAdcDelay, v);               // renderer time = converter time + its delay
                        }
                    }
                }
            }
            // ---------------- staircase at time n - latency
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                if (s.digitalOn) y[c] = zoh[c].end();
                else { zoh[c].naive[n & (ZohRenderer::kRing - 1)] = adc[c].at (tA); y[c] = adc[c].at (tA - BlepTable::W); zoh[c].n = n + 1; }
                postAdc[c] = lastAdcValue[c];
            }
            const float postDac = 0.5f * (y[0] + y[1]);
            // SSM envelope, aligned with the delayed staircase
            if (trigRing[(n - BlepTable::W) & 63]) dynEnv = 1.f;
            dynEnv = flush (dynEnv * sweepK);
            // ---------------- analog output side
            for (int c = 0; c < 2; ++c)
            {
                float v = y[c];
                if (plan.idealRecon) v = ideal[c].tick (v);
                if (s.analogOn)
                {
                    const VoiceTolerances& t = var.v[c];
                    const bool filt = s.filtersOn;
                    // the SSM and the output amp share one oversampled section (no down / up between them)
                    float os[4]; bool ssmRan = false;
                    if (s.channel == 0)
                    {
                        if (s.ssmOn)
                        {
                            const float oct = filt ? ssmFloorOct[c] + dynSpan * dynEnv : 14.2877f;          // log2 (20 kHz) with filters off
                            ssm[c].processOS (v + (L.ssm > 0.f ? noise[c].white() * L.ssm * t.noise : 0.f), oct, os); ssmRan = true;
                        }
                    }
                    else
                    {
                        if (s.channel != 3 && filt) v = ofilt[c].tick (v + (L.filters > 0.f ? noise[c].white() * L.filters * t.noise : 0.f));
                        if (s.ssmAll && s.ssmOn) { ssm[c].processOS (v, ssmAllOct[c], os); ssmRan = true; }    // non-hardware option
                    }
                    v = ssmRan ? aout[c].processOS (os, drive) : aout[c].process (v, drive);
                    if (L.hiss > 0.f) v += noise[c].hiss (L.color) * L.hiss * t.noise;
                    v += hum[c] * L.hum + gnd[c] * L.ground;
                }
                const float d = dryAt (c, n - kLatency);
                const float o = s.bypass ? d : (v * outG * s.mix + d * (1.f - s.mix));
                (c == 0 ? outL : outR)[i] = o;
            }
            // ---------------- analyzer
            if (s.analyzer && ! s.freeze)
            {
                const float tap = s.tap == 0 ? 0.5f * (dryAt (0, n - kLatency) + dryAt (1, n - kLatency))
                                : s.tap == 1 ? 0.5f * (postAdc[0] + postAdc[1]) : s.tap == 2 ? postDac : 0.5f * (outL[i] + outR[i]);
                if (an.feed (0.5f * (dryAt (0, n - kLatency) + dryAt (1, n - kLatency)), 0.5f * (outL[i] + outR[i]), tap)) anaReady = true;
            }
            ++n;
        }
    }
};
} // namespace sp
