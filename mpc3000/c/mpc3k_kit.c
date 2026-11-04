/* mpc3k_kit.c - build MPC3000 kit floppies (.PGM + .SND on a 1.44 MB FAT12 image) on the device.
 * XML, sample-search and name helpers are copied unchanged from the MPC4000 converter (mpc4k_trace.c). */
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "pgm_template.h"

static const char *xml_tag(const char *p, const char *end, const char *tag)
{
    size_t tl = strlen(tag);
    for (; p && p < end; p++) {
        p = strchr(p, '<');
        if (!p || p >= end) return NULL;
        if (!strncmp(p + 1, tag, tl) && (p[1 + tl] == '>' || p[1 + tl] == ' ' || p[1 + tl] == '/')) return p;
    }
    return NULL;
}
static int xml_text(const char *open, const char *end, char *out, int max)
{
    const char *gt = strchr(open, '>');
    if (!gt || gt >= end || gt[-1] == '/') { out[0] = 0; return 0; }
    const char *lt = strchr(gt + 1, '<');
    if (!lt || lt > end) lt = end;
    int n = 0;
    for (const char *q = gt + 1; q < lt && n < max - 1; q++) {     /* decode common entities */
        if (*q == '&') {
            if (!strncmp(q, "&amp;", 5)) { out[n++] = '&'; q += 4; continue; }
            if (!strncmp(q, "&lt;", 4)) { out[n++] = '<'; q += 3; continue; }
            if (!strncmp(q, "&gt;", 4)) { out[n++] = '>'; q += 3; continue; }
            if (!strncmp(q, "&quot;", 6)) { out[n++] = '"'; q += 5; continue; }
            if (!strncmp(q, "&apos;", 6)) { out[n++] = '\''; q += 5; continue; }
        }
        out[n++] = *q;
    }
    out[n] = 0;
    return n;
}
static int xml_attr_num(const char *open, const char *attr)
{
    const char *gt = strchr(open, '>');
    char pat[32]; snprintf(pat, sizeof pat, "%s=\"", attr);
    const char *a = strstr(open, pat);
    return a && (!gt || a < gt) ? atoi(a + strlen(pat)) : -1;
}

/* find "<name>.wav" (any case) in dir, its subfolders, or its parent */
static int find_wav(const char *dir, const char *name, char *out, size_t outsz, int depth)
{
    DIR *dp = opendir(dir);
    if (!dp) return 0;
    struct dirent *de; int found = 0;
    char want[300]; snprintf(want, sizeof want, "%s.wav", name);
    while (!found && (de = readdir(dp))) {
        if (!strcasecmp(de->d_name, want) || !strcasecmp(de->d_name, name)) {
            snprintf(out, outsz, "%s/%s", dir, de->d_name); found = 1;
        }
    }
    rewinddir(dp);
    while (!found && depth > 0 && (de = readdir(dp))) {
        if (de->d_name[0] == '.') continue;
        char sub[2048]; struct stat st;
        snprintf(sub, sizeof sub, "%s/%s", dir, de->d_name);
        if (!stat(sub, &st) && S_ISDIR(st.st_mode)) found = find_wav(sub, name, out, outsz, depth - 1);
    }
    closedir(dp);
    return found;
}

