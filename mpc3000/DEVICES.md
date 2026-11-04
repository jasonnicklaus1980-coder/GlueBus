# MPC3000 ROM: device behaviour the boot code depends on (v3.10 ROM)

Hypotheses from the write/read patterns in the ROM. Each needs confirming
against the running code.

| Port | Use in boot code | Required behaviour |
|------|------------------|--------------------|
| C033 | timer control word (0x74 = counter 1, LSB/MSB, mode 2) | accept writes |
| C031 | counter 1 divisor (B1 then 28) | accept writes |
| C032 | counter 2 load (00, 00) | accept writes |
| C030 | counter 0 read-back, compared with 0x4E20 for timing | counts down while running |
| C010/C011 | PC-style system control (0x61 written; bits cleared) | read/write, bits retained |
| C000 | written 01 at start of boot | accept writes |
| C001/C002 | read-modify-write; bit 0 cleared | retain bits |
| C006 | written with 0x30 OR'd in | accept writes |
| C00B | status: boot waits for bit 3 (0x08) at CB444 with no timeout | **bit 3 must become 1** or boot hangs |
| C00F | written 0x0F | accept writes |

Other points:
- Ports 0x60/0x62 are used in a timed loop near CB452 (port 0x60 write, then
  0x62 read masked with F000 and compared). Not yet identified.
- The boot code jumps to B000:0080 in RAM, where the OS is loaded from disk.

## Ports 0x60 / 0x62 (timed wait near CB458)
- Write 0 to port 0x60 (latch), then read 16-bit port 0x62, mask 0xF000, and
  loop until it equals the target (shifted left 12).
- Requirement: the top nibble of the value read from 0x62 must advance with time
  after a latch, reaching the target. Two uses: CB45D and DA01C.
