"""Boot to the main screen, then press panel buttons by injecting the OS's panel messages
(90h, 40h+index, velocity) into SIO channel 2, one byte at a time, waiting for the OS to read each."""
import sys, time
sys.path.insert(0, '/home/claude/mpc3000')
from run_main import make, skip_idle
from lcd_text import screen
def boot(limit_s=150):
    bus, cpu = make(); patched = False; t0 = time.time()
    while time.time() - t0 < limit_s:
        if not patched and cpu.s[1] == 0xD5CC and cpu.ip == 0x0644:
            bus.wr8(0x8010*16 + 0x834A, 1); patched = True
        if skip_idle(cpu, bus): continue
        cpu.step()
        if patched and cpu.steps > 16000000: break
    return bus, cpu
def run(bus, cpu, n):
    for _ in range(n):
        if skip_idle(cpu, bus): continue
        cpu.step()
def send(bus, cpu, data, max_wait=200000):
    for b in data:
        bus.te.receive(2, b)
        w = 0
        while bus.te.rx_ready[2] and w < max_wait:
            if not skip_idle(cpu, bus): cpu.step()
            w += 1
def press(bus, cpu, kk, settle=400000):
    send(bus, cpu, [0x90, kk, 0x7F]); run(bus, cpu, 60000)
    send(bus, cpu, [0x90, kk, 0x00]); run(bus, cpu, settle)
