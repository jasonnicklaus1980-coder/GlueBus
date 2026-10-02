# How close is G-Glue to the classic console bus compressor?

G-Glue is an original compressor. It is **not** an official SSL product, and it does not use SSL's or any licensee's
models, code or measurements (those aren't public). It does follow everything that is publicly documented about how
the classic G-series console bus compressor behaves.

## Matched to published behaviour
| Published behaviour | G-Glue |
|---|---|
| Ratios 2:1, 4:1, 10:1 | same |
| Attack 0.1 / 0.3 / 1 / 3 / 10 / 30 ms | same; measured 0.33 / 13 / 39 ms for 0.3 / 10 / 30 ms (63 % point, includes the knee) |
| Release 0.1 / 0.3 / 0.6 / 1.2 s and Auto | same; measured 0.100 / 0.601 / 1.197 s |
| Auto release: two time constants, fast for short peaks, slow for sustained compression | fast 100 ms stage + slow stage; measured 0.33 s after a 20 ms hit, 3.25 s after 2 s of compression |
| Feedback side-chain: the detector follows the gain-reduction VCA | feedback topology (default) |
| Each channel rectified by a true-peak full-wave detector; the louder channel controls both | same |
| Soft knee whose position depends on the ratio, softer when driven hard | ratio-dependent soft knee in a feedback loop |
| Side-chain high-pass filter, audio path untouched | 30–200 Hz, measured: audio path untouched |
| "Analog" mode: the circuit's noise and harmonic distortion | ANALOG: −92 dBFS noise floor + subtle even-order distortion |

## Not matched (no public data)
- The exact knee shape and position offsets per ratio, the detector's exact rise and decay, and the auto-release
  time constants of the original circuit. G-Glue's values are reasoned choices, not measurements of the hardware.
- The exact harmonic and noise spectrum of the console's VCA and op-amp stages.
- Threshold and make-up ranges follow your spec (−30…+10 dB, 0…+24 dB) rather than the hardware's.

## To get closer
Render the G-Glue test signals through an official plugin you own (for example Waves SSL G-Master Buss Compressor,
UAD SSL 4000 G or SSL Native Bus Compressor 2) with noted settings and send the files back. The curves above can then
be fitted to measurements, and each one reported with its remaining error.

## Sources
- SSL, *G Series Bus Compressor Module for 500 Series Racks*, User Guide (detector, side-chain topology, controls)
- Vintage King, *SSL G-Comp 500 Series* product pages (controls and values)
- bonedo.de, *SSL G Comp Bus Compressor Test* (feedback topology, ratio-dependent knee)
- Waves, *SSL G-Master Buss Compressor* product descriptions (Analog: noise and THD; knee softens when driven)
- Gearspace discussions of the G-series bus compressor (auto release, VCA types)
