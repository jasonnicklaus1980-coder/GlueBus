# MC-909 sampler: import engine (phase 1)

Reads documented formats only: RIFF/WAVE audio (PCM 8/16/24/32-bit, float 32/64-bit) and Standard MIDI Files.
Any other file is reported as unsupported and is not decoded or guessed at.

    make test                      # 13 checks, generated fixtures only
    ./build/mc909_import_check <file-or-folder>...

Each file gets a report: what it is, whether it was read in full, and any warnings (truncated data, cut-off
MIDI tracks, unsupported sample sizes).

## Not done
- No Roland proprietary formats. The MC-909 PC editor package has only the system updater and documents no file
  formats, so those formats are not decoded.
- No sample playback, pads or GUI yet (later phases).
- Not yet tested on an MPC X.
