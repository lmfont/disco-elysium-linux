#include "prepass_histogram.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

static_assert(sizeof(Color32) == 4, "Color32 must be 4 bytes");
static_assert(offsetof(Color32, r) == 0, "Color32::r must be at offset 0");
static_assert(offsetof(Color32, g) == 1, "Color32::g must be at offset 1");
static_assert(offsetof(Color32, b) == 2, "Color32::b must be at offset 2");
static_assert(offsetof(Color32, a) == 3, "Color32::a must be at offset 3");

static_assert(sizeof(AssetDesc) == 16, "AssetDesc must be 16 bytes");
static_assert(offsetof(AssetDesc, assetIndex) == 0, "AssetDesc::assetIndex must be at offset 0");
static_assert(offsetof(AssetDesc, maxTopSize) == 4, "AssetDesc::maxTopSize must be at offset 4");
static_assert(offsetof(AssetDesc, maxMip) == 8, "AssetDesc::maxMip must be at offset 8");
static_assert(offsetof(AssetDesc, mipCount) == 12, "AssetDesc::mipCount must be at offset 12");

static_assert(sizeof(PrePassHashEntry) == 8, "PrePassHashEntry must be 8 bytes");
static_assert(offsetof(PrePassHashEntry, key) == 0, "PrePassHashEntry::key must be at offset 0");
static_assert(offsetof(PrePassHashEntry, count) == 4, "PrePassHashEntry::count must be at offset 4");
static_assert(offsetof(PrePassHashEntry, next) == 6, "PrePassHashEntry::next must be at offset 6");

namespace {

struct PrePassHistogramInstance {
    uint32_t width;
    uint32_t height;
    uint32_t maxPageRefs;
    uint32_t bucketCount;
    uint32_t bucketMask;
    int32_t *buckets;
};

static inline uint32_t hashKey(uint32_t k) {
    k ^= k >> 16;
    k *= 0x85ebca6bu;
    k ^= k >> 13;
    return k;
}

static inline uint32_t computeBucketCount(uint32_t maxPageRefs) {
    if (maxPageRefs == 0) {
        return 0;
    }
    uint32_t count = 64u;
    uint32_t target = (maxPageRefs > 32768u) ? 65536u : (maxPageRefs * 2u);
    while (count < target && count < 65536u) {
        count <<= 1;
    }
    return count;
}

static inline bool emitPixelSample(
    const Color32 &px,
    uint16_t weight,
    const AssetDesc *assetDescs,
    uint32_t assetCount,
    PrePassHashEntry *hashEntries,
    uint16_t *pageRefs,
    int32_t *buckets,
    uint32_t bucketMask,
    uint32_t maxPageRefs,
    uint32_t &n)
{
    uint32_t b = px.b;
    if (b == 0xFFu) {
        return true;
    }

    uint32_t asset = b >> 4;
    if (asset >= assetCount) {
        return true;
    }

    uint32_t q = b & 0x0Fu;
    const AssetDesc &d = assetDescs[asset];

    uint32_t m = (q <= d.maxMip) ? q : d.maxMip;
    if (d.mipCount <= m) {
        return true;
    }

    uint32_t r = px.r;
    uint32_t g = px.g;
    uint32_t a = px.a;

    uint32_t mask = d.maxTopSize >> q;
    uint32_t x0 = ((r + ((a & 0x0Fu) << 8)) >> q) & mask;
    uint32_t y0 = ((g + ((a & 0xF0u) << 4)) >> q) & mask;

    uint32_t levels = d.mipCount - m;
    for (uint32_t l = 0; l < levels; ++l) {
        uint32_t x = (l < 32u) ? (x0 >> l) : 0u;
        uint32_t y = (l < 32u) ? (y0 >> l) : 0u;
        uint32_t mip = m + l;
        uint32_t key = (asset << 28) | (mip << 24) | (y << 12) | x;

        uint32_t bucket = hashKey(key) & bucketMask;
        int32_t idx = buckets[bucket];
        bool found = false;
        while (idx >= 0) {
            PrePassHashEntry &entry = hashEntries[idx];
            if (entry.key == key) {
                entry.count = static_cast<uint16_t>(entry.count + weight);
                found = true;
                break;
            }
            uint16_t next = entry.next;
            idx = (next == 0xFFFFu) ? -1 : static_cast<int32_t>(next);
        }

        if (!found) {
            if (n == maxPageRefs) {
                return false;
            }
            int32_t prevHead = buckets[bucket];
            hashEntries[n].key = key;
            hashEntries[n].count = weight;
            hashEntries[n].next = (prevHead < 0) ? 0xFFFFu : static_cast<uint16_t>(prevHead);
            buckets[bucket] = static_cast<int32_t>(n);
            pageRefs[n] = static_cast<uint16_t>(n);
            ++n;
        }
    }

    return true;
}

} // namespace

