/* mpc3k.c - MPC3000 (NEC V53A) emulator core, C port of x86cpu.py.
 * Line-for-line translation of the Python model, so the two can be run in lockstep.
 * Includes the same approximations (TCU 4 instructions per count, DSP DMA every 64 steps).
 * Build: gcc -O2 -o mpc3k mpc3k.c
 * Usage: mpc3k ROM.bin [--steps N] [--trace N] [--short-search] [--screen] [--time S]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

/* ---------------- memory ---------------- */
static u8 rom[0x80000], ram[0x100000], nvram[0x1000];
static u16 pgr[64];
static int xa;

static inline u32 phys(u32 a) {
    if (xa) return ((u32)(pgr[(a >> 14) & 0x3F] & 0x3FF) << 14) | (a & 0x3FFF);
    return a & 0xFFFFF;
}
static inline u8 read_phys(u32 p) {
    if (p < 0x100000) return rom[p & 0x7FFFF];
    if (p >= 0x300000 && p < 0x400000) return ram[p - 0x300000];
    if (p >= 0x500000 && p < 0x501000) return nvram[p - 0x500000];
    return 0xFF;
}
static inline void write_phys(u32 p, u8 v) {
    if (p >= 0x300000 && p < 0x400000) ram[p - 0x300000] = v;
    else if (p >= 0x500000 && p < 0x501000) nvram[p - 0x500000] = v;
}
static inline u8 rd8(u32 a) { return read_phys(phys(a & (xa ? 0xFFFFFF : 0xFFFFF))); }
static inline void wr8(u32 a, u8 v) { write_phys(phys(a & (xa ? 0xFFFFFF : 0xFFFFF)), v); }
static inline u16 rd16(u32 a) { return rd8(a) | (rd8(a + 1) << 8); }
static inline void wr16(u32 a, u16 v) { wr8(a, v & 0xFF); wr8(a + 1, v >> 8); }

/* ---------------- ICU (8259) ---------------- */
static struct { int init_stage, need_icw4; u8 icw[4], imr, irr, isr; } icu = {0, 0, {0}, 0xFF, 0, 0};
static void raise_irq(int n) { icu.irr |= 1 << n; }
static void icu_write(int a0, u8 v) {
    if (a0 == 0 && (v & 0x10)) { icu.init_stage = 1; icu.need_icw4 = v & 1; icu.imr = 0; icu.irr = 0; icu.isr = 0; }
    else if (a0 == 0 && (v & 0x18) == 0) {
        if ((v & 0xE0) == 0x20) { for (int i = 0; i < 8; i++) if (icu.isr & (1 << i)) { icu.isr &= ~(1 << i); break; } }
        else if ((v & 0xE0) == 0x60) icu.isr &= ~(1 << (v & 7));
    }
    else if (a0 == 1 && icu.init_stage == 1) { icu.icw[1] = v & 0xF8; icu.init_stage = icu.need_icw4 ? 2 : 0; }
    else if (a0 == 1 && icu.init_stage == 2) { icu.icw[3] = v; icu.init_stage = 0; }
    else if (a0 == 1) icu.imr = v;
}
static u8 icu_read(int a0) { return a0 ? icu.imr : icu.irr; }
static int icu_pending(void) {
    u8 r = icu.irr & ~icu.imr & ~icu.isr;
    for (int i = 0; i < 8; i++) if (r & (1 << i)) return i;
    return -1;
}
static int icu_ack(int n) { icu.irr &= ~(1 << n); icu.isr |= 1 << n; return (icu.icw[1] | n) & 0xFF; }

/* ---------------- TCU (8254-style) ---------------- */
static struct { int mode[3], lsb_msb[3], phase[3], load[3]; long count[3]; long tick_n; } tcu;
static u64 timer_ticks;
static void tcu_write(int k, u8 v) {
    if (k == 3) { int c = (v >> 6) & 3; if (c == 3) return;
        tcu.mode[c] = (v >> 1) & 7; tcu.lsb_msb[c] = (v >> 4) & 3; tcu.phase[c] = 0; return; }
    int c = k;
    if (tcu.lsb_msb[c] == 1) { tcu.load[c] = v; tcu.count[c] = tcu.load[c]; }
    else if (tcu.lsb_msb[c] == 2) { tcu.load[c] = v << 8; tcu.count[c] = tcu.load[c]; }
    else if (tcu.lsb_msb[c] == 3) {
        if (tcu.phase[c] == 0) { tcu.load[c] = (tcu.load[c] & 0xFF00) | v; tcu.phase[c] = 1; }
        else { tcu.load[c] = (tcu.load[c] & 0x00FF) | (v << 8); tcu.phase[c] = 0; tcu.count[c] = tcu.load[c]; }
    }
}
static u8 tcu_read(int k) { return tcu.count[k] & 0xFF; }
static void tcu_step(long n) {
    tcu.tick_n += n;
    if (tcu.tick_n < 4) return;
    long ticks = tcu.tick_n / 4; tcu.tick_n %= 4;
    if (tcu.load[0] && (tcu.mode[0] == 2 || tcu.mode[0] == 3)) {
        tcu.count[0] -= ticks;
        while (tcu.count[0] <= 0) { tcu.count[0] += tcu.load[0]; raise_irq(0); timer_ticks++; }
    }
}

/* ---------------- FDC (uPD72069, no disk) ---------------- */
static const int FDC_PARAMS[32] = { [0x03]=2, [0x04]=1, [0x05]=8, [0x06]=8, [0x07]=1, [0x08]=0, [0x0A]=1,
                                    [0x0D]=5, [0x0F]=2 };
static struct { int cmd_len, need, res[8], nres, rpos, last_param; u8 cmd0; } fdc;
static void fdc_execute(void) {
    int op = fdc.cmd0 & 0x1F; fdc.cmd_len = 0; fdc.nres = 0; fdc.rpos = 0;
    if (op == 0x04) { fdc.res[0] = fdc.last_param & 3; fdc.nres = 1; }
    else if (op == 0x08) { fdc.res[0] = 0x48; fdc.res[1] = 0; fdc.nres = 2; }
    else if (op == 0x07 || op == 0x0F) raise_irq(3);
    else if (op == 0x06 || op == 0x05) { int r[7] = {0x48,1,0,0,0,0,0}; memcpy(fdc.res, r, sizeof r); fdc.nres = 7; raise_irq(3); }
    else { fdc.res[0] = 0x80; fdc.nres = 1; }
}
static u8 fdc_status(void) { return (fdc.rpos < fdc.nres) ? 0xD0 : 0x80; }
static void fdc_write(u8 v) {
    if (fdc.cmd_len == 0) {
        fdc.need = FDC_PARAMS[v & 0x1F]; fdc.cmd0 = v; fdc.cmd_len = 1;
        if (fdc.need == 0) fdc_execute();
        return;
    }
    fdc.cmd_len++; fdc.need--; fdc.last_param = v;
    if (fdc.need == 0) fdc_execute();
}
static u8 fdc_read(void) { return (fdc.rpos < fdc.nres) ? fdc.res[fdc.rpos++] : 0; }

static u64 dbg_keyons, dbg_ctl_writes, dbg_dma_words, dbg_vreg[32];
/* ---------------- DSP (L7A1045 / L6028 DSP-A), following MAME l7a1045_l6028_dsp_a.cpp ----------------
 * 32 voices, 32 MB sample RAM (16-bit linear samples, word addressed), 44.1 kHz stereo output.
 * Differences from MAME: linear interpolation instead of 8-point windowed sinc; delay effect
 * and individual outputs not rendered (only the stereo mix). */
