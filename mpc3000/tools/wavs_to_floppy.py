"""Make an MPC3000 floppy image of sounds from WAV files (e.g. MPC X kit samples).
Usage: python3 wavs_to_floppy.py OUT.img a.wav b.wav ...   (1.44 MB; files that don't fit are skipped)
Load on the MPC3000: DISK -> 7 (Load,erase,rename) -> choose file -> soft key 1 <Load>."""
import sys, os, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wav2snd import read_wav, make_snd
from make_floppy import build
out, wavs = sys.argv[1], sys.argv[2:]
tmp = tempfile.mkdtemp(); files = []; used = 0; names = set()
for w in wavs:
    base = ''.join(c for c in os.path.splitext(os.path.basename(w))[0].upper() if c.isalnum())[:8] or 'SOUND'
    name = base; k = 1
    while name in names: name = base[:6] + '%02d' % k; k += 1
    data = make_snd(read_wav(w), os.path.splitext(os.path.basename(w))[0][:16])
    if used + len(data) > 1440 * 1024: print('skipped (disk full):', w); continue
    names.add(name); p = os.path.join(tmp, name + '.SND'); open(p, 'wb').write(data); files.append(p); used += len(data)
    print('%-40s -> %s.SND (%d KB)' % (w[-40:], name, len(data) // 1024))
build(out, files); print('%s: %d sounds, %d KB' % (out, len(files), used // 1024))
