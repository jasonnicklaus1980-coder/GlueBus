"""Add files to partition A..Z of an MPC3000 SCSI disk image formatted by the OS (FAT12 per partition,
11-sector FATs, 16-sector clusters).
Usage: python3 hd_add.py HD.img PART(A-Z) FILE [FILE ...]"""
import sys, struct
def add(path, part, files):
    f = open(path, 'r+b'); bs0 = f.read(512)
    psize = struct.unpack_from('<H', bs0, 19)[0] * 512          # partitions are contiguous, equal size
    base = (ord(part.upper()) - 65) * psize; f.seek(base); bs = f.read(512)
    bps, spc, res, nfat, nroot, tot, media, spf = struct.unpack_from('<HBHBHHBH', bs, 11)
    fat_off = base + res * bps; root_off = fat_off + nfat * spf * bps; data_off = root_off + nroot * 32
    f.seek(fat_off); fat = bytearray(f.read(spf * bps)); ncl = (tot - (data_off - base) // bps) // spc + 2
    f.seek(root_off); root = bytearray(f.read(nroot * 32)); clsz = spc * bps
    def g(c):                                                    # FAT12 (what the OS follows; see PROGRESS.md)
        o = c * 3 // 2; v = fat[o] | (fat[o + 1] << 8)
        return (v >> 4) if c & 1 else (v & 0xFFF)
    def s(c, v):
        o = c * 3 // 2
        if c & 1: fat[o] = (fat[o] & 0x0F) | ((v << 4) & 0xF0); fat[o + 1] = (v >> 4) & 0xFF
        else: fat[o] = v & 0xFF; fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F)
    for p in files:
        data = open(p, 'rb').read(); name = p.split('/')[-1].upper(); b, _, e = name.partition('.')
        n = max(1, (len(data) + clsz - 1) // clsz); free = [c for c in range(2, ncl) if g(c) == 0][:n]
        if len(free) < n: raise SystemExit('partition full')
        for i, c in enumerate(free):
            s(c, free[i + 1] if i + 1 < n else 0xFFF)
            f.seek(data_off + (c - 2) * clsz); f.write(data[i * clsz:(i + 1) * clsz].ljust(clsz, b'\0'))
        slot = next(i for i in range(nroot) if root[i * 32] in (0, 0xE5))
        ent = bytearray(32); ent[0:8] = b[:8].ljust(8).encode(); ent[8:11] = e[:3].ljust(3).encode(); ent[11] = 0x20
        struct.pack_into('<HI', ent, 26, free[0], len(data)); root[slot * 32:slot * 32 + 32] = ent
        print('%s -> partition %s, %d clusters from %d' % (name, part.upper(), n, free[0]))
    for k in range(nfat): f.seek(fat_off + k * spf * bps); f.write(fat)
    f.seek(root_off); f.write(root); f.close()
if __name__ == '__main__': add(sys.argv[1], sys.argv[2], sys.argv[3:])