#define DSP_WORDS (1u << 24)
static int16_t *wave;                                   /* 16 M words = 32 MB */
typedef struct {
    u32 start, end, step, pos, frac, loop_start; int sample_type;
    int env_volume, env_target, env_step, env_pos;
    int flt_freq, flt_target, flt_resonance, flt_step, flt_pos; long long l, b;
    int l_volume, r_volume, send_dest, send_level;
} Voice;
static struct { int cur_ch, cur_reg; u64 regs[16][32]; u16 control; Voice v[32]; u32 key; } dsp;
static void dsp_recalc_loop_start(Voice *v, int ch) {
    if ((dsp.regs[1][ch] >> 40) & 1) {
        u32 length = v->end - v->start;
        v->loop_start = (u32)((dsp.regs[2][ch] & 0xFFFFF000ull) >> 12);
        v->loop_start |= (u32)(dsp.regs[2][ch] & 0x000F) << 20;
        v->loop_start = v->end - v->loop_start;
        if (v->loop_start > length) v->loop_start = length;
    } else {
        u32 mult = ((u32)((dsp.regs[2][ch] & 0xFFFF0000ull) >> 16) ^ 0xFFFF) + 1;
        u32 base = dsp.regs[2][ch] & 0xFFFF;
        v->loop_start = (u32)(((u64)base * mult) >> 12);
    }
}
static void dsp_select_w(u16 v) { dsp.cur_ch = v & 0x1F; dsp.cur_reg = (v >> 8) & 0x0F; }
static u16 dsp_voiceregs_r(int port) {
    int off = (port - 0x62) / 2, ch = dsp.cur_ch; Voice *v = &dsp.v[ch];
    if (dsp.cur_reg == 0) {
        u32 cur = v->start + v->pos;
        u64 r = dsp.regs[0][ch] & 0xFFF000000000ull;
        r |= ((u64)cur << 12) & 0xFFFFFFFFF000ull; r |= v->frac & 0xFFF;
        dsp.regs[0][ch] = r;
    } else if (dsp.cur_reg == 3) {
        dsp.regs[3][ch] = (dsp.regs[3][ch] & 0xFFFF0000FFFFull) | ((u64)(v->env_volume & 0xFFFF) << 16);
    } else if (dsp.cur_reg == 5) {
        dsp.regs[5][ch] = (dsp.regs[5][ch] & 0xFFFF0000FFFFull) | ((u64)(v->flt_freq & 0xFFFF) << 16);
    }
    return (dsp.regs[dsp.cur_reg][ch] >> (off * 16)) & 0xFFFF;
}
static void dsp_voiceregs_w(int port, u16 data) {
    static const u64 mask[3] = {0xFFFFFFFF0000ull, 0xFFFF0000FFFFull, 0x0000FFFFFFFFull};
    int off = (port - 0x62) / 2, ch = dsp.cur_ch, r = dsp.cur_reg; Voice *v = &dsp.v[ch];
    dsp.regs[r][ch] = (dsp.regs[r][ch] & mask[off]) | ((u64)data << (off * 16));
    dbg_vreg[ch]++;
    u64 x = dsp.regs[r][ch];
    switch (r) {
    case 0: v->start = (x >> 12) & 0xFFFFFF; v->sample_type = (x >> 36) & 0xF;
            v->pos = 0; v->frac = 0; v->env_pos = 0; v->flt_pos = 0; v->l = v->b = 0;
            if (off == 2) dsp.regs[2][ch] = 0;
            break;
    case 1: v->end = (x >> 12) & 0xFFFFF0; v->step = x & 0xFFFF; if (off == 2) dsp_recalc_loop_start(v, ch); break;
    case 2: dsp_recalc_loop_start(v, ch); break;
    case 3: v->env_volume = (x & 0xFFFF0000ull) >> 16; v->env_pos = 0;
            if (off == 0 && data == 0) dsp.key &= ~(1u << ch);          /* MPC3000 writes 0 to offset 0 to silence */
            break;
    case 4: v->env_target = (x & 0xFFFF0000ull) >> 16; v->env_step = x & 0xFFFF; break;
    case 5: if (v->flt_pos == 0) v->flt_freq = (x & 0xFFFF0000ull) >> 16; break;
    case 6: v->flt_target = (x & 0xFFF00000ull) >> 16; v->flt_resonance = (x & 0x000F0000ull) >> 16; v->flt_step = x & 0xFFFF; break;
    case 7: v->r_volume = x & 0xFF; v->l_volume = (x >> 8) & 0xFF; v->send_dest = (x >> 16) & 0xFF; v->send_level = (x >> 24) & 0xFF; break;
    }
}

static void dsp_control_w(u16 data) {
    dsp.control = data; dbg_ctl_writes++;
    if (getenv("MPC3K_DSPLOG")) fprintf(stderr, "ctl %04X ch %d reg %d #%llu\n", data, dsp.cur_ch, dsp.cur_reg, (unsigned long long)dbg_ctl_writes);
    if (data & 0x100) {                                              /* key on the selected voice */
        Voice *v = &dsp.v[dsp.cur_ch];
        v->frac = 0; v->pos = 0; v->flt_pos = 0; v->l = v->b = 0; v->env_pos = 0;
        dsp.key |= 1u << dsp.cur_ch; dbg_keyons++;
        dsp_recalc_loop_start(v, dsp.cur_ch);
    }
}
static inline int32_t dsp_read_sample(const Voice *v, u32 addr) {
    if (v->sample_type != 0) return 0;                               /* no sample ROM on the MPC3000 */
    return wave[addr & (DSP_WORDS - 1)];
}
static int32_t audio_l, audio_r;                                     /* last rendered stereo sample */
static void dsp_render_sample(void) {
    int64_t L = 0, R = 0;
    for (int i = 0; i < 32; i++) {
        if (!(dsp.key & (1u << i))) continue;
        Voice *v = &dsp.v[i];
        u32 advance = v->frac >> 12; v->pos += advance; v->frac &= 0xFFF;
        if (v->end > v->start && v->start + v->pos >= v->end) v->pos = (v->end - v->start) - v->loop_start;
        u32 a = v->start + v->pos;
        int32_t s0 = dsp_read_sample(v, a), smp = s0;
        if (v->frac) {                                               /* linear interpolation (MAME: windowed sinc) */
            u32 a1 = a + 1; if (v->end > v->start && a1 >= v->end) a1 = v->end - v->loop_start;
            int32_t s1 = dsp_read_sample(v, a1);
            smp = s0 + (int32_t)(((int64_t)(s1 - s0) * (int32_t)v->frac) >> 12);
        }
        v->frac += v->step;
        v->env_pos += v->env_step; int steps = (u32)v->env_pos / 0x100;
        if (steps > 0) {
            if (v->env_volume < v->env_target) v->env_volume += steps < v->env_target - v->env_volume ? steps : v->env_target - v->env_volume;
            else if (v->env_volume > v->env_target) v->env_volume -= steps < v->env_volume - v->env_target ? steps : v->env_volume - v->env_target;
        }
        v->env_pos &= 0xFF;
        v->flt_pos += v->flt_step; int fs = (u32)v->flt_pos / 0x100;
        if (fs > 0) {
            if (v->flt_freq < v->flt_target) v->flt_freq += fs < v->flt_target - v->flt_freq ? fs : v->flt_target - v->flt_freq;
            else if (v->flt_freq > v->flt_target) v->flt_freq -= fs < v->flt_freq - v->flt_target ? fs : v->flt_freq - v->flt_target;
        }
        v->flt_pos &= 0xFF;
        int64_t h = (int64_t)smp - v->l - v->b + (((int64_t)v->flt_resonance * v->b) >> 4);
        v->b += ((int64_t)v->flt_freq * h) >> 15;
        v->l += ((int64_t)v->flt_freq * v->b) >> 15;
        int64_t fout = v->l;
        L += (fout * (int64_t)((u64)v->l_volume * (u64)v->env_volume)) >> 24;
        R += (fout * (int64_t)((u64)v->r_volume * (u64)v->env_volume)) >> 24;
    }
    audio_l = L > 32767 ? 32767 : L < -32768 ? -32768 : (int32_t)L;
    audio_r = R > 32767 ? 32767 : R < -32768 ? -32768 : (int32_t)R;
}
/* machine time: 4 instructions per microsecond (TCU assumption) -> 44100/4e6 = 441/40000 samples per instruction */
static u64 audio_acc, audio_samples;
static void (*audio_sink)(int16_t l, int16_t r);
static inline void dsp_advance(long n) {
    audio_acc += (u64)n * 441;
    while (audio_acc >= 40000) {
        audio_acc -= 40000; dsp_render_sample(); audio_samples++;
        if (audio_sink) audio_sink((int16_t)audio_l, (int16_t)audio_r);
    }
}

