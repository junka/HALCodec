#ifndef NVENC_LAYERS_CUDA_CONTEXT_H
#define NVENC_LAYERS_CUDA_CONTEXT_H

#include <cuda.h>

namespace halcodec {
namespace nvenc {

// Minimal RAII wrapper around a CUDA device context. Used by the NVDEC
// decoder adapter and the capability provider; NOT part of the unified API —
// context handling is a backend-internal detail.
class CUDAContext {
public:
    CUDAContext() { cuInit(0); }
    ~CUDAContext() { destroy(); }

    CUDAContext(const CUDAContext&) = delete;
    CUDAContext& operator=(const CUDAContext&) = delete;

    bool create(int idx) {
        if (cuContext_) {
            return true;
        }
        if (cuDeviceGet(&device_, idx) != CUDA_SUCCESS) {
            return false;
        }
        return cuCtxCreate(&cuContext_, 0, device_) == CUDA_SUCCESS;
    }

    void destroy() {
        if (cuContext_) {
            cuCtxDestroy(cuContext_);
            cuContext_ = nullptr;
        }
    }

    CUcontext get() const { return cuContext_; }

private:
    CUdevice device_ = 0;
    CUcontext cuContext_ = nullptr;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_CUDA_CONTEXT_H