// amplify_texture_native.cpp - Linux reimplementation of AmplifyTextureNative.dll
// (Strategy 1: managed AT2 runs unmodified; this .so provides the 27 exports).
//
// Decode chain (validated against the shipping .vtc2 caches):
//   TranscodeCrunchToBC  -> Unity-fork crunch (crn/crn_decomp.h), level 0
//   DecodePackedHeightRG -> LZHAM (zlib-wrapped, dict 32 KB) + half interleave
//   DecodeBC1/BC3/BC5    -> software decoders into AT2 channel layouts
// Prepass: the histogram is implemented in prepass_histogram.cpp. The
// readback exports capture the pre-pass render texture on the render thread
// into a ping-pong pair of PBOs
// (asynchronous DMA) and publish the previous frame's copy to the managed
// side: desktop GL has no managed AsyncGPUReadback, so the game always takes
// this native path (the readback feeds the streaming visibility histogram;
// without it nothing streams after the initial preload).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <new>
#include <vector>

#include <atomic>
#include <mutex>

#include "at2_native.h"
#include "bcn_shim.h"
#include "crn_decode.h"
#include "lzham_decode.h"

using namespace at2n;
using at2::crnDecodeLevel;
using at2::CrnInfo;
using at2::decodePackedHeightRG;

namespace {

// ---------------------------------------------------------------- decoder

PageDecoder *asDecoder(void *handle) {
    return static_cast<PageDecoder *>(handle);
}

// True when a level-0 CRN decode fits the decoder's page contract. The
// caller allocates at most pageSize*pageSize*4 bytes for a decoded page
// (BC1/BC3/BC5 all expand to 4 bytes per texel), so a stream with larger
// dimensions, more faces, or an unexpected decoded size must be rejected
// before the memcpy in TranscodeCrunchToBC.
bool crnFitsPage(const PageDecoder *d, const CrnInfo &info, size_t bytes) {
    if (!d || d->pageSize == 0 || info.faces != 1 ||
        info.width == 0 || info.height == 0)
        return false;
    // Crunch's supported formats are all 8- or 16-byte block formats; the
    // decoder itself rejects unknown format ids.
    if (info.bytesPerBlock != 8 && info.bytesPerBlock != 16)
        return false;
    const uint64_t maxTexels = (uint64_t)d->pageSize * d->pageSize;
    if ((uint64_t)info.width * info.height > maxTexels)
        return false;
    const uint64_t blocksX = ((uint64_t)info.width + 3) / 4;
    const uint64_t blocksY = ((uint64_t)info.height + 3) / 4;
    const uint64_t decoded = blocksX * blocksY * info.bytesPerBlock;
    return decoded <= maxTexels * 4 && decoded == (uint64_t)bytes;
}

// ---------------------------------------------------------------- prepass

struct PrePassReadback {
    void *texture;
    uint32_t width, height;
    uint8_t *pixels;  // w*h*4, guarded by pixelsMutex
    std::mutex pixelsMutex;
    std::atomic<bool> pending{false};