/* ---------------- DMA (8237 layout) ---------------- */
static struct { u16 addr[4], count[4]; int mask, tc; } dma = {{0},{0},0x0F,0};
static void dma_request(int ch) {
    if ((dma.mask >> ch) & 1) return;
    if (ch == 3) {
        Voice *v0 = &dsp.v[0];
        u32 wa = (v0->start + v0->pos) & (DSP_WORDS - 1);
        if (dsp.control & 0x20) {                /* control bit 5 = DMA read: MAME dma_r16_cb, sample RAM -> memory */
            wr16(dma.addr[3], v0->sample_type == 0 ? (u16)wave[wa] : 0);
        } else {                                 /* MAME dma_w16_cb: memory -> sample RAM at voice 0 start+pos */
            u16 w = rd16(dma.addr[3]);
            if (v0->sample_type == 0) wave[wa] = (int16_t)w;
        }
        v0->pos++; dbg_dma_words++;
        dma.addr[3] += 2;
        if (dma.count[3] == 0) dma.tc |= 1 << 3; else dma.count[3]--;
    }
}
static int dsp_div;
static inline void dsp_clk(void) { if (++dsp_div >= 64) { dsp_div = 0; if ((dsp.control >> 6) & 1) dma_request(3); } }

/* ---------------- SCSI (MB89352, no targets) ---------------- */
static struct { int bdid, sctl, scmd, ints, temp, tc, pctl, ssts, line, rad, int_enable, busfree_int; } spc =
    {0,0,0,0,0,0,0,0x05,0,1,0,0};
static void spc_irq(void) { if (spc.int_enable) raise_irq(3); }
static u8 spc_read(int n) {
    n &= 0x0F;
    switch (n) {
    case 0: return 1 << spc.bdid;
    case 1: return (spc.rad ? 0x80 : 0) | ((spc.sctl & 0x40) ? 0x40 : 0) | (spc.sctl & 0x1F);
    case 2: return spc.scmd; case 3: return 0xFF; case 4: return spc.ints; case 5: return spc.line;
    case 6: return spc.ssts; case 7: return 0; case 8: return (spc.line & 7) | (spc.busfree_int ? 0x80 : 0);
    case 0x0B: return spc.temp; case 0x0C: return (spc.tc >> 16) & 0xFF; case 0x0D: return (spc.tc >> 8) & 0xFF;
    case 0x0E: return spc.tc & 0xFF;
    }
    return 0xFF;
}
static void spc_write(int n, u8 v) {
    n &= 0x0F;
    if (n == 0) { spc.bdid = v & 7; spc.ssts &= ~0x40; spc.ssts |= 0x80; }
    else if (n == 1) { if (v & 0x80) { spc.rad = 1; spc.int_enable = 0; spc.ints = 0; } else spc.rad = 0;
                       spc.sctl = v; spc.int_enable = v & 1; }
    else if (n == 2) {
        spc.scmd = v;
        if (v & 0x10) { spc.ints |= 0x01; spc_irq(); }
        int cmd = (v >> 5) & 7;
        if (cmd == 0) { spc.line = 0; spc.ssts &= ~0x30; if (spc.busfree_int) spc_irq(); }
        else if (cmd == 1) { spc.ints |= 0x04; spc_irq(); }
        else if (cmd == 2) spc.line &= ~0x02;
        else if (cmd == 3) spc.line |= 0x02;
    }
    else if (n == 4) spc.ints &= ~v;
    else if (n == 8) { spc.pctl = v; spc.busfree_int = v & 0x80; }
    else if (n == 0x0B) spc.temp = v;
    else if (n == 0x0C) spc.tc = (spc.tc & 0xFFFF) | (v << 16);
    else if (n == 0x0D) spc.tc = (spc.tc & 0xFF00FF) | (v << 8);
    else if (n == 0x0E) spc.tc = (spc.tc & 0xFFFF00) | v;
}

/* ---------------- SIO (TE7774) ---------------- */
static struct { u8 rx[4]; int rx_ready[4]; u8 ctl1[4], ctl2[4], ctl3[4]; } te;
static void te_receive(int ch, u8 b) {
    te.rx[ch] = b; te.rx_ready[ch] = 1;
    if (te.ctl1[ch] & 0x04) raise_irq(ch == 3 ? 7 : 5);
}
static u8 te_read(int ch, int off) {
    if (off == 0) { te.rx_ready[ch] = 0; return te.rx[ch]; }
    if (off == 1) return 0x01 | (te.rx_ready[ch] ? 0x00 : 0x02);
    if (off == 2) return te.ctl2[ch];
    if (off == 3) return te.ctl3[ch];
    return 0xFF;
}
static void te_write(int ch, int off, u8 v) {
    if (off == 1) te.ctl1[ch] = v; else if (off == 2) te.ctl2[ch] = v; else if (off == 3) te.ctl3[ch] = v;
}

/* ---------------- LCD (HD61830, character mode), decoded live ---------------- */
static u8 lcd_mem[0x10000]; static int lcd_ir, lcd_cur; static int lcd_regs[16]; static long lcd_data_writes;

/* ---------------- on-chip peripheral relocation ---------------- */
static int IULA, TULA, OPHA;
static int hc365;

static u16 io_read(u32 port, int size) {
    port &= 0xFFFF;
    if (port >= 0xC0 && port <= 0xDF && !(port & 1)) return te_read((port - 0xC0) >> 3, (port & 7) >> 1);
    if (port >= 0xA0 && port <= 0xBF && !(port & 1)) return spc_read((port - 0xA0) >> 1);
    if (port == 0xC00B) { int v = dma.tc; dma.tc = 0; return v; }
    if (port >= 0xC000 && port <= 0xC00E) return 0;
    if (port == 0xC00F) return dma.mask;
    if (port == 0x62 || port == 0x64 || port == 0x66) return dsp_voiceregs_r(port);
    u32 pb = OPHA << 8;
    if (port >= pb + IULA && port <= pb + IULA + 1) return icu_read(port - (pb + IULA));
    if (port >= pb + TULA && port <= pb + TULA + 3) return tcu_read(port - (pb + TULA));
    u32 v;
    if (port == 0xE8) v = fdc_status() | (hc365 << 8);
    else if (port == 0xE9) v = hc365;
    else if (port == 0xEA) v = fdc_read();
    else if (port == 0xE2) v = 0;
    else if (port == 0xE0) v = 0;
    else if (port == 0xFF80) v = xa & 1;
    else if (port >= 0xFF00 && port <= 0xFF7E && !(port & 1)) v = pgr[(port - 0xFF00) / 2];
    else v = 0xFF;
    return v & (size == 2 ? 0xFFFF : 0xFF);
}
static void io_write(u32 port, u16 v, int size) {
    port &= 0xFFFF;
    if (port >= 0xC0 && port <= 0xDF && !(port & 1)) { te_write((port - 0xC0) >> 3, (port & 7) >> 1, v & 0xFF); return; }
    if (port >= 0xA0 && port <= 0xBF && !(port & 1)) { spc_write((port - 0xA0) >> 1, v & 0xFF); return; }
    if (port == 0x60) dsp_select_w(v);
    else if (port == 0x62 || port == 0x64 || port == 0x66) dsp_voiceregs_w(port, v);
    else if (port == 0x68) dsp_control_w(v);
    else if (port == 0x6C) dsp.regs[dsp.cur_reg][dsp.cur_ch] = 0;
    else if (port == 0xC000) dma.addr[0] = v; else if (port == 0xC002) dma.addr[1] = v;
    else if (port == 0xC004) dma.addr[2] = v; else if (port == 0xC006) dma.addr[3] = v;
    else if (port == 0xC001) dma.count[0] = v; else if (port == 0xC003) dma.count[1] = v;
    else if (port == 0xC005) dma.count[2] = v; else if (port == 0xC007) dma.count[3] = v;
    else if (port == 0xC00A) { if (v & 4) dma.mask |= 1 << (v & 3); else dma.mask &= ~(1 << (v & 3)); }
    else if (port == 0xC00F) dma.mask = v & 0x0F;
    if (port == 0xFFFA) IULA = v & 0xFF; else if (port == 0xFFF9) TULA = v & 0xFF; else if (port == 0xFFFC) OPHA = v & 0xFF;
    u32 pb = OPHA << 8;
    if (port >= pb + IULA && port <= pb + IULA + 1) { icu_write(port - (pb + IULA), v & 0xFF); return; }
    if (port >= pb + TULA && port <= pb + TULA + 3) { tcu_write(port - (pb + TULA), v & 0xFF); return; }
    if (port == 0xEA) fdc_write(v & 0xFF);
    if (port >= 0xFF00 && port <= 0xFF7E && !(port & 1) && size == 2 && !xa) pgr[(port - 0xFF00) / 2] = v & 0x3FF;
    if (port == 0xE2) lcd_ir = v & 0x0F;
    else if (port == 0xE0) {
        u8 b = v & 0xFF;
        if (lcd_ir == 0x0A) lcd_cur = (lcd_cur & 0xFF00) | b;
        else if (lcd_ir == 0x0B) lcd_cur = (lcd_cur & 0x00FF) | (b << 8);
        else if (lcd_ir == 0x0C) { lcd_mem[lcd_cur] = b; lcd_cur = (lcd_cur + 1) & 0xFFFF; lcd_data_writes++; }
        else lcd_regs[lcd_ir] = b;
    }
}

