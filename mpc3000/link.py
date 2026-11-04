"""Runs the main CPU and the panel uPD78C10 together (MAME wiring).
Panel TxD is wire-ORed to main SIO channels 2 and 3 RX. Channel 2 RX -> IRQ5 (merger),
channel 3 RX -> IRQ7. Bytes take 1280 panel cycles each (10 bits at 31250 baud, 12 MHz/3).
Panel instruction rate vs main: PANEL_RATIO panel steps per main step (ASSUMPTION)."""
import sys
sys.path.insert(0, '/home/claude/mpc3000')
from run_main import make, skip_idle
from upd7810core import Upd7810
FW = open('/mnt/user-data/uploads/mp3000__op_v1_0_am27c256__id0110_ic602.bin', 'rb').read()
PANEL_RATIO = 0.25
BYTE_CYCLES = 1280
class Panel(Upd7810):
    def __init__(self, deliver):
        super().__init__(FW); self.PC = 0; self.deliver = deliver
        self.pressed = {}; self.tx_pending = None; self.tx_done_at = 0
    def port_a_in(self):
        return 0xFF & ~self.pressed.get(7 - self.key_row, 0)
    def i_MOV_TXB_A(self, op):
        self.TXB = self.A; self.tx_pending = self.A; self.tx_done_at = self.cycles + BYTE_CYCLES
    def handle_timers(self, cycles):
        super().handle_timers(cycles)
        if self.tx_pending is not None and self.cycles >= self.tx_done_at:
            self.deliver(self.tx_pending); self.tx_pending = None
            self.IRR |= 0x0400                                  # INTFST: transmit complete
def build():
    bus, cpu = make()
    bus.te_sent = []
    def deliver(b):
        bus.te_sent.append(b); bus.te.receive(2, b); bus.te.receive(3, b)
    return bus, cpu, Panel(deliver)
def run_pair(bus, cpu, panel, main_steps, on_step=None, time_limit=None):
    import time
    t0 = time.time(); acc = 0.0
    for _ in range(main_steps):
        if on_step: on_step(cpu, bus, panel)
        if not skip_idle(cpu, bus):
            cpu.step()
        acc += PANEL_RATIO
        while acc >= 1.0:
            acc -= 1.0; panel.step()
        if cpu.stop or (time_limit and time.time() - t0 > time_limit): break
    return cpu.steps