    // Ping-pong PBO state. Touched only from the render thread, and always
    // under g_readbackMutex (DestroyPrePassReadback queues the buffers and
    // fences for deletion under the same lock).
    unsigned int pbos[2] = {0, 0};
    void *fences[2] = {nullptr, nullptr};  // fence for each PBO's queued capture
    bool complete[2] = {false, false};     // capture done, pixels not yet published
    int writeIndex = 0;                    // preferred PBO for the next capture
    bool pboDisabled = false;              // fence/allocation failure: use sync reads
};

// ---------------------------------------------------------------- GL readback
//
// Desktop GL has no managed AsyncGPUReadback, so the game always takes the
// native readback path: PrePassReadback (a MonoBehaviour) calls
// IssuePrePassRead() every end of frame and then GL.IssuePluginEvent with the
// pointer returned by GetRenderEventFunc(); PrePass.RenderAndReadback() later
// collects the pixels with GetPrePassPixels(). The original Windows DLL does
// the copy with its GPU device interface.
//
// Here the render-thread event (the GL context is current on that thread —
// that is exactly what GL.IssuePluginEvent guarantees) uses a two-stage PBO
// ping-pong instead of a synchronous glGetTexImage into client memory:
//
//   frame N   : glGetTexImage with a GL_PIXEL_PACK_BUFFER bound. The last
//               argument is then a byte offset, and the call only queues an
//               asynchronous DMA — the render thread does not wait. A fence
//               is inserted after the capture.
//   frame N+1 : poll the fence with zero timeout; only when the GPU has
//               signaled it is the *other* PBO mapped with
//               glMapBufferRange, copied under pixelsMutex for
//               GetPrePassPixels, and this frame's capture queued into the
//               first PBO. A pending fence skips the frame instead of
//               blocking the render thread on a map.
//
// A synchronous read would flush the pipeline and stall the render thread on
// the PCIe transfer every frame. The fence poll keeps the event non-blocking;
// while a fence is pending, the previously published pixels stay in place.
// PBOs are created lazily in the event (CreatePrePassReadback runs on the
// main thread, where no GL context exists), and DestroyPrePassReadback only
// queues the buffers and fences for deletion by a later event, for the same
// reason. GL state the readback touches (texture binding, pack buffer
// binding, pack alignment/row length/skip pixels/rows) is saved before any
// lazy initialization and restored afterwards (including pack swap bytes),
// so Unity sees the state it left — Unity assumes plugins leave it as they
// found it.
//
// Without this readback the visibility histogram is built from a zeroed
// buffer: pages visible at scene load (read once through the managed preload
// path) stream in, but nothing else ever does.

constexpr unsigned int kGlTexture2D = 0x0DE1;
constexpr unsigned int kGlTexBinding2D = 0x8069;
constexpr unsigned int kGlRgba = 0x1908;
constexpr unsigned int kGlUnsignedByte = 0x1401;
constexpr unsigned int kGlPixelPackBuffer = 0x88EB;
constexpr unsigned int kGlPixelPackBufferBinding = 0x88ED;
constexpr unsigned int kGlStreamRead = 0x88E1;
constexpr unsigned int kGlMapReadBit = 0x0001;
constexpr unsigned int kGlPackSwapBytes = 0x0D00;
constexpr unsigned int kGlPackAlignment = 0x0D05;
constexpr unsigned int kGlPackRowLength = 0x0D02;
constexpr unsigned int kGlPackSkipRows = 0x0D03;
constexpr unsigned int kGlPackSkipPixels = 0x0D04;
constexpr unsigned int kGlSyncGpuCommandsComplete = 0x9117;
constexpr unsigned int kGlAlreadySignaled = 0x911A;
constexpr unsigned int kGlConditionSatisfied = 0x911C;
constexpr unsigned int kGlWaitFailed = 0x911D;

typedef void (*PFN_glBindTexture)(unsigned int target, unsigned int texture);
typedef void (*PFN_glGetTexImage)(unsigned int target, int level, unsigned int format,
                                  unsigned int type, void *pixels);
typedef void (*PFN_glGetIntegerv)(unsigned int pname, int *params);
typedef void (*PFN_glPixelStorei)(unsigned int pname, int param);
typedef void (*PFN_glGenBuffers)(int n, unsigned int *buffers);
typedef void (*PFN_glDeleteBuffers)(int n, const unsigned int *buffers);
typedef void (*PFN_glBindBuffer)(unsigned int target, unsigned int buffer);
typedef void (*PFN_glBufferData)(unsigned int target, ptrdiff_t size, const void *data,
                                 unsigned int usage);
typedef void *(*PFN_glMapBufferRange)(unsigned int target, ptrdiff_t offset, ptrdiff_t length,
                                      unsigned int access);
typedef unsigned char (*PFN_glUnmapBuffer)(unsigned int target);
typedef void *(*PFN_glFenceSync)(unsigned int condition, unsigned int flags);
typedef unsigned int (*PFN_glClientWaitSync)(void *sync, unsigned int flags,
                                             uint64_t timeout);
typedef void (*PFN_glDeleteSync)(void *sync);

struct GLLoader {
    PFN_glBindTexture bindTexture = nullptr;
    PFN_glGetTexImage getTexImage = nullptr;
    PFN_glGetIntegerv getIntegerv = nullptr;
    PFN_glPixelStorei pixelStorei = nullptr;
    PFN_glGenBuffers genBuffers = nullptr;
    PFN_glDeleteBuffers deleteBuffers = nullptr;
    PFN_glBindBuffer bindBuffer = nullptr;
    PFN_glBufferData bufferData = nullptr;
    PFN_glMapBufferRange mapBufferRange = nullptr;
    PFN_glUnmapBuffer unmapBuffer = nullptr;
    PFN_glFenceSync fenceSync = nullptr;
    PFN_glClientWaitSync clientWaitSync = nullptr;
    PFN_glDeleteSync deleteSync = nullptr;
    bool ok = false;     // can read the texture at all
    bool pboOk = false;  // can use the asynchronous, fence-polled PBO path

