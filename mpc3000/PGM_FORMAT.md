# MPC3000 .PGM program file (as written by OS 3.10, "Save a Program")

Read back from a program saved by the emulated OS with SINEA on A01 and SINEB on A02. 4608 bytes.

| Offset | Size | Contents |
|--------|------|----------|
| 0000 | 1 | Type ID 07 (program) |
| 0001 | 1 | 00 |
| 0002 | 128 x 17 | Sound name table: 16 ASCII characters + 00 per entry; index 0 = first sound |
| 0882 | 6 | 22 56 00 00 22 56 (unknown) |
| 0888 | 506 | zeros in this file (unknown) |
| 0A82 | 17 | Program name (16 characters + 00) |
| 0A93 | 45 | Program parameters (defaults, not decoded) |
| 0AC0 | 64 x 24 | Note records, notes 35-98. Byte 0 = sound index (FF = none); bytes 1-23 = per-note parameters (all defaults here: 00 2C 22 58 22 00 22 22 00 00 00 00 00 64 00 00 00 00 64 00 00 00 00) |
| 10C0 | 64 x 4 | Per-note records 64 32 00 09 (probably mixer: level 100, pan 50, ...) |

Pads A01-A16 use the OS's default notes 37 36 42 82 40 38 46 44 48 47 45 43 49 55 51 53.
Loading a .PGM (DISK -> 7 -> <Load>) also loads the named .SND files from the same disk.
tools/make_kit.py builds kits from tools/template.pgm (this file) by filling in the name table and
the note records' sound index. Verified: a two-pad kit loaded from the .PGM alone, A01 played its
440 Hz sound, A02 its 660 Hz sound.
