# MC-909 sampler: import engine (phase 1)

Reads documented formats only: RIFF/WAVE audio (PCM 8/16/24/32-bit, float 32/64-bit) and Standard MIDI Files.
Any other file is reported as unsupported and is not decoded or guessed at.

    make test                      # import and sampler checks, generated fixtures only
    MC909_FACTORY_DIR=<folder of WAV files> ./build/test_sampler    # optional: decode every WAV in a folder
    ./build/mc909_import_check <file-or-folder>...

Each file gets a report: what it is, whether it was read in full, and any warnings (truncated data, cut-off
MIDI tracks, unsupported sample sizes).

## Sampler core (phase 2)
`src/sampler.h`: 16 pads, one-shot, loop and reverse playback, tuning by fractional read (+/-12 semitones and beyond),
linear interpolation, 1 ms attack and 10 ms release (no clicks), choke groups, stereo and mono samples, 16 voices with
oldest-voice stealing. Rendering allocates nothing.

## Not done
- No Roland proprietary formats. The MC-909 PC editor package contains a system updater, panel images and pattern
  data in Roland's own formats, which are not decoded. The package also holds 27 WAV samples; the importer reads all
  of them in full. Those files belong to Roland and are not in this repository.
- No waveform editor, pad GUI or pattern reconstruction yet (later phases).
- Not yet tested on an MPC X.