/* ---------------- CPU ---------------- */
enum { CF = 0x1, PF = 0x4, AF = 0x10, ZF = 0x40, SF = 0x80, TF = 0x100, IF = 0x200, DF = 0x400, OF = 0x800 };
static u16 R[8], S[4], IP, F = 0x0002;
static u64 steps;
static const char *stop_msg; static u32 stop_addr;
static int seg_ov, pending_valid; static u32 pending;
static u8 PARITY[256];

#define GETF(m) ((F & (m)) != 0)
static inline void SETF(int m, int on) { if (on) F |= m; else F &= ~m; }
static inline void lazy_zsp(u32 res, int bits) {
    u32 mask = (bits == 16) ? 0xFFFF : 0xFF, sign = (bits == 16) ? 0x8000 : 0x80, r = res & mask;
    SETF(ZF, r == 0); SETF(SF, (r & sign) != 0); SETF(PF, PARITY[r & 0xFF]);
}
static inline u8 fetch8(void) { u8 v = rd8((u32)S[1] * 16 + IP); IP++; return v; }
static inline u16 fetch16(void) { u16 lo = fetch8(); return lo | (fetch8() << 8); }
static u32 ea(int mod, int rm, int *segout) {
    int seg; u32 off;
    if (mod == 0 && rm == 6) { off = fetch16(); seg = 3; }
    else {
        int base;
        switch (rm) {
        case 0: base = R[3] + R[6]; break; case 1: base = R[3] + R[7]; break;
        case 2: base = R[5] + R[6]; break; case 3: base = R[5] + R[7]; break;
        case 4: base = R[6]; break; case 5: base = R[7]; break; case 6: base = R[5]; break; default: base = R[3];
        }
        seg = (rm == 2 || rm == 3 || rm == 6) ? 2 : 3;
        if (mod == 1) { int d = fetch8(); base += d > 127 ? d - 256 : d; }
        else if (mod == 2) base += fetch16();
        off = base & 0xFFFF;
    }
    if (seg_ov >= 0) seg = seg_ov;
    if (segout) *segout = seg;
    return ((u32)S[seg] * 16 + off) & 0xFFFFF;
}
static inline int reg_get(int i, int size) {
    if (size == 2) return R[i];
    return i >= 4 ? (R[i - 4] >> 8) & 0xFF : R[i] & 0xFF;
}
static inline void reg_set(int i, int size, int v) {
    if (size == 2) R[i] = v & 0xFFFF;
    else if (i >= 4) R[i - 4] = (R[i - 4] & 0x00FF) | ((v & 0xFF) << 8);
    else R[i] = (R[i] & 0xFF00) | (v & 0xFF);
}
static inline void modrm(int *mod, int *reg, int *rm) { u8 m = fetch8(); *mod = m >> 6; *reg = (m >> 3) & 7; *rm = m & 7; }
static int rm_get(int mod, int rm, int size) {
    if (mod == 3) return reg_get(rm, size);
    u32 a = ea(mod, rm, NULL); pending = a; pending_valid = 1;
    return size == 2 ? rd16(a) : rd8(a);
}
static void rm_set(int mod, int rm, int size, int v) {
    if (mod == 3) { reg_set(rm, size, v); return; }
    u32 a;
    if (pending_valid) { a = pending; pending_valid = 0; } else a = ea(mod, rm, NULL);
    if (size == 2) wr16(a, v); else wr8(a, v);
}
static inline void push(u16 v) { R[4] -= 2; wr16((u32)S[2] * 16 + R[4], v); }
static inline u16 pop(void) { u16 v = rd16((u32)S[2] * 16 + R[4]); R[4] += 2; return v; }
static int alu(int op, int a, int b, int size) {
    int bits = 8 * size, mask = (1 << bits) - 1, sign = 1 << (bits - 1), r, cf, of, c = GETF(CF);
    switch (op) {
    case 0: r = a + b; cf = r > mask; of = ((a ^ r) & (b ^ r) & sign) != 0; break;
    case 1: r = a | b; cf = of = 0; break;
    case 2: r = a + b + c; cf = r > mask; of = ((a ^ r) & (b ^ r) & sign) != 0; break;
    case 3: r = a - b - c; cf = (a & mask) < (b & mask) + c; of = ((a ^ b) & (a ^ r) & sign) != 0; break;
    case 4: r = a & b; cf = of = 0; break;
    case 6: r = a ^ b; cf = of = 0; break;
    default: r = a - b; cf = (a & mask) < (b & mask); of = ((a ^ b) & (a ^ r) & sign) != 0; break;
    }
    lazy_zsp(r, bits); SETF(CF, cf); SETF(OF, of); SETF(AF, 0);
    return r & mask;
}
static int cond(int c) {
    int cf = GETF(CF), zf = GETF(ZF), sf = GETF(SF), of = GETF(OF), pf = GETF(PF);
    switch (c) {
    case 0: return of; case 1: return !of; case 2: return cf; case 3: return !cf; case 4: return zf; case 5: return !zf;
    case 6: return cf || zf; case 7: return !(cf || zf); case 8: return sf; case 9: return !sf; case 10: return pf;
    case 11: return !pf; case 12: return sf != of; case 13: return sf == of; case 14: return zf || (sf != of);
    default: return !zf && sf == of;
    }
}
static void do_stop(const char *m, u32 a) { stop_msg = m; stop_addr = a; }
static void shift(int reg, int v, int cnt, int size, int mod, int rm) {
    int bits = 8 * size; u32 mask = (1u << bits) - 1; u32 r; int cf;
    if (cnt == 0) return;
    if (reg == 4 || reg == 6) { r = ((u64)v << cnt) & mask; cf = (((u64)v << (cnt - 1)) & (1u << (bits - 1))) != 0; }
    else if (reg == 5) { r = (u32)v >> cnt; cf = ((u32)v >> (cnt - 1)) & 1; }
    else if (reg == 7) { long long sv = (v & (1 << (bits - 1))) ? v - (1LL << bits) : v;
        r = (u32)((sv >> cnt) & mask); cf = (sv >> (cnt - 1)) & 1; }
    else if (reg == 0) { int c = cnt % bits; r = c ? ((((u32)v << c) | ((u32)v >> (bits - c))) & mask) : (u32)v; cf = r & 1; }
    else if (reg == 1) { int c = cnt % bits; r = c ? ((((u32)v >> c) | ((u32)v << (bits - c))) & mask) : (u32)v; cf = (r >> (bits - 1)) & 1; }
    else { /* RCL / RCR */
        int c = cnt % (bits + 1); u32 x = v; int cy = GETF(CF);
        for (int i = 0; i < c; i++) {
            if (reg == 2) { int nc = (x >> (bits - 1)) & 1; x = ((x << 1) | cy) & mask; cy = nc; }
            else { int nc = x & 1; x = (x >> 1) | ((u32)cy << (bits - 1)); cy = nc; }
        }
        SETF(CF, cy);
        if (cnt == 1) { int msb = (x >> (bits - 1)) & 1;
            SETF(OF, reg == 2 ? (msb ^ cy) : (msb ^ ((x >> (bits - 2)) & 1))); }
        rm_set(mod, rm, size, x); return;
    }
    lazy_zsp(r, bits); SETF(CF, cf); SETF(AF, 0);
    rm_set(mod, rm, size, r);
}
static void cmp_string(int op) {
    int size = (op == 0xA6 || op == 0xAE) ? 1 : 2, d = GETF(DF) ? -size : size;
    u32 dst = (u32)S[0] * 16 + R[7];
    int b = size == 2 ? rd16(dst) : rd8(dst), a;
    if (op == 0xA6 || op == 0xA7) { int sg = seg_ov >= 0 ? seg_ov : 3; u32 src = (u32)S[sg] * 16 + R[6];
        a = size == 2 ? rd16(src) : rd8(src); R[6] += d; }
    else a = reg_get(0, size);
    alu(7, a, b, size); R[7] += d;
}
static void string_op(int op) {
    if (op >= 0x6C && op <= 0x6F) {
        int size = (op == 0x6C || op == 0x6E) ? 1 : 2, d = GETF(DF) ? -size : size;
        if (op == 0x6E || op == 0x6F) { int sg = seg_ov >= 0 ? seg_ov : 3; u32 a = (u32)S[sg] * 16 + R[6];
            io_write(R[2], size == 2 ? rd16(a) : rd8(a), size); R[6] += d; }
        else { int v = io_read(R[2], size); u32 a = (u32)S[0] * 16 + R[7];
            if (size == 2) wr16(a, v); else wr8(a, v); R[7] += d; }
        return;
    }
    int size = (op == 0xAA || op == 0xA4 || op == 0xAC) ? 1 : 2, d = GETF(DF) ? -size : size;
    if (op == 0xAA || op == 0xAB) { u32 a = (u32)S[0] * 16 + R[7]; int v = reg_get(0, size);
        if (size == 2) wr16(a, v); else wr8(a, v); R[7] += d; }
    else if (op == 0xA4 || op == 0xA5) { int sg = seg_ov >= 0 ? seg_ov : 3;
        u32 src = (u32)S[sg] * 16 + R[6], dst = (u32)S[0] * 16 + R[7];
        int v = size == 2 ? rd16(src) : rd8(src); if (size == 2) wr16(dst, v); else wr8(dst, v); R[6] += d; R[7] += d; }
    else { int sg = seg_ov >= 0 ? seg_ov : 3; u32 src = (u32)S[sg] * 16 + R[6];
        reg_set(0, size, size == 2 ? rd16(src) : rd8(src)); R[6] += d; }
}
static void take_irq(void) {
    int n = icu_pending(); if (n < 0 || !GETF(IF)) return;
    int vec = icu_ack(n);
    push(F); SETF(IF, 0); SETF(TF, 0); push(S[1]); push(IP);
    IP = rd16(vec * 4); S[1] = rd16(vec * 4 + 2);
}

