# MPC3000 emulator: work in progress

## Done
- `x86len.py`: instruction-length decoder for the NEC V53 (8086-compatible, with
  the V53's 0F-prefixed extensions). Validated against objdump on the v3.10
  ROM's reset and start-up code: 142 of 142 instructions match.

## Facts established from the service manual and the ROM
- CPU: NEC V53, 16 MHz (x86 family, not a 68000).
- ROM: IC13 (low bytes) and IC14 (high bytes), AM27C020 each, 512 KB total,
  mapped at 0x80000-0xFFFFF. Reset vector: FFF0:0000.
- Start-up copies no system from ROM; a handler table is run, then the system
  is entered at B000:0080 in RAM.
- The upgrade file M3K316.SYS is a DOS (MZ) executable.

## Not done
- Instruction execution (the core), flags, segments, interrupts.
- V53 on-chip peripherals, the floppy path, and the MPC3000 hardware.
