#include "lzham_decode.h"

#include <cstring>

#include "lzham/lzham.h"

namespace at2 {

// norm0 streams are zlib-framed LZHAM: 2-byte header (CM=14), 32 KB
// dictionary, big-endian Adler-32 trailer.  The decompressed halves are the
// page's two channels and are interleaved per texel.
bool decodePackedHeightRG(const uint8_t *data, size_t size, uint32_t channelLength,
                          std::vector<uint8_t> &out) {
    if (!data || !size)
        return false;

    lzham_decompress_params p;
    memset(&p, 0, sizeof(p));
    p.m_struct_size = sizeof(lzham_decompress_params);
    p.m_dict_size_log2 = 15;                        // 32 KB dictionary
    p.m_table_update_rate = 0;                      // default
    p.m_decompress_flags = LZHAM_DECOMP_FLAG_COMPUTE_ADLER32 |
                           LZHAM_DECOMP_FLAG_READ_ZLIB_STREAM;
    p.m_num_seed_bytes = 0;
    p.m_pSeed_bytes = nullptr;
    p.m_table_max_update_interval = 0;
    p.m_table_update_interval_slow_rate = 0;

    std::vector<uint8_t> tmp((size_t)channelLength * 2);
    size_t outLen = tmp.size();
    lzham_uint32 adler = 0;
    lzham_decompress_status_t st =
        lzham_decompress_memory(&p, tmp.data(), &outLen, data, size, &adler);
    if (st != LZHAM_DECOMP_STATUS_SUCCESS || outLen != tmp.size())
        return false;

    // interleave halves: first channelLength bytes are one channel, the rest the other
    out.resize(outLen);
    for (size_t i = 0; i < channelLength; i++) {
        out[i * 2] = tmp[i];
        out[i * 2 + 1] = tmp[i + channelLength];
    }
    return true;
}

}  // namespace at2
