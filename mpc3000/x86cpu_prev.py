"""Minimal 8086/V53 interpreter for the MPC3000 boot ROM (first milestone).
Covers the instructions the reset and start-up code uses. Unknown opcodes stop
the run and are reported. Devices are stubs defined in DEVICES.md."""
import struct

class Bus:
    def __init__(self, rom):
        self.mem = bytearray(0x1000000)       # 16 MB: expanded (XA) mode can reach beyond 1 MB
        self.pgr = [0]*64                     # page registers PGR0-63 at I/O FF00h, FF02h, ...
        self.xa = 0                           # XAM register bit 0 (I/O FF80h)
        self.low_written = bytearray(0x400)     # low-memory bytes written by the code
        self.shadow_rom = None                 # ROM bytes seen at 0000-03FF until written
        self.mem[0x80000:0x80000+len(rom)] = rom
        self.rom_lo = 0x80000
        self.io = {}
        self.io_log = []
        self.ticks = 0
    def phys(self, a):
        # normal mode: 20-bit address. Expanded (XA) mode: 24-bit logical address; the page
        # register selected by bits 14-19 supplies a 10-bit physical page base (manual: Address Expansion).
        if self.xa:
            return ((self.pgr[(a >> 14) & 0x3F] & 0x3FF) << 14) | (a & 0x3FFF)
        return a & 0xFFFFF
    def rd8(self, a):
        if self.xa:
            return self.mem[self.phys(a & 0xFFFFFF)]
        a &= 0xFFFFF
        if a < 0x400 and self.shadow_rom is not None and not self.low_written[a]:
            return self.shadow_rom[a]          # boot shadow: the ROM is seen at address 0 until written
        return self.mem[a]
    def wr8(self, a, v):
        if self.xa:
            p = self.phys(a & 0xFFFFFF)
            if p < 0x80000 or p >= 0x100000: self.mem[p] = v & 0xFF
            return
        a &= 0xFFFFF
        if a < 0x400: self.low_written[a] = 1
        if a < self.rom_lo: self.mem[a] = v & 0xFF
    def rd16(self, a): return self.rd8(a) | (self.rd8(a+1) << 8)
    def wr16(self, a, v): self.wr8(a, v & 0xFF); self.wr8(a+1, (v >> 8) & 0xFF)
    # --- device stubs (see DEVICES.md) ---
    def io_read(self, port, size):
        port &= 0xFFFF
        self.ticks += 1
        if port == 0xFF80: v = self.xa & 1
        elif port == 0xC00B: v = 0x08          # ready bit set, so the boot wait at CB444 completes
        elif port == 0xC030: v = (0x4E20 - (self.ticks // 64)) & 0xFFFF   # counter 0, counts down
        elif port == 0x62:
            v = (self.io.get(0x60, 0) << 12) & 0xFFFF   # latched value, top nibble
            self.io[0x60] = (self.io.get(0x60, 0) + 1) & 0xF   # advances on each read
        else: v = self.io.get(port, 0)
        self.io_log.append(('R', port, v))
        return v & (0xFFFF if size == 2 else 0xFF)
    def io_write(self, port, v, size):
        port &= 0xFFFF
        if 0xFF00 <= port <= 0xFF7E and port % 2 == 0 and size == 2 and not self.xa:
            self.pgr[(port - 0xFF00) // 2] = v & 0x3FF
        self.io[port] = v & (0xFFFF if size == 2 else 0xFF)
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
        return self.b.rd16(a) if size == 2 else self.b.rd8(a)
    def rm_set(self, mod, rm, size, v, seg_ov=None):
        if mod == 3:
            return self.reg_set(rm, size, v)
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
    def step(self):
        cs_ip = self.s[1]*16 + self.ip
        self.steps += 1
        self.seg_ov = None; rep = None
        op = self.fetch8()
        while op in (0x26, 0x2E, 0x36, 0x3E, 0xF2, 0xF3):
            if op in (0xF2, 0xF3): rep = op
            else: self.seg_ov = {0x26: 0, 0x2E: 1, 0x36: 2, 0x3E: 3}[op]
            op = self.fetch8()
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
            mod, reg, rm = self.modrm(); a, _ = self.ea(mod, rm); self.reg_set(reg, 2, a & 0xFFFF); return
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
        if op in (0xC6, 0xC7):
            size = 1 if op == 0xC6 else 2; mod, reg, rm = self.modrm()
            v = self.fetch8() if size == 1 else self.fetch16(); self.rm_set(mod, rm, size, v); return
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
            if op == 0x6B and imm > 127: imm -= 256
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
            cnt = self.fetch8() & 0x1F
            v = self.rm_get(mod, rm, size); self.shift(reg, v, cnt, size, mod, rm); return
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
        else:
            self.stop = ('unsupported shift group %d' % reg, self.s[1]*16+self.ip); return
        self.lazy(r, bits, cf=cf, af=False)
        self.rm_set(mod, rm, size, r)
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
