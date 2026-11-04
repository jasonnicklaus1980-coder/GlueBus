# MPC3000 emulator progress

## Milestone 1 (reached): ROM start-up runs under the interpreter
- Reset at FFF0:0000 executes. Port writes FFEA-FFF6 done.
- Jump to FAC0:0000 runs the delay loop, then the table copy: 73 words written
  to the V53 register block at FF00, FF02, FF04 ... (OUTSW with DX stepping).
- Stops at FAC0:001D on 0F E0 11, the first V53-specific (0F-prefixed) instruction.

## Blocker
- The 0F-prefixed V53 instructions are not implemented. 0F E0 ib is needed here.
  Its exact meaning (V53 native-mode switch or similar) must be checked in NEC's
  V53 user manual before it is implemented. It is not guessed.

## Next after that
- Implement the 0F-prefixed instructions the ROM uses, with the manual as reference.
- Continue to the wait loops (C00B bit 3, C030 counter, 60/62 timer), which the
  device stubs in x86cpu.py already satisfy.

## Update: boot code runs about 199,000 instructions before stopping
- Table-driven device setup (ports 0x00-0xFF range, C0xx, FF00 block) executes.
- Far calls through the handler table F19E:0004 reach real handler code at F800:000A
  (CLI, pushes, writes to C033/C034). Handler code is confirmed as code, not padding.
- Stop cause: the handler switches SS to 8FDE (mov ss, ax at F800:006F) after it
  has pushed its return address on the old stack (SS=0000). The later RET pops from
  the new stack and jumps to garbage (F800:0001).
- Unresolved question: whether the real boot state sets SS/SP before this handler
  runs (our reset assumes SS=0000, SP=0400), or whether the handler restores SS
  before returning. The next step is to read F800:0060-0080 and 1870-1880 in full.
- The 0F E0 instruction is still skipped in this diagnostic run (blocker unchanged).

## Update: BRKXA implemented from NEC's user's manual (U11301EJ5V0UM)
- 0F E0 ib = BRKXA imm8 (V33A/V53A). Operation (manual section 2): PC <- word at
  imm8*4, PS <- word at imm8*4+2, XA flag set (XAM register, I/O FF80h). Nothing pushed.
- Implemented exactly that. The ROM reaches BRKXA at FAC0:001C with imm8 = 11h.
- At that moment vector 0000:0044 is zero, so execution jumps to 0000:0000 and runs
  garbage. The ROM expects this vector to be set before BRKXA, by a step not yet
  reproduced here (or the vector table is relocated by a V53 register; not yet checked).
- Address calculation in extended address mode is described in the separate
  hardware manual of each model, which is not in the uploaded file. Not implemented;
  it is assumed to match normal mode for addresses below 1 MB.
- Note: the manual lists BRKXA as V33A/V53A only. The ROM's use of it suggests the
  MPC3000's processor is a V53A, or the ROM was written for it. Worth checking the
  chip marking on the board (CPU PCB).
- Next: find which ROM code writes the vector table at 0000:0044 (search for stores
  to segment 0), and check the V53 IDB (interrupt table base) register.

## Update: boot ROM now runs through BRKXA and RETXA to the point where it needs the disk
- Boot shadow: at reset the ROM is seen at address 0000-03FF (the vector table) until
  the code writes there. Confirmed by the ROM's own bytes at 0000:0044 = FAC0:001F,
  the instruction after BRKXA, and by the code writing vector 0000:0048 itself.
- BRKXA (0F E0 ib) and RETXA (0F F0 ib) implemented from NEC's user's manual.
- Run now passes FAC0:001C (BRKXA 11h) and FAC0:009F (RETXA 12h), with XA mode set
  and cleared as the manual describes. ~400,000 instructions, no unsupported opcode.
- Stop: execution jumps to 0000:2DF6, which is uninitialised RAM (zeros). The ROM
  expects a loader to have placed code there, which means the disk must be read.
- Next: identify the floppy controller ports the ROM uses and implement enough of it
  to read the boot file. The MZ upgrade file (M3K316.SYS / M3K339.SYS) is the likely
  payload; its load address still needs confirming from the loader code.
- Caveat: the MPC3000's processor is V53A-class (BRKXA/RETXA exist only on V33A/V53A).
  Confirm from the chip marking on the CPU board.

## Update: second BRKXA (FAC0:00B1, operand 14h) reads a zeroed vector
- Sequence after RETXA: the code writes an OUTSW loop, then BRKXA 14h at FAC0:00B1.
- The code zeroed all of low memory earlier (rep stos from 0000, FAC40-FAC57), so the
  table at 0:0050 reads 0000:0000 and execution goes to empty RAM.
- Open question: in extended address mode the vector comes from a table at an address
  defined in the V53A hardware manual (not in the uploaded instruction manual). Also
  unclear whether the code expects vector 14h to be set from ROM data before this point.
- Needed to continue: the V53A/V53 hardware user's manual (the XAM register FF80h and
  the extended-mode vector table), or the MPC3000 service/schematic detail for it.

## Update: datasheet used (NEC uPD70236, 104 pages, OCR'd), expanded addressing added
- XAM register at I/O FF80h: read-only, bit 0 = expanded addressing on.
- Page registers PGR0-PGR63 at I/O FF00h, FF02h, ... (word writes). They supply a 10-bit
  physical page base, replacing the top 6 address bits (16 KB pages) in expanded mode.
  The boot code's table-copy loop writes exactly these registers (values C0, C1, C2, ...).
- Implemented: address translation in expanded mode (Bus.phys), page register writes,
  XAM read, LES and LDS.
- Result: run reaches ~248,000 instructions, still in expanded mode, and then executes
  from logical 0000:0105, which is empty memory. The boot code's clear loop (FAC40-FAC57)
  zeroes the logical vector area after the first BRKXA. It then writes vectors 10h-12h
  but not 14h. The second BRKXA (14h) therefore reads zero.
- Open: whether vector 14h is meant to be written by code not yet reached, or whether a
  detail of the clear loop (its segment or count) differs from the hardware.

## Update: the ROM's own setup is complete; it waits for the OS to be loaded
- Second BRKXA 14h (normal mode, FAC0:00B1) reads vector FAC0:0134 from the ROM
  shadow, which is the device-dispatch routine (checked by disassembly at FAD34).
  An earlier note blaming this vector was wrong: the vector is correct.
- The dispatch routine runs the device handlers (far calls through table F19E:0004).
- Later the code runs in extended mode at logical FB00:00xx. That page is not loaded
  (zeros), so execution runs empty memory. This is the point where the OS image must
  be in memory, i.e. the floppy load step, which is not modelled yet.
- Next: find the ROM code that calls the loader, and the floppy controller it uses.

## Update: block-transfer engine located (candidate for the OS load)
- Routine around CB37D-CB44B: programs an address and count through ports 60h-68h
  and C001-C00Aa, then polls C00B bit 3 (0x08) until the transfer completes.
  Parameters come from the caller's stack frame (bp+0Ah..bp+10h, a 32-bit count and
  an address). It is a good candidate for moving the OS image into RAM.
- Entry is a larger function. It is reached by jumps into CB362 and CB37D, and by a
  call from CB12B to CB231. The caller chain of CB231 identifies the load routine.
- Next: read CB231-CB260 and CB12B to find the parameters the OS load passes in.

## Update: the FB00 code is real ROM code; a stack frame is out of balance
- Segment FB00 in the ROM is physical FB000 (ROM). In extended mode, page register 62
  is 3Eh (=F8000h), so logical FB00 maps back to the ROM. The earlier "empty memory"
  reading was a debug-print artefact (it read 0 in extended mode).
