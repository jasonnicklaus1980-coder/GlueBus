#include "wav_read.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

float* rm_wav_read_mono(const char* path, size_t* n, double* sr) {
    *n = 0; *sr = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 44 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t* d = malloc((size_t)size);
    if (!d || fread(d, 1, (size_t)size, f) != (size_t)size) { free(d); fclose(f); return NULL; }
    fclose(f);

    float* out = NULL;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) goto fail;

    int have_fmt = 0;
    unsigned fmt_tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = NULL;
    uint32_t data_size = 0;
    size_t pos = 12;
    while (pos + 8 <= (size_t)size) {
        uint32_t csize = le32(d + pos + 4);
        const uint8_t* body = d + pos + 8;
        size_t avail = (size_t)size - (pos + 8);
        if (memcmp(d + pos, "fmt ", 4) == 0 && csize >= 16 && avail >= 16) {
            fmt_tag = le16(body);
            channels = le16(body + 2);
            rate = le32(body + 4);
            bits = le16(body + 14);
            if (fmt_tag == 0xFFFE && csize >= 40 && avail >= 40) fmt_tag = le16(body + 24);  /* sub-format GUID */
            have_fmt = 1;
        } else if (memcmp(d + pos, "data", 4) == 0) {
            data = body;
            data_size = csize > avail ? (uint32_t)avail : csize;   /* tolerate a truncated file */
        }
        size_t next = pos + 8 + (size_t)csize + (csize & 1);
        if (data && have_fmt) break;
        if (next <= pos) break;
        pos = next;
    }
    if (!have_fmt || !data || channels == 0 || rate == 0) goto fail;
    int is_float = fmt_tag == 3;
    if (!(fmt_tag == 1 || is_float)) goto fail;
    if (!((!is_float && (bits == 16 || bits == 24 || bits == 32)) || (is_float && bits == 32))) goto fail;

    unsigned bps = bits / 8;
    size_t frame_bytes = (size_t)bps * channels;
    size_t frames = data_size / frame_bytes;
    out = malloc((frames ? frames : 1) * sizeof(float));
    if (!out) goto fail;
    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        for (unsigned c = 0; c < channels; ++c) {
            const uint8_t* s = data + i * frame_bytes + c * bps;
            double v;
            if (is_float) {
                uint32_t u = le32(s);
                float fv; memcpy(&fv, &u, 4);
                v = fv;
            } else if (bits == 16) {
                v = (int16_t)le16(s) / 32768.0;
            } else if (bits == 24) {
                int32_t iv = (int32_t)(s[0] | (s[1] << 8) | (s[2] << 16));
                if (iv & 0x800000) iv -= 0x1000000;
                v = iv / 8388608.0;
            } else {
                v = (int32_t)le32(s) / 2147483648.0;
            }
            acc += v;
        }
        out[i] = (float)(acc / channels);
    }
    *n = frames;
    *sr = (double)rate;
    free(d);
    return out;

fail:
    free(out);
    free(d);
    return NULL;
}
