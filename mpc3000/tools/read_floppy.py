"""List or extract files from a FAT12 floppy image. Usage: read_floppy.py IMG [NAME [OUT]]"""
import sys, struct
def files(img):
    bps, spc, res, nfat, nroot, tot, media, spf = struct.unpack_from('<HBHBHHBH', img, 11)
    root = (res + nfat * spf) * bps; data = root + nroot * 32; fat = img[res * bps:(res + spf) * bps]
    def nxt(c):
        o = c * 3 // 2; v = fat[o] | (fat[o + 1] << 8)
        return (v >> 4) if c & 1 else (v & 0xFFF)
    out = {}
    for i in range(nroot):
        e = img[root + i * 32:root + i * 32 + 32]
        if e[0] in (0, 0xE5) or e[11] & 0x08: continue
        name = e[0:8].decode('latin1').rstrip() + '.' + e[8:11].decode('latin1').rstrip()
        cl, size = struct.unpack_from('<HI', e, 26); buf = b''
        while 2 <= cl < 0xFF8 and len(buf) < size:
            o = data + (cl - 2) * spc * bps; buf += img[o:o + spc * bps]; cl = nxt(cl)
        out[name] = buf[:size]
    return out
if __name__ == '__main__':
    img = open(sys.argv[1], 'rb').read(); fs = files(img)
    if len(sys.argv) == 2:
        for n, d in fs.items(): print('%-12s %8d' % (n, len(d)))
    else:
        d = fs[sys.argv[2]]
        if len(sys.argv) > 3: open(sys.argv[3], 'wb').write(d)
        else: sys.stdout.buffer.write(d)
