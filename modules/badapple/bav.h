/* bav.h — Bad Apple Video codec (1-bit frames, context-modelled range coding).
 *
 * Stream layout
 *   16-byte header: "BAV1", u16 width, u16 height, u8 fps, u8 adapt_shift,
 *                   u16 reserved (0), u32 frame_count   (all little-endian)
 *   then one LZMA-style range-coded bitstream for all frames.
 *
 * Each pixel (row-major, 1 = white) is coded with an adaptive probability
 * chosen by a 14-bit context:
 *   bits 0-4  current frame: W, WW, NW, N, NE   (already decoded neighbours)
 *   bits 5-13 previous frame: 3x3 block around the same position
 * Pixels outside the frame read as 0 (black). The previous frame of frame 0
 * is all black.
 *
 * The decoder is plain C with no allocation: the caller owns the buffers.
 * RAM: 2 * width * height bytes for frames + 32 KiB of probabilities.
 */
#ifndef BAV_H
#define BAV_H
#include <stdint.h>

#define BAV_CTX_BITS 14
#define BAV_NCTX (1u << BAV_CTX_BITS)
#define BAV_PROB_BITS 11
#define BAV_PROB_INIT (1u << (BAV_PROB_BITS - 1))
#define BAV_HEADER_SIZE 16

typedef struct {
    uint16_t width, height;
    uint8_t fps, shift;
    uint32_t frames;
} bav_info;

static inline int bav_parse_header(const uint8_t *p, bav_info *info) {
    if (p[0] != 'B' || p[1] != 'A' || p[2] != 'V' || p[3] != '1') return -1;
    info->width = (uint16_t)(p[4] | p[5] << 8);
    info->height = (uint16_t)(p[6] | p[7] << 8);
    info->fps = p[8];
    info->shift = p[9];
    info->frames = (uint32_t)p[12] | (uint32_t)p[13] << 8 | (uint32_t)p[14] << 16 | (uint32_t)p[15] << 24;
    return 0;
}

/* Context of pixel (x, y). cur/prev are width*height bytes holding 0 or 1. */
static inline uint32_t bav_context(const uint8_t *cur, const uint8_t *prev, int w, int h, int x, int y) {
#define CUR(xx, yy) (((xx) >= 0 && (xx) < w && (yy) >= 0) ? cur[(yy) * w + (xx)] : 0)
#define PRV(xx, yy) (((xx) >= 0 && (xx) < w && (yy) >= 0 && (yy) < h) ? prev[(yy) * w + (xx)] : 0)
    uint32_t c = CUR(x - 1, y) | CUR(x - 2, y) << 1 | CUR(x - 1, y - 1) << 2 | CUR(x, y - 1) << 3 |
                 CUR(x + 1, y - 1) << 4;
    int k = 5;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) c |= (uint32_t)PRV(x + dx, y + dy) << k++;
    return c;
#undef CUR
#undef PRV
}

/* ---- decoder ---- */
typedef struct {
    const uint8_t *src, *end;
    uint32_t range, code;
    uint16_t prob[BAV_NCTX];
    bav_info info;
    uint32_t frame_index;
} bav_decoder;

static inline uint8_t bav__byte(bav_decoder *d) { return d->src < d->end ? *d->src++ : 0; }

/* data/len: the whole .bav file. Returns 0 on success. */
static inline int bav_decoder_init(bav_decoder *d, const uint8_t *data, uint32_t len) {
    if (len < BAV_HEADER_SIZE + 5 || bav_parse_header(data, &d->info)) return -1;
    d->src = data + BAV_HEADER_SIZE;
    d->end = data + len;
    d->range = 0xFFFFFFFFu;
    d->code = 0;
    for (int i = 0; i < 5; i++) d->code = d->code << 8 | bav__byte(d);
    for (uint32_t i = 0; i < BAV_NCTX; i++) d->prob[i] = BAV_PROB_INIT;
    d->frame_index = 0;
    return 0;
}

static inline int bav__bit(bav_decoder *d, uint16_t *p) {
    uint32_t bound = (d->range >> BAV_PROB_BITS) * *p;
    int bit;
    if (d->code < bound) {
        d->range = bound;
        *p += ((1u << BAV_PROB_BITS) - *p) >> d->info.shift;
        bit = 0;
    } else {
        d->code -= bound;
        d->range -= bound;
        *p -= *p >> d->info.shift;
        bit = 1;
    }
    while (d->range < (1u << 24)) { /* fast adaptation lets range drop >8 bits */
        d->range <<= 8;
        d->code = d->code << 8 | bav__byte(d);
    }
    return bit;
}

/* Decode the next frame into cur (width*height bytes, 0/1) using prev
 * (the previous frame; all zero before frame 0). Returns 0, or -1 at end. */
static inline int bav_decode_frame(bav_decoder *d, uint8_t *cur, const uint8_t *prev) {
    if (d->frame_index >= d->info.frames) return -1;
    int w = d->info.width, h = d->info.height;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            cur[y * w + x] = (uint8_t)bav__bit(d, &d->prob[bav_context(cur, prev, w, h, x, y)]);
    d->frame_index++;
    return 0;
}

#endif
