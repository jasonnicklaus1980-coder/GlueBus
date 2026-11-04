"""Operate the emulated MPC3000: boot with the panel CPU in the loop, cancel the SCSI search
with SOFT KEY 4, then press keys by name (KEYMAP.txt) and print the LCD after each press.
Usage: python3 drive.py "Disk" "Main Screen" ..."""
import sys, time, re
sys.path.insert(0, '/home/claude/mpc3000')
from link import build, run_pair
from run_main import skip_idle
from lcd_text import screen
KEYS = {}
for ln in open('/home/claude/mpc3000/KEYMAP.txt'):
    m = re.match(r'Y(\d) ([0-9A-F]{2})  (.+?)\s+note (\S+)', ln)
    if m and m.group(4) != '--' and m.group(3) != 'None':
        KEYS[m.group(3).lower()] = (int(m.group(1)), int(m.group(2), 16))
def boot(t_limit=200):
    bus, cpu, panel = build(); t0 = time.time()
    while not (cpu.s[1] == 0xD5CC and cpu.ip == 0x0644):        # main alone up to the search
        if skip_idle(cpu, bus): continue
        cpu.step()
    press(bus, cpu, panel, 'soft key 4')                       # Cancel
    return bus, cpu, panel
def press(bus, cpu, panel, name, hold=60000, settle=1500000):
    r, mask = KEYS[name.lower()]
    panel.pressed = {r: mask}; run_pair(bus, cpu, panel, hold)
    panel.pressed = {};        run_pair(bus, cpu, panel, settle)
if __name__ == '__main__':
    t0 = time.time()
    bus, cpu, panel = boot()
    print('[after boot + Cancel, %ds]' % int(time.time() - t0)); print(screen(bus.io_log))
    for k in sys.argv[1:]:
        press(bus, cpu, panel, k)
        print('[after %s, %ds]' % (k, int(time.time() - t0))); print(screen(bus.io_log))
