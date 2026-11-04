# MPC3000 front-panel protocol (as the OS 3.10 receives it)

Read from the OS's IRQ5 handler (D66E:0063), which services SIO channel 2. MAME wires the panel
uPD78C10's TxD to channels 2 and 3; channel 2 RX -> IRQ5 (intp5), channel 3 -> IRQ7 (masked).

Messages are MIDI-like: a status byte, then data bytes. The handler's state offsets are in
segment D66E (a dump starting at D66E:0063 has state X at offset X-63h).

| Status | Data            | Meaning                                                          |
|--------|-----------------|------------------------------------------------------------------|
| 90h    | pp vv, pp < 10h | Drum pad pp, velocity vv. Panel numbering runs from the top row: |
|        |                 | pp 0-3 = A13-A16, 4-7 = A09-A12, 8-B = A05-A08, C-F = A01-A04     |
|        |                 | (table D66E:0053 = 0C 0D 0E 0F 08 09 0A 0B 04 05 06 07 00 01 02 03) |
| 90h    | kk vv, kk >= 40h| Button: index = (kk & 7Fh) - 40h -> function code at D66E:0002;  |
|        |                 | vv != 0 press, vv = 0 release                                    |
| A0h    | pp vv           | Pad pressure (aftertouch)                                        |
| B0h    | xx vv           | Note-variation slider value (panel sends B0 00 00 at start-up)   |
| E0h    | ...             | Data-entry dial (panel sends E0 00 00 when the dial moves)       |

## Button table (index: function code), read from D66E:0002 at runtime
00:63 01:62 02:61 03:37 04:38 05:39 06:78 07:79 08:7A 09:75 0A:48 0B:34 0C:35 0D:36 0E:46 0F:4B
10:4A 11:4C 12:47 13:-- 14:31 15:32 16:33 17:55 18:53 19:6C 1A:73 1B:4F 1C:64 1D:2E 1E:30 1F:0D
20:6B 21:41 22:5E 23:74 24:42 25:2B 26:45 27:52 28:54 29:43 2A:3C 2B:21 2C:3E 2D:4D 2E:2D 2F:5B
30:7B 31:2F 32:7D 33:5D 34:56 35:57 36:58 37:59 38:5A 39:69

Codes 30h-39h are the keypad digits '0'-'9'; 0Dh is ENTER; 75h is Cancel (index 09, kk = 49h).

## Screens reached by pressing buttons at the Play/Record screen (inject.py)
01 Assign '16 Levels' | 0E Save to Disk / Load, Other | 0F Tempo | 10 Mixer | 11 Programs / Sounds
12 Song Mode | 17 Edit Sequence | 18 Step Edit | 19 Edit Loop | 1A MIDI | 1B Metronome
21 Auto Punch | 23 Transpose | 24 Simul Sequence | 26 Erase | 27 Timing Correct / Step Size
28 Play/Record (Record Ready)
(Each press was made from the screen left by the previous one, so a few keys may act differently
from a fresh Play/Record screen. The mapping from index to physical button is not yet confirmed.)

## Consequence for an MPC X front end
The front end can send these messages straight into channel 2. The panel processor does not need
to be emulated for buttons, pads, slider or dial.