static void safe_name(const char *in, char *out, int max)       /* MPC4000-friendly, <=16 chars */
{
    int n = 0;
    for (const char *q = in; *q && n < max; q++) {
        unsigned char c = (unsigned char)*q;
        out[n++] = (c >= 32 && c < 127 && !strchr("\\/:*?\"<>|", c)) ? (char)c : '_';
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    if (!n) strcpy(out, "Sample");
}


/* ------------------------------------------------------------ WAV -> mono 16-bit 44.1 kHz */
static int16_t *wav_load(const char *path, long *nout)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(len); if (!d || fread(d, 1, len, f) != (size_t)len) { fclose(f); free(d); return NULL; }
    fclose(f);
    if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { free(d); return NULL; }
    int fmt = 0, ch = 0, bits = 0; long rate = 0; const uint8_t *data = NULL; long dlen = 0;
    for (long o = 12; o + 8 <= len; ) {
        uint32_t l; memcpy(&l, d + o + 4, 4);
        if (!memcmp(d + o, "fmt ", 4) && l >= 16) {
            fmt = d[o + 8] | d[o + 9] << 8; ch = d[o + 10] | d[o + 11] << 8;
            rate = d[o + 12] | d[o + 13] << 8 | d[o + 14] << 16 | (long)d[o + 15] << 24; bits = d[o + 22] | d[o + 23] << 8;
            if (fmt == 0xFFFE && l >= 40) fmt = d[o + 8 + 24] | d[o + 8 + 25] << 8;     /* WAVE_FORMAT_EXTENSIBLE sub-format */
        }
        if (!memcmp(d + o, "data", 4)) { data = d + o + 8; dlen = (long)l; if (o + 8 + dlen > len) dlen = len - o - 8; }
        o += 8 + l + (l & 1);
    }
    if (!data || ch < 1 || !rate || !(fmt == 1 || fmt == 3) || !(bits == 8 || bits == 16 || bits == 24 || bits == 32)) { free(d); return NULL; }
    int bps = bits / 8; long frames = dlen / (bps * ch);
    double *mono = malloc(sizeof(double) * (frames ? frames : 1));
    for (long i = 0; i < frames; i++) {
        double acc = 0;
        for (int c = 0; c < ch; c++) {
            const uint8_t *p = data + (i * ch + c) * bps; double v;
            if (fmt == 3 && bits == 32) { float fl; memcpy(&fl, p, 4); v = fl * 32767.0; }
            else if (bits == 8) v = (p[0] - 128) * 256.0;
            else if (bits == 16) v = (int16_t)(p[0] | p[1] << 8);
            else if (bits == 24) v = (int32_t)((uint32_t)(p[0] << 8 | p[1] << 16 | p[2] << 24)) / 65536.0;
            else v = (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) / 65536.0;
            acc += v;
        }
        mono[i] = acc / ch;
    }
    free(d);
    long n = rate == 44100 ? frames : (long)((double)frames * 44100.0 / rate);
    int16_t *out = malloc(sizeof(int16_t) * (n ? n : 1));
    for (long j = 0; j < n; j++) {
        double x = rate == 44100 ? (double)j : j * (double)rate / 44100.0; long i = (long)x; double fr = x - i;
        double a = mono[i < frames ? i : frames - 1], b = mono[i + 1 < frames ? i + 1 : frames - 1];
        double v = a + (b - a) * fr; out[j] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
    }
    free(mono); *nout = n; return out;
}

/* ------------------------------------------------------------ MPC3000 files (SND_FORMAT.md, PGM_FORMAT.md) */
static const int k3_pad_notes[16] = {37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53};
static long snd_build(uint8_t *out, const char *name16, const int16_t *s, long n)
{
    memset(out, 0, 0x26); out[0] = 1; out[1] = 2;
    for (int i = 0; i < 16; i++) out[2 + i] = name16[i] ? (uint8_t)toupper((unsigned char)name16[i]) : ' ';
    for (int i = (int)strlen(name16); i < 16; i++) out[2 + i] = ' ';
    out[0x13] = 100;                                              /* Vol% */
    uint32_t len = (uint32_t)n; memcpy(out + 0x1A, &len, 4); memcpy(out + 0x1E, &len, 4);   /* soft end, length */
    out[0x22] = 0xD0; out[0x23] = 0x07; out[0x24] = 1;            /* as the OS writes */
    for (long i = 0; i < n; i++) { out[0x26 + 2 * i] = s[i] & 0xFF; out[0x27 + 2 * i] = (uint8_t)(s[i] >> 8); }
    return 0x26 + 2 * n;
}
static void pgm_build(uint8_t *p, const char *kit, char names[16][17], const int used[16])
{
    memcpy(p, pgm_template, sizeof pgm_template);
    memset(p + 2, 0, 128 * 17);
    int idx = 0, sound_of_pad[16];
    for (int k = 0; k < 16; k++) {
        sound_of_pad[k] = -1;
        if (!used[k]) continue;
        for (int i = 0; i < 16; i++) p[2 + idx * 17 + i] = names[k][i] ? (uint8_t)toupper((unsigned char)names[k][i]) : ' ';
        sound_of_pad[k] = idx++;
    }
    for (int n = 0; n < 64; n++) p[0xAC0 + n * 24] = 0xFF;
    for (int k = 0; k < 16; k++) if (sound_of_pad[k] >= 0) p[0xAC0 + (k3_pad_notes[k] - 35) * 24] = (uint8_t)sound_of_pad[k];
    memset(p + 0xA82, 0, 16);
    for (int i = 0; i < 16 && kit[i]; i++) p[0xA82 + i] = (uint8_t)toupper((unsigned char)kit[i]);
}

