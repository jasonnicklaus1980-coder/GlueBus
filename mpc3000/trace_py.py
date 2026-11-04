"""Python counterpart of mpc3k.c's --trace output, for lockstep comparison."""
import sys
sys.path.insert(0, '/home/claude/mpc3000')
from run_main import make, skip_idle
def main(max_steps, trace, short_search=False):
    bus, cpu = make(); patched = False; last = 0; out = []
    while cpu.steps < max_steps and cpu.stop is None:
        if short_search and not patched and cpu.s[1] == 0xD5CC and cpu.ip == 0x0644:
            bus.wr8(0x8010*16 + 0x834A, 1); patched = True
        if skip_idle(cpu, bus): continue
        cpu.step()
        bus.io_log.clear()
        if trace and cpu.steps % trace == 0 and cpu.steps != last:
            last = cpu.steps
            r, s = cpu.r, cpu.s
            print('T %d %04X:%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X F=%04X irr=%02X isr=%02X imr=%02X c0=%d' % (
                cpu.steps, s[1], cpu.ip, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], s[0], s[2], s[3], s[1],
                cpu.f, bus.icu.irr, bus.icu.isr, bus.icu.imr, bus.tcu.count[0]), flush=True)

if __name__ == '__main__':
    main(int(sys.argv[1]), int(sys.argv[2]), len(sys.argv) > 3)
