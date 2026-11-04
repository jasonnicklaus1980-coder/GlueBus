def screen(io_log):
    mem=bytearray(0x10000); ir=0; regs={}; cur=0
    for d,p,v in io_log:
        if d!='W': continue
        if p==0xE2: ir=v & 0x0F
        elif p==0xE0:
            v&=0xFF
            if ir==0x0A: cur=(cur&0xFF00)|v
            elif ir==0x0B: cur=(cur&0x00FF)|(v<<8)
            elif ir==0x0C: mem[cur]=v; cur=(cur+1)&0xFFFF
            else: regs[ir]=v
    start=regs.get(8,0)|(regs.get(9,0)<<8); cols=regs.get(2,39)+1
    rows=(regs.get(3,63)+1)//((regs.get(1,0x75)>>4)+1)
    out=['+'+'-'*cols+'+']
    for r in range(rows):
        out.append('|'+''.join(chr(b) if 32<=b<127 else ' ' for b in mem[start+r*cols:start+(r+1)*cols])+'|')
    out.append('+'+'-'*cols+'+')
    return '\n'.join(out)