/* ------------------------------------------------------------ FAT12 1.44 MB image */
typedef struct { char name83[12]; const uint8_t *data; long len; } FFile;
static int fat12_build(const char *out, FFile *files, int nf)
{
    const int bps = 512, spf = 9, nroot = 224, tot = 2880, res = 1;
    uint8_t *img = calloc(tot, bps); if (!img) return -1;
    uint8_t *bs = img; memcpy(bs, "\xEB\x3C\x90MPC3KEMU", 11);
    bs[11] = 0; bs[12] = 2; bs[13] = 1; bs[14] = 1; bs[15] = 0; bs[16] = 2; bs[17] = nroot & 0xFF; bs[18] = nroot >> 8;
    bs[19] = tot & 0xFF; bs[20] = tot >> 8; bs[21] = 0xF0; bs[22] = spf; bs[24] = 18; bs[26] = 2;
    bs[38] = 0x29; memcpy(bs + 43, "MPC3000    FAT12   ", 19); bs[510] = 0x55; bs[511] = 0xAA;
    static uint16_t fat[3000]; memset(fat, 0, sizeof fat); fat[0] = 0xFF0; fat[1] = 0xFFF;
    long root = (res + 2 * spf) * bps, data = root + nroot * 32; int cl = 2;
    int maxcl = (int)((tot * (long)bps - data) / bps) + 1;
    for (int i = 0; i < nf; i++) {
        long n = (files[i].len + bps - 1) / bps;
        if (cl + n > maxcl + 1) { free(img); return -2; }
        for (long k = 0; k < n; k++) {
            fat[cl + k] = (k < n - 1) ? (uint16_t)(cl + k + 1) : 0xFFF;
            long take = files[i].len - k * bps; if (take > bps) take = bps;
            memcpy(img + data + (cl + k - 2) * (long)bps, files[i].data + k * bps, take);
        }
        uint8_t *e = img + root + i * 32; memcpy(e, files[i].name83, 11); e[11] = 0x20;
        e[26] = n ? cl & 0xFF : 0; e[27] = n ? cl >> 8 : 0;
        e[28] = files[i].len & 0xFF; e[29] = (files[i].len >> 8) & 0xFF; e[30] = (files[i].len >> 16) & 0xFF; e[31] = (uint8_t)(files[i].len >> 24);
        cl += (int)n;
    }
    for (int k = 0; k < 2; k++) {
        uint8_t *fb = img + (res + k * spf) * bps;
        for (int i = 0; i + 1 < 3000 && i * 3 / 2 + 2 < spf * bps; i += 2) {
            int o = i * 3 / 2; fb[o] = fat[i] & 0xFF; fb[o + 1] = ((fat[i] >> 8) & 0xF) | ((fat[i + 1] & 0xF) << 4); fb[o + 2] = (uint8_t)(fat[i + 1] >> 4);
        }
    }
    FILE *f = fopen(out, "wb"); if (!f) { free(img); return -1; }
    fwrite(img, bps, tot, f); fclose(f); free(img); return 0;
}
static void name83(const char *base, const char *ext, char out[12])
{
    memset(out, ' ', 11); out[11] = 0; int n = 0;
    for (const char *q = base; *q && n < 8; q++) if (isalnum((unsigned char)*q)) out[n++] = (char)toupper((unsigned char)*q);
    if (!n) out[n++] = 'S';
    memcpy(out + 8, ext, 3);
}

/* The OS names a loaded sound after its file (header name ignored) and finds a program's sounds as
 * <name>.SND, so each sound gets one DOS-safe name of up to 8 characters, used everywhere. */
