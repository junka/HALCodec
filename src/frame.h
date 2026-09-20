#ifndef SRC_FRAME_H
#define SRC_FRAME_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace halcodec {

enum class PixelFormat {
    Unknown = 0,
    NV12,          // 8-bit YUV 4:2:0, semi-planar
    P010,          // 10-bit YUV 4:2:0 (16-bit storage), semi-planar
    I420,          // 8-bit YUV 4:2:0, planar
    P016,          // 12-bit YUV 4:2:0 (16-bit storage), semi-planar
    NV16,          // 8-bit YUV 4:2:2, semi-planar
    P210,          // 10-bit YUV 4:2:2 (16-bit storage), semi-planar
    YUV444P,       // 8-bit YUV 4:4:4, planar
    YUV444P10LE,   // 10-bit planar 4:4:4, little endian
    RGB,
    BGR,
    GRAY,          // 8-bit single plane, greyscale (nvjpeg "y" output)
    BGRA,
    ARGB,
    RGBA,
};

// Where a frame's pixels live. Host = a normal CPU buffer reachable via
// `data`. The device variants hold a vendor-specific handle in `device` and
// leave `data` null; callers that only understand host memory must call
// DownloadToHost() before touching the pixels.
enum class FrameLocality {
    Host,         // data is a CPU pointer
    CudaDevice,   // device.cudaPtr is a CUdeviceptr (NVDEC/NVJPEG)
    NvSciBufObj,  // device.nvSciBufObj is an NvSciBufObj (NvMedia)
};

// Opaque device-memory descriptor. Only the field matching `locality` is
// meaningful. Kept free of vendor headers (handles stored as void*/uintptr_t)
// so frame.h stays includable from anywhere.
struct DeviceMem {
    uintptr_t cudaPtr = 0;       // FrameLocality::CudaDevice  (CUdeviceptr)
    void* nvSciBufObj = nullptr; // FrameLocality::NvSciBufObj (NvSciBufObj)
    size_t cudaPitch = 0;        // row pitch for CudaDevice (non-tight)
};

// Unified frame container exchanged between the HAL interface and the
// vendor adaptation layers. The frame object is owned by the caller (stack
// or container); the underlying buffer pointed to by `data` is allocated by
// the adaptation layer and must be released via `release` when done. An
// empty `release` means the buffer is managed by the framework and needs no
// explicit cleanup.
//
// Device-resident frames: when `locality` is not Host, `data` is null and the
// pixels live in `device`. `release` still owns that resource (e.g. returns a
// borrowed NvSciBufObj to the decoder pool). Consumers that only handle host
// memory call DownloadToHost() to materialize `data` on demand.
struct CodecFrame {
    uint8_t* data = nullptr;       // first byte of the host buffer (Host only)
    size_t size = 0;               // total bytes across all planes
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    std::array<size_t, 4> strides{}; // row stride per plane (0 = tightly packed)
    int64_t pts = 0;

    FrameLocality locality = FrameLocality::Host;
    DeviceMem device;

    // Releases the buffer pointed to by `data` (Host) or the device resource
    // in `device`. May capture the pointer by value; do not reference `this`
    // unless the frame struct outlives the call.
    std::function<void()> release;
};

// Materialize a frame's pixels into `data` as host memory. No-op for
// FrameLocality::Host. For device frames, performs the detile/copy and flips
// locality to Host (resetting `release` to free the new host buffer). Returns
// false if the device variant is not implemented for this build (the caller
// then has no way to read the pixels). Defined in frame.cc.
bool DownloadToHost(CodecFrame& frame);

// Backends that emit device frames call these at static-init time to install
// their detile/copy path, so DownloadToHost can reach across the core/backend
// .so boundary without frame.cc linking against NvSci/CUDA. Exported from
// halcodec_core. Stage 0 leaves them unregistered (DownloadToHost returns
// false for device frames) — wired up by the producing backends in Stage 1/3.
void RegisterNvSciBufDownload(std::function<bool(CodecFrame&)> hook);
void RegisterCudaFrameDownload(std::function<bool(CodecFrame&)> hook);

} // namespace halcodec

#endif // SRC_FRAME_H