#include "frame.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

#include "registry.h"

namespace halcodec {

namespace {

// Device-frame download hooks, registered by backends that emit device frames
// (so frame.cc stays free of NvSci/CUDA headers and of link-time coupling to
// the backend .so files). Each backend registers a hook that first checks
// whether the frame is one it produced (and returns false otherwise), so
// multiple CudaDevice-producing backends (NVDEC single-block, nvjpeg
// multi-plane) can coexist: DownloadToHost tries each in turn until one
// claims the frame.
//
// The tables live in halcodec_core (shared_table) rather than in file-local
// statics: the registering backends are dlopen'd .so's, and a file-local static
// in this TU would be a different object in each of them. The mutex comes with
// the table, so registration from a backend's static-init and lookup from a
// decode thread are ordered.
struct DownloadHooks {
    std::function<bool(CodecFrame&)> nvsci;
    std::vector<std::function<bool(CodecFrame&)>> cuda;
    std::vector<std::function<bool(CodecFrame&)>> vpl;
};

constexpr const char* kHooksKey = "halcodec.frame.download_hooks";

// Copies the hooks out under the table lock; the hooks themselves are then
// called unlocked. A hook may call back into a Register* entry (or into
// DownloadToHost for a nested frame), and holding the lock across the call
// would deadlock on this non-recursive mutex.
std::vector<std::function<bool(CodecFrame&)>> snapshotCudaHooks() {
    return with_locked_table<DownloadHooks>(kHooksKey,
                                            [](DownloadHooks& h) { return h.cuda; });
}

std::vector<std::function<bool(CodecFrame&)>> snapshotVplHooks() {
    return with_locked_table<DownloadHooks>(kHooksKey,
                                            [](DownloadHooks& h) { return h.vpl; });
}

}  // namespace

// Exported: codecinfo (and other out-of-dylib consumers) call it, and the
// build compiles under hidden visibility like the rest of halcodec_core.
__attribute__((visibility("default")))
std::string Name(PixelFormat format) {
    switch (format) {
        case PixelFormat::NV12: return "NV12";
        case PixelFormat::P010: return "P010";
        case PixelFormat::I420: return "I420";
        case PixelFormat::P016: return "P016";
        case PixelFormat::NV16: return "NV16";
        case PixelFormat::P210: return "P210";
        case PixelFormat::YUV444P: return "YUV444P";
        case PixelFormat::YUV444P10LE: return "YUV444P10LE";
        case PixelFormat::YUV444P16LE: return "YUV444P16LE";
        case PixelFormat::RGB: return "RGB";
        case PixelFormat::BGR: return "BGR";
        case PixelFormat::GRAY: return "GRAY";
        case PixelFormat::GRAY10LE: return "GRAY10LE";
        case PixelFormat::BGRA: return "BGRA";
        case PixelFormat::ARGB: return "ARGB";
        case PixelFormat::RGBA: return "RGBA";
        case PixelFormat::BAYER16LE: return "BAYER16LE";
        case PixelFormat::Unknown: break;
    }
    return "Unknown";
}

// Exported (default visibility) so backend .so's loaded at runtime can install
// their device-frame download path. Mirrors registry_storage()'s export model.
// DownloadToHost is also exported because the apps call it directly.
__attribute__((visibility("default")))
void RegisterNvSciBufDownload(std::function<bool(CodecFrame&)> h) {
    with_locked_table<DownloadHooks>(kHooksKey,
                                     [&h](DownloadHooks& t) { t.nvsci = std::move(h); });
}
__attribute__((visibility("default")))
void RegisterCudaFrameDownload(std::function<bool(CodecFrame&)> h) {
    with_locked_table<DownloadHooks>(kHooksKey,
                                     [&h](DownloadHooks& t) { t.cuda.push_back(std::move(h)); });
}
__attribute__((visibility("default")))
void RegisterVPLSurfaceDownload(std::function<bool(CodecFrame&)> h) {
    with_locked_table<DownloadHooks>(kHooksKey,
                                     [&h](DownloadHooks& t) { t.vpl.push_back(std::move(h)); });
}

__attribute__((visibility("default")))
bool DownloadToHost(CodecFrame& frame) {
    switch (frame.locality) {
        case FrameLocality::Host:
            return true;
        case FrameLocality::CudaDevice: {
            // Try each registered hook until one claims the frame. A hook
            // returns false for frames it did not produce (different layout /
            // format), letting the next backend's hook try.
            std::vector<std::function<bool(CodecFrame&)>> hooks = snapshotCudaHooks();
            for (auto& h : hooks) {
                if (h(frame)) return true;
            }
            return false;
        }
        case FrameLocality::NvSciBufObj: {
            std::function<bool(CodecFrame&)> hook =
                with_locked_table<DownloadHooks>(kHooksKey,
                                                 [](DownloadHooks& t) { return t.nvsci; });
            if (hook) return hook(frame);
            return false;
        }
        case FrameLocality::OneVPLSurface: {
            // Same try-each-hook model as CudaDevice: a hook returns false for
            // frames it did not produce.
            std::vector<std::function<bool(CodecFrame&)>> hooks = snapshotVplHooks();
            for (auto& h : hooks) {
                if (h(frame)) return true;
            }
            return false;
        }
    }
    return false;
}

}  // namespace halcodec
