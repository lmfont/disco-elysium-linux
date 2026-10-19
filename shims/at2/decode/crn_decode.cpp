#include "crn_decode.h"

#include "crn/crn_decomp.h"

namespace at2 {

bool crnDecodeLevel(const uint8_t *data, size_t size, uint32_t level, std::vector<uint8_t> &out, CrnInfo *info) {
    unitycrnd::crn_texture_info ti;
    if (!unitycrnd::crnd_get_texture_info(data, (unitycrnd::uint32)size, &ti))
        return false;
    if (level >= ti.m_levels)
        return false;
    if (info) {
        info->width = ti.m_width;
        info->height = ti.m_height;
        info->levels = ti.m_levels;
        info->faces = ti.m_faces;
        info->format = (int)ti.m_format;
        info->bytesPerBlock = ti.m_bytes_per_block;
    }

    uint64_t w = (uint64_t)ti.m_width >> level;
    uint64_t h = (uint64_t)ti.m_height >> level;
    if (!w)
        w = 1;
    if (!h)
        h = 1;
    uint32_t bytesPerBlock = unitycrnd::crnd_get_bytes_per_dxt_block(ti.m_format);
    if (!bytesPerBlock)
        return false;
    uint64_t bx = (w + 3) / 4;
    uint64_t by = (h + 3) / 4;
    uint64_t rowPitch = bx * bytesPerBlock;
    uint64_t total = rowPitch * by;
    // crnd_unpack_level takes 32-bit sizes; reject before allocating or
    // letting the decoder write past a wrapped size.
    if (rowPitch > UINT32_MAX || total > UINT32_MAX)
        return false;
    out.resize((size_t)total);

    unitycrnd::crnd_unpack_context ctx = unitycrnd::crnd_unpack_begin(data, (unitycrnd::uint32)size);
    if (!ctx)
        return false;

    void *dst[6] = {out.data(), nullptr, nullptr, nullptr, nullptr, nullptr};
    bool ok = unitycrnd::crnd_unpack_level(ctx, dst, (unitycrnd::uint32)total,
                                           (unitycrnd::uint32)rowPitch, level);
    unitycrnd::crnd_unpack_end(ctx);
    return ok;
}

bool crnDecodeStream(const uint8_t *data, size_t size, std::vector<uint8_t> &out, CrnInfo *info) {
    unitycrnd::crn_texture_info ti;
    if (!unitycrnd::crnd_get_texture_info(data, (unitycrnd::uint32)size, &ti))
        return false;
    if (info) {
        info->width = ti.m_width;
        info->height = ti.m_height;
        info->levels = ti.m_levels;
        info->faces = ti.m_faces;
        info->format = (int)ti.m_format;
        info->bytesPerBlock = ti.m_bytes_per_block;
    }
    out.clear();
    std::vector<uint8_t> lvl;
    for (uint32_t l = 0; l < ti.m_levels; l++) {
        if (!crnDecodeLevel(data, size, l, lvl, nullptr))
            return false;
        out.insert(out.end(), lvl.begin(), lvl.end());
    }
    return true;
}

}  // namespace at2
