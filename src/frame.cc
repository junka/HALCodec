#include "frame.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace halcodec {

namespace {
// Device-frame download hooks, registered by backends that emit device frames
// (so frame.cc stays free of NvSci/CUDA headers and of link-time coupling to
// the backend .so files). A null hook means "no backend produced this variant
// in this build" and DownloadToHost returns false.
std::function<bool(CodecFrame&)>& nvsciHook() {
    static std::function<bool(CodecFrame&)> h;
    return h;
}
std::function<bool(CodecFrame&)>& cudaHook() {
    static std::function<bool(CodecFrame&)> h;
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
    cudaHook() = std::move(h);
}

__attribute__((visibility("default")))
bool DownloadToHost(CodecFrame& frame) {
    switch (frame.locality) {
        case FrameLocality::Host:
            return true;
        case FrameLocality::CudaDevice:
            // Implemented once NVDEC/NVJPEG emit device frames (Stage 3).
            if (cudaHook()) return cudaHook()(frame);
            return false;
        case FrameLocality::NvSciBufObj:
            if (nvsciHook()) return nvsciHook()(frame);
            return false;
    }
    return false;
}

}  // namespace halcodec