static void step(void) {
    dsp_clk(); dsp_advance(1);
    if (icu_pending() >= 0 && GETF(IF)) { take_irq(); tcu_step(1); return; }
    tcu_step(1);
    u32 cs_ip = (u32)S[1] * 16 + IP;
    steps++;
    seg_ov = -1; int rep = 0; pending_valid = 0;
    int op = fetch8(), mod, reg, rm;
    while (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0xF2 || op == 0xF3) {
        if (op == 0xF2 || op == 0xF3) rep = op;
        else seg_ov = op == 0x26 ? 0 : op == 0x2E ? 1 : op == 0x36 ? 2 : 3;
        op = fetch8();
    }
    if (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF) {
        if (!rep) { cmp_string(op); return; }
        while (R[1] != 0) { cmp_string(op); R[1]--; int zf = GETF(ZF);
            if ((rep == 0xF3 && !zf) || (rep == 0xF2 && zf)) break; }
        return;
    }
    if (rep && (op == 0xA4 || op == 0xA5 || op == 0xAA || op == 0xAB || op == 0xAC || op == 0xAD || (op >= 0x6C && op <= 0x6F))) {
        while (R[1] != 0) { string_op(op); R[1]--; }
        return;
    }
    if (op == 0x0F) {
        int op2 = fetch8();
        if (op2 == 0xE0 || op2 == 0xF0) { int n = fetch8(); u16 t1 = rd16(n * 4), t2 = rd16(n * 4 + 2);
            xa = (op2 == 0xE0); IP = t1; S[1] = t2; return; }
        do_stop("unsupported 0F opcode", cs_ip); return;
    }
    if (op < 0x40 && (op & 7) < 6) {
        int kind = op >> 3, form = op & 7, size = (form == 0 || form == 2 || form == 4) ? 1 : 2, a, b, r;
        if (form < 4) {
            modrm(&mod, &reg, &rm);
            if (form <= 1) { a = rm_get(mod, rm, size); b = reg_get(reg, size); }
            else { b = rm_get(mod, rm, size); a = reg_get(reg, size); }
            r = alu(kind, a, b, size);
            if (kind != 7) { if (form <= 1) rm_set(mod, rm, size, r); else reg_set(reg, size, r); }
        } else {
            b = size == 2 ? fetch16() : fetch8(); a = size == 2 ? R[0] : reg_get(0, 1);
            r = alu(kind, a, b, size); if (kind != 7) reg_set(0, size, r);
        }
        return;
    }
    if (op >= 0x40 && op <= 0x4F) {
        int i = op & 7, cf = GETF(CF); u16 v;
        if (op < 0x48) { v = R[i] + 1; lazy_zsp(v, 16); SETF(OF, R[i] == 0x7FFF); }
        else { v = R[i] - 1; lazy_zsp(v, 16); SETF(OF, R[i] == 0x8000); }
        SETF(AF, 0); SETF(CF, cf); R[i] = v; return;
    }
    if (op >= 0x50 && op <= 0x57) { u16 v = R[op & 7]; push(v); return; }
    if (op >= 0x58 && op <= 0x5F) { u16 v = pop(); R[op & 7] = v; return; }
    if (op >= 0x70 && op <= 0x7F) { int d = fetch8(); d = d > 127 ? d - 256 : d; if (cond(op & 0xF)) IP += d; return; }
    if (op == 0x80 || op == 0x81 || op == 0x83) {
        int size = op == 0x80 ? 1 : 2, a, b; modrm(&mod, &reg, &rm);
        a = rm_get(mod, rm, size);
        if (op == 0x81) b = fetch16();
        else { b = fetch8(); if (op == 0x83 && b > 127) b = (b - 256) & (size == 2 ? 0xFFFF : 0xFF); }
        int r = alu(reg, a, b, size); if (reg != 7) rm_set(mod, rm, size, r); return;
    }
    if (op == 0x84 || op == 0x85) { int size = op == 0x84 ? 1 : 2; modrm(&mod, &reg, &rm);
        int a = rm_get(mod, rm, size); alu(4, a, reg_get(reg, size), size); return; }
    if (op == 0x86 || op == 0x87) { int size = op == 0x86 ? 1 : 2; modrm(&mod, &reg, &rm);
        int a = rm_get(mod, rm, size), b = reg_get(reg, size); rm_set(mod, rm, size, b); reg_set(reg, size, a); return; }
    if (op >= 0x88 && op <= 0x8B) { int size = (op == 0x88 || op == 0x8A) ? 1 : 2; modrm(&mod, &reg, &rm);
        if (op <= 0x89) rm_set(mod, rm, size, reg_get(reg, size)); else reg_set(reg, size, rm_get(mod, rm, size)); return; }
    if (op == 0x8C) { modrm(&mod, &reg, &rm); rm_set(mod, rm, 2, S[reg & 3]); return; }
    if (op == 0x8E) { modrm(&mod, &reg, &rm); S[reg & 3] = rm_get(mod, rm, 2); return; }
    if (op == 0x8D) { int sg; modrm(&mod, &reg, &rm); u32 a = ea(mod, rm, &sg);
        reg_set(reg, 2, (a - (u32)S[sg] * 16) & 0xFFFF); return; }
    if (op == 0x8F) { modrm(&mod, &reg, &rm); u16 v = pop(); rm_set(mod, rm, 2, v); return; }
    if (op == 0x90) return;
    if (op >= 0x91 && op <= 0x97) { int i = op & 7; u16 t = R[0]; R[0] = R[i]; R[i] = t; return; }
    if (op == 0x98) { R[0] = (R[0] & 0xFF) | ((R[0] & 0x80) ? 0xFF00 : 0); return; }
    if (op == 0x99) { R[2] = (R[0] & 0x8000) ? 0xFFFF : 0; return; }
    if (op == 0x9C) { push(F); return; }
    if (op == 0x9D) { F = pop() | 0x0002; return; }
    if (op == 0x9E) { F = (F & 0xFF00) | ((R[0] >> 8) & 0xD5) | 0x2; return; }
    if (op == 0x9F) { reg_set(4, 1, F & 0xFF); return; }
    if (op >= 0xA0 && op <= 0xA3) {
        int size = (op == 0xA0 || op == 0xA2) ? 1 : 2; u16 off = fetch16(); int sg = seg_ov >= 0 ? seg_ov : 3;
        u32 a = (u32)S[sg] * 16 + off;
        if (op <= 0xA1) reg_set(0, size, size == 2 ? rd16(a) : rd8(a));
        else { if (size == 2) wr16(a, reg_get(0, 2)); else wr8(a, reg_get(0, 1)); }
        return;
    }
    if (op == 0xA8 || op == 0xA9) { int size = op == 0xA8 ? 1 : 2; int b = size == 1 ? fetch8() : fetch16();
        alu(4, reg_get(0, size), b, size); return; }
    if (op == 0xAA || op == 0xAB || op == 0xA4 || op == 0xA5 || op == 0xAC || op == 0xAD) { string_op(op); return; }
    if (op >= 0x6C && op <= 0x6F) { string_op(op); return; }
    if (op >= 0xB0 && op <= 0xB7) { reg_set(op & 7, 1, fetch8()); return; }
    if (op >= 0xB8 && op <= 0xBF) { R[op & 7] = fetch16(); return; }
    if (op == 0xC2) { u16 n = fetch16(); IP = pop(); R[4] += n; return; }
    if (op == 0xC3) { IP = pop(); return; }
    if (op == 0xCB) { IP = pop(); S[1] = pop(); return; }
    if (op == 0xCA) { u16 n = fetch16(); IP = pop(); S[1] = pop(); R[4] += n; return; }
    if (op == 0xC4 || op == 0xC5) { modrm(&mod, &reg, &rm); u32 a = ea(mod, rm, NULL);
        u16 off = rd16(a), sg = rd16(a + 2); reg_set(reg, 2, off); S[op == 0xC4 ? 0 : 3] = sg; return; }
    if (op == 0xC6 || op == 0xC7) { int size = op == 0xC6 ? 1 : 2; modrm(&mod, &reg, &rm);
        u32 a = 0; if (mod != 3) a = ea(mod, rm, NULL);
        int v = size == 1 ? fetch8() : fetch16();
        if (mod == 3) reg_set(rm, size, v); else if (size == 2) wr16(a, v); else wr8(a, v);
        return; }
    if (op == 0xCC || op == 0xCD || op == 0xCE) {
        int n = op == 0xCC ? 3 : (op == 0xCD ? fetch8() : 4);
        if (op == 0xCE && !GETF(OF)) return;
        push(F); SETF(IF, 0); SETF(TF, 0); push(S[1]); push(IP);
        IP = rd16(n * 4); S[1] = rd16(n * 4 + 2); return;
    }
    if (op == 0xCF) { IP = pop(); S[1] = pop(); F = pop() | 0x2; return; }
    if (op == 0x06 || op == 0x0E || op == 0x16 || op == 0x1E) { push(S[op >> 3]); return; }
    if (op == 0x07 || op == 0x17 || op == 0x1F) { S[op >> 3] = pop(); return; }
    if (op == 0x68) { push(fetch16()); return; }
    if (op == 0x6A) { int d = fetch8(); push((d > 127 ? d - 256 : d) & 0xFFFF); return; }
    if (op == 0x69 || op == 0x6B) {
        modrm(&mod, &reg, &rm); int v = rm_get(mod, rm, 2);
        int imm = op == 0x69 ? fetch16() : fetch8();
        if (op == 0x6B && imm > 127) imm = (imm - 256) & 0xFFFF;
        long sv = (v & 0x8000) ? v - 0x10000 : v, si = (imm & 0x8000) ? imm - 0x10000 : imm, p = sv * si;
        reg_set(reg, 2, p & 0xFFFF); int ovf = !(p >= -32768 && p <= 32767); SETF(CF, ovf); SETF(OF, ovf); return;
    }
    if (op == 0x9A) { u16 off = fetch16(), sg = fetch16(); push(S[1]); push(IP); IP = off; S[1] = sg; return; }
    if (op == 0xC8) { u16 n = fetch16(); int lvl = fetch8() & 0x1F; push(R[5]); u16 frame = R[4];
        for (int i = 0; i < lvl; i++) { R[5] -= 2; push(rd16((u32)S[2] * 16 + R[5])); }
        if (lvl) push(frame);
        R[5] = frame; R[4] -= n; return; }
    if (op == 0xC9) { R[4] = R[5]; R[5] = pop(); return; }
    if (op == 0xD4) { int base = fetch8(), v = reg_get(0, 1); if (base == 0) { do_stop("AAM divide by zero", cs_ip); return; }
        reg_set(4, 1, v / base); reg_set(0, 1, v % base); lazy_zsp(v % base, 16); return; }
    if (op == 0xD5) { int base = fetch8(), v = reg_get(4, 1) * base + reg_get(0, 1);
        reg_set(0, 1, v & 0xFF); reg_set(4, 1, 0); lazy_zsp(v & 0xFF, 8); return; }
    if (op == 0xD7) { int sg = seg_ov >= 0 ? seg_ov : 3;
        reg_set(0, 1, rd8((u32)S[sg] * 16 + ((R[3] + reg_get(0, 1)) & 0xFFFF))); return; }
    if (op == 0xC0 || op == 0xC1) { int size = op == 0xC0 ? 1 : 2; modrm(&mod, &reg, &rm);
        int v = rm_get(mod, rm, size); int cnt = fetch8() & 0x1F; shift(reg, v, cnt, size, mod, rm); return; }
    if (op >= 0xD0 && op <= 0xD3) { int size = (op == 0xD0 || op == 0xD2) ? 1 : 2; modrm(&mod, &reg, &rm);
        int cnt = (op <= 0xD1) ? 1 : (reg_get(1, 1) & 0x1F); int v = rm_get(mod, rm, size);
        shift(reg, v, cnt, size, mod, rm); return; }
    if (op >= 0xE0 && op <= 0xE3) {
        int d = fetch8(); d = d > 127 ? d - 256 : d;
        if (op == 0xE3) { if (R[1] == 0) IP += d; return; }
        R[1]--;
        int take = R[1] != 0 && (op == 0xE2 || (op == 0xE1 && GETF(ZF)) || (op == 0xE0 && !GETF(ZF)));
        if (take) IP += d; return;
    }
    if (op == 0xE4 || op == 0xE5) { int p = fetch8(), size = op == 0xE4 ? 1 : 2; reg_set(0, size, io_read(p, size)); return; }
    if (op == 0xE6 || op == 0xE7) { int p = fetch8(), size = op == 0xE6 ? 1 : 2; io_write(p, reg_get(0, size), size); return; }
    if (op == 0xE8) { u16 d = fetch16(); push(IP); IP += d; return; }
    if (op == 0xE9) { u16 d = fetch16(); IP += d; return; }
    if (op == 0xEA) { u16 off = fetch16(), sg = fetch16(); IP = off; S[1] = sg; return; }
    if (op == 0xEB) { int d = fetch8(); d = d > 127 ? d - 256 : d; IP += d; return; }
    if (op == 0xEC || op == 0xED) { int size = op == 0xEC ? 1 : 2; reg_set(0, size, io_read(R[2], size)); return; }
    if (op == 0xEE || op == 0xEF) { int size = op == 0xEE ? 1 : 2; io_write(R[2], reg_get(0, size), size); return; }
    if (op == 0xF4) { do_stop("HLT", cs_ip); return; }
    if (op >= 0xF8 && op <= 0xFD) {
        if (op == 0xF8 || op == 0xF9) SETF(CF, op == 0xF9);
        if (op == 0xFA) SETF(IF, 0); if (op == 0xFB) SETF(IF, 1);
        if (op == 0xFC) SETF(DF, 0); if (op == 0xFD) SETF(DF, 1);
        return;
    }
    if (op == 0xF6 || op == 0xF7) {
        int size = op == 0xF6 ? 1 : 2, bits = 8 * size; modrm(&mod, &reg, &rm);
        if (reg <= 1) { int a = rm_get(mod, rm, size), b = size == 1 ? fetch8() : fetch16(); alu(4, a, b, size); return; }
        if (reg == 2) { int v = rm_get(mod, rm, size); rm_set(mod, rm, size, ~v & ((1 << bits) - 1)); return; }
        if (reg >= 4) {
            int a = size == 1 ? reg_get(0, 1) : R[0], v = rm_get(mod, rm, size);
            if (reg == 4 || reg == 5) {
                long long p;
                if (reg == 5) { long long sa = (a >> (bits - 1)) ? a - (1LL << bits) : a;
                                long long sv = (v >> (bits - 1)) ? v - (1LL << bits) : v; p = sa * sv; }
                else p = (long long)a * v;
                if (size == 1) R[0] = p & 0xFFFF; else { R[0] = p & 0xFFFF; R[2] = (p >> 16) & 0xFFFF; }
                SETF(CF, (p >> bits) != 0); SETF(OF, (p >> bits) != 0); return;
            }
            if (v == 0) { do_stop("divide by zero", cs_ip); return; }
            unsigned long dividend = size == 1 ? (R[0] & 0xFFFF) : (((unsigned long)R[2] << 16) | R[0]);
            long long q, rr;
            if (reg == 7) {
                long long dv = (dividend >> (2 * bits - 1)) ? (long long)dividend - (1LL << (2 * bits)) : (long long)dividend;
                long long sv = (v >> (bits - 1)) ? v - (1LL << bits) : v;
                q = dv / sv; rr = dv - q * sv;
            } else { q = dividend / (unsigned)v; rr = dividend % (unsigned)v; }
            if (size == 1) { reg_set(0, 1, q & 0xFF); reg_set(4, 1, rr & 0xFF); }
            else { R[0] = q & 0xFFFF; R[2] = rr & 0xFFFF; }
            return;
        }
        if (reg == 3) { int a = rm_get(mod, rm, size), mask = (1 << bits) - 1, r = (-a) & mask;
            lazy_zsp(r, bits); SETF(CF, a != 0); SETF(OF, a == (1 << (bits - 1))); SETF(AF, 0);
            rm_set(mod, rm, size, r); return; }
    }
    if (op == 0xFE || op == 0xFF) {
        int size = op == 0xFE ? 1 : 2, bits = 8 * size; modrm(&mod, &reg, &rm);
        if (reg <= 1) { int v = rm_get(mod, rm, size), cf = GETF(CF), mask = (1 << bits) - 1;
            int r = reg == 0 ? v + 1 : v - 1;
            lazy_zsp(r, bits); SETF(OF, reg == 0 ? (v == (mask >> 1)) : (v == (1 << (bits - 1)))); SETF(AF, 0);
            SETF(CF, cf); rm_set(mod, rm, size, r & mask); return; }
        if (op == 0xFF && reg == 2) { int t = rm_get(mod, rm, 2); push(IP); IP = t; return; }
        if (op == 0xFF && reg == 4) { IP = rm_get(mod, rm, 2); return; }
        if (op == 0xFF && reg == 6) { push(rm_get(mod, rm, 2)); return; }
        if (op == 0xFF && (reg == 3 || reg == 5)) { u32 a = ea(mod, rm, NULL); u16 off = rd16(a), sg = rd16(a + 2);
            if (reg == 3) { push(S[1]); push(IP); } IP = off; S[1] = sg; return; }
    }
    if (op == 0x60) { u16 sp0 = R[4], v[8] = {R[0], R[1], R[2], R[3], sp0, R[5], R[6], R[7]};
        for (int i = 0; i < 8; i++) push(v[i]); return; }
    if (op == 0x61) { R[7] = pop(); R[6] = pop(); R[5] = pop(); pop(); R[3] = pop(); R[2] = pop(); R[1] = pop(); R[0] = pop(); return; }
    if (op == 0xF5) { SETF(CF, !GETF(CF)); return; }
    if (op == 0x27 || op == 0x2F) {
        int al = reg_get(0, 1), cf = GETF(CF), af = GETF(AF), old = al;
        if ((al & 0x0F) > 9 || af) {
            if (op == 0x27) { cf = cf || al + 6 > 0xFF; al = (al + 6) & 0xFF; }
            else { cf = cf || al < 6; al = (al - 6) & 0xFF; }
            SETF(AF, 1);
        } else SETF(AF, 0);
        if (old > 0x99 || GETF(CF)) { al = op == 0x27 ? (al + 0x60) & 0xFF : (al - 0x60) & 0xFF; cf = 1; }
        else if (op == 0x27) cf = 0;
        reg_set(0, 1, al); lazy_zsp(al, 8); SETF(CF, cf); return;
    }
    if (op == 0x37 || op == 0x3F) {
        int al = reg_get(0, 1);
        if ((al & 0x0F) > 9 || GETF(AF)) {
            if (op == 0x37) R[0] += 0x106;
            else { reg_set(0, 1, (al - 6) & 0xFF); reg_set(4, 1, (reg_get(4, 1) - 1) & 0xFF); }
            SETF(AF, 1); SETF(CF, 1);
        } else { SETF(AF, 0); SETF(CF, 0); }
        reg_set(0, 1, reg_get(0, 1) & 0x0F); return;
    }
    do_stop("unsupported opcode", cs_ip);
}

