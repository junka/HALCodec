#if defined(__linux__) && defined(QSV_HAS_VA)

#include "qsv_va_allocator.h"

#include <dlfcn.h>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace halcodec {
namespace qsv {

bool VaApiAllocator::init() {
    fns_.libva = dlopen("libva.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!fns_.libva) {
        fns_.libva = dlopen("libva.so", RTLD_NOW | RTLD_GLOBAL);
    }
    if (!fns_.libva) {
        std::cerr << "QSV: dlopen libva.so.2 failed (" << dlerror()
                  << ") — VP8/VP9 VA-surface path unavailable\n";
        return false;
    }
    dlerror();
    fns_.vaInitialize = reinterpret_cast<int(*)(VADisplay,int*,int*)>(
        dlsym(fns_.libva, "vaInitialize"));
    fns_.vaTerminate = reinterpret_cast<int(*)(VADisplay)>(
        dlsym(fns_.libva, "vaTerminate"));
    fns_.vaCreateSurfaces = reinterpret_cast<int(*)(VADisplay,unsigned int,
        unsigned int,unsigned int,VASurfaceID*,unsigned int,
        VASurfaceAttrib*,unsigned int)>(
        dlsym(fns_.libva, "vaCreateSurfaces"));
    fns_.vaDestroySurfaces = reinterpret_cast<int(*)(VADisplay,VASurfaceID*,int)>(
        dlsym(fns_.libva, "vaDestroySurfaces"));
    fns_.vaDeriveImage = reinterpret_cast<int(*)(VADisplay,VASurfaceID,VAImage*)>(
        dlsym(fns_.libva, "vaDeriveImage"));
    fns_.vaDestroyImage = reinterpret_cast<int(*)(VADisplay,VAImageID)>(
        dlsym(fns_.libva, "vaDestroyImage"));
    fns_.vaMapBuffer = reinterpret_cast<int(*)(VADisplay,VABufferID,void**)>(
        dlsym(fns_.libva, "vaMapBuffer"));
    fns_.vaUnmapBuffer = reinterpret_cast<int(*)(VADisplay,VABufferID)>(
        dlsym(fns_.libva, "vaUnmapBuffer"));
    fns_.vaSyncSurface = reinterpret_cast<int(*)(VADisplay,VASurfaceID)>(
        dlsym(fns_.libva, "vaSyncSurface"));
    fns_.vaErrorStr = reinterpret_cast<const char*(*)(VAStatus)>(
        dlsym(fns_.libva, "vaErrorStr"));
    if (!fns_.vaCreateSurfaces || !fns_.vaDestroySurfaces
            || !fns_.vaDeriveImage || !fns_.vaDestroyImage
            || !fns_.vaMapBuffer || !fns_.vaUnmapBuffer
            || !fns_.vaSyncSurface) {
        std::cerr << "QSV: libva symbols missing — VA-surface path unavailable\n";
        destroy();
        return false;
    }
    return true;
}

bool VaApiAllocator::createPool(VADisplay dpy, mfxU16 w, mfxU16 h,
                                int numSurfaces, const mfxFrameInfo& info) {
    if (!fns_.libva || !dpy || numSurfaces <= 0) return false;
    display_ = dpy;  // borrowed; not owned
    ownsDisplay_ = false;

    surfaces_.resize(numSurfaces);
    std::vector<VASurfaceID> ids(numSurfaces, VA_INVALID_ID);
    VAStatus s = fns_.vaCreateSurfaces(dpy, VA_RT_FORMAT_YUV420,
        w, h, ids.data(), numSurfaces, nullptr, 0);
    if (s != VA_STATUS_SUCCESS) {
        std::cerr << "QSV: vaCreateSurfaces failed: "
                  << (fns_.vaErrorStr ? fns_.vaErrorStr(s) : "?") << "\n";
        surfaces_.clear();
        return false;
    }
    for (int i = 0; i < numSurfaces; i++) {
        surfaces_[i].id = ids[i];
        surfaces_[i].pair.first = &surfaces_[i].id;
        surfaces_[i].pair.second = (mfxMemId)MFX_INFINITE;
        surfaces_[i].surf.Info = info;
        surfaces_[i].surf.Data.MemId = &surfaces_[i].pair;
        surfaces_[i].inUse = false;
    }

    // Build the allocator callback table. pthis is this VaApiAllocator.
    alloc_.pthis = this;
    alloc_.Alloc = &VaApiAllocator::allocCb;
    alloc_.Lock = &VaApiAllocator::lockCb;
    alloc_.Unlock = &VaApiAllocator::unlockCb;
    alloc_.GetHDL = &VaApiAllocator::getHdlCb;
    alloc_.Free = &VaApiAllocator::freeCb;

    ready_ = true;
    return true;
}

mfxStatus MFX_CDECL VaApiAllocator::allocCb(mfxHDL pthis,
                                            mfxFrameAllocRequest* req,
                                            mfxFrameAllocResponse* resp) {
    // libmfx-gen's decode surface source calls Alloc during Init to obtain
    // the external surface pool. Hand back our pre-allocated VA surfaces
    // (mirrors ffmpeg's frame_alloc returning s->mem_ids). Only honor
    // video-memory decode/encode requests; reject system-memory (we don't
    // stage host buffers through this allocator).
    if (!pthis || !req || !resp) return MFX_ERR_NULL_PTR;
    VaApiAllocator* self = static_cast<VaApiAllocator*>(pthis);
    if (!(req->Type & (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET
                       | MFX_MEMTYPE_VIDEO_MEMORY_ENCODER_TARGET
                       | MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))) {
        return MFX_ERR_UNSUPPORTED;
    }
    // Return the pool: one mid per surface. The mids are the
    // mfxHDLPair pointers (each surface's Data.MemId).
    static thread_local std::vector<mfxMemId> respMids;
    respMids.clear();
    for (auto& s : self->surfaces_) {
        respMids.push_back(s.surf.Data.MemId);
    }
    resp->mids = respMids.data();
    resp->NumFrameActual = static_cast<mfxU16>(self->surfaces_.size());
    return MFX_ERR_NONE;
}

mfxStatus MFX_CDECL VaApiAllocator::getHdlCb(mfxHDL pthis, mfxMemId mid,
                                             mfxHDL* hdl) {
    if (!pthis || !mid || !hdl) return MFX_ERR_NULL_PTR;
    // mid is a mfxHDLPair* (our Data.MemId). Hand back pair.first — the
    // VASurfaceID* — exactly ffmpeg's frame_get_hdl.
    mfxHDLPair* src = static_cast<mfxHDLPair*>(mid);
    mfxHDLPair* dst = static_cast<mfxHDLPair*>(static_cast<void*>(hdl));
    dst->first = src->first;
    if (src->second != (mfxMemId)MFX_INFINITE) {
        dst->second = src->second;
    }
    return MFX_ERR_NONE;
}

mfxFrameSurface1* VaApiAllocator::acquireSurface() {
    // A surface is reusable when the decoder has released its reference
    // (Data.Locked == 0). That is independent of our inUse flag: after a
    // successful decode the decoder IncreaseReference's the surface (Locked>0)
    // to keep it resident as a reference frame; it DecreaseReference's later
    // when the frame drops out of the reference window. If we hand back a
    // surface that is still Locked the decoder's SetCurrentMFXSurface rejects
    // it with MFX_ERR_MORE_SURFACE (VP9) or a -14 (VP8) — the same failure that
    // originally blocked P-frame decode. So a surface is free iff:
    //   - never used yet (inUse false, Locked 0), OR
    //   - used and copied to host (inUse true) BUT the decoder already
    //     released it (Locked 0) — we just hadn't noticed.
    for (auto& s : surfaces_) {
        if (!s.inUse || s.surf.Data.Locked == 0) {
            s.inUse = true;
            return &s.surf;
        }
    }
    return nullptr;
}

void VaApiAllocator::releaseSurface(mfxFrameSurface1* surf) {
    if (!surf) return;
    // Data.MemId is &VaSurface::pair for surfaces in this pool. Match the
    // pointer identity (not the pointed-to id).
    void* memId = surf->Data.MemId;
    for (auto& s : surfaces_) {
        if (static_cast<void*>(&s.pair) == memId) {
            // Only actually free the slot if the decoder has released its
            // reference. If Data.Locked > 0 the decoder still needs this
            // surface as a reference frame — keep inUse=true so it stays
            // resident and is not handed to a new DecodeFrameAsync (which
            // would overwrite a live reference). The slot frees itself the
            // next time acquireSurface() sees Data.Locked == 0.
            if (s.surf.Data.Locked == 0) {
                s.inUse = false;
            }
            return;
        }
    }
}

uint8_t* VaApiAllocator::copySurfaceToHost(VASurfaceID id, int w, int h,
                                           int* outStride) {
    if (!fns_.libva || id == VA_INVALID_ID) return nullptr;
    VAStatus s = fns_.vaSyncSurface(display_, id);
    if (s != VA_STATUS_SUCCESS) return nullptr;
    VAImage img{};
    s = fns_.vaDeriveImage(display_, id, &img);
    if (s != VA_STATUS_SUCCESS) return nullptr;
    // RAII cleanup for the image.
    struct ImageGuard {
        VaApiAllocator* self; VAImage* img; bool dismissed = false;
        ~ImageGuard() { if (!dismissed) self->fns_.vaDestroyImage(self->display_, img->image_id); }
    } guard{this, &img};
    uint8_t* buf = nullptr;
    s = fns_.vaMapBuffer(display_, img.buf, reinterpret_cast<void**>(&buf));
    if (s != VA_STATUS_SUCCESS || !buf) return nullptr;
    // NV12: plane 0 = Y, plane 1 = UV interleaved. pitches[0]=Y stride,
    // offsets[1] = UV plane offset. Copy into a compact malloc'd buffer
    // with stride = w (the consumer's expected row stride).
    int yStride = img.pitches[0];
    int uvStride = img.pitches[1] ? img.pitches[1] : yStride;
    size_t ySize = static_cast<size_t>(yStride) * h;
    // Clamp to image data_size to avoid overflow on odd-sized derives.
    if (ySize > img.data_size) ySize = img.data_size;
    uint8_t* out = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * h * 3 / 2));
    if (!out) {
        fns_.vaUnmapBuffer(display_, img.buf);
        return nullptr;
    }
    const uint8_t* ySrc = buf + img.offsets[0];
    for (int row = 0; row < h; row++) {
        std::memcpy(out + static_cast<size_t>(w) * row,
                    ySrc + static_cast<size_t>(yStride) * row, w);
    }
    const uint8_t* uvSrc = buf + img.offsets[1];
    uint8_t* uvDst = out + static_cast<size_t>(w) * h;
    for (int row = 0; row < h / 2; row++) {
        std::memcpy(uvDst + static_cast<size_t>(w) * row,
                    uvSrc + static_cast<size_t>(uvStride) * row, w);
    }
    fns_.vaUnmapBuffer(display_, img.buf);
    if (outStride) *outStride = w;
    return out;
}

void VaApiAllocator::destroy() {
    if (!surfaces_.empty() && fns_.vaDestroySurfaces && display_) {
        std::vector<VASurfaceID> ids;
        ids.reserve(surfaces_.size());
        for (auto& s : surfaces_) {
            if (s.id != VA_INVALID_ID) ids.push_back(s.id);
        }
        if (!ids.empty()) {
            fns_.vaDestroySurfaces(display_, ids.data(),
                                   static_cast<int>(ids.size()));
        }
    }
    surfaces_.clear();
    ready_ = false;
    if (fns_.libva) {
        dlclose(fns_.libva);
        fns_ = VaApiFns{};
    }
}

} // namespace qsv
} // namespace halcodec

#endif // defined(__linux__) && defined(QSV_HAS_VA)