extern "C" {

__attribute__((visibility("default")))
void *CreatePrePassHistogram(uint32_t width, uint32_t height,
                             uint32_t maxPageRefs)
{
    PrePassHistogramInstance *inst = new (std::nothrow) PrePassHistogramInstance;
    if (!inst) {
        return nullptr;
    }

    inst->width = width;
    inst->height = height;
    inst->maxPageRefs = maxPageRefs;
    inst->bucketCount = computeBucketCount(maxPageRefs);
    inst->bucketMask = (inst->bucketCount > 0) ? (inst->bucketCount - 1u) : 0u;
    inst->buckets = nullptr;

    if (inst->bucketCount > 0) {
        inst->buckets = new (std::nothrow) int32_t[inst->bucketCount];
        if (!inst->buckets) {
            delete inst;
            return nullptr;
        }
    }

    return static_cast<void *>(inst);
}

__attribute__((visibility("default")))
void DestroyPrePassHistogram(void *histogram)
{
    if (!histogram) {
        return;
    }
    PrePassHistogramInstance *inst = static_cast<PrePassHistogramInstance *>(histogram);
    delete[] inst->buckets;
    delete inst;
}

__attribute__((visibility("default")))
int32_t BuildPrePassHistogram(void *histogram,
                              const Color32 *pixelColors,
                              PrePassHashEntry *hashEntries,
                              uint16_t *pageRefs,
                              const AssetDesc *assetDescs,
                              uint32_t assetCount)
{
    if (!histogram || !pixelColors || !hashEntries || !pageRefs || !assetDescs) {
        return 0;
    }

    PrePassHistogramInstance *inst = static_cast<PrePassHistogramInstance *>(histogram);
    uint32_t width = inst->width;
    uint32_t height = inst->height;
    uint32_t maxPageRefs = inst->maxPageRefs;

    uint32_t blocksX = width >> 1;
    uint32_t blocksY = height >> 1;

    if (blocksX == 0 || blocksY == 0 || maxPageRefs == 0 || assetCount == 0) {
        return 0;
    }

    int32_t *buckets = inst->buckets;
    uint32_t bucketMask = inst->bucketMask;
    std::memset(buckets, 0xFF, static_cast<size_t>(inst->bucketCount) * sizeof(int32_t));

    uint32_t n = 0;

    for (uint32_t by = 0; by < blocksY; ++by) {
        size_t row0 = static_cast<size_t>(2u * by) * static_cast<size_t>(width);
        size_t row1 = row0 + static_cast<size_t>(width);

        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            size_t col = static_cast<size_t>(2u * bx);
            const Color32 &c00 = pixelColors[row0 + col];
            const Color32 &c01 = pixelColors[row0 + col + 1u];
            const Color32 &c10 = pixelColors[row1 + col];
            const Color32 &c11 = pixelColors[row1 + col + 1u];

            uint32_t u00, u01, u10, u11;
            std::memcpy(&u00, &c00, sizeof(uint32_t));
            std::memcpy(&u01, &c01, sizeof(uint32_t));
            std::memcpy(&u10, &c10, sizeof(uint32_t));
            std::memcpy(&u11, &c11, sizeof(uint32_t));

            if (u00 == u01 && u00 == u10 && u00 == u11) {
                if (!emitPixelSample(c00, 4u, assetDescs, assetCount,
                                     hashEntries, pageRefs, buckets, bucketMask,
                                     maxPageRefs, n)) {
                    return static_cast<int32_t>(n);
                }
            } else {
                if (!emitPixelSample(c00, 1u, assetDescs, assetCount,
                                     hashEntries, pageRefs, buckets, bucketMask,
                                     maxPageRefs, n)) {
                    return static_cast<int32_t>(n);
                }
                if (!emitPixelSample(c01, 1u, assetDescs, assetCount,
                                     hashEntries, pageRefs, buckets, bucketMask,
                                     maxPageRefs, n)) {
                    return static_cast<int32_t>(n);
                }
                if (!emitPixelSample(c10, 1u, assetDescs, assetCount,
                                     hashEntries, pageRefs, buckets, bucketMask,
                                     maxPageRefs, n)) {
                    return static_cast<int32_t>(n);
                }
                if (!emitPixelSample(c11, 1u, assetDescs, assetCount,
                                     hashEntries, pageRefs, buckets, bucketMask,
                                     maxPageRefs, n)) {
                    return static_cast<int32_t>(n);
                }
            }
        }
    }

    return static_cast<int32_t>(n);
}

} // extern "C"