static void dos8(const char *in, char out[9])
{
    int n = 0;
    for (const char *q = in; *q && n < 8; q++) {
        unsigned char c = (unsigned char)*q;
        if (isalnum(c)) out[n++] = (char)toupper(c);
        else if ((c == ' ' || c == '_' || c == '-') && n && out[n - 1] != '_') out[n++] = c == '-' ? '-' : '_';
    }
    while (n && (out[n - 1] == '_' || out[n - 1] == '-')) n--;
    if (!n) out[n++] = 'S';
    out[n] = 0;
}
static void unique8(char names[16][17], const int used[16])
{
    for (int k = 0; k < 16; k++) if (used[k]) {
        char b[9]; dos8(names[k], b); snprintf(names[k], 17, "%s", b);
        for (int j = 0; j < k; j++) if (used[j] && !strcmp(names[j], names[k])) {
            char t[9]; snprintf(t, sizeof t, "%.6s%02d", b, k + 1); snprintf(names[k], 17, "%s", t); j = -1;
        }
    }
}

/* ------------------------------------------------------------ SCSI disk image (formatted by the MPC3000 OS)
 * Partitions are contiguous, equal-sized DOS volumes (OEM "MPC-60", 512-byte sectors, 16-sector clusters,
 * 2 FATs, 512 root entries). The FAT is read by the OS as FAT12 (tested: FAT16 chains made it read the
 * wrong clusters), although the FATs are 11 sectors long. */
