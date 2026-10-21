# SP-1200 model: aliasing check (tone tests and sweep)

Made with `tools/alias_sweep.cpp` at a 96 kHz host, SP-1200 clock 26.04 kHz, Out 7-8 (unfiltered), noise off, default
settings. Levels are relative to the input tone. The sweep is 20 Hz to 20 kHz, exponential, 1.5 s, as in the paper
Yeh, Nolting and Smith (ICMC 2007), which measured the SP-12.

```
tone test, host 96 kHz, SP-1200 clock 26.04 kHz, amplitude 0.5
 tone Hz    at tone      alias      image   (dB re the tone level)
    1000       -0.0      -28.0      -28.7
    4000       -0.3      -15.2      -17.9
    8000       -1.4       -8.5      -14.2
   12000       -3.8       -5.2      -15.0
   14000       -6.4       -5.1      -18.7
   16000      -10.8       -6.8      -27.9
   18000      -16.8       -9.8      -45.6
   20000      -24.0      -13.6      -73.1
```

## Reading the table
- Above about 13 kHz (the Nyquist of a 26.04 kHz clock), the alias at 26.04 kHz minus the tone is louder than the tone
  itself, from about 14 kHz upward.
- The first image above the clock (26.04 kHz plus the tone) is 11 to 33 dB below the alias above 12 kHz.
- The sweep shows the same mirror about 13 kHz.

## Limits
- The paper's measured plots are not in this repository, so the numbers are not compared with them.
- The paper measured the SP-12, not the SP-1200. The model's filters are assumed (see `docs/CIRCUIT.md`).
