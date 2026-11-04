"""Convert WAV files to MPC3000 .SND sounds (format read back from OS 3.10 itself, see SND_FORMAT.md).
Mono 16-bit 44.1 kHz; stereo is mixed to mono, other rates are resampled (linear), 8/24/32-bit converted.
Usage: python3 wav2snd.py IN.wav [OUT.SND] [--name NAME] [--vol 100] [--tune 0]"""
import sys, wave, struct
def read_wav(path):
    w = wave.open(path); ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n); vals = []
    for i in range(n):
        acc = 0
        for c in range(ch):
            o = (i * ch + c) * sw
            if sw == 1: v = (raw[o] - 128) << 8
            elif sw == 2: v = struct.unpack_from('<h', raw, o)[0]
            elif sw == 3: v = int.from_bytes(raw[o:o + 3], 'little', signed=True) >> 8
            else: v = struct.unpack_from('<i', raw, o)[0] >> 16
            acc += v
        vals.append(acc // ch)
    if rate != 44100 and vals:
        m = int(len(vals) * 44100 / rate); out = []
        for j in range(m):
            x = j * rate / 44100; i = int(x); f = x - i
            a = vals[min(i, len(vals) - 1)]; b = vals[min(i + 1, len(vals) - 1)]
            out.append(int(a + (b - a) * f))
        vals = out
    return vals
def make_snd(samples, name, vol=100, tune=0):
    n = len(samples)
    hdr = bytes([1, 2]) + name.upper()[:16].ljust(16).encode('ascii', 'replace')
    hdr += bytes([0, max(0, min(100, vol)), tune & 0xFF, 0])            # ?, Vol%, Tune, mode (must be 0)
    hdr += struct.pack('<III', 0, n, n)                                  # soft start, soft end, length
    hdr += struct.pack('<HH', 2000, 1)                                   # as written by the OS (meaning unknown)
    assert len(hdr) == 0x26
    return hdr + b''.join(struct.pack('<h', max(-32768, min(32767, v))) for v in samples)
if __name__ == '__main__':
    a = sys.argv[1:]; opt = {}
    for k in ('--name', '--vol', '--tune'):
        if k in a: i = a.index(k); opt[k] = a[i + 1]; del a[i:i + 2]
    src = a[0]; dst = a[1] if len(a) > 1 else src.rsplit('.', 1)[0][:8].upper() + '.SND'
    name = opt.get('--name', src.split('/')[-1].rsplit('.', 1)[0])
    data = make_snd(read_wav(src), name, int(opt.get('--vol', 100)), int(opt.get('--tune', 0)))
    open(dst, 'wb').write(data); print('%s -> %s (%d samples, name %s)' % (src, dst, (len(data) - 0x26) // 2, name.upper()[:16]))
