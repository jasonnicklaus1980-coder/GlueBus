"""uPD7810 core, first slice: instructions the panel firmware's code block uses.
Semantics transcribed from MAME upd7810_opcodes.cpp and upd7810_macros.h.
Instructions not yet transcribed raise NotImplementedError with name and address."""
import json, os
HERE = os.path.dirname(os.path.abspath(__file__))
T = json.load(open(os.path.join(HERE, 'upd7810_tables.json')))
MAIN = T['main']
SUBS = {int(k, 16): v for k, v in T['subs'].items()}   # entries: [name, length, cyc, cyc_skip, flags]
CY, F1, L0, L1, HC, SK, Z = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40

class Upd7810:
    def __init__(self, rom):
        self.rom = bytes(rom)              # program ROM at 0000-7FFF
        self.ram = bytearray(256)          # internal RAM at FF00-FFFF
        self.A = self.V = self.B = self.C = self.D = self.E = self.H = self.L = 0
        self.PC = 0; self.SP = 0; self.PSW = 0
        self.steps = 0; self.trace = []; self.MKL = 0; self.MKH = 0
        self.ECNT = 0; self.ETM0 = 0; self.ETMM = 0; self.OVCE = 0; self.cycles = 0
        self.MM = self.MKL = self.MKH = self.SML = self.SMH = 0; self.IRR = 0
        self.MA = self.MB = self.MCC = self.MC = self.TMM = self.ETMM = 0
        self.EAL = self.EAH = 0; self.IFF_pending = 0; self.PORTC_OUT = 0; self.HL2 = 0; self.ETM1 = 0; self.key_row = 0; self.EA2 = 0; self.VA2 = 0; self.CR0 = 0; self.TM0 = 0; self.TXB = 0; self.ANM = 0; self.IRR = 0; self.BC2 = 0; self.DE2 = 0; self.PB_OUT = 0xFF; self.pads = {}
    @property
    def BC(self): return (self.B << 8) | self.C
    @property
    def DE(self): return (self.D << 8) | self.E
    @property
    def HL(self): return (self.H << 8) | self.L
    @property
    def VA(self): return (self.V << 8) | self.A
    def rd(self, a):
        a &= 0xFFFF
        if a < 0x8000: return self.rom[a]
        if a >= 0xFF00: return self.ram[a - 0xFF00]
        return 0xFF
    def wr(self, a, v):
        a &= 0xFFFF
        if a >= 0xFF00: self.ram[a - 0xFF00] = v & 0xFF
    def fetch(self):
        v = self.rd(self.PC); self.PC = (self.PC + 1) & 0xFFFF; return v
    def set_z(self, is_zero):              # sets Z when the result is zero (MAME: SET_Z(n) clears Z if n != 0)
        self.PSW = (self.PSW | Z) if is_zero else (self.PSW & ~Z)
    def zhc_sub(self, after, before, carry):
        after &= 0xFF; before &= 0xFF
        self.set_z(after == 0)
        if before == after: self.PSW = (self.PSW & ~CY) | carry
        elif after > before: self.PSW |= CY
        else: self.PSW &= ~CY
        if (after & 15) > (before & 15): self.PSW |= HC
        else: self.PSW &= ~HC
    def skip_cy(self):
        if self.PSW & CY: self.PSW |= SK
    def skip_z(self):
        if self.PSW & Z: self.PSW |= SK
    def step(self):
        if self.check_irq(): return 'IRQ'
        pc0 = self.PC
        op = self.fetch()
        if op in SUBS:
            op2 = self.fetch()
            ent = SUBS[op][op2]
        else:
            ent = MAIN[op]
        name, length, flags = ent[0], ent[1], ent[4]
        # overlay bits named in the table's flags are cleared before the instruction (upd7810.cpp)
        if 'L0' in flags: self.PSW &= ~L0
        if 'L1' in flags: self.PSW &= ~L1
        self.steps += 1
        self.trace.append((pc0, name))
        if self.PSW & SK:                  # skip: consume the instruction without executing it
            self.PSW &= ~SK
            self.PC = (pc0 + length) & 0xFFFF
            self.handle_timers(ent[3])
            return name
        fn = getattr(self, 'i_' + name, None)
        if fn is None:
            raise NotImplementedError('%s at %04X' % (name, pc0))
        fn(op)
        self.handle_timers(ent[2])
        return name

    def zhc_add(self, after, before, carry):
        after &= 0xFF; before &= 0xFF
        self.set_z(after == 0)
        if after == before: self.PSW = (self.PSW & ~CY) | carry
        elif after < before: self.PSW |= CY
        else: self.PSW &= ~CY
        if (after & 15) < (before & 15): self.PSW |= HC
        else: self.PSW &= ~HC
    # --- external ports (MAME mpc3000 driver): no key, pad or dial activity assumed ---
    def port_a_in(self): return 0xFF        # keyboard row (key_row) read; no keys pressed
    def port_b_in(self):                    # drum pads, ACTIVE HIGH (MAME subcpu_pb_r): OR of rows whose select bit is low
        hit = 0
        for i in range(4):
            if not ((self.PB_OUT >> (3 - i)) & 1): hit |= self.pads.get(i, 0)
        return hit
    def port_c_in(self): return 0x00        # data-entry dial quadrature bits PC4/PC5; no movement
    # --- batch 1 (transcribed from upd7810_opcodes.cpp) ---
    def i_MOV_MM_A(self, op): self.MM = self.A                        # memory-mapping register (RAM view not modelled)
    def i_MVI_MKL_xx(self, op): self.MKL = self.fetch()
    def i_MVI_MKH_xx(self, op): self.MKH = self.fetch()
    def i_MOV_SML_A(self, op): self.SML = self.A
    def i_MOV_MA_A(self, op): self.MA = self.A                        # port output: WP not modelled
    def i_MOV_MB_A(self, op): self.MB = self.A
    def i_MOV_MCC_A(self, op): self.MCC = self.A
    def i_MOV_MC_A(self, op): self.MC = self.A
    def i_LXI_EA_s(self, op):
        self.EAL = self.fetch(); self.EAH = self.fetch()
    def i_MOV_ETMM_A(self, op): self.ETMM = self.A
    def i_MVI_TMM_xx(self, op): self.TMM = self.fetch()
    def i_MVI_V_xx(self, op): self.V = self.fetch()
    def i_STAX_Hp(self, op):
        self.wr(self.HL, self.A); self.set_hl((self.HL + 1) & 0xFFFF)
    def set_hl(self, v): self.H = (v >> 8) & 0xFF; self.L = v & 0xFF
    def i_SHLD_w(self, op):
        lo = self.fetch(); hi = self.fetch(); ea = (hi << 8) | lo
        self.wr(ea, self.L); self.wr(ea + 1, self.H)
    def i_EI(self, op): self.IFF_pending = 1                          # interrupt enable (pending)
    def i_ADI_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A + imm) & 0xFF
        self.zhc_add(tmp, self.A, 0); self.A = tmp
    def i_ANI_A_xx(self, op):
        self.A &= self.fetch(); self.set_z(self.A == 0)
    def i_ADD_E_A(self, op):
        tmp = (self.E + self.A) & 0xFF; self.zhc_add(tmp, self.E, 0); self.E = tmp
    def i_ADD_L_A(self, op):
        tmp = (self.L + self.A) & 0xFF; self.zhc_add(tmp, self.L, 0); self.L = tmp
    def i_MOV_B_A(self, op): self.B = self.A
    def i_ADD_A_A(self, op):
        tmp = (self.A + self.A) & 0xFF; self.zhc_add(tmp, self.A, 0); self.A = tmp
    def i_MOV_EAL_A(self, op): self.EAL = self.A
    def i_PUSH_HL(self, op):
        self.SP = (self.SP - 1) & 0xFFFF; self.wr(self.SP, self.H)
        self.SP = (self.SP - 1) & 0xFFFF; self.wr(self.SP, self.L)
    def i_MOV_A_B(self, op): self.A = self.B
    def i_SLL_A(self, op):
        self.PSW = (self.PSW & ~CY) | ((self.A >> 7) & CY); self.A = (self.A << 1) & 0xFF
    def i_MOV_PC_A(self, op): self.PORTC_OUT = self.A; self.key_row = ((self.A ^ 0xFF) >> 1) & 7   # keyboard row select (MAME subcpu_pc_w)

    # --- batch 2 ---
    @property
    def EA(self): return (self.EAH << 8) | self.EAL
    def set_ea(self, v): self.EAH = (v >> 8) & 0xFF; self.EAL = v & 0xFF
    def pop8(self):
        v = self.rd(self.SP); self.SP = (self.SP + 1) & 0xFFFF; return v
    def push8(self, v):
        self.SP = (self.SP - 1) & 0xFFFF; self.wr(self.SP, v)
    def skip_nz(self):
        if not (self.PSW & Z): self.PSW |= SK
    def i_POP_HL(self, op): self.L = self.pop8(); self.H = self.pop8()
    def i_LDAX_H(self, op): self.A = self.rd(self.HL)
    def i_MOV_C_A(self, op): self.C = self.A
    def i_LDAX_D(self, op): self.A = self.rd(self.DE)
    def i_MOV_A_PA(self, op): self.A = self.port_a_in()
    def i_XRI_A_xx(self, op): self.A ^= self.fetch(); self.set_z(self.A == 0)
    def i_STAX_D(self, op): self.wr(self.DE, self.A)
    def i_ANA_A_B(self, op): self.A &= self.B; self.set_z(self.A == 0)
    def i_STAX_H(self, op): self.wr(self.HL, self.A)
    def i_EQI_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A - imm) & 0xFF
        self.zhc_sub(tmp, self.A, 0); self.skip_z()
    def i_NEI_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A - imm) & 0xFF
        self.zhc_sub(tmp, self.A, 0); self.skip_nz()
    def i_SLR_A(self, op):
        self.PSW = (self.PSW & ~CY) | (self.A & CY); self.A >>= 1
    def i_SLR_C(self, op):
        self.PSW = (self.PSW & ~CY) | (self.C & CY); self.C >>= 1
    def i_INX_EA(self, op): self.set_ea((self.EA + 1) & 0xFFFF)
    def i_PUSH_VA(self, op): self.push8(self.V); self.push8(self.A)
    def i_PUSH_BC(self, op): self.push8(self.B); self.push8(self.C)
    def i_PUSH_EA(self, op): self.push8(self.EAH); self.push8(self.EAL)
    def i_ONI_C_xx(self, op):
        imm = self.fetch(); self.set_z((self.C & imm) == 0); self.skip_nz()
    def i_MOV_A_EAL(self, op): self.A = self.EAL
    def i_LDAX_H_A(self, op): self.A = self.rd((self.HL + self.A) & 0xFFFF)
    def i_POP_EA(self, op): self.EAL = self.pop8(); self.EAH = self.pop8()
    def i_POP_BC(self, op): self.C = self.pop8(); self.B = self.pop8()
    def i_POP_VA(self, op): self.A = self.pop8(); self.V = self.pop8()
    def bit_test(self, op, mask):
        ea = (self.V << 8) | self.fetch()
        if self.rd(ea) & mask: self.PSW |= SK
    def i_BIT_4_wa(self, op): self.bit_test(op, 0x10)
    def i_BIT_3_wa(self, op): self.bit_test(op, 0x08)
    def i_BIT_2_wa(self, op): self.bit_test(op, 0x04)
    def i_BIT_1_wa(self, op): self.bit_test(op, 0x02)
    def i_INR_C(self, op):
        old = self.PSW & CY; tmp = (self.C + 1) & 0xFF
        self.zhc_add(tmp, self.C, 0); self.C = tmp
        self.skip_cy(); self.PSW = (self.PSW & ~CY) | old
    def i_INR_A(self, op):
        old = self.PSW & CY; tmp = (self.A + 1) & 0xFF
        self.zhc_add(tmp, self.A, 0); self.A = tmp
        self.skip_cy(); self.PSW = (self.PSW & ~CY) | old
    def i_DCR_A(self, op):
        old = self.PSW & CY; tmp = (self.A - 1) & 0xFF
        self.zhc_sub(tmp, self.A, 0); self.A = tmp
        self.skip_cy(); self.PSW = (self.PSW & ~CY) | old
    def i_EXH(self, op):
        tmp = self.HL; self.set_hl(self.HL2); self.HL2 = tmp
    def i_CALF(self, op):
        lo = self.fetch(); hi = 0x08 + (op & 0x07)
        self.push8(self.PC >> 8); self.push8(self.PC & 0xFF)
        self.PC = (hi << 8) | lo

    # --- batch 3 ---
    def write_smh(self, data):
        if not ((self.SMH >> 2) & 1) and ((data >> 2) & 1): self.IRR = getattr(self, 'IRR', 0) | 0x0400   # INTFST
        self.SMH = data
    def i_MOV_SMH_A(self, op): self.write_smh(self.A)

    # --- batch 4 ---
    def i_DMOV_ETM1_EA(self, op):
        self.ETM1 = self.EA

    # --- batch 5 (transcribed from upd7810_opcodes.cpp) ---
    def m16(self, op): return self.fetch() | (self.fetch() << 8)
    def sub_ZHC(self, after, before, carry): self.zhc_sub(after, before, carry)
    def i_MOV_TM0_A(self, op): self.TM0 = self.A
    def i_DEQ_EA_HL(self, op):
        tmp = (self.EA - self.HL) & 0xFFFF; self.zhc_sub(tmp & 0xFF, self.EA & 0xFF, 0); self.skip_z()
    def i_EQIW_wa_xx(self, op):
        ea = (self.V << 8) | self.fetch(); imm = self.fetch(); m = self.rd(ea)
        tmp = (m - imm) & 0xFF; self.zhc_sub(tmp, m, 0); self.skip_z()
    def i_ONI_A_xx(self, op):
        imm = self.fetch(); self.set_z((self.A & imm) == 0); self.skip_nz()
    def i_BIT_5_wa(self, op):
        ea = (self.V << 8) | self.fetch()
        if self.rd(ea) & 0x20: self.PSW |= SK
    def i_ONI_PA_xx(self, op):
        pa = self.port_a_in(); imm = self.fetch(); self.set_z((pa & imm) == 0); self.skip_nz()
    def i_MVI_L_xx(self, op):
        if self.PSW & L0:
            self.PC = (self.PC + 1) & 0xFFFF; return
        self.L = self.fetch(); self.PSW |= L0
    def i_MVI_H_xx(self, op): self.H = self.fetch()
    def i_SUI_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A - imm) & 0xFF; self.zhc_sub(tmp, self.A, 0); self.A = tmp
    def i_MVIX_DE_xx(self, op):
        imm = self.fetch(); self.wr(self.DE, imm)
    def i_SBI_E_xx(self, op):
        imm = self.fetch(); c = self.PSW & CY
        tmp = (self.E - imm - c) & 0xFF; self.zhc_sub(tmp, self.E, c); self.E = tmp
    def i_OFFI_PC_xx(self, op):
        pc = self.port_c_in(); imm = self.fetch(); self.set_z((pc & imm) == 0); self.skip_z()
    def i_ORI_A_xx(self, op):
        self.A |= self.fetch(); self.set_z(self.A == 0)
    def i_MOV_B_w(self, op):
        ea = self.m16(op); self.B = self.rd(ea)
    def i_ORA_A_B(self, op):
        self.A |= self.B; self.set_z(self.A == 0)
    def i_MOV_w_B(self, op):
        ea = self.m16(op); self.wr(ea, self.B)
    def i_SLRC_C(self, op):
        self.PSW = (self.PSW & ~CY) | (self.C & CY); self.C >>= 1; self.skip_cy()
    def i_MOV_A_C(self, op): self.A = self.C
    def i_TABLE(self, op):
        ea = (self.PC + self.A + 1) & 0xFFFF
        self.C = self.rd(ea); self.B = self.rd(ea + 1)
    def i_MOV_A_EAH(self, op): self.A = self.EAH
    def i_MOV_EAH_A(self, op): self.EAH = self.A
    def i_DMOV_EA_HL(self, op): self.set_ea(self.HL)
    def i_DNE_EA_HL(self, op):
        tmp = (self.EA - self.HL) & 0xFFFF; self.zhc_sub(tmp & 0xFF, self.EA & 0xFF, 0); self.skip_nz()
    def i_LDEAX_Hp(self, op):
        self.EAL = self.rd(self.HL); self.EAH = self.rd(self.HL + 1); self.set_hl((self.HL + 2) & 0xFFFF)
    def i_ANI_L_xx(self, op):
        self.L &= self.fetch(); self.set_z(self.L == 0)
    def i_ORI_L_xx(self, op):
        self.L |= self.fetch(); self.set_z(self.L == 0)
    def i_DMOV_BC_EA(self, op):
        self.B = (self.EA >> 8) & 0xFF; self.C = self.EA & 0xFF
    def i_ONI_B_xx(self, op):
        imm = self.fetch(); self.set_z((self.B & imm) == 0); self.skip_nz()
    def i_OFFI_B_xx(self, op):
        imm = self.fetch(); self.set_z((self.B & imm) == 0); self.skip_z()
    def i_ANI_B_xx(self, op):
        self.B &= self.fetch(); self.set_z(self.B == 0)
    def i_SKIT_FST(self, op):
        if self.IRR & 0x0400: self.PSW |= SK
        self.IRR &= ~0x0400
    def i_MOV_TXB_A(self, op):
        self.TXB = self.A
        self.IRR |= 0x0400          # ASSUMPTION: transmit completes at once (INTFST); serial timing not yet modelled
    def i_EXX(self, op):
        self.BC2, self.DE2, self.HL2 = self.BC2, self.DE2, self.HL2
        bc, de, hl = self.BC, self.DE, self.HL
        self.set_bc(self.BC2); self.set_de(self.DE2); self.set_hl(self.HL2)
        self.BC2, self.DE2, self.HL2 = bc, de, hl
    def set_bc(self, v): self.B = (v >> 8) & 0xFF; self.C = v & 0xFF
    def set_de(self, v): self.D = (v >> 8) & 0xFF; self.E = v & 0xFF
    def i_ORI_TMM_xx(self, op):
        self.TMM |= self.fetch(); self.set_z(self.TMM == 0)
    def i_SKIT_FT0(self, op):
        if self.IRR & 0x0002: self.PSW |= SK
        self.IRR &= ~0x0002
    def i_MOV_C_w(self, op):
        ea = self.m16(op); self.C = self.rd(ea)
    def i_ANA_C_A(self, op):
        self.C &= self.A; self.set_z(self.C == 0)
    def i_ORAW_wa(self, op):
        ea = (self.V << 8) | self.fetch(); self.A |= self.rd(ea); self.set_z(self.A == 0)
    def i_SKIT_FE1(self, op):
        if self.IRR & 0x0040: self.PSW |= SK
        self.IRR &= ~0x0040
    def i_RETI(self, op):
        lo = self.pop8(); hi = self.pop8(); self.PC = (hi << 8) | lo
        self.PSW = self.pop8()
    def i_MOV_A_E(self, op): self.A = self.E
    def i_MOV_ANM_A(self, op): self.ANM = self.A
    def i_MOV_A_D(self, op): self.A = self.D
    def i_OFFA_A_C(self, op):
        if self.A & self.C: self.PSW &= ~Z
        else: self.PSW = self.PSW | Z | SK
    def i_LTI_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A - imm) & 0xFF; self.zhc_sub(tmp, self.A, 0); self.skip_cy()
    def i_MOV_E_A(self, op): self.E = self.A
    def i_MOV_A_CR0(self, op): self.A = 0xFF        # AN0-AN3 read FFh in MAME (an0_r..an3_r)
    def i_GTI_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A - imm - 1) & 0xFF; self.zhc_sub(tmp, self.A, 0)
        if not (self.PSW & CY): self.PSW |= SK
    def i_LTA_A_E(self, op):
        tmp = (self.A - self.E) & 0xFF; self.zhc_sub(tmp, self.A, 0); self.skip_cy()
    def i_ORI_B_xx(self, op):
        self.B |= self.fetch(); self.set_z(self.B == 0)
    def i_MOV_D_A(self, op): self.D = self.A
    def i_LTI_E_xx(self, op):
        imm = self.fetch(); tmp = (self.E - imm) & 0xFF; self.zhc_sub(tmp, self.E, 0); self.skip_cy()
    def i_GTI_E_xx(self, op):
        imm = self.fetch(); tmp = (self.E - imm - 1) & 0xFF; self.zhc_sub(tmp, self.E, 0)
        if not (self.PSW & CY): self.PSW |= SK
    def i_STEAX_Hp(self, op):
        self.wr(self.HL, self.EAL); self.wr(self.HL + 1, self.EAH); self.set_hl((self.HL + 2) & 0xFFFF)
    def i_SUBNB_A_C(self, op):
        tmp = (self.A - self.C) & 0xFF; self.zhc_sub(tmp, self.A, 0); self.A = tmp
        if not (self.PSW & CY): self.PSW |= SK
    def i_MOV_A_PB(self, op): self.A = self.port_b_in()
    def handle_timers(self, cycles):
        # MAME upd7810_device::handle_timers, ETIMER part: ECNT counts every 12/3 = 4 cycles
        self.cycles += cycles
        if (self.ETMM & 0x0C) == 0:
            self.ECNT = 0; return
        if (self.ETMM & 0x03) == 0:
            self.OVCE += cycles
            while self.OVCE >= 4:
                self.OVCE -= 4
                self.ECNT = (self.ECNT + 1) & 0xFFFF
                if self.ETM0 == self.ECNT: self.IRR |= 0x0020        # INTFE0
                if self.ETM1 == self.ECNT: self.IRR |= 0x0040        # INTFE1
                if (self.ETMM & 0x0C) == 0x0C and self.ETM1 == self.ECNT: self.ECNT = 0
    # ---------------- interrupts (upd7810.cpp take_irq, INTFE1 -> vector 0018h) ----------------
    INTFE1 = 0x0040; INTFE0 = 0x0020; INTFNMI = 0x0001
    def check_irq(self):
        if not self.IFF_pending: return False
        if self.IRR & self.INTFE1 and not (self.MKL & 0x40):
            vec = 0x0018
        elif self.IRR & self.INTFE0 and not (self.MKL & 0x20):
            vec = 0x0018
        else:
            return False
        self.IRR &= ~(self.INTFE1 if vec and (self.IRR & self.INTFE1) else self.INTFE0)
        self.IFF_pending = 0
        self.push8(self.PSW); self.push8(self.PC >> 8); self.push8(self.PC & 0xFF)   # MAME take_irq order
        self.PSW &= ~(SK | L0 | L1)                                                 # MAME clears SK/L0/L1 on entry
        self.PC = vec
        return True

    # --- batch 6: remaining instructions in the panel code (transcribed from upd7810_opcodes.cpp) ---
    def i_DAA(self, op):
        l = self.A & 0x0F; h = self.A >> 4; adj = 0x00; old_cy = self.PSW & CY
        if not (self.PSW & HC):
            if l < 10:
                if not (h < 10 and not (self.PSW & CY)): adj = 0x60
            else:
                if h < 9 and not (self.PSW & CY): adj = 0x06
                else: adj = 0x66
        elif l < 3:
            if h < 10 and not (self.PSW & CY): adj = 0x06
            else: adj = 0x66
        tmp = (self.A + adj) & 0xFF
        self.zhc_add(tmp, self.A, self.PSW & CY)
        self.PSW |= old_cy
        self.A = tmp
    def i_SOFTI(self, op):
        self.push8(self.PSW); self.push8(self.PC >> 8); self.push8(self.PC & 0xFF)
        self.PC = 0x0060
    def i_illegal(self, op): pass                     # MAME logs and continues
    def i_illegal2(self, op): pass
    def i_MUL_B(self, op): self.set_ea((self.A * self.B) & 0xFFFF)
    def i_SUB_A_C(self, op):
        tmp = (self.A - self.C) & 0xFF; self.zhc_sub(tmp, self.A, 0); self.A = tmp
    def i_DIV_A(self, op):
        if self.A:
            rem = self.EA % self.A; self.set_ea((self.EA // self.A) & 0xFFFF); self.A = rem
        else:
            self.A = self.EAL; self.set_ea(0xFFFF)
    def i_DIV_B(self, op):
        if self.B:
            rem = self.EA % self.B; self.set_ea((self.EA // self.B) & 0xFFFF); self.B = rem
        else:
            self.B = self.EAL; self.set_ea(0xFFFF)
    def i_SUINB_A_xx(self, op):
        imm = self.fetch(); tmp = (self.A - imm) & 0xFF; self.zhc_sub(tmp, self.A, 0); self.A = tmp
        if not (self.PSW & CY): self.PSW |= SK                 # SKIP_NC
    def i_MVI_ANM_xx(self, op): self.ANM = self.fetch()
    def i_NEIW_wa_xx(self, op):
        ea = (self.V << 8) | self.fetch(); imm = self.fetch(); m = self.rd(ea)
        tmp = (m - imm) & 0xFF; self.zhc_sub(tmp, m, 0)
        if not (self.PSW & Z): self.PSW |= SK                  # SKIP_NZ
    def i_NEGA(self, op): self.A = (~self.A + 1) & 0xFF
    # ---------------- instructions ----------------
    def i_NOP(self, op): pass
    def i_JR(self, op):
        offs = ((op << 2) & 0xFF)
        offs = (offs - 256 if offs > 127 else offs) >> 2    # int8 (OP << 2) >> 2
        self.PC = (self.PC + offs) & 0xFFFF
    def i_LXI_S_w(self, op):
        lo = self.fetch(); hi = self.fetch(); self.SP = (hi << 8) | lo
    def i_JMP_w(self, op):
        lo = self.fetch(); hi = self.fetch(); self.PC = (hi << 8) | lo
    def i_CALL_w(self, op):
        lo = self.fetch(); hi = self.fetch()
        self.SP = (self.SP - 1) & 0xFFFF; self.wr(self.SP, self.PC >> 8)
        self.SP = (self.SP - 1) & 0xFFFF; self.wr(self.SP, self.PC & 0xFF)
        self.PC = (hi << 8) | lo
    def i_RET(self, op):
        lo = self.rd(self.SP); self.SP = (self.SP + 1) & 0xFFFF
        hi = self.rd(self.SP); self.SP = (self.SP + 1) & 0xFFFF
        self.PC = (hi << 8) | lo
    def i_MVI_A_xx(self, op):
        if self.PSW & L1:                  # overlay active: skip the operand
            self.PC = (self.PC + 1) & 0xFFFF; return
        self.A = self.fetch(); self.PSW |= L1
    def i_MVI_B_xx(self, op): self.B = self.fetch()
    def i_MVI_C_xx(self, op): self.C = self.fetch()
    def i_LXI_H_w(self, op):
        if self.PSW & L0:
            self.PC = (self.PC + 2) & 0xFFFF; return
        self.L = self.fetch(); self.H = self.fetch(); self.PSW |= L0
    def i_LXI_D_w(self, op):
        self.E = self.fetch(); self.D = self.fetch()
    def i_LHLD_w(self, op):
        lo = self.fetch(); hi = self.fetch(); ea = (hi << 8) | lo
        self.L = self.rd(ea); self.H = self.rd(ea + 1)
    def i_LDAW_wa(self, op):
        ea = (self.V << 8) | self.fetch(); self.A = self.rd(ea)
    def i_STAW_wa(self, op):
        ea = (self.V << 8) | self.fetch(); self.wr(ea, self.A)
    def i_MVIW_wa_xx(self, op):
        ea = (self.V << 8) | self.fetch(); imm = self.fetch(); self.wr(ea, imm)
    def i_DCRW_wa(self, op):
        ea = (self.V << 8) | self.fetch()
        old_cy = self.PSW & CY
        m = self.rd(ea); tmp = (m - 1) & 0xFF
        self.zhc_sub(tmp, m, 0)
        self.wr(ea, tmp)
        self.skip_cy()
        self.PSW = (self.PSW & ~CY) | old_cy
    def i_OFFI_A_xx(self, op):
        imm = self.fetch()
        self.set_z((self.A & imm) == 0)
        self.skip_z()
    def i_JRE(self, op):
        offs = self.fetch()
        if op & 0x01: self.PC = (self.PC - (256 - offs)) & 0xFFFF
        else: self.PC = (self.PC + offs) & 0xFFFF
    def i_EXA(self, op):
        ea = self.EA; self.set_ea(self.EA2); self.EA2 = ea
        va = self.VA; self.V = (self.VA2 >> 8) & 0xFF; self.A = self.VA2 & 0xFF; self.VA2 = va
    def i_STAX_H_B(self, op):
        self.wr((self.HL + self.B) & 0xFFFF, self.A)
    def i_LDAX_H_B(self, op):
        self.A = self.rd((self.HL + self.B) & 0xFFFF)
    def i_XRA_A_C(self, op):
        self.A ^= self.C; self.set_z(self.A == 0)
    def i_MVI_PB_xx(self, op):
        self.PB_OUT = self.fetch()          # pad row select (MAME subcpu_pb_w)