static int hd_is_formatted(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0; uint8_t b[16]; int ok = fread(b, 1, 16, f) == 16 && !memcmp(b + 3, "MPC-60", 6); fclose(f); return ok;
}
static long hd_free_bytes(const char *path, int part) {
    FILE *f = fopen(path, "rb"); if (!f) return -1; uint8_t bs[512];
    if (fread(bs, 1, 512, f) != 512) { fclose(f); return -1; }
    long psz = (bs[19] | bs[20] << 8) * 512L, base = part * psz; fseek(f, base, SEEK_SET);
    if (fread(bs, 1, 512, f) != 512 || memcmp(bs + 3, "MPC-60", 6)) { fclose(f); return -1; }
    int bps = bs[11] | bs[12] << 8, spc = bs[13], res = bs[14] | bs[15] << 8, nfat = bs[16], nroot = bs[17] | bs[18] << 8, spf = bs[22] | bs[23] << 8;
    long tot = bs[19] | bs[20] << 8, data_sec = res + nfat * spf + nroot * 32 / bps, ncl = (tot - data_sec) / spc + 2;
    uint8_t *fat = malloc(spf * bps); fseek(f, base + res * bps, SEEK_SET);
    if (fread(fat, bps, spf, f) != (size_t)spf) { fclose(f); free(fat); return -1; }
    fclose(f); long n = 0;
    for (long c = 2; c < ncl; c++) { long o = c * 3 / 2; int v = fat[o] | fat[o + 1] << 8; v = (c & 1) ? v >> 4 : v & 0xFFF; if (!v) n++; }
    free(fat); return n * spc * bps;
}
static int hd_add(const char *path, int part, FFile *files, int nf) {
    FILE *f = fopen(path, "r+b"); if (!f) { perror(path); return -1; }
    uint8_t bs[512]; if (fread(bs, 1, 512, f) != 512) { fclose(f); return -1; }
    long psz = (bs[19] | bs[20] << 8) * 512L, base = part * psz; fseek(f, base, SEEK_SET);
    if (fread(bs, 1, 512, f) != 512 || memcmp(bs + 3, "MPC-60", 6)) { fprintf(stderr, "partition %c is not formatted\n", 'A' + part); fclose(f); return -1; }
    int bps = bs[11] | bs[12] << 8, spc = bs[13], res = bs[14] | bs[15] << 8, nfat = bs[16], nroot = bs[17] | bs[18] << 8, spf = bs[22] | bs[23] << 8;
    long tot = bs[19] | bs[20] << 8, fat_off = base + res * (long)bps, root_off = fat_off + (long)nfat * spf * bps, data_off = root_off + nroot * 32L;
    long ncl = (tot - (data_off - base) / bps) / spc + 2, clsz = (long)spc * bps;
    uint8_t *fat = malloc(spf * bps), *root = malloc(nroot * 32), *clbuf = malloc(clsz);
    fseek(f, fat_off, SEEK_SET); if (fread(fat, bps, spf, f) != (size_t)spf) goto fail;
    fseek(f, root_off, SEEK_SET); if (fread(root, 32, nroot, f) != (size_t)nroot) goto fail;
    #define FGET(c) (((c) & 1) ? ((fat[(c) * 3 / 2] | fat[(c) * 3 / 2 + 1] << 8) >> 4) : ((fat[(c) * 3 / 2] | fat[(c) * 3 / 2 + 1] << 8) & 0xFFF))
    for (int i = 0; i < nf; i++) {
        for (int e = 0; e < nroot; e++) if (root[e * 32] && root[e * 32] != 0xE5 && !memcmp(root + e * 32, files[i].name83, 11)) {   /* replace */
            int c = root[e * 32 + 26] | root[e * 32 + 27] << 8;
            while (c >= 2 && c < 0xFF8 && c < ncl) { int nx = FGET(c); long o = c * 3 / 2;
                if (c & 1) { fat[o] &= 0x0F; fat[o + 1] = 0; } else { fat[o] = 0; fat[o + 1] &= 0xF0; } c = nx; }
            root[e * 32] = 0xE5;
        }
        long need = files[i].len ? (files[i].len + clsz - 1) / clsz : 0, got = 0, prev = -1, first = 0;
        for (long c = 2; c < ncl && got < need; c++) {
            if (FGET(c)) continue;
            if (prev >= 0) { long o = prev * 3 / 2; if (prev & 1) { fat[o] = (fat[o] & 0x0F) | ((c << 4) & 0xF0); fat[o + 1] = (c >> 4) & 0xFF; } else { fat[o] = c & 0xFF; fat[o + 1] = (fat[o + 1] & 0xF0) | ((c >> 8) & 0x0F); } }
            else first = c;
            { long o = c * 3 / 2; if (c & 1) { fat[o] = (fat[o] & 0x0F) | 0xF0; fat[o + 1] = 0xFF; } else { fat[o] = 0xFF; fat[o + 1] = (fat[o + 1] & 0xF0) | 0x0F; } }
            memset(clbuf, 0, clsz); long take = files[i].len - got * clsz; if (take > clsz) take = clsz;
            memcpy(clbuf, files[i].data + got * clsz, take); fseek(f, data_off + (c - 2) * clsz, SEEK_SET); fwrite(clbuf, 1, clsz, f);
            prev = c; got++;
        }
        if (got < need) { fprintf(stderr, "partition %c is full\n", 'A' + part); goto fail; }
        int slot = -1; for (int e = 0; e < nroot && slot < 0; e++) if (root[e * 32] == 0 || root[e * 32] == 0xE5) slot = e;
        if (slot < 0) { fprintf(stderr, "partition %c: directory full\n", 'A' + part); goto fail; }
        uint8_t *e = root + slot * 32; memset(e, 0, 32); memcpy(e, files[i].name83, 11); e[11] = 0x20;
        e[26] = first & 0xFF; e[27] = first >> 8; e[28] = files[i].len & 0xFF; e[29] = (files[i].len >> 8) & 0xFF; e[30] = (files[i].len >> 16) & 0xFF; e[31] = (uint8_t)(files[i].len >> 24);
    }
    #undef FGET
    for (int k = 0; k < nfat; k++) { fseek(f, fat_off + (long)k * spf * bps, SEEK_SET); fwrite(fat, bps, spf, f); }
    fseek(f, root_off, SEEK_SET); fwrite(root, 32, nroot, f); fclose(f); free(fat); free(root); free(clbuf); return 0;
fail:
    fclose(f); free(fat); free(root); free(clbuf); return -1;
}
static int kit_part = 0;              /* --part A..Z for hard-disk targets */

