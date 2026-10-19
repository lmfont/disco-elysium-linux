#ifndef PREPASS_HISTOGRAM_H
#define PREPASS_HISTOGRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Color32 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} Color32;

typedef struct AssetDesc {
    uint32_t assetIndex;
    uint32_t maxTopSize;
    uint32_t maxMip;
    uint32_t mipCount;
} AssetDesc;

typedef struct PrePassHashEntry {
    uint32_t key;
    uint16_t count;
    uint16_t next;
} PrePassHashEntry;

void *CreatePrePassHistogram(uint32_t width, uint32_t height,
                             uint32_t maxPageRefs);

void DestroyPrePassHistogram(void *histogram);

int32_t BuildPrePassHistogram(void *histogram,
                              const Color32 *pixelColors,
                              PrePassHashEntry *hashEntries,
                              uint16_t *pageRefs,
                              const AssetDesc *assetDescs,
                              uint32_t assetCount);

#ifdef __cplusplus
}
#endif

#endif // PREPASS_HISTOGRAM_H
