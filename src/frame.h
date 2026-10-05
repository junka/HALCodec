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
    YUV444P16LE,   // planar 4:4:4 in 16-bit words, little endian (VideoToolbox
                   // 'sv44': the samples already sit at 16-bit scale, so the
                   // decode layer de-interleaves without rescaling)
    RGB,
    BGR,
    GRAY,          // 8-bit single plane, greyscale (nvjpeg "y" output)
    GRAY10LE,      // 10-bit single plane, greyscale, each sample in the top ten
                   // bits of a 16-bit little-endian word -- the alignment HAL's
                   // other 10-bit formats carry, and the one VideoToolbox's 'L010'
                   // hands over unchanged. Note that ffmpeg's gray10le keeps the
                   // ten bits at the *bottom* of the word, so the two are not the
                   // same bytes.
    BGRA,
    ARGB,
    RGBA,
    BAYER16LE,     // one plane of 16-bit little-endian Bayer sensels at the
                   // sensor's full size (VideoToolbox 'bp16', what a ProRes RAW
                   // picture decodes to). `width`/`height` count sensels, so the
                   // grid is the mosaic itself -- not a demosaiced picture, and
                   // not subsampled like the YUV formats. Which phase is which
                   // colour, and the levels the samples sit between, are in
                   // `bayer`: they are not recoverable from the bytes.
};

// The phase order of a Bayer grid: the colour of the top-left sensel and how the
// rows alternate. The numbering is the one the ProRes RAW bitstream and
// VideoToolbox use (`bayer_pattern`), carried through rather than remapped.
enum class BayerPattern {
    Unknown = -1,
    RGGB = 0,   // top-left red, rows alternating with green
    GRBG = 1,   // top-left green, top row alternating with red
    GBRG = 2,   // top-left green, top row alternating with blue
    BGGR = 3,   // top-left blue
};

// What a consumer needs to turn a Bayer grid into colour and cannot reconstruct
// from the sensels: the phase order, and the two levels the grid is scaled
// between. Measured on Apple's ProRes RAW decode, which hands back 16-bit
// sensels with a black level of 256 and a white level of 61568 -- so the samples
// are neither left-aligned like P010 nor a bare 12-bit number.
struct BayerInfo {
    BayerPattern pattern = BayerPattern::Unknown;
    uint16_t blackLevel = 0;
    uint16_t whiteLevel = 0;
};

// Where a frame's pixels live. Host = a normal CPU buffer reachable via
// `data`. The device variants hold a vendor-specific handle in `device` and
// leave `data` null; callers that only understand host memory must call
// DownloadToHost() before touching the pixels.
enum class FrameLocality {
    Host,           // data is a CPU pointer
    CudaDevice,     // device.cudaPtr is a CUdeviceptr (NVDEC/NVJPEG)
    NvSciBufObj,    // device.nvSciBufObj is an NvSciBufObj (NvMedia)
    OneVPLSurface,  // device.vplExportedHeader is an mfxSurfaceHeader* from
                    // mfxFrameSurfaceInterface::Export (QSV zero-copy). The
                    // handle is opaque (runtime-owned, refcounted); the app
                    // never touches libva — Export/Import are runtime-mediated.
};

// Opaque device-memory descriptor. Only the field matching `locality` is
// meaningful. Kept free of vendor headers (handles stored as void*/uintptr_t)
// so frame.h stays includable from anywhere.
//
// Single-block device frames (NVDEC's pitched NV12 surface) use `cudaPtr` +
// `cudaPitch` and leave `cudaNumPlanes` 0: the chroma planes sit at fixed
// offsets inside that one allocation. Multi-plane device frames (nvjpeg's
// `nvjpegImage_t`, where Y/U/V are separate cudaMalloc allocations that are
// not contiguous) fill `cudaPlanes`/`cudaPitches` and set `cudaNumPlanes`;
// `cudaPtr`/`cudaPitch` mirror plane 0 so single-plane consumers still work.
struct DeviceMem {
    uintptr_t cudaPtr = 0;       // FrameLocality::CudaDevice  (CUdeviceptr)
    void* nvSciBufObj = nullptr; // FrameLocality::NvSciBufObj (NvSciBufObj)
    size_t cudaPitch = 0;        // row pitch for CudaDevice (non-tight)
    uintptr_t cudaPlanes[4] = {0};  // per-plane device ptrs (multi-plane only)
    size_t cudaPitches[4] = {0};    // per-plane row pitches (multi-plane only)
    int cudaNumPlanes = 0;          // >0 => use cudaPlanes/cudaPitches
    void* vplExportedHeader = nullptr;  // FrameLocality::OneVPLSurface:
                                        //   mfxSurfaceHeader* from
                                        //   mfxFrameSurfaceInterface::Export.
                                        //   Refcounted runtime object; release
                                        //   via its mfxSurfaceInterface::Release.
    void* vplVaDisplay = nullptr;       // FrameLocality::OneVPLSurface:
                                        //   the decode session's VADisplay
                                        //   (MFX_HANDLE_VA_DISPLAY), an opaque
                                        //   void* — the app never touches libva.
                                        //   The encode session must SetHandle
                                        //   this before Init so the imported
                                        //   surface's vaDisplay matches the
                                        //   encode session's own VADisplay
                                        //   (otherwise ImportFrameSurface -4).
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

    // Set for a Bayer format, meaningless for every other one. VideoToolbox hands
    // the rest of a RAW stream's metadata over as buffer attachments -- white
    // balance factors, a colour matrix, a recommended crop, gain -- and this is
    // the part a consumer genuinely cannot do without; the remainder stays
    // unread rather than half-modelled.
    BayerInfo bayer;

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
void RegisterVPLSurfaceDownload(std::function<bool(CodecFrame&)> hook);

} // namespace halcodec

#endif // SRC_FRAME_H