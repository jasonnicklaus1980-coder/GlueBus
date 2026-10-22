# Compatibility matrix

Status words are strict: **supported** means the file is read in full and tested; **partial** means part of it is read;
**unsupported** means it is not decoded. A file that opens is not supported unless it is read in full.

| Format | Status | How it is checked |
|---|---|---|
| WAV (PCM 8/16/24/32-bit, float 32/64-bit) | supported | decoding tests; the factory WAV check (optional, local files) |
| AIFF (uncompressed PCM 8/16/24/32-bit, big-endian) | supported | decoding tests incl. truncated files |
| AIFC (compressed AIFF) | unsupported | reported; not decoded |
| Standard MIDI File (format 0, 1, 2) | supported (checks and tempo/notes) | import tests, tracks checked for end-of-track |
| Sampler project file, version 1 (this project's format) | supported | save/load round trip; newer versions refused |
| Roland MC-909 `.MCS` (sequencer data) | unsupported | Roland's own format; no public specification in the files supplied; not decoded |
| Roland MC-909 `.IMG` (panel images) | unsupported | Roland's own format; not decoded |
| Roland MC-909 `.PRG` (editor program) | unsupported | Roland's software; not decoded |
| Roland SRX-01 backup `.SVD` | unsupported | Roland's own format; not decoded |
| Roland MV-8000, MV-8800 | not started | no format work; will need their documentation |

## Conversion workflow (verified methods only)
No conversion method for Roland data has been verified. The only verified route is: export audio as WAV or AIFF and
MIDI as SMF from a device or software that documents those exports, then import those files here. Check the
owner's manual of the device for its export options before relying on one. Nothing in this project invents a
Roland export or converter.

## Limits
- Unsupported files are reported, never guessed at.
- Source files are read, never written. Processed copies are not made.
- Nothing has been tested on an MPC X yet.
