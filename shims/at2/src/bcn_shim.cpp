#include "bcn_shim.h"

#include <cstring>

namespace at2n {

static inline void unpack565(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    g = (uint8_t)(((c >> 5) & 63) * 255 / 63);
    b = (uint8_t)((c & 31) * 255 / 31);
}

void decodeBC1ToRGBX(const uint8_t *in, uint32_t width, uint32_t height, uint8_t *out) {
    const uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    for (uint32_t by = 0; by < bh; by++) {
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t *blk = in + ((size_t)by * bw + bx) * 8;
            uint16_t c0, c1;
            uint32_t idx;
            memcpy(&c0, blk, 2);
            memcpy(&c1, blk + 2, 2);
            memcpy(&idx, blk + 4, 4);
            uint8_t pal[4][3];
            unpack565(c0, pal[0][0], pal[0][1], pal[0][2]);
            unpack565(c1, pal[1][0], pal[1][1], pal[1][2]);
            if (c0 > c1) {
                for (int i = 0; i < 3; i++) {
                    pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i]) / 3);
                    pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i]) / 3);
                }
            } else {
                for (int i = 0; i < 3; i++) {
                    pal[2][i] = (uint8_t)((pal[0][i] + pal[1][i]) / 2);
                    pal[3][i] = 0;
                }
            }
            for (uint32_t y = 0; y < 4; y++) {
                uint32_t py = by * 4 + y;
                if (py >= height)
                    break;
                for (uint32_t x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width)
                        continue;
                    uint32_t p = (idx >> (2 * (4 * y + x))) & 3;
                    uint8_t *o = out + ((size_t)py * width + px) * 4;
                    o[0] = pal[p][0];
                    o[1] = pal[p][1];
                    o[2] = pal[p][2];
                    o[3] = 255;
                }
            }
        }
    }
}

static void decodeBC4Channel(const uint8_t *blk, uint8_t out[16]) {
    uint8_t e0 = blk[0], e1 = blk[1];
    uint8_t pal[8];
    pal[0] = e0;
    pal[1] = e1;
    // Interpolate with a positive weighted numerator: e0 + (e1-e0)*k would
    // truncate a negative difference toward zero when e0 > e1, giving
    // off-by-one palette entries compared with the BC4 formula.
    if (e0 > e1) {
        for (int i = 0; i < 6; i++) {
            int k = i + 1;
            pal[2 + i] = (uint8_t)((e0 * (7 - k) + e1 * k) / 7);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            int k = i + 1;
            pal[2 + i] = (uint8_t)((e0 * (5 - k) + e1 * k) / 5);
        }
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++)
        bits |= (uint64_t)blk[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++)
        out[i] = pal[(bits >> (3 * i)) & 7];
}

void decodeBC5ToRGBA(const uint8_t *in, uint32_t width, uint32_t height, uint8_t *out,
                     uint32_t chan0, uint32_t chan1) {
    const uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    for (uint32_t by = 0; by < bh; by++) {
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t *blk = in + ((size_t)by * bw + bx) * 16;
            uint8_t ch0[16], ch1[16];
            decodeBC4Channel(blk, ch0);
            decodeBC4Channel(blk + 8, ch1);
            for (uint32_t y = 0; y < 4; y++) {
                uint32_t py = by * 4 + y;
                if (py >= height)
                    break;
                for (uint32_t x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width)
                        continue;
                    int i = y * 4 + x;
                    uint8_t *o = out + ((size_t)py * width + px) * 4;
                    if (chan0 < 4)
                        o[chan0] = ch0[i];
                    if (chan1 < 4)
                        o[chan1] = ch1[i];
                }
            }
        }
    }
}

void decodeBC3ToRGBA(const uint8_t *in, uint32_t width, uint32_t height, uint8_t *out) {
    const uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    for (uint32_t by = 0; by < bh; by++) {
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t *blk = in + ((size_t)by * bw + bx) * 16;
            uint8_t alpha[16];
            decodeBC4Channel(blk, alpha);
            uint16_t c0, c1;
            uint32_t idx;
            memcpy(&c0, blk + 8, 2);
            memcpy(&c1, blk + 10, 2);
            memcpy(&idx, blk + 12, 4);
            uint8_t pal[4][3];
            unpack565(c0, pal[0][0], pal[0][1], pal[0][2]);
            unpack565(c1, pal[1][0], pal[1][1], pal[1][2]);
            for (int i = 0; i < 3; i++) {
                pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i]) / 3);
                pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i]) / 3);
            }
            for (uint32_t y = 0; y < 4; y++) {
                uint32_t py = by * 4 + y;
                if (py >= height)
                    break;
                for (uint32_t x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width)
                        continue;
                    uint32_t p = (idx >> (2 * (4 * y + x))) & 3;
                    uint8_t *o = out + ((size_t)py * width + px) * 4;
                    o[0] = pal[p][0];
                    o[1] = pal[p][1];
                    o[2] = pal[p][2];
                    o[3] = alpha[y * 4 + x];
                }
            }
        }
    }
}

}  // namespace at2n