    GLLoader() {
        // Prefer the GL entry points already loaded in the process (whatever
        // library Unity's context runs on), then libGL, then libOpenGL.
        void *libGL = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
        if (!libGL)
            libGL = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
        void *libOpenGL = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_GLOBAL);
        auto sym = [libGL, libOpenGL](const char *name) -> void * {
            void *s = dlsym(RTLD_DEFAULT, name);
            if (!s && libGL)
                s = dlsym(libGL, name);
            if (!s && libOpenGL)
                s = dlsym(libOpenGL, name);
            return s;
        };
        bindTexture = (PFN_glBindTexture)sym("glBindTexture");
        getTexImage = (PFN_glGetTexImage)sym("glGetTexImage");
        getIntegerv = (PFN_glGetIntegerv)sym("glGetIntegerv");
        pixelStorei = (PFN_glPixelStorei)sym("glPixelStorei");
        genBuffers = (PFN_glGenBuffers)sym("glGenBuffers");
        deleteBuffers = (PFN_glDeleteBuffers)sym("glDeleteBuffers");
        bindBuffer = (PFN_glBindBuffer)sym("glBindBuffer");
        bufferData = (PFN_glBufferData)sym("glBufferData");
        mapBufferRange = (PFN_glMapBufferRange)sym("glMapBufferRange");
        unmapBuffer = (PFN_glUnmapBuffer)sym("glUnmapBuffer");
        fenceSync = (PFN_glFenceSync)sym("glFenceSync");
        clientWaitSync = (PFN_glClientWaitSync)sym("glClientWaitSync");
        deleteSync = (PFN_glDeleteSync)sym("glDeleteSync");
        ok = bindTexture && getTexImage && getIntegerv && pixelStorei;
        pboOk = ok && genBuffers && deleteBuffers && bindBuffer && bufferData &&
                mapBufferRange && unmapBuffer && fenceSync && clientWaitSync &&
                deleteSync;
    }
};

GLLoader &glLoader() {
    static GLLoader loader;
    return loader;
}

// Saves the pack state this shim may touch and restores it on every exit
// path. The guard is created before the lazy PBO setup, so an existing pack
// buffer binding is preserved. It also resets skip pixels/rows (a caller can
// otherwise shift the transfer past the end of the readback buffer) and
// unbinds any pixel-pack buffer so the synchronous fallback writes to client
// memory. Only used when gl.ok is true (bindTexture/getIntegerv/pixelStorei
// are available); the pack-buffer parts are conditional on bindBuffer being
// available, so they also cover the synchronous fallback when a fence or map
// entry point is missing.
struct ScopedPackState {
    GLLoader &gl;
    int texture = 0;
    int packBuffer = 0;
    int swapBytes = 0;
    int alignment = 4;
    int rowLength = 0;
    int skipRows = 0;
    int skipPixels = 0;
    bool packBufferSaved = false;

