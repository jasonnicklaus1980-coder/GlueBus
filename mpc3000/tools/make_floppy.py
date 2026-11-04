"""Build a FAT12 floppy image (1.44 MB or 720 KB) from files, for the MPC3000 emulator.
Usage: python3 make_floppy.py OUT.img FILE [FILE ...] [--dd]"""
import sys, struct
def build(out, files, dd=False):
    spt, tot, spf, nroot, media = (9, 1440, 3, 112, 0xF9) if dd else (18, 2880, 9, 224, 0xF0)
    bps, spc, res, nfat = 512, (2 if dd else 1), 1, 2
    img = bytearray(tot * bps)
    bs = bytearray(512)
    bs[0:3] = b'\xEB\x3C\x90'; bs[3:11] = b'MPC3KEMU'
    struct.pack_into('<HBHBHHBHHHLL', bs, 11, bps, spc, res, nfat, nroot, tot, media, spf, spt, 2, 0, 0)
    bs[36] = 0; bs[38] = 0x29; bs[43:54] = b'MPC3000    '; bs[54:62] = b'FAT12   '; bs[510:512] = b'\x55\xAA'
    img[0:512] = bs
    fat = [0] * (tot // spc + 2); fat[0] = 0xF00 | media; fat[1] = 0xFFF
    root_off = (res + nfat * spf) * bps; data_off = root_off + nroot * 32; clsz = spc * bps
    cl = 2; ent = 0
    for path in files:
        data = open(path, 'rb').read(); name = path.split('/')[-1].upper()
        base, _, ext = name.partition('.')
        n = (len(data) + clsz - 1) // clsz
        first = cl if n else 0
        for i in range(n):
            fat[cl + i] = (cl + i + 1) if i < n - 1 else 0xFFF
            img[data_off + (cl + i - 2) * clsz: data_off + (cl + i - 2) * clsz + clsz] = data[i * clsz:(i + 1) * clsz].ljust(clsz, b'\0')
        e = bytearray(32); e[0:8] = base[:8].ljust(8).encode(); e[8:11] = ext[:3].ljust(3).encode(); e[11] = 0x20
        struct.pack_into('<HI', e, 26, first, len(data))
        img[root_off + ent * 32: root_off + ent * 32 + 32] = e; ent += 1; cl += n
    fb = bytearray(spf * bps)
    for i in range(0, len(fat) - 1, 2):
        a, b = fat[i], fat[i + 1] if i + 1 < len(fat) else 0
        o = i * 3 // 2
        if o + 2 < len(fb): fb[o] = a & 0xFF; fb[o + 1] = ((a >> 8) & 0xF) | ((b & 0xF) << 4); fb[o + 2] = b >> 4
    for k in range(nfat): img[(res + k * spf) * bps:(res + (k + 1) * spf) * bps] = fb
    open(out, 'wb').write(img)
if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    build(a[0], a[1:], '--dd' in sys.argv)
