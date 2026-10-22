# MC-909 sampler: import engine (phase 1)

Reads documented formats only: RIFF/WAVE audio (PCM 8/16/24/32-bit, float 32/64-bit) and Standard MIDI Files.
Any other file is reported as unsupported and is not decoded or guessed at.

    make test                      # import, sampler, project, extras and VST2 wrapper checks, generated fixtures only
    MC909_FACTORY_DIR=<folder of WAV files> ./build/test_sampler    # optional: decode every WAV in a folder
    ./build/mc909_import_check <file-or-folder>...
    make -C MPC package            # ARM (MPC OS) instrument and installer, dist/Legacy-Sampler-0.1.0-mpc-armv7.zip

Each file gets a report: what it is, whether it was read in full, and any warnings (truncated data, cut-off
MIDI tracks, unsupported sample sizes).

## Sampler core (phase 2)
`src/sampler.h`: 16 pads, one-shot, loop and reverse playback, tuning by fractional read (+/-12 semitones and beyond),
linear interpolation, 1 ms attack and 10 ms release (no clicks), choke groups, stereo and mono samples, 16 voices with
oldest-voice stealing. Rendering allocates nothing.

## MPC OS build (phase 3)
`MPC/`: the sampler as a 32-bit ARM hard-float VST2 instrument (`mc909_sampler.so`), built the same way as Quanta.
It needs only libc and the loader (`make -C MPC check`), uses glibc up to 2.34, and exports one symbol, `VSTPluginMain`.
The math it needs is in `src/mathlite.h`, so libm is not linked. The installer and uninstaller are tested against a fake
MPC root only. `MPC/mpc/INSTALL.md` covers installing and the project file the plugin reads from
`/sdcard/LegacySampler/project.mc909`.

## Not done
- No Roland proprietary formats. The MC-909 PC editor package contains a system updater, panel images and pattern
  data in Roland's own formats, which are not decoded. The package also holds 27 WAV samples; the importer reads all
  of them in full. Those files belong to Roland and are not in this repository.
- No waveform editor, pad GUI or pattern reconstruction yet (later phases).
- Not yet tested on an MPC X. The note-to-pad mapping (notes 36-51 to pads 0-15) is an assumed convention.
- No on-device project editor: the project file is written by hand, or on the computer.
