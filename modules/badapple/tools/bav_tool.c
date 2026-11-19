/* bav_tool — encode / decode Bad Apple Video streams.
 *   bav_tool enc in.raw width height fps shift out.bav
 *   bav_tool dec in.bav out.raw
 * in.raw / out.raw: frames back to back, one byte per pixel (0 or 1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../bav.h"

typedef struct {
    FILE *f;
    uint64_t low;
    uint32_t range;
    uint8_t cache;
    uint64_t cache_size, bytes;
} enc_t;

static void shift_low(enc_t *e) {
    if ((uint32_t)e->low < 0xFF000000u || (e->low >> 32) != 0) {
        uint8_t temp = e->cache;
        do {
            fputc((uint8_t)(temp + (e->low >> 32)), e->f);
            e->bytes++;
            temp = 0xFF;
        } while (--e->cache_size);
        e->cache = (uint8_t)(e->low >> 24);
    }
    e->cache_size++;
    e->low = (e->low & 0x00FFFFFFu) << 8;
}

static void enc_bit(enc_t *e, uint16_t *p, int bit, int shift) {
    uint32_t bound = (e->range >> BAV_PROB_BITS) * *p;
    if (!bit) {
        e->range = bound;
        *p += ((1u << BAV_PROB_BITS) - *p) >> shift;
    } else {
        e->low += bound;
        e->range -= bound;
        *p -= *p >> shift;
    }
    while (e->range < (1u << 24)) {
        e->range <<= 8;
        shift_low(e);
    }
}

static uint8_t *read_all(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != (size_t)*len) { perror("read"); exit(1); }
    fclose(f);
    return b;
}

static int encode(const char *in, int w, int h, int fps, int shift, const char *out) {
    long len;
    uint8_t *raw = read_all(in, &len);
    long fsz = (long)w * h;
    uint32_t n = (uint32_t)(len / fsz);
    uint8_t *zero = calloc(fsz, 1);
    static uint16_t prob[BAV_NCTX];
    for (uint32_t i = 0; i < BAV_NCTX; i++) prob[i] = BAV_PROB_INIT;

    enc_t e = {0};
    e.f = fopen(out, "wb");
    if (!e.f) { perror(out); return 1; }
    uint8_t hdr[16] = {'B', 'A', 'V', '1', w & 255, w >> 8, h & 255, h >> 8, fps, shift, 0, 0,
                       n & 255, n >> 8 & 255, n >> 16 & 255, n >> 24};
    fwrite(hdr, 1, 16, e.f);
    e.range = 0xFFFFFFFFu;
    e.cache_size = 1;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *cur = raw + i * fsz, *prev = i ? cur - fsz : zero;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                enc_bit(&e, &prob[bav_context(cur, prev, w, h, x, y)], cur[y * w + x], shift);
    }
    for (int i = 0; i < 5; i++) shift_low(&e);
    fclose(e.f);
    printf("%u frames %dx%d @%d fps -> %llu bytes (+16 header)\n", n, w, h, fps,
           (unsigned long long)e.bytes);
    return 0;
}

static int decode(const char *in, const char *out) {
    long len;
    uint8_t *data = read_all(in, &len);
    static bav_decoder d;
    if (bav_decoder_init(&d, data, (uint32_t)len)) { fprintf(stderr, "bad header\n"); return 1; }
    long fsz = (long)d.info.width * d.info.height;
    uint8_t *a = calloc(fsz, 1), *b = calloc(fsz, 1);
    FILE *f = fopen(out, "wb");
    while (bav_decode_frame(&d, a, b) == 0) {
        fwrite(a, 1, fsz, f);
        uint8_t *t = a; a = b; b = t;
    }
    fclose(f);
    printf("decoded %u frames\n", d.frame_index);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 8 && !strcmp(argv[1], "enc"))
        return encode(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), argv[7]);
    if (argc == 4 && !strcmp(argv[1], "dec")) return decode(argv[2], argv[3]);
    fprintf(stderr, "usage: bav_tool enc in.raw w h fps shift out.bav | dec in.bav out.raw\n");
    return 2;
}
