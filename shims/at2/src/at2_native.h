// at2_native.h - shared types for the AmplifyTextureNative Linux shim.
#pragma once

#include <cstdint>

// Must match AmplifyTexture.NativeRuntime.PlaneDesc (3 bytes, sequential).
#pragma pack(push, 1)
struct PlaneDesc {
    uint8_t numChannels;
    uint8_t compType;
    uint8_t compQuality;
};
#pragma pack(pop)

namespace at2n {

constexpr int kPageSize = 136;                 // VirtualTexture.PageSize
constexpr int kPageChannelLength = 18496;      // 136*136

struct PageDecoder {
    uint32_t pageSize = kPageSize;
    uint32_t channelLength = kPageChannelLength;
    uint8_t numPlanes = 0;
    PlaneDesc planes[16];
};

}  // namespace at2n
