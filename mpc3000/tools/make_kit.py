"""Make an MPC3000 kit floppy: up to 16 WAVs -> .SND files + a .PGM that assigns them to pads A01-A16.
The .PGM is built from template.pgm, a program saved by the emulated OS 3.10 itself (see PGM_FORMAT.md).
Usage: python3 make_kit.py OUT.img KITNAME pad1.wav [pad2.wav ... pad16.wav]
On the MPC3000: DISK -> 7 -> select KITNAME.PGM -> <Load> (loads the program and its sounds)."""
import sys, os, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wav2snd import read_wav, make_snd
from make_floppy import build
PAD_NOTES = [37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53]   # A01..A16 (OS defaults)
NAMES_AT, NAME_SIZE, NOTES_AT, NOTE_SIZE, FIRST_NOTE = 0x0002, 17, 0x0AC0, 24, 35
PGM_NAME_AT = 0x0A82
def make_pgm(kit, sound_names):
    p = bytearray(open(os.path.join(HERE, 'template.pgm'), 'rb').read())
    for i in range(128): p[NAMES_AT + i * NAME_SIZE: NAMES_AT + (i + 1) * NAME_SIZE] = bytes(NAME_SIZE)
    for i, n in enumerate(sound_names):
        p[NAMES_AT + i * NAME_SIZE: NAMES_AT + i * NAME_SIZE + 16] = n.upper()[:16].ljust(16).encode('ascii', 'replace')
    for k in range(64): p[NOTES_AT + k * NOTE_SIZE] = 0xFF
    for i in range(len(sound_names)): p[NOTES_AT + (PAD_NOTES[i] - FIRST_NOTE) * NOTE_SIZE] = i
    p[PGM_NAME_AT:PGM_NAME_AT + 16] = kit.upper()[:16].ljust(16, '\0').encode('ascii', 'replace')
    return bytes(p)
if __name__ == '__main__':
    out, kit, wavs = sys.argv[1], sys.argv[2], sys.argv[3:19]
    tmp = tempfile.mkdtemp(); files = []; names = []
    for i, w in enumerate(wavs):
        nm = ''.join(c for c in os.path.splitext(os.path.basename(w))[0].upper() if c.isalnum())[:14] or 'S'
        nm = '%s%02d' % (nm[:14], i + 1) if nm in names else nm
        names.append(nm)
        path = os.path.join(tmp, nm[:8] + '.SND'); open(path, 'wb').write(make_snd(read_wav(w), nm)); files.append(path)
    pgm = os.path.join(tmp, ''.join(c for c in kit.upper() if c.isalnum())[:8] + '.PGM'); open(pgm, 'wb').write(make_pgm(kit, names))
    build(out, [pgm] + files)
    print('%s: %s with %d sounds: %s' % (out, os.path.basename(pgm), len(names), ', '.join('A%02d=%s' % (i + 1, n) for i, n in enumerate(names))))
