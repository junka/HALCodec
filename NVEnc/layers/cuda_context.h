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
        // Share one context per device across backends (the primary context)
        // rather than cuCtxCreate'ing a private one per backend. Device memory
        // is only usable inside the context that allocated it, so private
        // contexts made a device frame minted by one backend (e.g. an NVDEC
        // zero-copy frame) unusable by another (e.g. NVENC's input copy) and
        // forced a host round-trip. Retain/release keeps the primary context
        // alive only while a backend holds it, and never destroys it out from
        // under another library that is using it.
        if (cuDevicePrimaryCtxRetain(&cuContext_, device_) != CUDA_SUCCESS) {
            cuContext_ = nullptr;
            return false;
        }
        // cuCtxCreate used to leave the new context current on the calling
        // thread, and callers rely on that: cuvidGetDecoderCaps (the NVDEC
        // codec probe) and the NVENC capability session both query "the current
        // context" implicitly. Retain does not change the current context, so
        // make the switch explicitly to keep those callers working. Remember
        // what was current so destroy() can put it back.
        prevContext_ = nullptr;
        cuCtxGetCurrent(&prevContext_);
        if (cuCtxSetCurrent(cuContext_) != CUDA_SUCCESS) {
            cuDevicePrimaryCtxRelease(device_);
            cuContext_ = nullptr;
            return false;
        }
        return true;
    }

    void destroy() {
        if (!cuContext_) {
            return;
        }
        // Restore whatever was current before create() instead of clearing the
        // thread's context. Several CUDAContext objects legitimately coexist
        // over the same primary context (the decoder holds one, and
        // nvdecSupportsCodec makes a transient one for its probe), so clearing
        // here would yank the context out from under the holder still using it:
        // the next cuStreamCreate / cuvidMapVideoFrame then fails with
        // CUDA_ERROR_INVALID_CONTEXT even though the context is alive.
        CUcontext cur = nullptr;
        if (cuCtxGetCurrent(&cur) == CUDA_SUCCESS && cur == cuContext_) {
            cuCtxSetCurrent(prevContext_);
        }
        // The context is retained, not owned: releasing drops the reference but
        // the primary context outlives us, so device memory of any frame still
        // in flight stays valid.
        cuDevicePrimaryCtxRelease(device_);
        cuContext_ = nullptr;
    }

    CUcontext get() const { return cuContext_; }

private:
    CUdevice device_ = 0;
    CUcontext cuContext_ = nullptr;
    // Thread context that was current when create() switched to ours; restored
    // by destroy() so co-existing holders of the primary context survive.
    CUcontext prevContext_ = nullptr;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_CUDA_CONTEXT_H