    explicit ScopedPackState(GLLoader &loader) : gl(loader) {
        gl.getIntegerv(kGlTexBinding2D, &texture);
        gl.getIntegerv(kGlPackSwapBytes, &swapBytes);
        gl.getIntegerv(kGlPackAlignment, &alignment);
        gl.getIntegerv(kGlPackRowLength, &rowLength);
        gl.getIntegerv(kGlPackSkipRows, &skipRows);
        gl.getIntegerv(kGlPackSkipPixels, &skipPixels);
        if (gl.bindBuffer) {
            gl.getIntegerv(kGlPixelPackBufferBinding, &packBuffer);
            gl.bindBuffer(kGlPixelPackBuffer, 0);
            packBufferSaved = true;
        }
        gl.pixelStorei(kGlPackSwapBytes, 0);
        gl.pixelStorei(kGlPackAlignment, 4);
        gl.pixelStorei(kGlPackRowLength, 0);
        gl.pixelStorei(kGlPackSkipRows, 0);
        gl.pixelStorei(kGlPackSkipPixels, 0);
    }

    ~ScopedPackState() {
        gl.pixelStorei(kGlPackSwapBytes, swapBytes);
        gl.pixelStorei(kGlPackAlignment, alignment);
        gl.pixelStorei(kGlPackRowLength, rowLength);
        gl.pixelStorei(kGlPackSkipRows, skipRows);
        gl.pixelStorei(kGlPackSkipPixels, skipPixels);
        gl.bindTexture(kGlTexture2D, (unsigned int)texture);
        if (packBufferSaved)
            gl.bindBuffer(kGlPixelPackBuffer, (unsigned int)packBuffer);
    }

    ScopedPackState(const ScopedPackState &) = delete;
    ScopedPackState &operator=(const ScopedPackState &) = delete;
};

std::mutex g_readbackMutex;
std::vector<PrePassReadback *> g_readbacks;
// PBO names and sync objects of destroyed readbacks; deleted by a later
// render-thread event, since DestroyPrePassReadback runs on the main thread
// (no GL context there). Guarded by g_readbackMutex.
std::vector<unsigned int> g_pboGarbage;
std::vector<void *> g_syncGarbage;

bool readbackDebug() {
    static const bool enabled = getenv("AT2_SHIM_DEBUG") != nullptr;
    return enabled;
}

void logReadback(PrePassReadback *r) {
    static std::atomic<int> reads{0};
    int n = reads.fetch_add(1);
    if (n < 5 || n % 300 == 0) {
        size_t count = (size_t)r->width * r->height;
        size_t nonzero = 0;
        for (size_t i = 0; i < count; i++) {
            if (r->pixels[i * 4] || r->pixels[i * 4 + 1] || r->pixels[i * 4 + 2] ||
                r->pixels[i * 4 + 3])
                nonzero++;
        }
        fprintf(stderr, "[AT2 shim] prepass readback #%d: %ux%u, %zu/%zu non-zero pixels\n",
                n, r->width, r->height, nonzero, count);
    }
}

void readbackSync(GLLoader &gl, PrePassReadback *r) {
    // ScopedPackState has already unbound any pack buffer and set tightly
    // packed state, so this writes straight into client memory.
    gl.bindTexture(kGlTexture2D, (unsigned int)(uintptr_t)r->texture);
    {
        std::lock_guard<std::mutex> lock(r->pixelsMutex);
        gl.getTexImage(kGlTexture2D, 0, kGlRgba, kGlUnsignedByte, r->pixels);
    }
}