- Corrected: the bus now keeps 24-bit logical addresses in extended mode.
- The function at FB00:0650-010B ends in RETF (CA) at FB00:010B, which pops zeros: the
  return frame at SS:SP does not match the pushes that built it. Likely a stack-handling
  bug in the interpreter (candidates: RETF/LRET/RET, LOOP, PUSHF/POPF, CALL near, or the
  segment/offset arithmetic for SS in extended mode). Not yet isolated.
- Next: trace push/pop pairs across the FB00:0650-010B call to find the first unbalanced
  instruction.

## Update: stack switch located; boot path diverges in a table-walk loop
- The stack is switched on purpose at FB00:0048-004C (LDS SP,[BX]; mov ss,ax), from
  SS 8FDE to 8FEE. A return must restore the old stack first. The RETF at FB00:010B is
  reached without that restore, so the run has taken a different path from the one
  the hardware takes earlier.
- The loop at FB00:00E1-00FD (dh = 4 iterations per block) walks a table at ES:BP and
  calls helpers at FB13C and FB60A. Its behaviour depends on table contents set up by
  earlier routines and on device status reads. Not yet identified which read diverges.
- Status: the emulator runs about 240,000 instructions of the real boot ROM through
  device setup, both mode switches and the first stack switch. It does not yet reach
  the operating system. Reaching it needs the floppy load path and a complete device
  model, which is many sessions of work at the current rate.

## MAME reference (src/mame/akai/mpc3000.cpp), used as ground truth
- CPU: V53A. Memory: ROM at 00000-7FFFF, mirrored at 80000-FFFFF (read-only).
  RAM 300000-3FFFFF (2 x HM658512). NVRAM 500000-500FFF. Writes to ROM are ignored.
- I/O (AS_IO, 16-bit): 0000/0020 LED latches; 0060-006F DSP (L7A1045); 0080-0087 i8255
  (DIO); 00A0-00BF SCSI (MB89352); 00C0-00DF serial (TE7774, 4 channels); 00E0/00E2 LCD
  (HD61830 data/status); 00E8-00EB floppy (uPD72069) + HC365; 00F0-00F7 PIT 8254
  (sync timer); 00F8-00FF i8255 (ADC). Unmapped I/O reads FFh, writes ignored.
- The ROM's C0xx and FF00 accesses: FF00-FF7E are V53 page registers (verified).
  The C0xx ports the boot code uses are not in MAME's map. Reads of them return FFh here.
- Expanded addressing (XAM FF80h): page registers PGR0-63 map pages 16 KB to 10-bit
  physical pages. Page 62 = 3Eh = F8000h (ROM), page 0 = C0h = 300000h (RAM).

## Emulator state (memory and I/O now follow the MAME map)
- Boot runs to ~248,000 instructions; stop: RETF at FB00:010B pops a return frame that
  was never written at SS:SP (8FEE:1130). The stack switch at FB00:0048-004C
  (LDS SP,[BX]; mov ss,ax) loses the caller's frame. Next: read FB00:0030-0060 for the
  save/restore of the old SS:SP and check the frame layout the code expects.

## Update: instruction-length bug fixed; boot reaches its idle loop
- Bug: read-modify-write memory instructions (or/add/inc/dec/shifts/not/neg) fetched the
  displacement twice (rm_get then rm_set). Fixed by decoding the address once.
  This was the cause of the stack-frame and stray-jump problems seen earlier.
- Now the boot code runs to FB00:022E: STI; NOP x4; JMP (idle loop waiting for an
  interrupt). ~900,000 instructions, no unsupported opcode.
- Next: deliver hardware interrupts. MAME: the 8254 timer (I/O F0-F7) drives the V53
  timer outputs to IRQ0-IRQ2 (tout_handler), and the input merger feeds IRQ3.
  Implement: timer counts -> interrupt request -> CPU takes INT via the vector table.

## Interrupt work: V53 on-chip timer and interrupt controller (from datasheet pp. 57-58, 300 dpi OCR)
- System I/O area FF00-FFFF (table 6): SCTL FFFEh, OPSEL FFFDh, OPHA FFFCh,
  DULA FFFBh, IULA FFFAh, TULA FFF9h? (OCR: FFF9h, FFF8h for SULA), BRC FFE9h,
  WMB0 FFEAh, WCY1 FFEBh, WCY0 FFECh, WAC FFEDh, WCY2 FFF4h, WCY3 FFF5h, WCY4 FFF6h,
  RFC FFF2h, SBCR FFF1h, TCKS FFF0h, BADR FFE1h?, BSEL FFE0h?, XAM FF80h, PGR FF00-FF7Eh.
- Peripheral relocation (figure 47): each block (DMAU, TCU, ICU, SCU) sits at
  OPHA x 256 + its ULA register value within the 256-byte area.
- Next: OCR the ICU and TCU register pages (figures for the ICU and TCU register
  layouts) at 300 dpi, then implement: TCU timer 0 -> ICU request -> vector -> INT.

## Update: interrupts work; the boot code now draws and tries the floppy
- Peripheral relocation from the boot code: OPHA (FFFC) = C0h, so the on-chip blocks sit
  at C000+. ICU (8259-compatible) at C010 with ULA IULA (FFFA) = 10h; TCU (8254-style)
  at C030 with TULA (FFF9) = 30h. SCTL (FFFE) = 11h selects contiguous addressing.
- ICU init seen in the ROM: ICW1 13h, ICW2 08h (vector base), ICW4 01h, OCW1 FFh.
  Counter 0 mode 2 (control word 34h at C033). Timer output = IRQ0 -> vector 08h.
- Implemented: 8259 ICU (ICW, OCW1, non-specific and specific EOI), counter 0 mode 2,
  interrupt delivery (push flags/CS/IP, vector through the table, IF cleared).
- Result: timer interrupts arrive periodically; the handler at D651:0004 runs and sends a
  specific EOI (60h to C010). Boot continues in its main loop at FB00:022E.
- Timer rate is an assumption (4 CPU instructions per count). Needs checking against the
  ROM's timing loops and the real clock (MAME uses PCLKOUT/4 MHz for the TCU).
- LCD: the boot writes ~1,000 data bytes to E0 with control writes to E2. Addressing is not
  yet modelled, so no text can be read yet. The writes look like character bitmaps.
- Floppy: the ROM reads the uPD72069 status at E8h. Next: model the controller's status
  for "no disk" and see what the boot code does, and the SCSI MB89352 at A0-BF (read at A2h).

## Update: floppy status only; the boot code waits for the front-panel controller
- FDC (uPD72069 at E8-E9): the boot code reads the status 8 times and sends no command.
  No disk is attempted at this stage. Model: status 80h, commands collected per uPD765.
