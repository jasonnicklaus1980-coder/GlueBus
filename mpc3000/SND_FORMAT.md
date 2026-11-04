# MPC3000 .SND sound file (as written and read by OS 3.10)

Learned from the emulated OS itself: a test file was loaded, then saved by the OS ("Save a Sound"),
and single header bytes were varied while reading the Edit Sound screen.

| Offset | Size | Field |
|--------|------|-------|
| 00 | 1 | Type ID: 01 = sound (others: "internally corrupted ... type ID") |
| 01 | 1 | Format version: 0-2 accepted by OS 3.10, 3+ "created on a later OS"; the OS writes 02 |
| 02 | 16 | Name, ASCII, space-padded |
| 12 | 1 | Unknown (no visible effect; 0) |
| 13 | 1 | Vol% (0-100) |
| 14 | 1 | Tune |
| 15 | 1 | Must be 0 here (77 gave "contains invalid data"); probably a mode flag |
| 16 | 4 | Soft start (sample position, little-endian) |
| 1A | 4 | Soft end (sample position) |
| 1E | 4 | Length in samples |
| 22 | 2 | Unknown; the OS writes 2000 (07D0h) |
| 24 | 2 | Unknown; the OS writes 1 |
| 26 | 2 x length | Samples: 16-bit signed little-endian, mono, 44.1 kHz |

Verified: a 4410-sample sine loaded, saved and compared byte-for-byte; a 22050-sample 440 Hz
decaying sine (Vol% 100, soft end = length) assigned to pad A01 played at 440.0 Hz for 0.491 s.
Not yet known: field 12, the 15 flag values, fields 22/24, loop points, stereo sounds.
