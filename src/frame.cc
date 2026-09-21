#include "frame.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

namespace halcodec {

namespace {
// Device-frame download hooks, registered by backends that emit device frames
// (so frame.cc stays free of NvSci/CUDA headers and of link-time coupling to
// the backend .so files). Each backend registers a hook that first checks
// whether the frame is one it produced (and returns false otherwise), so
// multiple CudaDevice-producing backends (NVDEC single-block, nvjpeg
// multi-plane) can coexist: DownloadToHost tries each in turn until one
// claims the frame.
std::function<bool(CodecFrame&)>& nvsciHook() {
    static std::function<bool(CodecFrame&)> h;
    return h;
}
std::vector<std::function<bool(CodecFrame&)>>& cudaHooks() {
    static std::vector<std::function<bool(CodecFrame&)>> h;
    return h;
}
}  // namespace

// Exported (default visibility) so backend .so's loaded at runtime can install
// their device-frame download path. Mirrors registry_storage()'s export model.
// DownloadToHost is also exported because the apps call it directly.
__attribute__((visibility("default")))
void RegisterNvSciBufDownload(std::function<bool(CodecFrame&)> h) {
    nvsciHook() = std::move(h);
}
__attribute__((visibility("default")))
void RegisterCudaFrameDownload(std::function<bool(CodecFrame&)> h) {
    cudaHooks().push_back(std::move(h));
}

__attribute__((visibility("default")))
bool DownloadToHost(CodecFrame& frame) {
    switch (frame.locality) {
        case FrameLocality::Host:
            return true;
        case FrameLocality::CudaDevice:
            // Try each registered hook until one claims the frame. A hook
            // returns false for frames it did not produce (different layout /
            // format), letting the next backend's hook try.
            for (auto& h : cudaHooks()) {
                if (h(frame)) return true;
            }
            return false;
        case FrameLocality::NvSciBufObj:
            if (nvsciHook()) return nvsciHook()(frame);
            return false;
    }
    return false;
}

}  // namespace halcodec