void performReadback(PrePassReadback *r) {
    if (!r->pending.exchange(false))
        return;
    GLLoader &gl = glLoader();
    if (!gl.ok || r->texture == nullptr)
        return;

    const size_t bytes = (size_t)r->width * r->height * 4;
    ScopedPackState packState(gl);

    if (!gl.pboOk || r->pboDisabled) {
        // Fallback for a GL without buffer objects (or after a fence
        // failure): synchronous read into client memory. This stalls the
        // render thread, but keeps the shim alive.
        readbackSync(gl, r);
        return;
    }

    // Lazy PBO creation (see the comment block above): the buffers must be
    // generated here, on the render thread.
    if (r->pbos[0] == 0) {
        gl.genBuffers(2, r->pbos);
        if (r->pbos[0] == 0 || r->pbos[1] == 0) {
            r->pboDisabled = true;
            readbackSync(gl, r);
            return;
        }
        for (int i = 0; i < 2; i++) {
            gl.bindBuffer(kGlPixelPackBuffer, r->pbos[i]);
            gl.bufferData(kGlPixelPackBuffer, (ptrdiff_t)bytes, nullptr, kGlStreamRead);
        }
    }

    // Poll both fences with a zero timeout. `complete` marks a PBO whose
    // transfer the GPU finished but whose pixels are not published yet.
    for (int i = 0; i < 2; i++) {
        if (r->fences[i] == nullptr)
            continue;
        unsigned int status = gl.clientWaitSync(r->fences[i], 0, 0);
        if (status == kGlAlreadySignaled || status == kGlConditionSatisfied) {
            gl.deleteSync(r->fences[i]);
            r->fences[i] = nullptr;
            r->complete[i] = true;
        } else if (status == kGlWaitFailed) {
            gl.deleteSync(r->fences[i]);
            r->fences[i] = nullptr;
            r->complete[i] = false;
            r->pboDisabled = true;
        }
    }
    if (r->pboDisabled) {
        // A broken wait invalidates the PBO completion tracking; fall back
        // to a synchronous read for this and later frames.
        readbackSync(gl, r);
        return;
    }

    // Stage 2: publish every completed capture, oldest first. writeIndex
    // points at the PBO that was queued first (or is next to be queued), so
    // when both completed its partner holds the newer frame and must be
    // copied last. Mapping only runs after a fence signaled, so it never
    // waits on the transfer.
    for (int n = 0; n < 2; n++) {
        int index = (n == 0) ? r->writeIndex : 1 - r->writeIndex;
        if (!r->complete[index])
            continue;
        gl.bindBuffer(kGlPixelPackBuffer, r->pbos[index]);
        void *mapped = gl.mapBufferRange(kGlPixelPackBuffer, 0, (ptrdiff_t)bytes,
                                         kGlMapReadBit);
        if (mapped) {
            {
                std::lock_guard<std::mutex> lock(r->pixelsMutex);
                memcpy(r->pixels, mapped, bytes);
            }
            gl.unmapBuffer(kGlPixelPackBuffer);
            r->complete[index] = false;
            if (readbackDebug())
                logReadback(r);
        } else {
            // A failed map leaves the transfer unusable; drop it so the PBO
            // can be reused. Fall back to a synchronous read rather than
            // retrying the same failing map every frame.
            r->complete[index] = false;
            if (!r->pboDisabled)
                fprintf(stderr, "[AT2 shim] PBO map failed; using synchronous readback\n");
            r->pboDisabled = true;
            break;
        }
    }
    if (r->pboDisabled) {
        readbackSync(gl, r);
        return;
    }

    // Stage 1: queue this frame's capture, but never overwrite a PBO whose
    // previous transfer is still in flight. Prefer the normal ping-pong slot;
    // if it is pending, use the other PBO when that one is free. With a PBO
    // bound the last argument of glGetTexImage is a byte offset, not a
    // pointer — asynchronous DMA.
    int writeIndex = (r->fences[r->writeIndex] == nullptr)
                         ? r->writeIndex
                         : 1 - r->writeIndex;
    if (r->fences[writeIndex] == nullptr) {
        gl.bindBuffer(kGlPixelPackBuffer, r->pbos[writeIndex]);
        gl.bindTexture(kGlTexture2D, (unsigned int)(uintptr_t)r->texture);
        gl.getTexImage(kGlTexture2D, 0, kGlRgba, kGlUnsignedByte, nullptr);
        r->fences[writeIndex] = gl.fenceSync(kGlSyncGpuCommandsComplete, 0);
        if (r->fences[writeIndex] == nullptr) {
            // Fence creation failed after the capture was queued; do not
            // reuse this PBO without a completion signal. Later frames use
            // the synchronous path.
            r->pboDisabled = true;
            return;
        }
        r->complete[writeIndex] = false;
        r->writeIndex = 1 - writeIndex;
    }
}