/* idle-skip, identical to run_main.skip_idle */
static int skip_idle(void) {
    if (S[1] == 0xFB00 && IP >= 0x022E && IP < 0x0234 && icu_pending() < 0 && (F & 0x200)) {
        if (tcu.load[0] && (tcu.mode[0] == 2 || tcu.mode[0] == 3)) {
            long need = (tcu.count[0] > 1 ? tcu.count[0] : 1) * 4 - tcu.tick_n;
            tcu_step(need); dsp_advance(need); return 1;
        }
    }
    return 0;
}

static void print_screen(void) {
    int start = lcd_regs[8] | (lcd_regs[9] << 8);
    int cols = (lcd_regs[2] >= 0 ? lcd_regs[2] : 39) + 1;
    int duty = lcd_regs[3] >= 0 ? lcd_regs[3] : 63, pitch = lcd_regs[1] >= 0 ? lcd_regs[1] : 0x75;
    int rows = (duty + 1) / ((pitch >> 4) + 1);
    printf("+"); for (int i = 0; i < cols; i++) putchar('-'); printf("+\n");
    for (int r = 0; r < rows; r++) { putchar('|');
        for (int c = 0; c < cols; c++) { u8 b = lcd_mem[(start + r * cols + c) & 0xFFFF]; putchar(b >= 32 && b < 127 ? b : ' '); }
        printf("|\n"); }
    printf("+"); for (int i = 0; i < cols; i++) putchar('-'); printf("+\n");
    printf("cursor %d (row %d col %d) reg4=%d\n", lcd_cur, (lcd_cur - start) / cols, (lcd_cur - start) % cols, lcd_regs[4]);
}

