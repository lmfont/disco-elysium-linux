// bcn_shim.h - software BC decoders writing into AT2 channel layouts.
#pragma once

#include <cstdint>

namespace at2n {

// BC1 -> 4 bytes/texel; RGB from the block, 4th byte set to 255 (RGBX).
void decodeBC1ToRGBX(const uint8_t *in, uint32_t width, uint32_t height, uint8_t *out);

// BC3 (DXT5) -> RGBA.
void decodeBC3ToRGBA(const uint8_t *in, uint32_t width, uint32_t height, uint8_t *out);

// BC5 -> RGBA but only channels chan0/chan1 are written; other channels untouched.
void decodeBC5ToRGBA(const uint8_t *in, uint32_t width, uint32_t height, uint8_t *out,
                     uint32_t chan0, uint32_t chan1);

}  // namespace at2n
