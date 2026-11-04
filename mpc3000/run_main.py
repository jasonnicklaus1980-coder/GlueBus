"""Run the MPC3000 main CPU with idle-skip: while the CPU sits in the boot ROM's idle
loop (FB00:022E-0233: STI; NOP x4; JMP) with no interrupt pending, time is advanced
straight to the next timer tick. The loop has no side effects, so this is exact."""
import sys, collections
sys.path.insert(0, '/home/claude/mpc3000')
from x86cpu import Bus, CPU
IDLE = range(0x022E, 0x0234)
def make():
    rom = open('/tmp/mpc3000_v310_x86.bin', 'rb').read()
    bus = Bus(rom); cpu = CPU(bus)
    cpu.s = [0, 0xFFF0, 0, 0]; cpu.ip = 0; cpu.r[4] = 0x0400
    return bus, cpu
def skip_idle(cpu, bus):
    if cpu.s[1] == 0xFB00 and cpu.ip in IDLE and bus.icu.pending() is None and (cpu.f & 0x200):
        t = bus.tcu
        if t.load[0] and t.mode[0] in (2, 3):
            need = max(1, t.count[0]) * 4 - t.tick_n      # instructions until counter 0 reaches 0
            t.step(need)
            return True
    return False