/* ------------------------------------------------------------ kit: up to 16 samples -> floppy */
#define FLOPPY_BUDGET (1440L * 1024 - 8192)                /* data clusters minus the .PGM and slack */
static int make_kit_image(const char *out, const char *kit, char paths[16][1024], char names[16][17])
{
    int16_t *smp[16] = {0}; long len[16] = {0}, keep[16]; int used[16] = {0}; long total = 0;
    for (int k = 0; k < 16; k++) if (paths[k][0]) {
        smp[k] = wav_load(paths[k], &len[k]);
        if (!smp[k]) { fprintf(stderr, "  pad A%02d: can't read %s (needs PCM or float WAV)\n", k + 1, paths[k]); continue; }
        used[k] = 1; total += 0x26 + 2 * len[k] + 511;
    }
    /* fit on the disk: trim the longest samples first (water-filling), with a 10 ms fade */
    for (int k = 0; k < 16; k++) keep[k] = len[k];
    int to_hd = hd_is_formatted(out); long budget = FLOPPY_BUDGET;
    if (to_hd) { budget = hd_free_bytes(out, kit_part) - 65536; if (budget < 0) { fprintf(stderr, "%s: partition %c not usable\n", out, 'A' + kit_part); return -1; } }
    if (total > budget) {
        long lo = 0, hi = 0; for (int k = 0; k < 16; k++) if (len[k] > hi) hi = len[k];
        while (lo < hi) { long mid = (lo + hi + 1) / 2, t = 0;
            for (int k = 0; k < 16; k++) if (used[k]) t += 0x26 + 2 * (len[k] < mid ? len[k] : mid) + 511;
            if (t <= budget) lo = mid; else hi = mid - 1; }
        for (int k = 0; k < 16; k++) if (used[k] && len[k] > lo) {
            keep[k] = lo; long fade = 441 < lo ? 441 : lo;
            for (long i = 0; i < fade; i++) smp[k][lo - fade + i] = (int16_t)(smp[k][lo - fade + i] * (double)(fade - i) / fade);
        }
    }
    unique8(names, used);
    static uint8_t pgm[4608]; pgm_build(pgm, kit, names, used);
    FFile ff[17]; int nf = 0; uint8_t *bufs[16] = {0};
    char kb[12]; name83(kit, "PGM", kb); memcpy(ff[nf].name83, kb, 12); ff[nf].data = pgm; ff[nf].len = sizeof pgm; nf++;
    for (int k = 0; k < 16; k++) if (used[k]) {
        bufs[k] = malloc(0x26 + 2 * keep[k]); ff[nf].len = snd_build(bufs[k], names[k], smp[k], keep[k]); ff[nf].data = bufs[k];
        char nb[12]; memset(nb, ' ', 11); nb[11] = 0; memcpy(nb, names[k], strlen(names[k])); memcpy(nb + 8, "SND", 3);
        memcpy(ff[nf].name83, nb, 12); nf++;
        printf("  A%02d  %-16s %6.2f s%s\n", k + 1, names[k], keep[k] / 44100.0, keep[k] < len[k] ? (to_hd ? "  (trimmed to fit the partition)" : "  (trimmed to fit the floppy)") : "");
    }
    int r = to_hd ? hd_add(out, kit_part, ff, nf) : fat12_build(out, ff, nf);
    for (int k = 0; k < 16; k++) { free(smp[k]); free(bufs[k]); }
    if (r) { fprintf(stderr, "  could not write %s\n", out); return -1; }
    if (to_hd) printf("%s: added to SCSI DISK PART %c: program %.8s.PGM, %d sounds. On the MPC3000: DISK -> 7 -> choose the .PGM -> <Load> -> <Do it>\n", out, 'A' + kit_part, kb, nf - 1);
    else printf("%s: program %.8s.PGM, %d sounds. On the MPC3000: DISK -> 7 -> choose the .PGM -> <Load>\n", out, kb, nf - 1);
    return 0;
}

