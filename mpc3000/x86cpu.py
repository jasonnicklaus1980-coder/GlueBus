"""Minimal 8086/V53 interpreter for the MPC3000 boot ROM (first milestone).
Covers the instructions the reset and start-up code uses. Unknown opcodes stop
the run and are reported. Devices are stubs defined in DEVICES.md."""
import struct

class ICU8259:
    """V53 on-chip interrupt controller (uPD71059 = 8259-compatible). Contiguous 8-bit
    addressing: base+0 = A0 0 (ICW1/OCW2/OCW3), base+1 = A0 1 (ICW2-4/OCW1)."""
    def __init__(self):
        self.init_stage = 0; self.icw = [0]*4; self.imr = 0xFF
        self.irr = 0; self.isr = 0; self.need_icw4 = False
    def write(self, a0, v):
        if a0 == 0 and (v & 0x10):                 # ICW1
            self.init_stage = 1; self.need_icw4 = bool(v & 1); self.imr = 0; self.irr = 0; self.isr = 0
        elif a0 == 0 and (v & 0x18) == 0:          # OCW2 (EOI handling)
            if (v & 0xE0) == 0x20:                 # non-specific EOI: clear highest in-service
                for i in range(8):
                    if self.isr & (1 << i): self.isr &= ~(1 << i); break
            elif (v & 0xE0) == 0x60:               # specific EOI: clear the named level (v & 7)
                self.isr &= ~(1 << (v & 7))
        elif a0 == 1 and self.init_stage == 1:     # ICW2 vector base
            self.icw[1] = v & 0xF8; self.init_stage = 2 if self.need_icw4 else 0
        elif a0 == 1 and self.init_stage == 2:     # ICW4
            self.icw[3] = v; self.init_stage = 0
        elif a0 == 1:                              # OCW1 interrupt mask
            self.imr = v
    def read(self, a0):
        return self.imr if a0 else self.irr
    def raise_irq(self, n): self.irr |= (1 << n)
    def pending(self):
        ready = self.irr & ~self.imr & ~self.isr
        for i in range(8):
            if ready & (1 << i): return i
        return None
    def acknowledge(self, n):
        self.irr &= ~(1 << n); self.isr |= (1 << n)
        return (self.icw[1] | n) & 0xFF

class TCU8254:
    """Timer unit (8254-style): counter 0 = mode 2 rate generator driving IRQ0.
    Counts one step per 4 CPU instructions (clock ratio assumed, to be verified)."""
    def __init__(self, icu):
        self.icu = icu; self.ctl = [0,0,0]; self.load = [0,0,0]; self.count = [0,0,0]
        self.phase = [0,0,0]; self.mode = [0,0,0]; self.lsb_msb = [0,0,0]; self.tick_n = 0
    def write(self, k, v):
        if k == 3:                                  # control word
            c = (v >> 6) & 3
            if c == 3: return
            self.mode[c] = (v >> 1) & 7; self.lsb_msb[c] = (v >> 4) & 3; self.phase[c] = 0
            return
        c = k
        if self.lsb_msb[c] == 1:  self.load[c] = v & 0xFF; self.count[c] = self.load[c]
        elif self.lsb_msb[c] == 2: self.load[c] = (v & 0xFF) << 8; self.count[c] = self.load[c]
        elif self.lsb_msb[c] == 3:
            if self.phase[c] == 0: self.load[c] = (self.load[c] & 0xFF00) | (v & 0xFF); self.phase[c] = 1
            else: self.load[c] = (self.load[c] & 0x00FF) | ((v & 0xFF) << 8); self.phase[c] = 0; self.count[c] = self.load[c]
    def read(self, k): return self.count[k] & 0xFF
    def step(self, n):
        self.tick_n += n
        if self.tick_n < 4: return
        ticks = self.tick_n // 4; self.tick_n %= 4
        if self.load[0] and self.mode[0] in (2, 3):
            self.count[0] -= ticks
            while self.count[0] <= 0:
                self.count[0] += self.load[0] or 0x10000
                self.icu.raise_irq(0)

class FDC765:
    """uPD72069 (uPD765 family), no disk inserted. Ports E8h status / EAh data (MAME map).
    Commands are collected byte by byte; results are queued for reading."""
    PARAMS = {0x03: 2, 0x04: 1, 0x07: 1, 0x08: 0, 0x0F: 2, 0x46: 8, 0x66: 8, 0x4A: 1, 0x0D: 5,
              0x45: 8, 0x05: 8, 0x06: 8, 0x0A: 1}
    def __init__(self, icu):
        self.icu = icu; self.cmd = []; self.need = 0; self.result = []; self.log = []; self.last_cmd_param = 0
    def status(self):
        if self.result: return 0xD0          # RQM | DIO (controller to CPU) | CB
        return 0x80                          # RQM, ready for a command
    def write_data(self, v):
        if not self.cmd:
            op = v & 0x1F if (v & 0x1F) in self.PARAMS else v
            self.log.append(v)
            self.need = self.PARAMS.get(v & 0x1F, self.PARAMS.get(v, 0))
            self.cmd = [v]
            if self.need == 0: self._execute()
            return
        self.cmd.append(v); self.need -= 1; self.last_cmd_param = v
        if self.need == 0: self._execute()
    def _execute(self):
        op = self.cmd[0] & 0x1F
        self.cmd = []
        if op == 0x04:                       # SENSE DRIVE STATUS: ST3 with no disk (ready bit 5 clear, drive unit in bits 0-1)
            self.result = [0x00 | (self.last_cmd_param & 3)]
        elif op == 0x08:                     # SENSE INTERRUPT STATUS: ST0 (no disk: not ready), PCN
            self.result = [0x48, 0x00]
        elif op in (0x07, 0x0F):             # RECALIBRATE / SEEK: completes with interrupt, error
            self.icu.raise_irq(3)
        elif op in (0x06, 0x46, 0x66, 0x4A, 0x05, 0x45):   # data/ID commands: abnormal termination
            self.result = [0x48, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]
            self.icu.raise_irq(3)
        else:
            self.result = [0x80]             # invalid command
    def read_data(self):
        if self.result: return self.result.pop(0)
        return 0x00