/* Load the ROM: one 512 KB interleaved image, or the two 256 KB EPROMs (LS = ic13 low bytes,
 * MS = ic14 high bytes) given as "LS.bin,MS.bin". */
static int mpc3k_load_rom(const char *spec) {
    char a[1024]; snprintf(a, sizeof a, "%s", spec);
    char *comma = strchr(a, ',');
    if (!comma) {
        FILE *fp = fopen(a, "rb"); if (!fp) { perror(a); return -1; }
        size_t n = fread(rom, 1, sizeof rom, fp); fclose(fp);
        if (n != sizeof rom) { fprintf(stderr, "%s: ROM image must be 524288 bytes\n", a); return -1; }
        return 0;
    }
    *comma = 0; static u8 lo[0x40000], hi[0x40000];
    FILE *f1 = fopen(a, "rb"), *f2 = fopen(comma + 1, "rb");
    if (!f1 || !f2) { perror("ROM pair"); return -1; }
    size_t n1 = fread(lo, 1, sizeof lo, f1), n2 = fread(hi, 1, sizeof hi, f2); fclose(f1); fclose(f2);
    if (n1 != sizeof lo || n2 != sizeof hi) { fprintf(stderr, "each EPROM file must be 262144 bytes\n"); return -1; }
    for (int i = 0; i < 0x40000; i++) { rom[2 * i] = lo[i]; rom[2 * i + 1] = hi[i]; }
    return 0;
}
/* Power-on state (as run_main.make). */
static void mpc3k_reset(void) {
    if (!wave) wave = calloc(DSP_WORDS, sizeof *wave);
    for (int i = 0; i < 256; i++) { int c = 0; for (int b = 0; b < 8; b++) c += (i >> b) & 1; PARITY[i] = !(c & 1); }
    for (int i = 0; i < 16; i++) lcd_regs[i] = -1; lcd_regs[8] = lcd_regs[9] = 0;
    S[0] = 0; S[1] = 0xFFF0; S[2] = 0; S[3] = 0; IP = 0; R[4] = 0x0400;
}