/* a .xpm (MPC X drum program, XML) -> pads A01-A16: first active layer of instruments 1-16 */
static int kit_from_xpm(const char *xpm, const char *out)
{
    FILE *f = fopen(xpm, "rb"); if (!f) { perror(xpm); return -1; }
    fseek(f, 0, SEEK_END); long xl = ftell(f); fseek(f, 0, SEEK_SET);
    char *x = malloc(xl + 1); if (!x || fread(x, 1, xl, f) != (size_t)xl) { fclose(f); free(x); return -1; }
    x[xl] = 0; fclose(f); const char *end = x + xl;
    char prog[128] = ""; const char *pn = xml_tag(x, end, "ProgramName");
    if (pn) xml_text(pn, end, prog, sizeof prog);
    if (!strncmp(prog, "Kit-", 4)) memmove(prog, prog + 4, strlen(prog) - 3);
    if (!prog[0]) { const char *b = strrchr(xpm, '/'); snprintf(prog, sizeof prog, "%s", b ? b + 1 : xpm); char *dot = strrchr(prog, '.'); if (dot) *dot = 0; }
    char dir[1024]; snprintf(dir, sizeof dir, "%s", xpm); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; else strcpy(dir, ".");
    static char paths[16][1024], names[16][17]; memset(paths, 0, sizeof paths); memset(names, 0, sizeof names);
    int found = 0;
    for (const char *p = xml_tag(x, end, "Instrument"); p; p = xml_tag(p + 1, end, "Instrument")) {
        const char *next = xml_tag(p + 1, end, "Instrument"), *lim = next ? next : end;
        int num = xml_attr_num(p, "number");                       /* MPC 2.x: instrument number = pad (1-128) */
        if (num < 1 || num > 16 || paths[num - 1][0]) continue;
        for (const char *l = xml_tag(p, lim, "Layer"); l; l = xml_tag(l + 1, lim, "Layer")) {
            const char *ln = xml_tag(l + 1, lim, "Layer"), *ll = ln ? ln : lim;
            const char *sn = xml_tag(l, ll, "SampleName"), *ac = xml_tag(l, ll, "Active");
            char nm[128] = "", act[16] = "True";
            if (sn) xml_text(sn, ll, nm, sizeof nm);
            if (ac) xml_text(ac, ll, act, sizeof act);
            if (!nm[0] || !strcasecmp(act, "False")) continue;
            if (find_wav(dir, nm, paths[num - 1], sizeof paths[0], 2)) { char sn16[64]; safe_name(nm, sn16, 16); snprintf(names[num - 1], 17, "%.16s", sn16); found++; }
            else fprintf(stderr, "  pad A%02d: sample \"%s\" not found next to %s\n", num, nm, xpm);
            break;
        }
    }
    free(x);
    if (!found) { fprintf(stderr, "%s: no samples found for pads 1-16\n", xpm); return -1; }
    char kit[17]; safe_name(prog, kit, 16);
    printf("%s -> %s\n", xpm, out);
    return make_kit_image(out, kit, paths, names);
}
/* a folder of WAVs (sorted by name) -> pads A01-A16 */
static int cmpstr(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }
static int kit_from_folder(const char *dir, const char *out)
{
    DIR *dp = opendir(dir); if (!dp) { perror(dir); return -1; }
    char *list[512]; int n = 0; struct dirent *de;
    while ((de = readdir(dp)) && n < 512) { size_t l = strlen(de->d_name); if (l > 4 && !strcasecmp(de->d_name + l - 4, ".wav")) list[n++] = strdup(de->d_name); }
    closedir(dp); qsort(list, n, sizeof *list, cmpstr);
    static char paths[16][1024], names[16][17]; memset(paths, 0, sizeof paths); memset(names, 0, sizeof names);
    for (int k = 0; k < n && k < 16; k++) {
        snprintf(paths[k], sizeof paths[0], "%s/%s", dir, list[k]);
        char b[300]; snprintf(b, sizeof b, "%s", list[k]); b[strlen(b) - 4] = 0; char s16[64]; safe_name(b, s16, 16); snprintf(names[k], 17, "%.16s", s16);
    }
    for (int k = 0; k < n; k++) free(list[k]);
    if (!n) { fprintf(stderr, "%s: no .wav files\n", dir); return -1; }
    const char *b = strrchr(dir, '/'); char kit[17]; safe_name(b && b[1] ? b + 1 : dir, kit, 16);
    printf("%s (%d WAVs%s) -> %s\n", dir, n, n > 16 ? ", first 16 used" : "", out);
    return make_kit_image(out, kit, paths, names);
}
/* --make-kit SOURCE OUT.img: SOURCE = .xpm or a folder of WAVs. OUT = a floppy image (new), or an
 * OS-formatted SCSI disk image (the kit is added to partition kit_part). */
static int make_kit_main(const char *src, const char *out)
{
    size_t l = strlen(src);
    if (l > 4 && !strcasecmp(src + l - 4, ".xpm")) return kit_from_xpm(src, out);
    return kit_from_folder(src, out);
}