// Render-thread entry point (GL.IssuePluginEvent).
void UnityRenderingEvent(int eventID) {
    (void)eventID;
    std::lock_guard<std::mutex> lock(g_readbackMutex);
    GLLoader &gl = glLoader();
    if (!g_syncGarbage.empty()) {
        if (gl.pboOk)
            for (void *sync : g_syncGarbage)
                gl.deleteSync(sync);
        g_syncGarbage.clear();
    }
    if (!g_pboGarbage.empty()) {
        if (gl.pboOk)
            for (unsigned int id : g_pboGarbage)
                gl.deleteBuffers(1, &id);
        g_pboGarbage.clear();
    }
    for (PrePassReadback *r : g_readbacks)
        performReadback(r);
}

}  // namespace

// ---------------------------------------------------------------- exports

extern "C" {

void *GetRenderEventFunc(void) {
    return (void *)&UnityRenderingEvent;
}

void *CreatePageDecoder(uint32_t size, const PlaneDesc *planes, uint8_t numPlanes) {
    if (!planes || numPlanes == 0 || numPlanes > 16)
        return nullptr;
    PageDecoder *d = new (std::nothrow) PageDecoder();
    if (!d)
        return nullptr;
    uint32_t pageSize = size ? size : kPageSize;
    uint64_t channelLength = (uint64_t)pageSize * pageSize;
    if (channelLength == 0 || channelLength > UINT32_MAX) {
        delete d;
        return nullptr;
    }
    d->pageSize = pageSize;
    d->channelLength = (uint32_t)channelLength;
    d->numPlanes = numPlanes;
    memcpy(d->planes, planes, numPlanes * sizeof(PlaneDesc));
    return d;
}

void DestroyPageDecoder(void *handle) {
    delete asDecoder(handle);
}

int TranscodeCrunchToBC(void *handle, const uint8_t *input, uint32_t inputSize, uint8_t *output) {
    if (!handle || !input || !output)
        return -1;
    if (inputSize < 0x3d)  // min CRN header size (matches the original)
        return -3;
    PageDecoder *d = asDecoder(handle);
    std::vector<uint8_t> bc;
    CrnInfo info;
    if (!crnDecodeLevel(input, inputSize, 0, bc, &info))
        return -4;
    if (!crnFitsPage(d, info, bc.size()))
        return -5;
    memcpy(output, bc.data(), bc.size());
    return 0;
}

int DecodePackedHeightRG(void *handle, const uint8_t *input, uint32_t inputSize, uint8_t *output) {
    if (!handle || !input || !output)
        return -1;
    PageDecoder *d = asDecoder(handle);
    std::vector<uint8_t> rg;
    if (!decodePackedHeightRG(input, inputSize, d->channelLength, rg))
        return -2;
    if (rg.size() != (size_t)d->channelLength * 2)
        return -5;
    memcpy(output, rg.data(), rg.size());
    return 0;
}

int DecodeBC1ToRGBX(void *handle, const uint8_t *input, uint8_t *output) {
    if (!handle || !input || !output)
        return -1;
    PageDecoder *d = asDecoder(handle);
    decodeBC1ToRGBX(input, d->pageSize, d->pageSize, output);
    return 0;
}

int DecodeBC3ToRGBA(void *handle, const uint8_t *input, uint8_t *output) {
    if (!handle || !input || !output)
        return -1;
    PageDecoder *d = asDecoder(handle);
    decodeBC3ToRGBA(input, d->pageSize, d->pageSize, output);
    return 0;
}

int DecodeBC5ToRGBA(void *handle, const uint8_t *input, uint8_t *output, uint32_t chan0,
                    uint32_t chan1) {
    if (!handle || !input || !output)
        return -1;
    PageDecoder *d = asDecoder(handle);
    decodeBC5ToRGBA(input, d->pageSize, d->pageSize, output, chan0, chan1);
    return 0;
}

// ETC paths are mobile-only; the desktop builds never call them.
int DecodeETC1ToRGBX(void *, const uint8_t *, uint8_t *) { return -1; }
int DecodeETC2ToRGBX(void *, const uint8_t *, uint8_t *) { return -1; }
int DecodeETC2AToRGBA(void *, const uint8_t *, uint8_t *) { return -1; }
int DecodeETC2AToRGBA2(void *, const uint8_t *, uint8_t *, uint32_t, uint32_t) { return -1; }

// Legacy combined decode (old PageFile format); not used by the PageFile2 path.
int DecodePage(void *, const uint8_t *, uint32_t, uint8_t *, uint32_t) { return -1; }
int DecodePagePlanes(void *, const uint8_t *, uint32_t, uint8_t *, uint32_t, uint32_t) { return -1; }

// Encode paths are used by the AT2 *editor* only; never at runtime.
int EncodeRGBXToBC1(void *, const uint8_t *, uint8_t *) { return -1; }
int EncodeRGBAToBC3(void *, const uint8_t *, uint8_t *) { return -1; }
int EncodeRGBAToBC5(void *, const uint8_t *, uint8_t *) { return -1; }
int EncodeRGBATo2xBC5(void *, const uint8_t *, uint8_t *, uint8_t *) { return -1; }

// ---------------------------------------------------------------- prepass

void *CreatePrePassReadback(void *tex, uint32_t width, uint32_t height) {
    PrePassReadback *r = new (std::nothrow) PrePassReadback();
    if (!r)
        return nullptr;
    if (width == 0 || height == 0 || (size_t)width > SIZE_MAX / height) {
        delete r;
        return nullptr;
    }
    size_t texels = (size_t)width * height;
    if (texels > SIZE_MAX / 4) {
        delete r;
        return nullptr;
    }
    r->texture = tex;
    r->width = width;
    r->height = height;
    r->pixels = (uint8_t *)calloc(texels, 4);
    if (!r->pixels) {
        delete r;
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_readbackMutex);
        g_readbacks.push_back(r);
    }
    return r;
}