#ifndef MPC3K_NO_MAIN
static FILE *wav_fp; static u64 wav_frames;
static void wav_sink(int16_t l, int16_t r) { int16_t f[2] = {l, r}; fwrite(f, 2, 2, wav_fp); wav_frames++; }
static void wav_header(FILE *f, u64 frames) {
    u32 data = (u32)(frames * 4), rate = 44100, br = rate * 4; u16 fmt = 1, ch = 2, ba = 4, bits = 16; u32 fsz = 16, riff = 36 + data;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fsz, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s ROM.bin [--steps N] [--trace N] [--short-search] [--screen] [--time S]\n", argv[0]); return 1; }
    if (mpc3k_load_rom(argv[1])) return 1;
    u64 max_steps = ~0ull, trace = 0; int short_search = 0, screen = 0; double tlimit = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--steps") && i + 1 < argc) max_steps = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "--short-search")) short_search = 1;
        else if (!strcmp(argv[i], "--screen")) screen = 1;
        else if (!strcmp(argv[i], "--time") && i + 1 < argc) tlimit = atof(argv[++i]);
    }
    mpc3k_reset();
    for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "--wav") && i + 1 < argc) {
        wav_fp = fopen(argv[i + 1], "wb"); if (!wav_fp) { perror(argv[i + 1]); return 1; }
        wav_header(wav_fp, 0); audio_sink = wav_sink; }
    int patched = 0; u64 last_trace = 0; clock_t t0 = clock();
    /* --press KK@STEP: button message 90 KK 7F at STEP, release 90 KK 00 60000 steps later */
    struct { u64 at; int kk; } ev[64]; int nev = 0;
    for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "--press") && i + 1 < argc && nev < 32) {
        unsigned kk; unsigned long long at; if (sscanf(argv[i + 1], "%x@%llu", &kk, &at) == 2) {
            ev[nev].at = at; ev[nev].kk = kk; nev++; ev[nev].at = at + 60000; ev[nev].kk = kk | 0x100; nev++; } }
    u8 q[256]; int qh = 0, qt = 0; int next_ev = 0;
    while (steps < max_steps && !stop_msg) {
        if (short_search && !patched && S[1] == 0xD5CC && IP == 0x0644) { wr8(0x8010 * 16 + 0x834A, 1); patched = 1; }
        while (next_ev < nev && steps >= ev[next_ev].at) {
            q[qt++ & 255] = 0x90; q[qt++ & 255] = ev[next_ev].kk & 0xFF; q[qt++ & 255] = (ev[next_ev].kk & 0x100) ? 0x00 : 0x7F;
            next_ev++; }
        if (qh != qt && !te.rx_ready[2]) te_receive(2, q[qh++ & 255]);
        if (skip_idle()) continue;
        step();
        if (trace && steps % trace == 0 && steps != last_trace) {
            last_trace = steps;
            printf("T %llu %04X:%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X F=%04X irr=%02X isr=%02X imr=%02X c0=%ld\n",
                   (unsigned long long)steps, S[1], IP, R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], S[0], S[2], S[3], S[1],
                   F, icu.irr, icu.isr, icu.imr, tcu.count[0]);
        }
        if (tlimit > 0 && (steps & 0xFFFFF) == 0 && (double)(clock() - t0) / CLOCKS_PER_SEC > tlimit) break;
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "steps %llu in %.2f s (%.1f M/s) | machine time %.2f s (%.1fx real time) | stop: %s at %05X | CS:IP %04X:%04X | lcd writes %ld\n",
            (unsigned long long)steps, secs, steps / secs / 1e6, timer_ticks * 0.02, timer_ticks * 0.02 / secs,
            stop_msg ? stop_msg : "none", stop_addr, S[1], IP, lcd_data_writes);
    if (screen) print_screen();
    { u64 nz = 0, first = 0; for (u32 i = 0; i < DSP_WORDS; i++) if (wave[i]) { if (!nz) first = i; nz++; }
      fprintf(stderr, "dsp: control writes %llu, key-ons %llu, DMA words %llu, non-zero sample words %llu (first at %llX), keyed now %08X\n",
              (unsigned long long)dbg_ctl_writes, (unsigned long long)dbg_keyons, (unsigned long long)dbg_dma_words,
              (unsigned long long)nz, (unsigned long long)first, dsp.key);
      fprintf(stderr, "dsp: voice-register writes per channel:"); for (int c = 0; c < 32; c++) if (dbg_vreg[c]) fprintf(stderr, " %d:%llu", c, (unsigned long long)dbg_vreg[c]); fprintf(stderr, "\n"); }
    if (wav_fp) { wav_header(wav_fp, wav_frames); fclose(wav_fp);
        fprintf(stderr, "wav: %llu frames (%.2f s)\n", (unsigned long long)wav_frames, wav_frames / 44100.0); }
    return 0;
}
#endif /* MPC3K_NO_MAIN */
