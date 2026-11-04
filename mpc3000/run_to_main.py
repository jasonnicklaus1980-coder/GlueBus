"""Boot the MPC3000 BIOS/OS 3.10 to the Play/Record main screen and print the LCD.
TEST SETTING: the SCSI search time (byte DS:834A, value 1Eh = 30 -> 300 timer units ~60 s of
machine time) is set to 1 when the search starts, so the search ends in ~2 s of machine time.
This mimics a shorter user setting; no ROM code is changed. Takes ~4-5 minutes in Python."""
import sys, time
sys.path.insert(0, '/home/claude/mpc3000')
from run_main import make, skip_idle
from lcd_text import screen
def main(limit_s=280, short_search=True):
    bus, cpu = make(); patched = False; t0 = time.time()
    while time.time() - t0 < limit_s:
        if short_search and not patched and cpu.s[1] == 0xD5CC and cpu.ip == 0x0644:
            bus.wr8(0x8010*16 + 0x834A, 1); patched = True
        if skip_idle(cpu, bus): continue
        cpu.step()
        if cpu.stop: break
    print(screen(bus.io_log))
    return bus, cpu
if __name__ == '__main__':
    main()
