// lzham_decode.h - AT2 norm0 plane decode: zlib-framed LZHAM stream, two
// per-texel interleaved channels.
#pragma once

#include <cstdint>
#include <vector>

namespace at2 {

// Decompresses `size` bytes of a norm0 stream into `out` (2 * channelLength
// bytes, interleaved RG pairs). Returns false on failure.
// channelLength = 136*136 = 18496 for all shipping pages.
bool decodePackedHeightRG(const uint8_t *data, size_t size, uint32_t channelLength,
                          std::vector<uint8_t> &out);

}  // namespace at2