class DSP_L7A1045:
    """Sound chip (MAME l7a1045_l6028_dsp_a.cpp), register file and DMA cursor only.
    Ports 60h select (ch low byte, reg high byte); 62h/64h/66h voice registers (16-bit words of a
    64-bit register); 68h control; 6Ch atomic clear. Voice 0's cursor is advanced by DMA words."""
    def __init__(self, dma):
        self.dma = dma
        self.cur_ch = 0; self.cur_reg = 0
        self.regs = [[0]*32 for _ in range(16)]
        self.control = 0
        self.frac = [0]*32; self.pos = [0]*32; self.start = [0]*32; self.stype = [0]*32
    def select_w(self, v):
        self.cur_ch = v & 0x1F; self.cur_reg = (v >> 8) & 0x0F
    def voiceregs_r(self, port):
        off = (port - 0x62) // 2
        ch = self.cur_ch
        if self.cur_reg == 0:                              # Start: read back the playback position
            cur = (self.start[ch] + self.pos[ch]) & 0xFFFFFFFF
            r = self.regs[0][ch] & 0xFFF000000000
            r |= (cur << 12) & 0xFFFFFFFFF000; r |= self.frac[ch] & 0xFFF
            self.regs[0][ch] = r
        return (self.regs[self.cur_reg][ch] >> (off * 16)) & 0xFFFF
    def voiceregs_w(self, port, v):
        off = (port - 0x62) // 2; ch = self.cur_ch; r = self.cur_reg
        mask = [0xFFFFFFFF0000, 0xFFFF0000FFFF, 0x0000FFFFFFFF][off]
        self.regs[r][ch] = (self.regs[r][ch] & mask) | ((v & 0xFFFF) << (off * 16))
        if r == 0:                                         # sample start address and type, as MAME
            self.start[ch] = (self.regs[0][ch] >> 12) & 0xFFFFFF
            self.stype[ch] = (self.regs[0][ch] >> 36) & 0xF
            self.pos[ch] = 0; self.frac[ch] = 0
    def control_w(self, v):
        self.control = v & 0xFFFF
    @property
    def dma_running(self): return bool((self.control >> 6) & 1)
    def atomic_w(self, v):
        self.regs[self.cur_reg][self.cur_ch] = 0
    def dma_word(self, data):                              # MAME dma_w16_cb: write a word at start+pos, pos++
        ch = 0
        self.pos[ch] += 1                                  # cursor advances per DMA word (voice 0)
    def dma_tick(self):
        if self.dma_running: self.dma.request(3)

class DMA8237:
    """Main processor DMA unit (8237-compatible register layout, as the boot programs it).
    Channel 3 = sound chip (MAME). Only the parts the boot uses: address, mask and word transfers."""
    def __init__(self):
        self.addr = [0]*4; self.count = [0]*4; self.mask = 0x0F; self.bus = None; self.dsp = None
        self.tc = 0                                        # terminal-count flags, reported in status bits 0-3
    def request(self, ch):
        if (self.mask >> ch) & 1: return                  # masked: no transfer
        if ch == 3 and self.dsp is not None:
            word = self.bus.rd16(self.addr[3])
            self.dsp.dma_word(word)
            self.addr[3] = (self.addr[3] + 2) & 0xFFFF
            if self.count[3] == 0:
                self.tc |= 1 << 3                          # terminal count reached
            else:
                self.count[3] = (self.count[3] - 1) & 0xFFFF

class MB89352:
    """Fujitsu MB89352A SCSI protocol controller, following MAME mb89352.cpp (uploaded).
    Port A0h+2n = register n (MAME map 00A0-00BF, umask 00FF). No SCSI target is attached,
    so selection times out. IRQ goes to the interrupt merger (INTP3 -> ICU IRQ3) only when
    SCTL bit 0 (interrupt enable) is set."""
    def __init__(self, irq):
        self.irq = irq
        self.bdid = 0; self.sctl = 0; self.scmd = 0; self.ints = 0; self.temp = 0
        self.tc = 0; self.pctl = 0; self.ssts = 0x05; self.line = 0; self.target = None
        self.reset_and_disable = 1; self.int_enable = 0; self.busfree_int = 0; self.log = []
    def _irq(self):
        if self.int_enable: self.irq(1)
    def read(self, n):
        n &= 0x0F
        if n == 0: return 1 << self.bdid
        if n == 1:
            v = 0
            v |= 0x80 if self.reset_and_disable else 0
            v |= 0x40 if (self.sctl & 0x40) else 0
            v |= self.sctl & 0x1F
            return v
        if n == 2: return self.scmd
        if n == 3: return 0xFF
        if n == 4: return self.ints
        if n == 5: return self.line
        if n == 6: return self.ssts
        if n == 7: return 0
        if n == 8: return (self.line & 0x07) | (0x80 if self.busfree_int else 0)
        if n == 0x0B: return self.temp
        if n == 0x0C: return (self.tc >> 16) & 0xFF
        if n == 0x0D: return (self.tc >> 8) & 0xFF
        if n == 0x0E: return self.tc & 0xFF
        return 0xFF
    def write(self, n, v):
        n &= 0x0F; v &= 0xFF
        self.log.append((n, v))
        if n == 0: self.bdid = v & 7; self.ssts &= ~0x40; self.ssts |= 0x80
        elif n == 1:
            if v & 0x80:
                self.reset_and_disable = 1; self.int_enable = 0; self.ints = 0
            else: self.reset_and_disable = 0
            self.sctl = v; self.int_enable = v & 1
        elif n == 2:
            self.scmd = v
            if v & 0x10:                                   # RST Out: bus reset condition
                self.ints |= 0x01; self._irq()
            cmd = (v >> 5) & 7
            if cmd == 0:                                   # bus release
                self.line = 0; self.ssts &= ~0x30
                if self.busfree_int: self._irq()
            elif cmd == 1:                                 # selection: target from TEMP
                t = self.temp & ~(1 << self.bdid)
                self.target = t.bit_length() - 1 if t else None
                self.ints |= 0x10 if False else 0
                self.ints |= 0x04                          # no target attached: timeout bit (INTS bit 2)
                self._irq()
            elif cmd in (2, 3):                            # reset / set ATN
                self.line = (self.line & ~0x02) if cmd == 2 else (self.line | 0x02)
        elif n == 4: self.ints &= ~v                       # write 1 to clear
        elif n == 8:
            self.pctl = v; self.busfree_int = v & 0x80
        elif n == 0x0B: self.temp = v
        elif n == 0x0C: self.tc = (self.tc & 0xFFFF) | (v << 16)
        elif n == 0x0D: self.tc = (self.tc & 0xFF00FF) | (v << 8)
        elif n == 0x0E: self.tc = (self.tc & 0xFFFF00) | v

class TE7774:
    """Quad UART (MAME te7774.cpp, uploaded). Channel n block at C0h+8n, register r at +2r:
    r0 data, r1 control1 (bit0 TX enable, bit2 RX enable) / status read, r2 control2, r3 control3.
    Status bit 1 (RxRdy) is active-low in MAME: cleared when a byte is received.
    Channel 3 RX raises the main processor's IRQ7 (rxrdy handler, MAME)."""
    def __init__(self, irq7, irq5=None):
        self.irq7 = irq7; self.irq5 = irq5
        self.rx = [0]*4; self.rx_ready = [False]*4
        self.ctl1 = [0]*4; self.ctl2 = [0]*4; self.ctl3 = [0]*4
        self.tx_log = [[] for _ in range(4)]
    def rx_enabled(self, ch): return bool(self.ctl1[ch] & 0x04)
    def tx_enabled(self, ch): return bool(self.ctl1[ch] & 0x01)
    def receive(self, ch, byte):                       # byte arrives from the attached device
        self.rx[ch] = byte & 0xFF
        self.rx_ready[ch] = True
        if self.rx_enabled(ch):
            if ch == 3: self.irq7(True)
            elif self.irq5 is not None: self.irq5(True)      # MAME: rxrdy 0-2 -> intp5 merger -> IRQ5
    def read(self, ch, off):
        if off == 0:                                   # data: read clears RxRdy and the IRQ
            v = self.rx[ch]; self.rx_ready[ch] = False
            if ch == 3: self.irq7(False)
            return v
        if off == 1:                                   # status: bit0 TxRdy, bit1 RxRdy (1 = empty), MAME te7774
            return 0x01 | (0x00 if self.rx_ready[ch] else 0x02)
        if off == 2: return self.ctl2[ch]
        if off == 3: return self.ctl3[ch]
        return 0xFF
    def write(self, ch, off, v):
        v &= 0xFF
        if off == 0: self.tx_log[ch].append(v)        # transmit (no attached device on the main side)
        elif off == 1: self.ctl1[ch] = v              # register index (MAME read_cs/write_cs offset & 3)
        elif off == 2: self.ctl2[ch] = v
        elif off == 3: self.ctl3[ch] = v

