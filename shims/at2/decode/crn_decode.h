// crn_decode.h - wrapper around the pinned Unity-fork crunch decoder.
#pragma once

#include <cstdint>
#include <vector>

// provided by crn/crn_decomp.h inclusion in crn_decode.cpp
namespace at2 {

struct CrnInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 0;
    uint32_t faces = 0;
    int format = -1;       // crn_format
    uint32_t bytesPerBlock = 0;
};

// Returns false if the stream is not a valid CRN stream or decode fails.
// On success `out` holds `levels` contiguous face-0 levels (level 0 first).
bool crnDecodeStream(const uint8_t *data, size_t size, std::vector<uint8_t> &out, CrnInfo *info = nullptr);

// Extracts a single level (0-based) into out. Sizes come from the stream header.
bool crnDecodeLevel(const uint8_t *data, size_t size, uint32_t level, std::vector<uint8_t> &out, CrnInfo *info = nullptr);

}  // namespace at2