- The idle loop waits for input from the panel controller, which talks over the serial
  block (TE7774 channels 2 and 3 are wired to the uPD78C10's TxD/RxD, per MAME).
- MAME ROM set (romset "mpc3000"): panel firmware mp3000__op_v1.0.am27c256__id0110.ic602.bin
  (32 KB, uPD78C10 code). This file is among the uploads.
- Next: a uPD78C10 (uPD7810 family) core to run the panel firmware. It is a second CPU, so
  this is a substantial addition. Its instruction set is needed (not yet read).

## Panel controller firmware (uPD78C10 image), first look
- File mp3000__op_v1.0.am27c256__id0110.ic602.bin, 32768 bytes, mostly FFh padding
  (31706 of 32768 bytes).
- Vector area at 0000h: word 7A54h at 0000h, 62AAh at 0004h, 0008h, 0018h etc.
  Consistent with a reset entry at 7A54h and interrupt handlers at 62AAh.
- Needed to run it: the uPD7810/uPD78C10 instruction set and timing. Not yet available
  here. The MAME upd7810 core source (src/devices/cpu/upd7810/) or NEC's uPD7810 user's
  manual would provide it. The MAME core is GPL, so it may be uploaded and read.

## µPD7810 core (MAME upd7810.cpp, 2209 lines) - what it provides
- Framework: device reset (PC = 0, all ports and timers reset), main loop execute_run,
  interrupt handling (upd7810_take_irq), SIO (serial) send/receive with parity,
  timers 0/1 (handle_timers), port reads/writes (RP, WP, pa_w/pb_w/pc_w), ADC lines.
- NOT in this file: the instruction implementations and their opcode tables
  (s_opXX_7810, s_op48, s_op4C, s_op4D, s_op60, s_op64, s_op70, s_op74). The file
  includes upd7810.h, upd7810_macros.h and upd7810_dasm.h, which are also needed.
- Needed to continue: upd7810.h, upd7810_macros.h, and the opcode implementation file
  (MAME: upd7810ops.hxx or the equivalent), all from the same folder.

## µPD7810 headers received (upd7810.h 1472 lines, upd7810_macros.h 163 lines)
- upd7810.h declares the register state, the class, and the opcode tables as static
  members (s_opXX_7810[256], s_op48[256], s_op4C, s_op4D, s_op60, s_op64, s_op70, s_op74).
  Their definitions are NOT in these headers.
- upd7810_macros.h defines the fetch macro RDOP (opcode fetch with PC increment) and the
  register macros.
- Still needed: the file that defines the opcode tables and the instruction bodies
  (the source containing "s_opXX_7810[256] = {" and the NOP/STOP etc. functions).

## µPD7810 instruction implementations received (upd7810_opcodes.cpp, 9492 lines)
- Contains 1003 instruction functions (e.g., SLRC_A, SK_NV, illegal, illegal2), with
  each opcode's bit pattern in its comment (e.g., "48 01: 0100 1000 0000 0001").
- Does NOT contain the opcode table (s_opXX_7810[256], s_op48[256], ...) that maps each
  opcode byte to a function and its cycle count. That table is in another source file.
- Next: the file containing "s_opXX_7810[256]" and the other tables (search in the same
  folder). Without it, the opcode-to-function mapping can only be read from the bit-pattern
  comments, which is possible but error-prone.

## Panel firmware: decoded with the µPD7810 opcode table (upd7810_table.cpp)
- Table parsed: main 256 entries (151 functions), prefix sub-tables for 48,4C,4D,60,64,
  70,74. Sub-table length includes the prefix byte.
- Firmware layout: 0000 reset JMP 007A (inside the ASCII string "---<<NOV.08,1993>>---" at 0060-007B);
  code block 007D-0497 (1050 bytes); vectors at 0000-002A; interrupt pointers 62AAh
  point at padding (not code in this image).
- Linear decode of 007D-0497: 540 instructions, 147 distinct functions. Most used: JR 41,
  CALL_w 37, NOP 31, MVI_A_xx 30, RET 22, MVIW_wa_xx 21, DCRW_wa 21, MVI_B_xx 20.
  7 positions decode as illegal (0A5, 0B0, 14B, 17F, 394, 3AC, 44B): probably data or
  misalignment, to check.
- Next: transcribe the semantics of the 147 functions from upd7810_opcodes.cpp, implement
  the core, and run the code block against the serial and port model.

## Instruction semantics: first pass (µPD7810)
- Macros (upd7810_macros.h): PC/SP/A/B/C/D/E/H/L, PSW, and flag helpers ZHC_ADD and
  similar. RDOP/RDOPARG fetch bytes and advance PC. RM/WM are memory read/write.
- Overlay rule: the table's L0|L1 flags and the PSW bit L1 control some instructions.
  MVI_A_xx: if PSW has L1 set, it acts as a NOP that only skips its operand; otherwise it
  reads the operand into A and sets L1. Other instructions depend on the same overlay bits,
  so the PSW overlay state must be modelled exactly.
- JR: 1-byte form, offset = sign-extended 6 bits (OP << 2 >> 2), PC += offset.
- CALL_w: fetch 16-bit address, push PC, jump. RET: pop PC.
- Status: the most-used instructions have been identified (see the decode above). Their
  semantics are being transcribed from upd7810_opcodes.cpp. About 147 functions in total.

## Core progress (upd7810core.py)
- Implemented and verified by execution: JMP_w, LXI_S_w, MVI_A_xx (with the L1 overlay
  rule), JR (6-bit offset), CALL_w, RET, MVI_B/C, LXI_H_w (L0 overlay), LXI_D_w, LHLD_w,
  LDAW_wa, STAW_wa, MVIW_wa_xx, DCRW_wa, OFFI_A_xx, JRE, STAX_H_B, LDAX_H_B, XRA_A_C.
- The firmware runs: reset JMP -> LXI SP -> MVI A -> prefix 4D (MOV_MM_A) which is next.
- Still needed in the first region: 101 instruction bodies, extracted to
  upd7810_needed_bodies.cpp for batch transcription.

## Panel core status (upd7810core.py) - runs the firmware from reset
- Table fixed: the opcode tables now use exactly the 256 active entries (commented-out
  lines excluded). Example check: 48 D3 = DMOV_ETM1_EA.
- Overlay rule implemented: before each instruction, PSW's L0/L1 bits named in the table's
  flags are cleared. Skip rule: a set SK bit makes the next instruction consume its bytes
  without executing.
- All 68 remaining instructions of the code block are transcribed (except DAA, which
  needs its full body, and the port-read and serial-receive paths).
- Result: 60,000 steps without an unimplemented instruction.
- Current wait: the loop at 0182-01D1 polls port C (OFFI_PC_xx), which the core reads as 0.
  Next: model the external inputs (port C, port A, port B) and the serial receive flag.
- Assumption to verify: MOV_TXB_A sets INTFST at once (serial transmit timing not modelled).

## Port and serial inputs: experiment (not a model)
- With port C made to change over time (a test stimulus only), the firmware leaves its
  first wait at 0182 but enters another loop at 0195-01A5, so it waits on several inputs.
- The real inputs come from the main processor over the serial link (TE7774 channels
  2 and 3 wired to the panel's TxD/RxD, per MAME). Modelling them correctly needs both
  ends of the link: the main side's serial writes and the panel's receive timing.
- Next: model the serial link as a two-way channel between the two cores, with the
  receive bit timing from upd7810_sio_input, and the main side's TE7774 transmit path.

## Panel <-> main link (MAME driver wiring and both ROMs)
- Panel ports (MAME driver): PA in = key matrix row selected by PC output (key_row =
  ((PC^FF)>>1)&7); PB in = drum pad rows selected by PB output; PC in = dial quadrature
  bits (PC4/PC5). With no input, the core returns PA=FFh, PB=FFh, PC=00h.
- Panel TX (7810 TxD) feeds main SIO channels 2 and 3 (RX). Channel 3 RXRDY raises main
  IRQ7. Nothing from the main side is wired to the panel's RXD in the MAME driver
  (the driver is marked MACHINE_NOT_WORKING; this may be why).
- Panel start-up output observed: B0h, 00h, 00h.
- Panel idle behaviour: it loops through its key/pad scan and a table lookup (TABLE at
  01A3). No further serial output without input. This looks like correct idle behaviour.
- Main ROM serial code: init sequence for each channel (reset 00 00 00 C0, mode 02,
  command 95) at B0446-B04F0. Routine B0530 reads all four data ports (C0,C8,D0,D8) to
  clear receive flags (a flush, not message decoding).
- Next: locate the IRQ7 receive handler in the main ROM (vector 0Fh in the ICU map,
  base 08h -> interrupt vector 0Fh) and decode the message format it expects from B0h.

## Main CPU: first readable output, and where the boot waits
- LCD (HD61830-compatible, character mode): mode 3Ch (display on, cursor on/blink, internal
  CG), pitch 75h (6x8 cells), 40 chars/line, duty 3Fh (64 lines) = 40 x 8 characters.
  Screen rebuilt from the E2 (instruction) / E0 (data) stream: line 1 reads
  "MPC-3000 BIOS 11/20/95", rest blank.
- run_main.py: idle-skip harness. While the CPU is in the idle loop FB00:022E-0233 with IF set
  and nothing pending, time jumps to the next timer tick (exact: the loop has no side effects).
- 3,000 ticks (~30 s at an assumed 100 Hz): no change in IMR (F6: only IRQ0 timer and IRQ3
  enabled; IRQ7 = panel channel stays masked), no FDC commands, no new LCD output.
- Tick handler (vector 08h -> D651:0004, runs from RAM in XA mode): reads word E8 (low = FDC
  status, high = sense byte E9), masks 1800h (E9 bits 3 and 4), and on a change posts event
  codes through tables (56h/57h/58h) to the message queue (FB00:0670, DD74:0006). With two
  independent bits this looks like the two footswitch inputs rather than disk change
  (MAME's HC365 read uses bits 0 and 2 for disk change/HD). Not confirmed.
- FDC map corrected from the MAME driver: E8 = main status, EA = data FIFO, E9 = HC365 sense.
- The ROM holds the full OS 3.10: strings "Version 3.10 (", "Searching SCSI for hard disk...
  <Cancel>", "Loading files ...", "Load/Erase/Rename Files", "Save to Disk", the disk list
  "FLOPPY DISK", "SCSI DISK PART A".."Z".
- The boot stops after the banner. The only SCSI access is one read of A2 (MB89352 register 1,
  SCTL, with the 00FF umask), which returns FFh in this model.
- Next: model the MB89352 SCSI controller as present with no devices attached, so the
  "Searching SCSI" step can complete, then check the LCD for the next screen.

## Corrections and findings: what the idle main CPU is (and is not) waiting for
- CORRECTION: the single SCSI-window access (read A2, write A2 with bit 0 cleared) at
  F800:1B0C is NOT a SCSI search. It is the SCSI layer's shutdown/initialisation: it clears
  SCTL bit 0 (interrupt enable), installs a handler at vector 42h, and zeroes the flag byte
  at 06DD:0009. The boot is not blocked on the SCSI controller. An earlier note said it was.
- IRQ3 handler (F800:2057): sets byte 06DD:0009 = 1, sends specific EOI (63h). The flag is
  written twice during boot (both clears) and is never read, so nothing is waiting on it yet.
- Tick period: the tick handler adds 20000 to a 32-bit counter each tick and counter 0 loads
  20000, so the timer is a 1 MHz microsecond clock and a tick is 20 ms (50 Hz). 3,000 ticks
  is therefore 60 s of machine time, not the 30 s noted earlier.
- FB00:01C3-0234 is a task scheduler. Task entries are 0x40 bytes starting at 0264 in the
  data segment A822 (control block A822:0024). The state word at [entry] is tested as:
  bits 2/3 (0Ch) = blocked, bits 0/1/15 (8003h) = ready candidates, 4000h written when
  running. The idle loop (FB00:022E) runs when no entry is ready. (Reading inferred from
  the test instructions, not confirmed.)
- Task dump at idle: entry 0 state 8000h; entries 1-3 hold 0500h/0700h/0900h followed by
  what look like pointer tables; entries 4-23 are empty. The 0500h/0700h/0900h values do not
  look like state words, so the 0x40 stride or my reading of the entries may be wrong.
- Still unexplained: what event the OS is waiting for after the banner. Candidates: a start-up
  message from the panel (B0h 00h 00h is what the panel sends), disk detection, or a task
  that is blocked on a queue. Unmasked interrupts are IRQ0 (timer) and IRQ3 only.
- To test the panel hypothesis the main side needs the TE7774 quad UART model. The ROM's
  init writes C2h/CAh/D2h/DAh: 00 00 00 C0 02 95 (then C4/CC/D4/DCh = 00 and C6/CEh/D6/DEh
  = a value from RAM), and the flush reads data ports C0/C8/D0/D8h. The register layout is
  not known without the device source (MAME src/devices/machine/te7774.cpp and .h).

## Two CPU bugs fixed; boot now reaches the sound DSP initialisation
- BUG (fixed): C6/C7 (MOV r/m, imm) fetched the immediate BEFORE the ModRM displacement.
  For C6 45 10 01 (mov byte [di+10h],1) it wrote 10h to [di+1]. Every MOV-immediate to a
  memory operand with a displacement was wrong. Same ordering bug in C0/C1 (shift r/m, imm8).
  Both fixed and tested (value and address checked in RAM).
- Consequence: the scheduler's nesting counter (A822:0034) was never set to 1 at FB00:01EB,
  so after boot every interrupt looked nested, no rescheduling happened, and task 0 never ran.
  The earlier "missing terminator" and "~440-entry walk" findings were artefacts of this bug,
  not ROM behaviour (likely the terminator write was misdirected too; not checked directly).
- Added: RCL/RCR (shift groups 2/3), CMPS/SCAS with REPE/REPNE, PUSHA/POPA, CMC, DAA/DAS/AAA/AAS.
  Tested: rcl/rcr known values, repne scasb search, PUSHA/POPA round trip, DAA 0Fh -> 15h.
- Result: the boot runs on (863 ticks, ~17 s machine time) without going idle. It now loops at
  CB34:0118-0124 (CB458-CB464): out 60h,0; in 62h; and F000h; cmp with [bp+0Ah]<<12; repeat.
  The routine before it programs the V53 DMA unit (relocated to C000: C001-C00Ah, status C00Bh
  bit 3), so the boot is transferring data to the sound DSP and waiting for its status.
- MAME map: 0060-006F = L7A1045 (L6028 DSP-A), the sound chip. Not modelled; reads give FFh.
- Needed next: MAME's src/devices/sound/l7a1045_l6028_dsp_a.cpp and .h for the DSP register
  interface. (te7774.cpp/.h are still needed for the panel serial link.)

## Current state (packaged)
- x86cpu.py: C6/C7/C0/C1 operand-order fix; RCL/RCR; CMPS/SCAS with REPE/REPNE; PUSHA/POPA;
  CMC; DAA/DAS/AAA/AAS; SCSI SCTL (A2h) stored read/write; INTS (A8h) with reset-condition
  bit raising IRQ3 on SCMD RST (A4h bit 4) (ASSUMPTION: from datasheet memory); FDC SENSE
  DRIVE STATUS (04h) answered as not ready.
- run_main.py: idle-skip harness.
- run_dsp_stub.py: EXPERIMENT. Answers the boot's wait on DSP port 62h at CB34:011D so the
  start-up screen can be reached. NOT a sound-chip model.
- lcd_text.py: decodes the HD61830-style LCD stream into text.
- Boot result: start-up screen shows through "Searching SCSI for hard disk... <Cancel>";
  after ~330,000 ticks the CPU is at D651:0024 with no further screen change. Cause
  unknown; likely the SCSI selection timeout (not modelled).
- Needed: MAME sources for L7A1045 DSP (sound/), MB89352 (machine/), TE7774 (machine/).

## Where the boot stands (after the SCSI reset step)
- The CPU is in the scheduler's idle loop FB00:022E-0233 with IMR C4h (timer and IRQ3 enabled).
  Nothing is ready to run, so the OS waits for an event that never comes.
- Flag 06DD:0009 = 1 (set by the IRQ3 handler). Nesting counter A822:0034 = 1.
- A further IRQ3 wakes the tick path, but the only extra activity is the tick handler's
  input scan (reads of E8h from D651:003D). It is NOT a floppy wait. The screen does not change.
- Unknown: which completion event the SCSI search task is blocked on. The only SCSI
  commands seen are a reset pulse (A4h = 10h then 00h); no select command is ever issued,
  so the search never starts. This points at the MB89352 command sequence, which needs its
  datasheet or MAME source to check.

## Sound chip (L7A1045) modelled from MAME source; bypass removed
- Source: l7a1045_l6028_dsp_a.cpp (uploaded). Ports: 60h select (ch = low byte & 1Fh,
  reg = high byte & 0Fh); 62h/64h/66h voice register words (word offset 0/1/2 of a 64-bit
  register); 68h control (bit 6 = DMA start, bits 0-3 wave DMA); 6Ch atomic clear.
- Reading Start (reg 0) returns the playback address (start+pos) << 12 with the fraction in
  the low 12 bits, as MAME does. The boot waits for the low nibble of that address to reach 1.
- DMA: the chip is on DMA channel 3 of the main DMA unit (8237 layout at C000: ch3 address
  C006h, count C007h, single mask C00Ah, all-mask C00Fh, status C00Bh bit 3 = terminal count).
  The boot unmasks channel 3 with C00Fh = F7h. Each DMA request (assumed every 64 CPU steps)
  moves one word and advances voice 0's cursor.
- Result without any bypass: the boot passes the sound wait, voice 0 cursor reaches 16, the
  transfer completes, and the boot returns to the SCSI screen. run_dsp_stub.py is now unused.
- Approximations: DMA request rate (64 steps) and the word address (logical, not the
  chip's page register). Both are assumptions to check against timing.
- Still blocked: the SCSI search (MB89352 selection sequence, TE7774 panel link).

## SCSI controller (MB89352) modelled from MAME mb89352.cpp (uploaded)
- Port map: register n = (port - A0h) / 2 (MAME map 00A0-00BF, umask 00FF). So A2h = SCTL,
  A4h = SCMD, A8h = INTS, AAh = PSNS/SDGC, B6h = TEMP, BCh/BEh/C0h = TCH/TCM/TCL.
  (My earlier note that AAh was DREG was wrong; DREG is B4h.)
- Interrupts are raised only when SCTL bit 0 is set. The boot writes SCTL = 08h, so no
  interrupt comes from the reset pulse. My earlier unconditional IRQ3 on reset was wrong.
- Selection: TEMP holds the target bitmap; SCMD command 001 selects. With no target attached
  the chip sets INTS bit 2 (timeout). The timeout bit is the only one set (ASSUMPTION: the
  MAME code path for an absent target is not in the uploaded text).
- Result: the boot scans target IDs 0-6 (TEMP 81h,82h,84h,...,C0h) and times out on each.
  It repeats the scan without end and shows "Searching SCSI for hard disk... <Cancel>".
  That matches a machine with no drive: the search waits for the user to press Cancel.
- Next: the Cancel key comes from the front panel, through the panel's key matrix and
  serial link into main SIO channel 3 (IRQ7). That needs the TE7774 serial chip model
  (te7774.cpp and .h, still needed) and the key code the main ROM's IRQ7 handler expects.

## Serial chip (TE7774) modelled from te7774.cpp (uploaded); panel link wired
- Channel n block at C0h+8n: offset 0 data, 2 control1 (bit0 TX, bit2 RX enable) and status
  (bit1 RxRdy, active low), 4 control2, 6 control3. Channel 3 RX raises IRQ7 when enabled.
- link.py runs the main CPU and the panel uPD78C10 together. Panel TX bytes go to channel 3.
  Ratio of panel to main instructions is an ASSUMPTION (0.25).
- Verified: panel start-up bytes B0h 00h 00h reach channel 3 and the main side reads them.
- Key test: pressing each soft key alone (matrix rows Y3/Y4, bits 01h/02h) produced no panel
  TX. The panel never reads port A in the paths explored (no MOV_A_PA executed).
- Correction to earlier notes: the panel's "idle scan loop" is not a key scan. The key-scan
  code (MOV_PC_A at 0102h, MOV_A_PA at 0109h) is not reached from the main loop (00D3-00DC),
  which calls 00DD, 0180 and 01C6.
- The CALT jump table (0080h-00BFh per the MAME core: target = word at 80h + 2n) decodes to
  values that are not valid addresses (e.g. 64D0h, BE07h, FF01h). So either the table is
  not at 0080h in this image or the code is not reached as I assumed. Unresolved.
- Next: find the panel's real entry flow (interrupt-driven or table-driven) before the key
  path can be tested. Main-side TX to the panel (D0h/D8h writes) is never performed by the
  main ROM, so the panel cannot receive commands from it in this model.
- The earlier MAME-driver note that the boot waits for Cancel still holds: the search waits
  for the user. The Cancel soft key is one of Y3/Y4 bits 01h/02h, not yet confirmed.

## Panel key path: current findings (no Cancel yet)
- MAME driver: panel interrupts are internal only. No external interrupt line and no serial
  RXD from the main side are connected (upd78c10 txd goes to te7774 ch2/3 RX; nothing feeds
  the panel). Panel inputs are PA (key rows), PB (pads), PC (dial bits) only.
- Main ROM never writes channels 2/3 data (D0h/D8h). Only the one OUT to C8h (channel 1).
  So the main side never sends commands to the panel.
- Dial quadrature on PC4/PC5 drives the panel main loop (00D3-00E0) and produces messages
  E0h 00h 00h. Key scanning (MOV_PC_A 0102h, MOV_A_PA 0109h) was not reached in any run.
- Forced external edge (INTFE1, vector 0018h): after the interrupt the panel runs data as code
  (CALT table area). Either the handler at 0154h is not this interrupt's handler, or the
  return path is wrong. Not resolved.
- Main side: vector 0Fh (IRQ7) after the search screen points at F800:195Eh, which is an IRET
  followed by text. So the channel-3 receive handler is not installed at that vector in this
  run. IRQ7 stays masked (IMR C4h). Finding the installed handler needs the OS's
  serial-setup code, not yet traced.
- Options: (a) find the OS serial handler installation and inject Cancel's code into channel 3
  RX; (b) read the key matrix code path once the panel reaches it, which needs the panel's
  real trigger. Both need more tracing than was possible this round.

## Channel-3 key path in the BIOS (IRQ7): traced
- Vector 0Fh (IRQ7) is written three times during boot: cleared at FAC0:0052; set to F800:198Ch
  (F800:0093); set to F800:195Eh (F800:00BE, step ~198842). The last write wins.
- In XA mode, F800:195Eh fetches CF (IRET) followed by E8 5D 00 (CALL). So the IRQ7 handler is a
  return-immediately stub. Channel-3 receive interrupts do nothing in this BIOS.
- Channel 3 data (D8h) and status (DAh) are read only during init (B0530 flush). They are not
  polled during the SCSI search, so no key code is consumed there.
- The search loop reads a scheduler task list at 973D0h (linked records with pointers to D5CC),
  not a key flag.
- CAVEAT: the XA page mapping used for F800 is inferred from the boot's own page-register
  writes (PGR 3Eh for page 62). It has not been checked against the datasheet. If that mapping
  is wrong, the IRQ7 stub result could be wrong too. Verify before relying on this conclusion.
- Search duration: about 330,000 ticks (about 110 minutes of machine time at 20 ms per tick)
  with no exit. The search does not time out by itself.

## Resolved: XA mapping caveat and a dump-label error
- Datasheet (uPD70236, page registers, p.143): the top six bits of the logical address select one
  of 64 page registers (PGR0-63 at FF00h-FF7Eh); each supplies a 10-bit physical page base, giving
  a 24-bit expanded address. The emulator's XA mapping follows this.
- F800:195Eh = linear F995Eh. The ROM bytes there (mirror offset 7995Eh) are CF E8 5D 00 ...,
  i.e. IRET followed by a CALL. So the BIOS's IRQ7 handler is a return stub. Confirmed.
- ERROR in earlier notes: several listings labelled F800:195E were read from linear F895E
  (F800:095E). The IRET conclusion comes from the correct linear address F995E.
- Open: the Cancel key is not read through IRQ7 in this BIOS. Whether it is read by polling
  somewhere the search never reaches is unknown. Candidate: the tick handler's E8/E9 input
  scan (D651:003D), which reads the HC365 sense bits. Not yet checked against MAME's HC365 map.

## MILESTONE: boots to the Play/Record main screen (OS 3.10)
- Start-up flow (B000:0080-014C): banner; print "Searching SCSI for hard disk... <Cancel>"
  (DS:11F9) at row 7; call D5CC:0644 (search); if found, load files; then call C036:000A (OS).
- Search (D5CC:0644): timer 0 = [834A] * 10 (FB00:062B start, FB00:063C check, decremented at
  FB00:0608 about once per 10 ticks, i.e. 200 ms). While running: scan IDs (call 0276h), and
  check a key (D81C:000C / D808:000E); key code 75h = Cancel. Returns 1 if a disk is found.
- CORRECTION: earlier notes said the search never times out. It does. [834A] = 1Eh (30) gives
  300 units, about 60 s of machine time, which the Python CPU could not cover within one run.
  The "endless" search was compute time, not a bug.
- Test: run_to_main.py sets [834A] = 1 at search entry (a ~2 s search). The search returns 0 at
  tick ~180 and the boot calls C036:000A. The LCD then shows the Play/Record screen:
  "Play/Record", "Seq: 0-(unused)  BPM:120.0 (SEQ)", "Track Data", "Trk: 0-(unused) Type:DRUM
  On:YES", "Chn: 0A-(off) & 0A Vel%:100 Pgm:OFF", "Now:001.01.00 (00:00:00.00)".
- Cancel key code is 75h (from the search loop's compare). Its path from the panel is still open.
- Open items: panel key path; MIDI channels 0/1 (C0h/C8h); floppy disk image; audio output from
  the L7A1045; speed (Python prototype, roughly 0.2 M instructions/s vs a 32 MHz V53).

## MILESTONE: front-panel keys work end to end (Cancel at the SCSI search)
Panel CPU (uPD78C10) fixes, all from MAME upd7810.cpp / upd7810_opcodes.cpp:
- Interrupts jump to the vector address itself (not a pointer). 0018h holds JMP 0201h (the
  event-counter handler); other vectors hold EI; RETI (unused).
- Push order on interrupt was wrong: MAME pushes PSW, PCH, PCL. Fixed.
- MAME clears SK/L0/L1 in PSW on interrupt entry (saved PSW keeps them). Missing before, so
  an interrupt taken after a skip-setting instruction skipped the vector's JMP. Fixed.
- set_z helper was inverted (every "== 0" caller cleared Z on zero). Fixed; affected all
  compares, tests and skip-on-zero instructions.
- Event counter: ECNT counts every 4 cycles; ECNT == ETM1 sets INTFE1; ETMM 0Ch resets on match.
  Firmware sets ETM1 = 01F4h, ETMM = 0Ch: ~2 kHz scan interrupt. Cycles come from the opcode table.
- Pads are ACTIVE HIGH (MAME subcpu_pb_r); row select = panel port B output. AN0-AN3 read FFh.
Panel output (MIDI-style, 31250 baud): idle E0 7F 7F (bend), B0 01 7F (CC1, slider).
Keys send Note On: SOFT1 90 46 40, SOFT2 90 47 40, SOFT3 90 48 40, SOFT4 90 49 40, ENTER 90 5F 40.
Main side fixes:
- TE7774 register index bug: the bus passes index 0-3 (MAME offset & 3) but the class used byte
  offsets 0/2/4/6, so control1 was never stored. Fixed. Control1 now 95h,95h,95h,91h (ch3 RX off).
- Channel 0-2 RX -> IRQ5 (MAME intp5 merger); channel 3 RX -> IRQ7. The OS listens on channel 2
  via IRQ5 (enabled, IMR C4h). This explains the IRQ7 stub: channel 3 RX is disabled.
- link.py: panel TxD wire-ORed to channels 2 and 3; each byte takes 1280 panel cycles.
Result: with the emulated panel running and SOFT KEY 4 held during the search, the panel sends
E0 7F 7F B0 01 7F 90 49 40, the OS takes IRQ5 for each byte, and the search exits (Cancel).
Without the panel, injecting 90 49 40 on channel 2 gives the same exit and then the Play/Record
screen. No search-time shortcut is needed. SOFT KEY 4 = Cancel.

## MILESTONE: the emulated MPC3000 is operable (OS 3.10 menus respond to keys)
- BUG (fixed): LEA returned the low 16 bits of the LINEAR address (segment*16 + offset)
  instead of the offset. lea ax,[bp+6] with SS=9359 gave A924h instead of 7394h. Every LEA with
  a non-zero segment was wrong; compiled C uses LEA for pointers to locals constantly.
  Symptom: printf("%02d:%02d:%02d:%02d") (the Now: time field) parsed its width through a bad
  pointer and looped forever after the main screen appeared, so no key was ever processed.
  Tested: lea ax,[bp+6] -> 7394h, lea ax,[bx+78BFh] -> 78CFh.
- With the fix the main screen is complete: "Seq: 1-(unused)", "Sig: 4/ 4  Bars: 0",
  "Chn:OFF-(off)", "Now:001.01.00 (00:00:00:00)", soft keys "<Tk on/off> <Solo=OFF> <Track-> <Track+>".
- drive.py: boots with the panel CPU in the loop, cancels the SCSI search with SOFT KEY 4, then
  presses keys by name (KEYMAP.txt, 57 keys) and prints the LCD. Verified screens:
  Disk -> "Save to Disk / Load, Other" menu; Program/Sounds -> "Programs / Sounds" menu;
  Mixer/Effects -> "Mixer" menu; Song -> "Song Mode"; Tempo/Sync -> "Tempo"; Main Screen -> back.
- KEYMAP.txt: panel note number for every key, produced by running the panel firmware against
  MAME's key matrix (rows Y0-Y7).
- Timing: boot + Cancel to the main screen takes ~40 s in Python; each key ~10 s.
- Not done: floppy disk image (Load menu), sound output from the L7A1045 (register model only, no
  audio rendering), MIDI ports, pads (scanned but not yet tested end to end), C port and MPC X
  front end for real-time use.

## Buttons work: the OS is operable through SIO channel 2
- The OS reads the panel on channel 2 (IRQ5 handler D66E:0063), not channel 3. Protocol and
  button table in PANEL_PROTOCOL.md. Cancel = index 09 (message 90 49 7F, then 90 49 00).
- inject.py: boots to Play/Record, then injects button messages; the OS moves through 17
  different screens (Mixer, Programs/Sounds, Song Mode, Step Edit, MIDI, Save/Load, ...).
- The panel uPD78C10 is not needed for input in an MPC X front end: send the messages directly.

## C port (c/mpc3k.c): lockstep-identical, ~8x real time on the build PC
- Line-for-line port of x86cpu.py (CPU, ICU, TCU, FDC, DSP, DMA, SCSI, SIO, LCD) and run_main's
  idle-skip. Fixed in both: IMUL r,r/m,imm8 (6Bh) sign extension (Python masking bug).
- Lockstep: identical register/flag/ICU/timer state every 1,000 steps for 3 M steps, and every
  50,000 steps for 16 M steps with the short search (through SCSI search and OS start).
- Speed: 30-33 M instructions/s on the build PC = 7.4-8.3x real time. Full unshortened boot (60 s
  SCSI search) reaches Play/Record in under 20 s of wall time. MPC X speed not measured.
- Buttons: --press KK@STEP injects channel-2 panel messages; Mixer (50h), Programs/Sounds (51h)
  and MIDI (5Ah) each open from the main screen, as in Python.
- Build: c/Makefile (native, mpcx-arm static ARMv7), .github/workflows/build-mpc3k.yml (untested).
- Next: MPC X front end (framebuffer LCD, pads/buttons -> 90 kk vv on channel 2, dial -> E0),
  modelled on the MPC4000 front end; then sound, disk and MIDI.

## MPC X front end (c/mpc3k_mpcx.c) - tested on a virtual framebuffer, not yet on the MPC X
- Display, DRM, rotation, touch, MIDI parsing and font copied unchanged from the MPC4000 front end.
- LCD: 40 x 8 characters at 5x scale (1200 x 320 landscape), HD61830 cursor drawn as an underline on
  cell line 7 at the cursor address, blinking. Touch: SOFT 1-4, MAIN, ENTER, -, +, EXIT; tapping the
  LCD's bottom row also presses the soft key under it.
- Input: MPC X pads (ch 10) -> 90 pp vv (pad order unverified); data wheel (CC 100) -> + / - presses
  (approximation; the OS's E0 dial message is not decoded); buttons via --mpc4k-map (the user's
  MPC4000 map, translated by name) and/or --buttons (MPC3000 names or 0xNN indexes).
- Button indexes confirmed by test this round: soft keys 06-09, cursor up 22 / down 2B / left 2A /
  right 2C, STOP 30, PLAY 33, plus the screen keys. Likely but unconfirmed: MAIN 2D, REC 34,
  OVER DUB 35, STEP > 32; 2F and 38 reset the position (probably << / start).
- Test: virtual framebuffer + raw MIDI note 60 with a map line "60 PROGRAM" opened Programs/Sounds.
- launcher/mpc3k-run.sh: stops acvs.service, runs, always restarts it. EPROMs in /data/mpc3k.
- Real-time pacing holds machine time (timer ticks x 20 ms) to wall time.

## Sound chip rendering (c/mpc3k.c), following MAME l7a1045_l6028_dsp_a.cpp
- 32 voices, 32 MB sample RAM (16-bit words, 24-bit word address; MAME dsp_map 0-1FFFFFF RAM),
  clock 33.8688 MHz -> 44.1 kHz. Register decode as MAME: start/type, end/rate (4.12), loop
  (length or absolute), volume envelope (start, target, 8.8 rate), Chamberlin low-pass with envelope
  and resonance, left/right volume, key-on = control bit 8, key-off = 0 written to reg 3 offset 0.
- Differences from MAME: linear interpolation (MAME: 8-point windowed sinc); delay effect and the
  individual outputs are not rendered.
- DMA: control bit 5 selects direction. Write (004Fh): memory -> sample RAM at voice 0 start+pos.
  Read (006Fh): sample RAM -> memory. The earlier model always wrote, which was wrong for 006Fh.
- Audio time base: 441 samples per 40,000 instructions (the 4 instructions/us TCU assumption);
  idle-skip jumps render the skipped samples too. --wav FILE writes the stereo mix.
- Result: boot uploads 40 words by DMA (31 non-zero at word 10000h). The OS writes all 32 voices at
  boot (49 writes each, 74 on channel 2) and never keys a voice, before or during recording.
  So the output is silent: no sounds are loaded, and the metronome does not sound in this state.
- Lockstep with the Python model no longer applies after this change (register readbacks of
  envelope and filter state now follow MAME). The lockstep-checked core is kept as
  c/mpc3k_before_audio.c.
- From the MAME driver comment on the HC365 sense byte (E9h): bit 0 disk change, bits 1/2 density,
  bits 3/4 footswitches. This confirms the earlier footswitch reading of the tick handler.
- Next: load sounds. Options: floppy (uPD72069 READ DATA on a 720K/1.44M image) or SCSI disk
  (MB89352 transfer phases + a disk image). Either is needed before any sound can be heard.

## Floppy boot works: OS 3.39.3 (Track Mute Edition) loads from a 1.44 MB image and runs
Changes in c/mpc3k.c this round (the lockstep-checked core is c/mpc3k_before_audio.c):
- DMA unit rewritten to the uPD71071 register map (V53 datasheet figs 93-98): 0 init, 1 channel
  select (bit 2 base-only), 2-3 count, 4-6 24-bit physical address, 8-9 device control, A mode
  (TDIR bits 3-2, word bit 0, auto-init bit 4, decrement bit 5), B status (TC, clear on read),
  F mask. Physical addresses (no page registers). The old 8237 guess read sound-chip DMA data from
  address 003Ah; now the boot's memory-sizing patterns reach sample RAM (1001h words per block).
- Sound-chip DMA direction now comes from the DMA mode register (MAME dma_r16/dma_w16).
- Timer: all three counters run (1 MHz machine time), with latch command and LSB/MSB reads;
  counter n raises IRQn. The BIOS floppy delay loop uses counter 2.
- Floppy controller: uPD765 command subset on a raw image (SPECIFY, SENSE DRIVE STATUS,
  RECALIBRATE, SEEK, SENSE INTERRUPT, READ ID, READ DATA with EOT and TC, DMA and non-DMA);
  uPD72069 auxiliary commands on E8 (36h reset; others return one byte, which the BIOS ignores).
  DMA TC is delivered as an event from channel 1's count underflow (the sticky DST bit cut
  multi-sector reads after one sector).
- HC365 sense polarity follows the MAME driver comment, not its code: bit 0 = 1 (not ejected),
  bit 1 = 1, bit 2 = 0 for HD. Only this polarity was tried with a successful load.
- Result: with OS_3.39.3_Beta_Track_Mute_Edition.img inserted, the BIOS shows "Loading files...",
  loads M3K316.SYS (an MZ executable, 0x7A00-byte header, 7355 relocations) to segment 7000 with
  pages 2C-3C remapped from ROM to RAM, and starts it. The banner becomes "OS 3.39.3 Beta Track
  Mute Edition 10/11/2026", the OS runs its own SCSI search and reaches Play/Record. 34.5 M
  instructions/s, 8.6x real time.
- REP string instructions count as one instruction of machine time (real V53 timing is longer).
- Front end and launcher accept --floppy / MPC3K_FLOPPY / /data/mpc3k/floppy.img.
- Next: sounds. A floppy image with MPC3000 sound files, then key-on and audio output.

## First sound: a .SND loaded from floppy plays through the emulated sound chip
- Floppy WRITE DATA added (DMA memory->FDC on channel 1, and non-DMA); --floppy-out writes the image.
- The .SND format was learned from the OS: load test file -> "Save a Sound" -> read back, plus single-
  byte probes on the Edit Sound screen. See SND_FORMAT.md. tools/: make_floppy.py (FAT12 image),
  read_floppy.py (list/extract), wav2snd.py (WAV -> .SND).
- Load screen lists files and shows Free(snd):32636K (the 32 MB sample RAM is detected).
- Test: SINE.SND (440 Hz, 0.5 s fade) loaded, assigned to note 37/A01 (Program/Sounds -> 1,
  cursor down, down, left, +), auditioned with <Play>: 1 key-on, 440.0 Hz, 0.491 s, falling envelope.
  mpc3000_first_sound.wav is that output.
- Pads (90 pp vv, pp 0-3) produced no key-on in this state: open item (pad number mapping, or the
  context in which pads trigger).
- Boot timing: with the timer and DMA changes the main screen is ready later; button presses
  before ~30 M instructions are lost.

## Pads and audio output work end to end (front end, tested on this PC)
- Pads: the panel numbers pads from the top row (table D66E:0053): panel 0-3 = A13-A16 ...
  0C-0F = A01-A04. Tested: panel pad 0Ch = A01 keyed the assigned sound. The OS's default pad
  notes (A01=37, A02=36, A03=42, A04=82, ...) equal the MPC X pad notes, so MPC X pad n = A0n.
  Front end pad mapping fixed accordingly (earlier "order unverified").
- Audio out: the MPC4000 front end's raw-ALSA code (S32_LE, outputs 1-2, ring buffer, optional
  96 kHz resampling, --audio-test tone) is reused unchanged; the sound chip's mix feeds it.
- End-to-end test in mpc3k_mpcx (virtual framebuffer, audio to file): load SINE.SND from floppy,
  assign to A01, MAIN, then MIDI note 37 on channel 10 (MPC X pad 1): 440.0 Hz for 0.490 s.
- tools/wavs_to_floppy.py: WAV files -> a 1.44 MB floppy of .SND sounds.
- Front end test options: --press KK@STEP, --midi-at NOTE@STEP, --audio-file F (raw stereo 16-bit).
- Not yet on the MPC X hardware.

## Programs (.PGM) and voice timing
- .PGM format read back from an OS-saved program: see PGM_FORMAT.md. tools/make_kit.py builds a kit
  floppy (up to 16 WAVs -> .SND + .PGM on pads A01-A16) from tools/template.pgm. Loading only the .PGM
  loads its sounds and assignments.
- Voice end: the OS writes end address 0 (no end) and silences each voice itself by writing 0 to
  register 3 offset 0 after the sound's duration (as MAME's source notes). That duration is timed with
  the V53 timer, so the timer rate decides sound length.
- Timer clocks from TCKS (FFF0h): boot value 12h = prescaler /16 (32 MHz -> 2 MHz) for counters 0/1,
  TCLK (4 MHz, MAME) for counter 2. The earlier model ran all counters at 1 MHz, so voices stopped
  about 2x late (0.927 s for a 0.5 s sound) and ran on into the next sample in memory.
  Now: key-off at 0.465 s for a 0.5 s sound (A01 440 Hz 0.464 s, A02 660 Hz 0.462 s). The remaining 7%
  is unexplained (OS rounding or a small timing error).
- Counter 0 therefore ticks every 10 ms (not 20 ms as noted earlier); the "us counter" in the tick
  handler adds timer counts.
- Machine time is now instructions / 4 M (machine_seconds()), used for real-time pacing and reports.
- The timer runs on deadlines (counters sync when read, written or due), same results, ~35 M instr/s.
- Test tool: the CLI accepted only 16 --press events and dropped the rest silently; now 255.

## Kit building on the device (C, in mpc3k_mpcx: --make-kit SOURCE OUT.img)
- c/mpc3k_kit.c: .xpm (MPC X drum program; first active layer of instruments 1-16 -> A01-A16; XML,
  sample search and name helpers copied unchanged from the MPC4000 converter) or a folder of WAVs
  (first 16 by name). WAV: 8/16/24/32-bit PCM and 32-bit float, extensible, any channels/rate ->
  mono 16-bit 44.1 kHz. Program from the embedded OS-saved template (pgm_template.h).
  Fits a 1.44 MB floppy by trimming the longest samples first (10 ms fade).
- Naming: the OS names a loaded sound after its file (the header name is ignored) and finds a
  program's sounds as <name>.SND, so one DOS-safe name (<= 8 chars, A-Z 0-9 _ -) is used for the
  file, the header and the program's name table. (A first version stripped "_" from file names
  only, and the sounds did not load.)
- Tested in the emulator: WAV folder (16-bit mono, 24-bit stereo, float 48 kHz) -> A01-A03 play
  440/660/880 Hz; .xpm with an inactive layer and a sample in a subfolder -> A01 440 Hz, A02 empty,
  A04 880 Hz.
- launcher/mpc3k-make-kit.sh (--find, .xpm or folder -> /data/mpc3k/floppy.img), QUICKSTART_MPCX.md.

## SCSI hard disk: the OS formats it, and kits load from it
- MB89352 host rewritten after MAME (selection -> command phase; bytes via DREG or, as the OS does,
  DMA channel 0 with SCMD Transfer bit 2 = 0) plus a hard-disk target written for this emulator
  (TEST UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY, READ/WRITE 6/10, MODE SENSE/SELECT, FORMAT,
  START/STOP, VERIFY; CHECK CONDITION otherwise). --hd FILE attaches it as ID 0.
- Fixes found against the OS: SSTS must be live (a stale TC-zero bit made the OS think a 6-byte DMA
  had finished); the target must not report "disconnected" until the OS next reads the bus lines
  (INTS bit 20h during the message-in transfer counts as an error, code 103h in the OS driver).
- Boot with a blank disk: TEST UNIT READY, READ CAPACITY, READ block 0 (unformatted). DISK -> 9 ->
  3 "Format a SCSI hard disk" -> FORMAT UNIT, then partitions "of up to 30 mbytes": 64 MB -> 3 x 22 MB.
- Partition format: contiguous DOS volumes, boot sector OEM "MPC-60", 512 B sectors, 16-sector
  clusters, 2 FATs of 11 sectors, 512 root entries, media F8. FAT entries 2-3 pre-used by the OS.
  The OS reads the FAT as FAT12 (FAT16 chains made it read clusters of other files).
- Test: kit in partition A (tools/hd_add.py, then the C builder) -> load .PGM from SCSI DISK PART A
  (<Load>, then <Do it>) -> A01-A03 play 441/661/879 Hz for 0.465 s each. A kit also written to B.
- --make-kit SRC DISK.img [--part X] adds to a formatted disk (budget = partition free space).
  Launcher creates /data/mpc3k/hd.img (sparse, 256 MB) and passes --hd; mpc3k-make-kit.sh targets it
  once it is formatted.