class Bus:
    """Memory and I/O as in MAME's mpc3000 driver (src/mame/akai/mpc3000.cpp):
    ROM at 00000-7FFFF, mirrored at 80000-FFFFF; RAM 300000-3FFFFF; NVRAM 500000-500FFF.
    Expanded (XA) mode uses the 24-bit physical address from the page registers.
    Unmapped I/O reads return FFh and writes are ignored."""
    def __init__(self, rom):
        self.rom = bytes(rom)                  # 512 KB boot ROM image
        self.ram = bytearray(0x100000)         # 1 MB at 300000h
        self.nvram = bytearray(0x1000)         # 4 KB at 500000h
        self.pgr = [0]*64                      # page registers PGR0-63, I/O FF00h, FF02h, ...
        self.xa = 0                            # XAM register bit 0 (I/O FF80h)
        self.io_log = []
        self.ticks = 0
        self.icu = ICU8259(); self.tcu = TCU8254(self.icu); self.fdc = FDC765(self.icu); self.hc365 = 0x00
        self.dma = DMA8237(); self.dma.bus = self; self.dsp = DSP_L7A1045(self.dma); self.dma.dsp = self.dsp; self.dsp_div = 0
        self.te = TE7774(lambda on: self.icu.raise_irq(7) if on else None, lambda on: self.icu.raise_irq(5) if on else None)
        self.spc = MB89352(lambda on: self.icu.raise_irq(3) if on else None)
        self.spc_sctl = None          # MB89352 SCTL (register 1, I/O A2h): read/write storage; None until first written
        self.spc_ints = 0             # MB89352 INTS (register 4, I/O A8h): bit0 = reset condition. Semantics from memory of the datasheet (ASSUMPTION)
        self.spc_log = []             # (port, value) writes to the SCSI window, in order
        self.ulas = {'IULA': 0, 'TULA': 0, 'OPHA': 0}
    def periph_base(self): return self.ulas['OPHA'] << 8
    def phys(self, a):
        if self.xa:
            return ((self.pgr[(a >> 14) & 0x3F] & 0x3FF) << 14) | (a & 0x3FFF)
        return a & 0xFFFFF
    def _read_phys(self, p):
        if p < 0x100000: return self.rom[p & 0x7FFFF]
        if 0x300000 <= p < 0x400000: return self.ram[p - 0x300000]
        if 0x500000 <= p < 0x501000: return self.nvram[p - 0x500000]
        return 0xFF
    def _write_phys(self, p, v):
        if 0x300000 <= p < 0x400000: self.ram[p - 0x300000] = v & 0xFF
        elif 0x500000 <= p < 0x501000: self.nvram[p - 0x500000] = v & 0xFF
        # ROM and unmapped areas ignore writes
    def rd8(self, a):
        return self._read_phys(self.phys(a & (0xFFFFFF if self.xa else 0xFFFFF)))
    def wr8(self, a, v):
        self._write_phys(self.phys(a & (0xFFFFFF if self.xa else 0xFFFFF)), v)
    def rd16(self, a): return self.rd8(a) | (self.rd8(a+1) << 8)
    def wr16(self, a, v): self.wr8(a, v & 0xFF); self.wr8(a+1, (v >> 8) & 0xFF)
    def dsp_clk(self):
        self.dsp_div += 1
        if self.dsp_div >= 64:
            self.dsp_div = 0; self.dsp.dma_tick()
    def io_read(self, port, size):
        port &= 0xFFFF
        self.ticks += 1
        if 0xC0 <= port <= 0xDF:
            ch = (port - 0xC0) >> 3; off = (port & 7) >> 1
            if not (port & 1):
                v = self.te.read(ch, off); self.io_log.append(('R', port, v)); return v
        if 0xA0 <= port <= 0xBF and not (port & 1):
            v = self.spc.read((port - 0xA0) >> 1); self.io_log.append(('R', port, v)); return v
        if port == 0xC00B:                                 # DMA status: TC flags, read clears them
            v = self.dma.tc; self.dma.tc = 0; self.io_log.append(('R', port, v)); return v
        if 0xC000 <= port <= 0xC00F and port != 0xC00F:
            v = 0; self.io_log.append(('R', port, v)); return 0x00
        if port == 0xC00F:
            v = self.dma.mask; self.io_log.append(('R', port, v)); return v
        if port in (0x62, 0x64, 0x66):
            v = self.dsp.voiceregs_r(port); self.io_log.append(('R', port, v)); return v
        pb = self.periph_base()
        if pb + self.ulas['IULA'] <= port <= pb + self.ulas['IULA'] + 1:
            v = self.icu.read(port - (pb + self.ulas['IULA']))
            self.io_log.append(('R', port, v)); return v
        if pb + self.ulas['TULA'] <= port <= pb + self.ulas['TULA'] + 3:
            v = self.tcu.read(port - (pb + self.ulas['TULA']))
            self.io_log.append(('R', port, v)); return v
        if port == 0xA8:
            v = self.spc_ints; self.io_log.append(('R', port, v)); return v
        if port == 0xA2 and self.spc_sctl is not None:
            v = self.spc_sctl; self.io_log.append(('R', port, v)); return v
        if port == 0xE8:                                   # word E8: low = FDC main status, high = HC365 sense (E9)
            v = self.fdc.status() | (self.hc365 << 8)
        elif port == 0xE9: v = self.hc365                  # HC365 sense byte (bits 3/4 tested by the ROM's tick handler)
        elif port == 0xEA: v = self.fdc.read_data()        # FDC data/FIFO (MAME: 00E8-00EB, umask 00FF -> offsets 0,1 at E8,EA)
        elif port == 0xE2: v = 0x00              # LCD controller (HD61830) status: not busy (MAME 00E2)
        elif port == 0xE0: v = 0x00            # LCD data read
        elif port == 0xFF80: v = self.xa & 1
        elif 0xFF00 <= port <= 0xFF7E and port % 2 == 0:
            v = self.pgr[(port - 0xFF00) // 2]
        else:
            v = 0xFF                           # unmapped I/O reads as FF (as in MAME)
        self.io_log.append(('R', port, v))
        return v & (0xFFFF if size == 2 else 0xFF)
    def io_write(self, port, v, size):
        port &= 0xFFFF
        if 0xC0 <= port <= 0xDF and not (port & 1) and not (0xC000 <= port <= 0xC00F):
            ch = (port - 0xC0) >> 3; off = (port & 7) >> 1
            self.te.write(ch, off, v & 0xFF); self.io_log.append(('W', port, v)); return
        if 0xA0 <= port <= 0xBF and not (port & 1):
            self.spc.write((port - 0xA0) >> 1, v & 0xFF); self.io_log.append(('W', port, v)); return
        if port == 0x60: self.dsp.select_w(v)
        elif port in (0x62, 0x64, 0x66): self.dsp.voiceregs_w(port, v)
        elif port == 0x68: self.dsp.control_w(v)
        elif port == 0x6C: self.dsp.atomic_w(v)
        elif port == 0xC000: self.dma.addr[0] = v & 0xFFFF
        elif port == 0xC002: self.dma.addr[1] = v & 0xFFFF
        elif port == 0xC004: self.dma.addr[2] = v & 0xFFFF
        elif port == 0xC006: self.dma.addr[3] = v & 0xFFFF
        elif port == 0xC001: self.dma.count[0] = v & 0xFFFF
        elif port == 0xC003: self.dma.count[1] = v & 0xFFFF
        elif port == 0xC005: self.dma.count[2] = v & 0xFFFF
        elif port == 0xC007: self.dma.count[3] = v & 0xFFFF
        elif port == 0xC00A:
            if v & 4: self.dma.mask |= 1 << (v & 3)
            else: self.dma.mask &= ~(1 << (v & 3))
        elif port == 0xC00F: self.dma.mask = v & 0x0F
        if port == 0xFFFA: self.ulas['IULA'] = v & 0xFF
        elif port == 0xFFF9: self.ulas['TULA'] = v & 0xFF
        elif port == 0xFFFC: self.ulas['OPHA'] = v & 0xFF
        pb = self.periph_base()
        if pb + self.ulas['IULA'] <= port <= pb + self.ulas['IULA'] + 1:
            self.icu.write(port - (pb + self.ulas['IULA']), v & 0xFF)
            self.io_log.append(('W', port, v)); return
        if pb + self.ulas['TULA'] <= port <= pb + self.ulas['TULA'] + 3:
            self.tcu.write(port - (pb + self.ulas['TULA']), v & 0xFF)
            self.io_log.append(('W', port, v)); return
        if 0xA0 <= port <= 0xBF: self.spc_log.append((port, v & 0xFF))
        if port == 0xA2: self.spc_sctl = v & 0xFF
        if port == 0xA4 and (v & 0x10):               # SCMD RST Out: the chip reports a reset condition (ASSUMPTION)
            self.spc_ints |= 0x01; self.icu.raise_irq(3)
        if port == 0xA8: self.spc_ints &= ~(v & 0xFF) & 0xFF    # write 1 to clear (ASSUMPTION)
        if port == 0xEA: self.fdc.write_data(v & 0xFF)
        if 0xFF00 <= port <= 0xFF7E and port % 2 == 0 and size == 2 and not self.xa:
            self.pgr[(port - 0xFF00) // 2] = v & 0x3FF
        self.io_log.append(('W', port, v))
        self.ticks += 1

PARITY = [bin(i).count('1') % 2 == 0 for i in range(256)]

class CPU:
    @property
    def xa(self): return self._xa
    @xa.setter
    def xa(self, v):
        self._xa = v; self.b.xa = v
    def __init__(self, bus):
        self.b = bus
        self.r = [0]*8                  # AX CX DX BX SP BP SI DI
        self.s = [0]*4                  # ES CS SS DS
        self.ip = 0
        self.f = 0x0002                 # flags register
        self.halted = False
        self.steps = 0
        self.stop = None
        self._xa = 0
        self._pending = None
    # flag helpers
    CF, PF, AF, ZF, SF, TF, IF, DF, OF = 0x1, 0x4, 0x10, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800
    def getf(self, m): return bool(self.f & m)
    def setf(self, m, on): self.f = (self.f | m) if on else (self.f & ~m)
    def lazy(self, res, bits, cf=None, of=None, af=None):
        mask = (1 << bits) - 1; sign = 1 << (bits-1)
        r = res & mask
        self.setf(self.ZF, r == 0); self.setf(self.SF, bool(r & sign))
        self.setf(self.PF, PARITY[r & 0xFF])
        if cf is not None: self.setf(self.CF, cf)
        if of is not None: self.setf(self.OF, of)
        if af is not None: self.setf(self.AF, af)
    def fetch8(self):
        v = self.b.rd8(self.s[1]*16 + self.ip); self.ip = (self.ip+1) & 0xFFFF; return v
    def fetch16(self):
        lo = self.fetch8(); return lo | (self.fetch8() << 8)
    def ea(self, mod, rm, seg_override=None):
        """returns (linear address, segment index) for a memory ModRM operand"""
        if mod == 0 and rm == 6:
            off = self.fetch16(); seg = 3
        else:
            base = {0: self.r[3]+self.r[6], 1: self.r[3]+self.r[7], 2: self.r[5]+self.r[6],
                    3: self.r[5]+self.r[7], 4: self.r[6], 5: self.r[7], 6: self.r[5], 7: self.r[3]}[rm]
            seg = 2 if rm in (2, 3, 6) else 3
            if mod == 1:
                d = self.fetch8(); base += d - 256 if d > 127 else d
            elif mod == 2: base += self.fetch16()
            off = base & 0xFFFF
        if seg_override is None: seg_override = getattr(self, 'seg_ov', None)
        if seg_override is not None: seg = seg_override
        return (self.s[seg]*16 + off) & 0xFFFFF, seg
    def fetch_peek(self): return self.b.rd8(self.s[1]*16 + self.ip)
    def modrm(self):
        m = self.fetch8(); return m >> 6, (m >> 3) & 7, m & 7
    def rm_get(self, mod, rm, size, seg_ov=None):
        if mod == 3:
            return self.reg_get(rm, size)
        a, _ = self.ea(mod, rm, seg_ov)
        self._pending = a                  # the same operand may be written back by rm_set
        return self.b.rd16(a) if size == 2 else self.b.rd8(a)
    def rm_set(self, mod, rm, size, v, seg_ov=None):
        if mod == 3:
            return self.reg_set(rm, size, v)
        if self._pending is not None:      # address already decoded by rm_get: do not refetch displacement
            a = self._pending; self._pending = None
        else:
            a, _ = self.ea(mod, rm, seg_ov)
        self.b.wr16(a, v) if size == 2 else self.b.wr8(a, v)
    def reg_get(self, i, size):
        if size == 2: return self.r[i]
        return (self.r[i-4] >> 8) & 0xFF if i >= 4 else self.r[i] & 0xFF
    def reg_set(self, i, size, v):
        if size == 2: self.r[i] = v & 0xFFFF
        elif i >= 4: self.r[i-4] = (self.r[i-4] & 0x00FF) | ((v & 0xFF) << 8)
        else: self.r[i] = (self.r[i] & 0xFF00) | (v & 0xFF)
    def push(self, v):
        self.r[4] = (self.r[4] - 2) & 0xFFFF; self.b.wr16(self.s[2]*16 + self.r[4], v)
    def pop(self):
        v = self.b.rd16(self.s[2]*16 + self.r[4]); self.r[4] = (self.r[4] + 2) & 0xFFFF; return v
    def alu(self, op, a, b, size):
        bits = 8*size; mask = (1 << bits) - 1; sign = 1 << (bits-1)
        if op == 0: r = a + b; cf = r > mask; of = bool((a ^ r) & (b ^ r) & sign)
        elif op == 1: r = a | b; cf = of = False
        elif op == 2: r = a + b + self.getf(self.CF); cf = r > mask; of = bool((a ^ r) & (b ^ r) & sign)
        elif op == 3: r = a - b - self.getf(self.CF); cf = (a & mask) < (b & mask) + self.getf(self.CF); of = bool((a ^ b) & (a ^ r) & sign)
        elif op == 4: r = a & b; cf = of = False
        elif op == 5 or op == 7: r = a - b; cf = (a & mask) < (b & mask); of = bool((a ^ b) & (a ^ r) & sign)
        elif op == 6: r = a ^ b; cf = of = False
        self.lazy(r, bits, cf=cf, of=of, af=False)
        return r & mask
    def run(self, max_steps=200000):
        while self.steps < max_steps and self.stop is None:
            self.step()
        return self.stop or 'step limit'
    def take_irq(self):
        n = self.b.icu.pending()
        if n is None or not self.getf(self.IF): return False
        vec = self.b.icu.acknowledge(n)
        self.push(self.f); self.setf(self.IF, False); self.setf(self.TF, False)
        self.push(self.s[1]); self.push(self.ip)
        self.ip = self.b.rd16(vec*4); self.s[1] = self.b.rd16(vec*4 + 2)
        return True
    def step(self):
        self.b.dsp_clk()
        if self.b.icu.pending() is not None and self.getf(self.IF):
            self.take_irq(); self.b.tcu.step(1); return
        self.b.tcu.step(1)
        cs_ip = self.s[1]*16 + self.ip
        self.steps += 1
        self.seg_ov = None; rep = None; self._pending = None
        op = self.fetch8()
        while op in (0x26, 0x2E, 0x36, 0x3E, 0xF2, 0xF3):
            if op in (0xF2, 0xF3): rep = op
            else: self.seg_ov = {0x26: 0, 0x2E: 1, 0x36: 2, 0x3E: 3}[op]
            op = self.fetch8()
        if op in (0xA6, 0xA7, 0xAE, 0xAF):           # CMPS / SCAS, with optional REPE (F3) / REPNE (F2)
            if rep is None:
                self.cmp_string(op); return
            while self.r[1] != 0:
                self.cmp_string(op); self.r[1] = (self.r[1] - 1) & 0xFFFF
                zf = self.getf(self.ZF)
                if (rep == 0xF3 and not zf) or (rep == 0xF2 and zf): break
            return
        if rep is not None and op in (0xA4, 0xA5, 0xAA, 0xAB, 0xAC, 0xAD, 0x6C, 0x6D, 0x6E, 0x6F):
            while self.r[1] != 0:
                self.string_op(op); self.r[1] = (self.r[1] - 1) & 0xFFFF
            return
        if op == 0x0F:
            op2 = self.fetch8()
            if op2 == 0xE0:                          # BRKXA imm8 (V33A/V53A): per NEC user's manual U11301EJ5V0UM
                n = self.fetch8()
                self.b.wr16 if False else None
                temp1 = self.b.rd16(n*4); temp2 = self.b.rd16(n*4 + 2)
                self.xa = 1                          # XAM register (I/O FF80h) XA flag
                self.ip = temp1; self.s[1] = temp2
                return
            if op2 == 0xF0:                          # RETXA imm8 (V33A/V53A): per NEC user's manual U11301EJ5V0UM
                n = self.fetch8()
                temp1 = self.b.rd16(n*4); temp2 = self.b.rd16(n*4 + 2)
                self.xa = 0                          # XAM register XA flag cleared
                self.ip = temp1; self.s[1] = temp2
                return
            self.stop = ('unsupported 0F %02X opcode' % op2, cs_ip); return
        if op < 0x40 and (op & 7) < 6:
            kind = op >> 3; form = op & 7; size = 1 if form in (0, 2, 4) else 2
            if form < 4:
                mod, reg, rm = self.modrm()
                if form in (0, 1): a = self.rm_get(mod, rm, size); b = self.reg_get(reg, size)
                else: b = self.rm_get(mod, rm, size); a = self.reg_get(reg, size)
                r = self.alu(kind, a, b, size)
                if kind != 7:
                    if form in (0, 1): self.rm_set(mod, rm, size, r)
                    else: self.reg_set(reg, size, r)
            else:
                b = self.fetch16() if size == 2 else self.fetch8()
                a = self.r[0] if size == 2 else self.reg_get(0, 1)
                r = self.alu(kind, a, b, size)
                if kind != 7: self.reg_set(0, size, r)
            return
        if 0x40 <= op <= 0x4F:                       # INC/DEC reg16 (CF preserved)
            i = op & 7; cf = self.getf(self.CF)
            if op < 0x48:
                v = (self.r[i] + 1) & 0xFFFF; self.lazy(v, 16, of=(self.r[i] == 0x7FFF), af=False)
            else:
                v = (self.r[i] - 1) & 0xFFFF; self.lazy(v, 16, of=(self.r[i] == 0x8000), af=False)
            self.setf(self.CF, cf); self.r[i] = v; return
        if 0x50 <= op <= 0x57: self.push(self.r[op & 7]); return
        if 0x58 <= op <= 0x5F: self.r[op & 7] = self.pop(); return
        if 0x70 <= op <= 0x7F:
            d = self.fetch8(); d = d - 256 if d > 127 else d
            if self.cond(op & 0xF): self.ip = (self.ip + d) & 0xFFFF
            return
        if op in (0x80, 0x81, 0x83):
            size = 1 if op == 0x80 else 2
            mod, reg, rm = self.modrm()
            a = self.rm_get(mod, rm, size)
            if op == 0x81: b = self.fetch16()
            else:
                b = self.fetch8()
                if op == 0x83 and b > 127: b = (b - 256) & (0xFFFF if size == 2 else 0xFF)
            r = self.alu(reg, a, b, size)
            if reg != 7: self.rm_set(mod, rm, size, r)
            return
        if op in (0x84, 0x85):
            size = 1 if op == 0x84 else 2; mod, reg, rm = self.modrm()
            self.alu(4, self.rm_get(mod, rm, size), self.reg_get(reg, size), size); return
        if op in (0x86, 0x87):
            size = 1 if op == 0x86 else 2; mod, reg, rm = self.modrm()
            a = self.rm_get(mod, rm, size); b = self.reg_get(reg, size)
            self.rm_set(mod, rm, size, b); self.reg_set(reg, size, a); return
        if op in (0x88, 0x89, 0x8A, 0x8B):
            size = 1 if op in (0x88, 0x8A) else 2; mod, reg, rm = self.modrm()
            if op in (0x88, 0x89): self.rm_set(mod, rm, size, self.reg_get(reg, size))
            else: self.reg_set(reg, size, self.rm_get(mod, rm, size))
            return
        if op == 0x8C:
            mod, reg, rm = self.modrm(); self.rm_set(mod, rm, 2, self.s[reg & 3]); return
        if op == 0x8E:
            mod, reg, rm = self.modrm(); self.s[reg & 3] = self.rm_get(mod, rm, 2); return
        if op == 0x8D:
            mod, reg, rm = self.modrm(); a, sg = self.ea(mod, rm)
            self.reg_set(reg, 2, (a - self.s[sg]*16) & 0xFFFF); return      # LEA: the OFFSET, not the linear address
        if op == 0x8F:
            mod, reg, rm = self.modrm(); self.rm_set(mod, rm, 2, self.pop()); return
        if op == 0x90: return
        if 0x91 <= op <= 0x97:
            i = op & 7; self.r[0], self.r[i] = self.r[i], self.r[0]; return
        if op == 0x98: self.r[0] = (self.r[0] & 0xFF) | (0xFF00 if self.r[0] & 0x80 else 0); return
        if op == 0x99: self.r[2] = 0xFFFF if self.r[0] & 0x8000 else 0; return
        if op == 0x9C: self.push(self.f); return
        if op == 0x9D: self.f = self.pop() | 0x0002; return
        if op == 0x9E: self.f = (self.f & 0xFF00) | ((self.r[0] >> 8) & 0xD5) | 0x2; return
        if op == 0x9F: self.reg_set(4, 1, self.f & 0xFF); return
        if op in (0xA0, 0xA1, 0xA2, 0xA3):          # MOV AL/AX <-> moffs
            size = 1 if op in (0xA0, 0xA2) else 2; off = self.fetch16()
            seg = self.seg_ov if self.seg_ov is not None else 3
            a = self.s[seg]*16 + off
            if op in (0xA0, 0xA1): self.reg_set(0, size, self.b.rd16(a) if size == 2 else self.b.rd8(a))
            else: self.b.wr16(a, self.reg_get(0, size)) if size == 2 else self.b.wr8(a, self.reg_get(0, 1))
            return
        if op in (0xA8, 0xA9):
            size = 1 if op == 0xA8 else 2; b = self.fetch8() if size == 1 else self.fetch16()
            self.alu(4, self.reg_get(0, size), b, size); return
        if op in (0xAA, 0xAB, 0xA4, 0xA5, 0xAC, 0xAD):
            self.string_op(op); return
        if op in (0x6C, 0x6D, 0x6E, 0x6F):
            self.string_op(op); return
        if 0xB0 <= op <= 0xB7: self.reg_set(op & 7, 1, self.fetch8()); return
        if 0xB8 <= op <= 0xBF: self.r[op & 7] = self.fetch16(); return
        if op in (0xC2, 0xC3, 0xCA, 0xCB):
            if op == 0xC2: n = self.fetch16(); self.ip = self.pop(); self.r[4] = (self.r[4] + n) & 0xFFFF; return
            if op == 0xC3: self.ip = self.pop(); return
            if op == 0xCB: self.ip = self.pop(); self.s[1] = self.pop(); return
            if op == 0xCA: n = self.fetch16(); self.ip = self.pop(); self.s[1] = self.pop(); self.r[4] = (self.r[4]+n) & 0xFFFF; return
        if op in (0xC4, 0xC5):                       # LES / LDS: load far pointer
            mod, reg, rm = self.modrm(); a, _ = self.ea(mod, rm)
            off = self.b.rd16(a); seg = self.b.rd16(a+2)
            self.reg_set(reg, 2, off); self.s[0 if op == 0xC4 else 3] = seg; return
        if op in (0xC6, 0xC7):                       # MOV r/m, imm: the displacement comes BEFORE the immediate
            size = 1 if op == 0xC6 else 2; mod, reg, rm = self.modrm()
            a = None if mod == 3 else self.ea(mod, rm)[0]
            v = self.fetch8() if size == 1 else self.fetch16()
            if mod == 3: self.reg_set(rm, size, v)
            else: self.b.wr16(a, v) if size == 2 else self.b.wr8(a, v)
            return
        if op in (0xCC, 0xCD, 0xCE):                  # INT 3 / INT n / INTO
            n = 3 if op == 0xCC else (self.fetch8() if op == 0xCD else 4)
            if op == 0xCE and not self.getf(self.OF): return
            self.push(self.f); self.setf(self.IF, False); self.setf(self.TF, False)
            self.push(self.s[1]); self.push(self.ip)
            self.ip = self.b.rd16(n*4); self.s[1] = self.b.rd16(n*4 + 2); return
        if op == 0xCF: self.ip = self.pop(); self.s[1] = self.pop(); self.f = self.pop() | 0x2; return
        if op in (0x06, 0x0E, 0x16, 0x1E):          # PUSH ES/CS/SS/DS
            self.push(self.s[{0x06:0,0x0E:1,0x16:2,0x1E:3}[op]]); return
        if op in (0x07, 0x17, 0x1F):                # POP ES/SS/DS
            self.s[{0x07:0,0x17:2,0x1F:3}[op]] = self.pop(); return
        if op == 0x68: self.push(self.fetch16()); return          # PUSH imm16 (V53)
        if op == 0x6A:                                             # PUSH imm8 sign-extended
            d = self.fetch8(); self.push((d - 256 if d > 127 else d) & 0xFFFF); return
        if op in (0x69, 0x6B):                                     # IMUL reg, r/m, imm
            mod, reg, rm = self.modrm(); v = self.rm_get(mod, rm, 2)
            imm = self.fetch16() if op == 0x69 else self.fetch8()
            if op == 0x6B and imm > 127: imm = (imm - 256) & 0xFFFF   # sign-extend to 16 bits
            sv = v - 0x10000 if v & 0x8000 else v; si = imm - 0x10000 if imm & 0x8000 else imm
            p = sv * si; self.reg_set(reg, 2, p & 0xFFFF)
            ovf = not (-32768 <= p <= 32767); self.setf(self.CF, ovf); self.setf(self.OF, ovf); return
        if op == 0x9A:                                             # CALL far direct
            off = self.fetch16(); seg = self.fetch16()
            self.push(self.s[1]); self.push(self.ip); self.ip = off; self.s[1] = seg; return
        if op == 0xC8:                                             # ENTER
            n = self.fetch16(); lvl = self.fetch8() & 0x1F
            self.push(self.r[5]); frame = self.r[4]
            for _ in range(lvl): self.r[5] = (self.r[5] - 2) & 0xFFFF; self.push(self.b.rd16(self.s[2]*16 + self.r[5]))
            if lvl: self.push(frame)
            self.r[5] = frame; self.r[4] = (self.r[4] - n) & 0xFFFF; return
        if op == 0xC9:                                             # LEAVE
            self.r[4] = self.r[5]; self.r[5] = self.pop(); return
        if op == 0xD4:                                             # AAM
            base = self.fetch8(); v = self.reg_get(0, 1)
            if base == 0: self.stop = ('AAM divide by zero', self.s[1]*16+self.ip); return
            self.reg_set(4, 1, v // base); self.reg_set(0, 1, v % base); self.lazy(v % base, 16); return
        if op == 0xD5:                                             # AAD
            base = self.fetch8(); v = self.reg_get(4, 1)*base + self.reg_get(0, 1)
            self.reg_set(0, 1, v & 0xFF); self.reg_set(4, 1, 0); self.lazy(v & 0xFF, 8); return
        if op == 0xD7:                                             # XLAT
            seg = self.seg_ov if self.seg_ov is not None else 3
            self.reg_set(0, 1, self.b.rd8(self.s[seg]*16 + ((self.r[3] + self.reg_get(0, 1)) & 0xFFFF))); return
        if op in (0xC0, 0xC1):                       # shift/rotate with imm8 count (80186/V53)
            size = 1 if op == 0xC0 else 2; mod, reg, rm = self.modrm()
            v = self.rm_get(mod, rm, size)               # decode the address (and its displacement) first
            cnt = self.fetch8() & 0x1F                   # then the immediate count
            self.shift(reg, v, cnt, size, mod, rm); return
        if op in (0xD0, 0xD1, 0xD2, 0xD3):
            size = 1 if op in (0xD0, 0xD2) else 2; mod, reg, rm = self.modrm()
            cnt = 1 if op in (0xD0, 0xD1) else (self.reg_get(1, 1) & 0x1F)
            v = self.rm_get(mod, rm, size); self.shift(reg, v, cnt, size, mod, rm); return
        if op in (0xE0, 0xE1, 0xE2, 0xE3):
            d = self.fetch8(); d = d - 256 if d > 127 else d
            if op == 0xE3:
                if self.r[1] == 0: self.ip = (self.ip + d) & 0xFFFF
                return
            self.r[1] = (self.r[1] - 1) & 0xFFFF
            take = self.r[1] != 0 and (op == 0xE2 or (op == 0xE1 and self.getf(self.ZF)) or (op == 0xE0 and not self.getf(self.ZF)))
            if take: self.ip = (self.ip + d) & 0xFFFF
            return
        if op == 0xE4 or op == 0xE5:
            p = self.fetch8(); size = 1 if op == 0xE4 else 2; v = self.b.io_read(p, size)
            self.reg_set(0, size, v); return
        if op == 0xE6 or op == 0xE7:
            p = self.fetch8(); size = 1 if op == 0xE6 else 2; self.b.io_write(p, self.reg_get(0, size), size); return
        if op == 0xE8:
            d = self.fetch16(); self.push(self.ip); self.ip = (self.ip + d) & 0xFFFF; return
        if op == 0xE9:
            d = self.fetch16(); self.ip = (self.ip + d) & 0xFFFF; return
        if op == 0xEA:
            off = self.fetch16(); seg = self.fetch16(); self.ip = off; self.s[1] = seg; return
        if op == 0xEB:
            d = self.fetch8(); d = d - 256 if d > 127 else d; self.ip = (self.ip + d) & 0xFFFF; return
        if op in (0xEC, 0xED):
            size = 1 if op == 0xEC else 2; v = self.b.io_read(self.r[2], size); self.reg_set(0, size, v); return
        if op in (0xEE, 0xEF):
            size = 1 if op == 0xEE else 2; self.b.io_write(self.r[2], self.reg_get(0, size), size); return
        if op == 0xF4: self.halted = True; self.stop = ('HLT', cs_ip); return
        if op in (0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD):
            self.setf(self.CF, op == 0xF9) if op in (0xF8, 0xF9) else None
            if op == 0xFA: self.setf(self.IF, False)
            if op == 0xFB: self.setf(self.IF, True)
            if op == 0xFC: self.setf(self.DF, False)
            if op == 0xFD: self.setf(self.DF, True)
            return
        if op in (0xF6, 0xF7):
            size = 1 if op == 0xF6 else 2; mod, reg, rm = self.modrm()
            if reg in (0, 1):
                a = self.rm_get(mod, rm, size); b = self.fetch8() if size == 1 else self.fetch16()
                self.alu(4, a, b, size); return
            if reg == 2: self.rm_set(mod, rm, size, ~self.rm_get(mod, rm, size) & ((1 << 8*size)-1)); return
            if reg in (4, 5, 6, 7):                      # MUL, IMUL, DIV, IDIV
                bits = 8*size; a = self.reg_get(0, 1) if size == 1 else self.r[0]
                v = self.rm_get(mod, rm, size)
                if reg in (4, 5):
                    if reg == 5:
                        sa = a - (1 << bits) if a >> (bits-1) else a; sv = v - (1 << bits) if v >> (bits-1) else v
                        p = sa * sv
                    else: p = a * v
                    mask = (1 << bits)-1
                    if size == 1: self.r[0] = p & 0xFFFF
                    else: self.r[0] = p & 0xFFFF; self.r[2] = (p >> 16) & 0xFFFF
                    self.setf(self.CF, bool(p >> bits)); self.setf(self.OF, bool(p >> bits)); return
                if v == 0:
                    self.stop = ('divide by zero (interrupt 0 not modelled)', self.s[1]*16+self.ip); return
                if size == 1: dividend = self.r[0] & 0xFFFF
                else: dividend = ((self.r[2] & 0xFFFF) << 16) | (self.r[0] & 0xFFFF)
                if reg == 7:
                    dv = dividend - (1 << (2*bits)) if dividend >> (2*bits-1) else dividend
                    sv = v - (1 << bits) if v >> (bits-1) else v
                    q = int(dv / sv); r_ = dv - q*sv
                else:
                    q, r_ = divmod(dividend, v)
                if size == 1: self.reg_set(0, 1, q & 0xFF); self.reg_set(4, 1, r_ & 0xFF)
                else: self.r[0] = q & 0xFFFF; self.r[2] = r_ & 0xFFFF
                return
            if reg == 3:
                a = self.rm_get(mod, rm, size); mask = (1 << 8*size)-1
                r = (-a) & mask; self.lazy(r, 8*size, cf=a != 0, of=(a == (1 << (8*size-1))), af=False)
                self.rm_set(mod, rm, size, r); return
        if op in (0xFE, 0xFF):
            size = 1 if op == 0xFE else 2; mod, reg, rm = self.modrm()
            if reg in (0, 1):
                v = self.rm_get(mod, rm, size); cf = self.getf(self.CF)
                r = (v + 1) if reg == 0 else (v - 1); mask = (1 << 8*size)-1
                self.lazy(r, 8*size, of=(v == (mask >> 1)) if reg == 0 else (v == (1 << (8*size-1))), af=False)
                self.setf(self.CF, cf); self.rm_set(mod, rm, size, r & mask); return
            if op == 0xFF and reg == 2:              # call near indirect
                t = self.rm_get(mod, rm, 2); self.push(self.ip); self.ip = t; return
            if op == 0xFF and reg == 4:              # jmp near indirect
                self.ip = self.rm_get(mod, rm, 2); return
            if op == 0xFF and reg == 6:              # push
                self.push(self.rm_get(mod, rm, 2)); return
            if op == 0xFF and reg in (3, 5):         # far call / far jmp via memory
                a, _ = self.ea(mod, rm)
                off = self.b.rd16(a); seg = self.b.rd16(a+2)
                if reg == 3: self.push(self.s[1]); self.push(self.ip)
                self.ip = off; self.s[1] = seg; return
        if op == 0x60:                                 # PUSHA: AX CX DX BX SP(original) BP SI DI
            sp0 = self.r[4]
            for v in (self.r[0], self.r[1], self.r[2], self.r[3], sp0, self.r[5], self.r[6], self.r[7]): self.push(v)
            return
        if op == 0x61:                                 # POPA: DI SI BP (SP discarded) BX DX CX AX
            self.r[7] = self.pop(); self.r[6] = self.pop(); self.r[5] = self.pop(); self.pop()
            self.r[3] = self.pop(); self.r[2] = self.pop(); self.r[1] = self.pop(); self.r[0] = self.pop()
            return
        if op == 0xF5: self.setf(self.CF, not self.getf(self.CF)); return     # CMC
        if op in (0x27, 0x2F):                         # DAA / DAS
            al = self.reg_get(0, 1); cf = self.getf(self.CF); af = self.getf(self.AF); old = al
            if (al & 0x0F) > 9 or af:
                if op == 0x27: cf = cf or al + 6 > 0xFF; al = (al + 6) & 0xFF
                else: cf = cf or al < 6; al = (al - 6) & 0xFF
                self.setf(self.AF, True)
            else: self.setf(self.AF, False)
            if old > 0x99 or self.getf(self.CF):
                al = (al + 0x60) & 0xFF if op == 0x27 else (al - 0x60) & 0xFF; cf = True
            else: cf = False if op == 0x27 else cf
            self.reg_set(0, 1, al); self.lazy(al, 8, cf=cf); return
        if op in (0x37, 0x3F):                         # AAA / AAS
            al = self.reg_get(0, 1)
            if (al & 0x0F) > 9 or self.getf(self.AF):
                if op == 0x37: self.r[0] = (self.r[0] + 0x106) & 0xFFFF
                else:
                    self.reg_set(0, 1, (al - 6) & 0xFF); self.reg_set(4, 1, (self.reg_get(4, 1) - 1) & 0xFF)
                self.setf(self.AF, True); self.setf(self.CF, True)
            else:
                self.setf(self.AF, False); self.setf(self.CF, False)
            self.reg_set(0, 1, self.reg_get(0, 1) & 0x0F); return
        self.stop = ('unsupported opcode %02X' % op, cs_ip)
    def cond(self, c):
        cf, zf, sf, of, pf = self.getf(self.CF), self.getf(self.ZF), self.getf(self.SF), self.getf(self.OF), self.getf(self.PF)
        return [of, not of, cf, not cf, zf, not zf, cf or zf, not(cf or zf), sf, not sf, pf, not pf, sf != of, sf == of, zf or (sf != of), not zf and sf == of][c]
    def shift(self, reg, v, cnt, size, mod, rm):
        mask = (1 << 8*size)-1; bits = 8*size
        if cnt == 0: return
        if reg == 4 or reg == 6:                     # SHL/SAL
            r = (v << cnt) & mask; cf = bool((v << (cnt-1)) & (1 << (bits-1)))
        elif reg == 5:                               # SHR
            r = v >> cnt; cf = bool((v >> (cnt-1)) & 1)
        elif reg == 7:                               # SAR
            sv = v - (1 << bits) if v & (1 << (bits-1)) else v
            r = (sv >> cnt) & mask; cf = bool((sv >> (cnt-1)) & 1)
        elif reg == 0:                               # ROL
            c = cnt % bits; r = ((v << c) | (v >> (bits - c))) & mask if c else v; cf = bool(r & 1)
        elif reg == 1:                               # ROR
            c = cnt % bits; r = ((v >> c) | (v << (bits - c))) & mask if c else v; cf = bool(r >> (bits-1) & 1)
        elif reg in (2, 3):                          # RCL / RCR: rotate through carry, (bits+1)-bit rotation
            c = cnt % (bits + 1); r = v; cf = self.getf(self.CF)
            for _ in range(c):
                if reg == 2:
                    nc = (r >> (bits-1)) & 1; r = ((r << 1) | cf) & mask; cf = nc
                else:
                    nc = r & 1; r = (r >> 1) | (cf << (bits-1)); cf = nc
            self.setf(self.CF, bool(cf))
            if cnt == 1:
                msb = (r >> (bits-1)) & 1
                self.setf(self.OF, bool(msb ^ cf) if reg == 2 else bool(msb ^ ((r >> (bits-2)) & 1)))
            self.rm_set(mod, rm, size, r); return
        else:
            self.stop = ('unsupported shift group %d' % reg, self.s[1]*16+self.ip); return
        self.lazy(r, bits, cf=cf, af=False)
        self.rm_set(mod, rm, size, r)
    def cmp_string(self, op):
        size = 1 if op in (0xA6, 0xAE) else 2
        d = -size if self.getf(self.DF) else size
        dst = self.s[0]*16 + self.r[7]                 # ES:DI (no override)
        b = self.b.rd16(dst) if size == 2 else self.b.rd8(dst)
        if op in (0xA6, 0xA7):                         # CMPS: DS:SI (override allowed) - ES:DI
            seg = self.seg_ov if self.seg_ov is not None else 3
            src = self.s[seg]*16 + self.r[6]
            a = self.b.rd16(src) if size == 2 else self.b.rd8(src)
            self.r[6] = (self.r[6] + d) & 0xFFFF
        else:                                          # SCAS: AL/AX - ES:DI
            a = self.reg_get(0, size)
        self.alu(7, a, b, size)
        self.r[7] = (self.r[7] + d) & 0xFFFF
    def string_op(self, op):
        if op in (0x6C, 0x6D, 0x6E, 0x6F):          # INS/OUTS: port DX, source seg:SI
            size = 1 if op in (0x6C, 0x6E) else 2
            d = -size if self.getf(self.DF) else size
            if op in (0x6E, 0x6F):
                seg = self.seg_ov if self.seg_ov is not None else 3
                v = self.b.rd16(self.s[seg]*16 + self.r[6]) if size == 2 else self.b.rd8(self.s[seg]*16 + self.r[6])
                self.b.io_write(self.r[2], v, size)
            else:
                v = self.b.io_read(self.r[2], size)
                a = self.s[0]*16 + self.r[7]
                self.b.wr16(a, v) if size == 2 else self.b.wr8(a, v)
                self.r[7] = (self.r[7] + d) & 0xFFFF
            self.r[6] = (self.r[6] + d) & 0xFFFF if op in (0x6E, 0x6F) else self.r[6]
            return
        size = 1 if op in (0xAA, 0xA4, 0xAC) else 2
        d = -size if self.getf(self.DF) else size
        if op in (0xAA, 0xAB):                       # STOS
            a = self.s[0]*16 + self.r[7]; v = self.reg_get(0, size)
            self.b.wr16(a, v) if size == 2 else self.b.wr8(a, v); self.r[7] = (self.r[7] + d) & 0xFFFF
        elif op in (0xA4, 0xA5):                     # MOVS
            seg = self.seg_ov if self.seg_ov is not None else 3
            src = self.s[seg]*16 + self.r[6]; dst = self.s[0]*16 + self.r[7]
            v = self.b.rd16(src) if size == 2 else self.b.rd8(src)
            self.b.wr16(dst, v) if size == 2 else self.b.wr8(dst, v)
            self.r[6] = (self.r[6] + d) & 0xFFFF; self.r[7] = (self.r[7] + d) & 0xFFFF
        else:                                        # LODS: source is DS unless overridden
            seg = self.seg_ov if self.seg_ov is not None else 3
            src = self.s[seg]*16 + self.r[6]; v = self.b.rd16(src) if size == 2 else self.b.rd8(src)
            self.reg_set(0, size, v); self.r[6] = (self.r[6] + d) & 0xFFFF
