"""EXPERIMENT ONLY: not a sound-chip model. While the boot polls DSP port 62h for the
transfer-address nibble (CB34:011D), return the value the loop compares against (BX)."""
import sys, time, collections
sys.path.insert(0, '/home/claude/mpc3000')
from run_main import make, skip_idle
def run(limit_s=200, max_steps=None, log=None):
    bus, cpu = make()
    state = {'stub': None, 'hits': collections.Counter()}
    o = bus.io_read
    def rd(port, size):
        if port == 0x62 and state['stub'] is not None:
            state['hits'][(cpu.s[1], cpu.ip)] += 1
            return state['stub'] & 0xFFFF
        return o(port, size)
    bus.io_read = rd
    ticks = [0]; oa = bus.icu.acknowledge
    def ack(n):
        if n == 0: ticks[0] += 1
        return oa(n)
    bus.icu.acknowledge = ack
    t0 = time.time()
    while time.time() - t0 < limit_s and (max_steps is None or cpu.steps < max_steps):
        state['stub'] = cpu.r[3] if (cpu.s[1] == 0xCB34 and cpu.ip == 0x011D) else None
        if skip_idle(cpu, bus): continue
        cpu.step()
        if cpu.stop: break
    return bus, cpu, ticks[0], state