void DestroyPrePassReadback(void *inst) {
    PrePassReadback *r = static_cast<PrePassReadback *>(inst);
    if (!r)
        return;
    {
        std::lock_guard<std::mutex> lock(g_readbackMutex);
        for (auto it = g_readbacks.begin(); it != g_readbacks.end(); ++it) {
            if (*it == r) {
                g_readbacks.erase(it);
                break;
            }
        }
        // No GL context on this (main) thread: hand the PBOs and fences to
        // the next render-thread event. Reading them is safe under the
        // mutex, since the event only touches them while holding it.
        for (void *&fence : r->fences) {
            if (fence != nullptr) {
                g_syncGarbage.push_back(fence);
                fence = nullptr;
            }
        }
        if (r->pbos[0] != 0)
            g_pboGarbage.push_back(r->pbos[0]);
        if (r->pbos[1] != 0)
            g_pboGarbage.push_back(r->pbos[1]);
    }
    free(r->pixels);
    delete r;
}

void IssuePrePassRead(void *inst) {
    PrePassReadback *r = static_cast<PrePassReadback *>(inst);
    if (r)
        r->pending.store(true);
}

void GetPrePassPixels(void *inst, void *pixels) {
    PrePassReadback *r = static_cast<PrePassReadback *>(inst);
    if (!r || !pixels)
        return;
    std::lock_guard<std::mutex> lock(r->pixelsMutex);
    memcpy(pixels, r->pixels, (size_t)r->width * r->height * 4);
}

// ---------------------------------------------------------------- plugin lifecycle

void UnityPluginLoad(void * /*interfaces*/) {}
void UnityPluginUnload(void) {}

}  // extern "C"
