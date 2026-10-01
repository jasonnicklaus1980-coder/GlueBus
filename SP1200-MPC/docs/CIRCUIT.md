# SP1200 2.0: the circuit model, stage by stage

This document says, for every stage of the SP-1200 signal path:
- what the hardware does;
- the evidence for it;
- what the plugin computes;
- what is an assumption.

The measured behaviour of the model is in `docs/MEASUREMENTS.md`. `make test` regenerates that file, and every figure in it is checked by `test/stage_test.cpp`.

**Ground rules**

- **No measurements of a physical unit.** None were available to this build. The plugin is a *circuit-informed model*, not an exact reproduction. Each stage follows the documented architecture.
- **Undocumented values are labelled.** Where a component value or a curve is not published, the value used is marked **assumed** below. Those values are kept inside the range that the documented parts and figures allow.
- **No third-party code.** No code, algorithm, asset or preset from E-mu, Native Instruments or any other product was used. "S1200 Reference" is a voicing built from the *publicly described* behaviour of Maschine's S1200 mode, not from its implementation.

## Sources

| # | Source | Used for |
|---|---|---|
| S1 | E-mu SP-1200 owner's manual and spec sheet | 26.04 kHz, 12-bit, tune +7 / -8 semitones, 2.5 s per bank / 10 s total, eight outputs, S/N 90 dB (A), THD 0.05 % |
| S2 | D. Yeh et al., ICMC 2007: an analysis of the E-mu SP-12 sampler | SP-12 at 27.5 kHz; 12-bit successive-approximation quantizer; an op-amp anti-alias filter attenuating above ~15 kHz |
| S3 | SP-1200 parts lists and clone analyses (service documentation, published DIY clone write-ups) | AD7541 / MP7621 12-bit multiplying DAC; 8-bit level DAC; 4051 + TL084 sample-and-hold multiplex; NE5534 / TL084 op-amps; SSM2044 on Out 1-2; 7 trimmers on the main board |
| S4 | Owners' measurements, widely reported | Out 3-4 ~7.5 kHz and Out 5-6 ~10 kHz low-pass; Out 7-8 unfiltered; Out 1-2 envelope closing to ~250 Hz within milliseconds; no resonance |
| S5 | SSI2144 datasheet (a modern chip using the SSM2044's circuit) | ±20 mV nominal / ±50 mV clipping differential input; a note that control feedthrough improved over the original |

## Signal path

```
input ─ AnalogInput ─ ADC ─ SamplerMemory ─ PlaybackEngine/PitchEngine ─ DAC ─ ReconstructionFilter ─┬─ Out 1-2: SSMModel ─┐
        (coupling, op-amp, (T/H, SAR on  (12-bit words)  (26.04 kHz clock,       (12-bit ×      (ZOH staircase)    ├─ Out 3-4 / 5-6: LPF ┼─ AnalogOutput ─ OUTPUT ─ MIX
         rails, AA filter)  the ladder)                    drop-sample reads)     8-bit level,                       └─ Out 7-8: none ─────┘   (drive, rails,
                                                                                  S/H)                                                         coupling, hiss/hum)
NoiseModel injects each noise at its own stage. HardwareVariation sets component tolerances. Analyzer taps INPUT / POST-ADC / POST-DAC / OUTPUT.
```

The digital stages (sampling, memory, pitch, conversion) are **never oversampled**: their aliasing is the SP-1200's sound. Only the nonlinear analog stages run at 2× or 4×, depending on QUALITY.

## Stage by stage

### 1. AnalogInput (`src/sp/AnalogInput.h`)

**Hardware.** A coupling capacitor feeds an op-amp input buffer / gain stage (NE5534 class, S3), then an op-amp anti-alias low-pass (TL084, S3), then the converter's track-and-hold.

**Evidence.**
- S2 measured the SP-12's anti-alias filter attenuating above ~15 kHz. The SP-1200 uses the same design family.
- The corner and filter order are not published for the SP-1200.

**Model.**
1. Coupling high-pass at 5 Hz.
2. Input noise, injected before the filter so it is sampled like the signal.
3. A gain stage that is linear up to HEADROOM, then rounds off into the rails with KNEE. It is oversampled.
4. A 4th-order Butterworth low-pass (two Sallen-Key sections) at ANTI-ALIAS FILTER, 15 kHz by default.

That filter is **leaky** between 13.02 kHz (the Nyquist frequency of 26.04 kHz) and ~20 kHz. Some aliasing reaches the samples, as on the hardware.

**Assumed.**
- Coupling corner: 5 Hz.
- Rail headroom: +8 dB above converter full scale. This is typical of ±15 V op-amps feeding a ±5 V converter, but not documented.
- Filter order and Q: 4th order, Butterworth.

### 2. ADC and Quantizer12Bit (`ADC.h`, `Quantizer12Bit.h`)

**Hardware.**
- A track-and-hold samples at the 26.04 kHz clock (27.5 kHz on the SP-12).
- A successive-approximation search uses the **same 12-bit DAC** that plays samples back.
- An offset trimmer sets the code centring.

**Evidence.** S1 (rate, 12-bit), S2 ("12-bit successive approximation quantizer"), S3 (AD7541 / MP7621, trimmers).

**Model.**

- **Converter instants.** The analog signal is evaluated at the converter's exact instants by band-limited interpolation of the host-rate signal. The interpolator depends on QUALITY:

  | QUALITY | Interpolator | Hardware-exact? |
  |---|---|---|
  | ECO | nearest host sample | no: adds timing jitter |
  | NORMAL | cubic | yes, within the kernel's accuracy |
  | ACCURATE | 16-tap Kaiser sinc | yes, within the kernel's accuracy |
  | REFERENCE | 32-tap Kaiser sinc | yes, within the kernel's accuracy |

  Nothing here band-limits the signal to 13 kHz. Whatever the input stage passes folds back, as it does on the hardware.
- **The SAR search.** The plugin runs an actual 12-bit SAR search, MSB first, on a binary-weighted ladder (codes −2048 … +2047).
  - With COMPONENT VARIATION, each bit weight gets an error of up to ~0.5 LSB INL. The steps then come out uneven at the major carries.
  - The same ladder errors apply on record and on playback.
- **Offset trim.** QUANTIZER *Mid-tread* (default) is a 0.5 LSB offset, so silence records as one code. *Truncate* is the untrimmed alternative.
- **Converter noise.** Comparator noise is added in LSBs before the search.

**Assumed.**
- Which way E-mu trimmed the offset. Mid-tread is the default because a trimmed unit records silence as one code.
- Converter noise of 0.1 LSB rms.
- Bit-weight errors are typical AD7541-class tolerances, not measured ones.

**Measured.** A −40 dBFS sine gives 41 levels (ideal 42). A −80 dBFS sine gives 1. A 12 kHz tone has no alias at 26.04 kHz, but aliases to 8 kHz at 20 kHz.

### 3. SamplerMemory (`SamplerMemory.h`)

**Hardware.** 12-bit words in RAM. 2.5 s per bank, 10 s total (S1).

**Model.**
- Stores converter **codes**, not floats.
- 65,536 words, which is 2.52 s at 26.04 kHz.

### 4. PlaybackEngine and PitchEngine (`PlaybackEngine.h`, `PitchEngine.h`)

**Hardware.**
- The DAC clock is fixed at 26.04 kHz.
- Tuning changes how fast the read position steps through memory, with **no interpolation** ("drop-sample"). Words are skipped when tuned up and repeated when tuned down.
- Tune range is +7 / −8 semitones (S1).
- The aliasing therefore differs at every tune setting.

**Model.** Equal-tempered ratio `2^(st/12)`. RANGE *HW* is −8 … +7; *Ext* is −12 … +12 (non-hardware). The plugin is an insert on a live signal, so it offers three playback schemes built from the same drop-sample reads:

- **45>33 GRIT.**
  - The sampling clock runs at 26.04 kHz × ratio, and the DAC replays at 26.04 kHz.
  - Pitch is kept and the effective sample rate changes. This is the hardware result of sampling a record at 45 rpm and tuning it down.
- **PITCH.**
  - Live drop-sample pitch shift with two read heads over a 79 ms window. One head plays alone at the exact rate; a crossfade covers 20 % of the window.
  - The splice is **not** a hardware feature. It is needed on a continuous stream and adds sidebands at large intervals (`MEASUREMENTS.md` §4).
- **REPLAY.**
  - The hardware's own behaviour. Each detected hit starts a voice that reads memory from the hit at the tuning ratio until the next hit.
  - Tuned up, the hit is shorter. It cannot read audio that has not arrived yet, so it ends like the end of a sample.
  - Tuned down, the hit is longer.
  - Use REPLAY for hardware-faithful pitch.

**Comparison modes.** ALIAS *Bypass* switches to linear-interpolated reads (non-hardware), for comparison. At +7 st, drop-sample reads produce 17.4 dB more spurious energy than interpolated reads.

**Assumed.** The hit detector, which stands in for the sequencer's trigger. Its envelopes are fast / slow with a 40 ms hold-off.

### 5. DAC (`DAC.h`)

**Hardware (S3).**
- One 12-bit multiplying DAC is shared by eight voices.
- Its output is the reference of an 8-bit multiplying level DAC, which sets volume and decay.
- A per-voice sample-and-hold (4051 switch, capacitor, TL084 buffer) holds each voice between its multiplex slots.

**Model.**
- The ladder value of the code (same ladder as the ADC) × the 8-bit level / 255.
  - Decay is applied as level-DAC steps, so a tail falls in 8-bit steps. The measured result is 2 steps between 250 and 300 ms of a 300 ms decay.
- S/H SETTLING (100 % = complete within its slot).
- A charge-injection pedestal, scaled by COMPONENT VARIATION.
- DIGITAL NOISE (off by default): a small disturbance proportional to the number of DAC bits that change on the clock.

**Assumed.**
- The pedestal size.
- The digital-noise model. This one is **behavioural**: no measurement of the coupling exists.

### 6. ReconstructionFilter (`ReconstructionFilter.h`)

**Hardware.** The held voltage is a staircase (zero-order hold). Its spectrum is the audio × sinc(f / 26.04 kHz), plus images at 26.04 kHz ± f and above. **There is no reconstruction filter** beyond the per-output filters.

**Model.** RECON has three settings:

| RECON | What it does | Hardware? |
|---|---|---|
| **Analog ZOH** (default) | Each step is a band-limited step (BLEP, ±24 samples, Kaiser β 8) at its exact sub-sample time. Every image below the host's Nyquist is kept; images above it are removed, as any audio interface recording the hardware would. Nothing folds back: an image just above the host Nyquist measures −101.9 dB. | yes |
| **Raw Steps** | Steps snapped to the host grid. Images fold back (−17.9 dB). | no: the "Enhanced" sound |
| **Ideal LPF** | Adds a steep low-pass at 0.45 × the clock. | no: shows what a proper reconstruction filter would do |

**Measured.** ZOH droop matches sinc(f / 26.04k) to within 0.01 dB from 1 to 12.5 kHz.

### 7. Output channel filters (`ReconstructionFilter.h`, `SSMModel.h`)

| Output | Hardware (S4) | Model | Measured |
|---|---|---|---|
| 1-2 | SSM2044 VCF; Z80 AR envelope opens on a hit, then closes to ~250 Hz; no resonance | SSMModel (below) | first ms 35.6 dB louder than after closing (4 kHz burst) |
| 3-4 | fixed low-pass ~7.5 kHz | 4th-order Butterworth at 7.5 kHz | −3.0 dB at 7.5 kHz |
| 5-6 | fixed low-pass ~10 kHz | 4th-order Butterworth at 10 kHz | −3.0 dB at 10 kHz |
| 7-8 | unfiltered | none | — |

**Assumed.** The order of the fixed filters is not documented. Two Sallen-Key sections are assumed, matching the op-amps in the parts list.

### 8. SSMModel (`SSMModel.h`)

**Hardware.**
- The SSM2044 is a four-pole ladder of transconductance cells.
- The SSI2144, which reuses its circuit, specifies ±20 mV nominal / ±50 mV clip at the input (S5).
- The SSI2144 sheet also says control feedthrough improved over the original, which implies the SSM2044 has noticeable feedthrough.
- The envelope is generated by the Z80; DYN SWEEP and DYN FLOOR set its speed and floor (the floor is the cutoff trimmer).

**Model.**
- Four cells, each `dy/dt = wc·(tanh(x) − tanh(y))`.
- Solved semi-implicitly (zero-delay, linearised per sample) and oversampled per QUALITY.
- SSM CHARACTER sets the level into the chip. At 0 dB, full scale is about ±10 mV.
- COMPONENT VARIATION adds input offset (even harmonics) and cutoff spread.
- SSM RESONANCE is 0 by default, as on the hardware; above 0 it is non-hardware.
- SSM PATH *All Outputs* runs the SSM on every output (non-hardware).

**Assumed.**
- The attenuator before the chip: about ±10 mV at full scale. It is calibrated so THD stays near the published 0.05 % at normal level; the model measures 0.036 %.
- Control-feedthrough size: −45 dBFS per opened octave (**behavioural**).
- Envelope shape: AR, with the defaults 12 ms / 250 Hz.

### 9. AnalogOutput (`AnalogOutput.h`)

**Hardware.** NE5534-class output op-amps (S3) and output coupling.

**Model.**
- DRIVE raises the level into the stage and lowers it after, so only the colour changes.
- Rail saturation with a soft knee above OUTPUT HEADROOM.
- Slight asymmetry from VARIATION.
- Coupling high-pass at 3 Hz.
- The stage is oversampled.

**Assumed.** Headroom of +12 dB and a 3 Hz coupling corner.

**Measured.**

| Condition | THD |
|---|---|
| −10 dBFS on Out 7-8 | 0.005 % (published spec: 0.05 %) |
| DRIVE +18 dB | 1.59 % |
| converter clipping | 29 % |

### 10. NoiseModel (`NoiseModel.h`)

Each source is injected where it arises, uncorrelated between channels and generated continuously (nothing loops).

| Source | Stage | Default |
|---|---|---|
| input amp noise | before the AA filter | −100 dB |
| converter | comparator, in LSB | 0.1 LSB |
| S/H + DAC buffer | held with each step | −97 dB |
| SSM2044 | SSM input | −92 dB |
| filter op-amps | before Out 3-6 filters | −98 dB |
| output hiss (HISS) | output stage | −91 dB |
| HUM (50/60 Hz + harmonics), GROUND (rectifier buzz at 2× mains) | output stage (−12 dB at the input) | off |
| DIGITAL NOISE | DAC, per changed bit | off |

**Calibration.** The defaults give an idle output of −89.7 dB unweighted / −92.1 dB (A). The published S/N is 90 dB (A).

**Noise controls.**
- NOISE COLOR tilts the hiss (pink ↔ white ↔ bright).
- NOISE LEVEL scales every source together.
- ANALOG NOISE scales the circuit noises.
- CONVERTER NOISE scales the converter noise.

### 11. HardwareVariation (`HardwareVariation.h`)

**At 0 %.** The reference unit: idealised components, and left and right identical.

**Above 0 %.** COMPONENT VARIATION scales seeded tolerances. UNIT picks the seed, so each unit is repeatable. Tolerances:

| Part | Variation |
|---|---|
| Filter corners | ±5 % |
| DAC bit weights | up to ~0.5 LSB INL |
| Level-DAC weights | up to ~0.5 LSB |
| SSM2044 | cutoff ±15 %, input offset ±3 mV |
| Gains | ±0.2 dB |
| Asymmetry | slight |
| Noise | ±1.5 dB |
| S/H pedestal | ±0.3 LSB |

These are **typical part tolerances, not measurements** of any particular SP-1200.

### 12. AliasingEngine (`AliasingEngine.h`)

Adds no processing of its own. It only chooses how the stages above run.

**ALIAS.**
- *Authentic*: the hardware path.
- *Enhanced*: converter instants and steps snap to the host grid. The fold-back and jitter go beyond the hardware.
- *Bypass*: interpolated memory reads.

**MACHINE.**
- *SP-1200*: the default.
- *SP-12*: 27.5 kHz clock (S2).
- *S1200 REF*: anti-alias filter off, ALIAS Enhanced, +4 dB into the output stage. This is a voicing that leans on the documented artifacts, as Maschine's S1200 mode is publicly described. It is not NI's algorithm.

**QUALITY.** Picks the interpolator, the BLEP and the oversampling (CPU in `MEASUREMENTS.md` §13).

### 13. Analyzer (`Analyzer.h`)

**Spectrum.** A 1024-point Hann FFT of the input and of the output, in 32 log bands from 40 Hz to 20 kHz. The input is aligned with the output's latency.

**Oscilloscope.**
- One tap at a time: INPUT, POST-ADC (the held codes), POST-DAC (the staircase) or OUTPUT.
- Rising-zero trigger, 1 to 20 ms window.
- Off by default and not on the screen pages; it runs when the Analyzer parameter is on and feeds the readout parameters.

**Alias meter.** Shows the level of the images / aliases above the effective Nyquist.

## Latency

The fixed latency is 40 host samples (0.9 ms at 44.1 kHz): 16 for the converter's interpolation plus 24 for the band-limited steps. The latency is reported to the host, and the dry path is delayed to match, so MIX never comb-filters.

## Final authenticity check

`make test` runs both test programs:
- `stage_test`: 36 checks, one per stage, against the published figures.
- `host_test`: 21 checks covering the plugin interface, presets, latency, Q-Link stepping and readouts.

**What is hardware-faithful.**
- The clock and resolution.
- The SAR on the shared ladder.
- Drop-sample reads in GRIT / REPLAY.
- ZOH images.
- The level-DAC steps.
- The Out 3-4 / 5-6 corners (to the owners' figures).
- The SSM2044 on Out 1-2 only, with no resonance.
- Noise to the published S/N.

**What is approximate.**
- PITCH mode's splice.
- The hit detector standing in for the sequencer.
- Every **assumed** value above.

**What is non-hardware and labelled as such.**
- *Ext* pitch range.
- BITS above 12.
- ALIAS Enhanced / Bypass.
- RECON Raw / Ideal.
- SSM RESONANCE.
- SSM PATH All.
- The S1200 REF voicing.
