#if defined(__linux__) && defined(QSV_HAS_VA)
#ifndef QSV_LAYERS_QSV_VA_ALLOCATOR_H
#define QSV_LAYERS_QSV_VA_ALLOCATOR_H

// Linux-VAAPI external frame allocator for the QSV decode path. dlopens
// libva.so.2 (no link dependency) and builds a pool of real VA surfaces
// (vaCreateSurfaces) exposed to libmfx-gen as a legacy mfxFrameAllocator.
//
// Why: libmfx-gen's VP8/VP9 P-frame reference handling rejects surfaces from
// the new-API GetSurfaceForDecode internal pool with -14/-16. ffmpeg's
// vp8_qsv decodes fully by registering a VA-API mfxFrameAllocator
// (hwcontext_qsv.c) and handing MemId-only surfaces — libmfx-gen then takes
// the legacy surface path (m_redirect_to_vpl_path=false because
// IsExternalFrameAllocator()=true), which tracks VA surfaces via GetHDL.
// Without a registered allocator libmfx-gen takes the new-API FrameInterface
// path and segfaults on a MemId-only surface. This class mirrors ffmpeg's
// recipe: register an allocator whose GetHDL returns the VASurfaceID.
//
// Platform guard: the whole file is #if'd to Linux + QSV_HAS_VA (CMake
// find_path va/va.h). Non-Linux or libva-header-less builds compile this
// out — the QSV decode path falls back to GetSurfaceForDecode (h264/hevc/
// av1/jpeg unaffected; VP8/VP9 stay -14 but no crash). Runtime dlopen of
// libva.so.2 failing (libva-less Linux) makes init() return false → same
// fallback.

#include <va/va.h>
#include <vpl/mfx.h>

#include <vector>

namespace halcodec {
namespace qsv {

// dlopen'd libva function table. All nullptr = unavailable.
struct VaApiFns {
    void* libva;  // dlopen handle; dlclose'd in destroy()
    int (*vaInitialize)(VADisplay, int* major, int* minor);
    int (*vaTerminate)(VADisplay);
    int (*vaCreateSurfaces)(VADisplay, unsigned int format,
        unsigned int width, unsigned int height,
        VASurfaceID* surfaces, unsigned int num_surfaces,
        VASurfaceAttrib* attrib_list, unsigned int num_attribs);
    int (*vaDestroySurfaces)(VADisplay, VASurfaceID* surfaces, int num_surfaces);
    int (*vaDeriveImage)(VADisplay, VASurfaceID, VAImage* image);
    int (*vaDestroyImage)(VADisplay, VAImageID image);
    int (*vaMapBuffer)(VADisplay, VABufferID buf_id, void** pbuf);
    int (*vaUnmapBuffer)(VADisplay, VABufferID buf_id);
    int (*vaSyncSurface)(VADisplay, VASurfaceID);
    const char* (*vaErrorStr)(VAStatus error);
};

// A VA-surface-backed mfxFrameSurface1. MemId points at the mfxHDLPair
// {first = &surfaceId, second = MFX_INFINITE} — the shape libmfx-gen's
// legacy surface path expects (mirrors ffmpeg's handle_pairs_internal).
struct VaSurface {
    VASurfaceID id = VA_INVALID_ID;
    mfxHDLPair pair;           // {first=&id, second=MFX_INFINITE}
    mfxFrameSurface1 surf{};   // Info + Data.MemId = &pair; no FrameInterface
    bool inUse = false;
};

class VaApiAllocator {
public:
    ~VaApiAllocator() { destroy(); }

    // dlopen libva.so.2 + resolve symbols. Does NOT create surfaces yet
    // (needs a VADisplay). Returns false on any failure (caller falls back
    // to GetSurfaceForDecode — VP8/VP9 stay blocked, no crash).
    bool init();

    // Create the VA surface pool. dpy is the session's VADisplay (obtained
    // via MFXVideoCORE_GetHandle(MFX_HANDLE_VA_DISPLAY) AFTER decodeInit,
    // when the VADisplay is materialized). w/h are the coded (aligned-16)
    // dimensions; numSurfaces is the pool size (libmfx-gen needs >=
    // AsyncDepth + reorder depth; 16 is safe). Fills surfaces_ and wires
    // each mfxFrameSurface1.Data.MemId = &pair. Returns false on failure.
    bool createPool(VADisplay dpy, mfxU16 w, mfxU16 h, int numSurfaces,
                    const mfxFrameInfo& info);

    // The legacy allocator callbacks table (registered via
    // MFXVideoCORE_SetFrameAllocator). Stable for the allocator's lifetime.
    mfxFrameAllocator* allocator() { return &alloc_; }

    bool ready() const { return ready_; }
    VADisplay display() const { return display_; }

    // Acquire a free surface from the pool for the worker to hand to
    // DecodeFrameAsync. Returns nullptr if all in use (caller can retry or
    // fall back). The surface stays marked inUse until releaseSurface().
    mfxFrameSurface1* acquireSurface();

    // Return a surface to the pool after the worker has copied it to host.
    // Matched by Data.MemId (the mfxHDLPair* the allocator wired in createPool).
    // The decoder still tracks references internally (it calls
    // IncreaseReference/DecreaseReference on the VASurfaceID through the
    // allocator's GetHDL); this only frees our bookkeeping slot so the pool
    // can be reused for the next DecodeFrameAsync. Safe to call with any
    // surface pointer (no-op if not from this pool).
    void releaseSurface(mfxFrameSurface1* surf);

    // Read back a decoded surface to host NV12. Synchronizes the VA surface,
    // derives an image, maps the buffer, copies Y/UV planes into a malloc'd
    // buffer (caller frees), unmaps + destroys the image. outData/outStride
    // filled on success. Returns nullptr on failure.
    // NOTE: VA surfaces have no mfxFrameSurfaceInterface, so the worker
    // cannot fi->Map them — it must use this VA-direct path instead.
    uint8_t* copySurfaceToHost(VASurfaceID id, int w, int h, int* outStride);

    // Tear down: destroy VA surfaces, dlclose libva. Idempotent.
    void destroy();

private:
    static mfxStatus MFX_CDECL allocCb(mfxHDL pthis, mfxFrameAllocRequest* req,
                                       mfxFrameAllocResponse* resp);
    static mfxStatus MFX_CDECL lockCb(mfxHDL, mfxMemId, mfxFrameData*) {
        return MFX_ERR_UNSUPPORTED;  // ffmpeg never locks; runtime stages sysmem itself
    }
    static mfxStatus MFX_CDECL unlockCb(mfxHDL, mfxMemId, mfxFrameData*) {
        return MFX_ERR_UNSUPPORTED;
    }
    static mfxStatus MFX_CDECL getHdlCb(mfxHDL pthis, mfxMemId mid, mfxHDL* hdl);
    static mfxStatus MFX_CDECL freeCb(mfxHDL, mfxFrameAllocResponse*) {
        // Pool is owned by VaApiAllocator; nothing to free per-response.
        return MFX_ERR_NONE;
    }

    VaApiFns fns_{};
    mfxFrameAllocator alloc_{};
    VADisplay display_ = nullptr;
    bool ownsDisplay_ = false;  // false: display borrowed from the session
    std::vector<VaSurface> surfaces_;
    bool ready_ = false;
};

} // namespace qsv
} // namespace halcodec

#endif // QSV_LAYERS_QSV_VA_ALLOCATOR_H
#endif // defined(__linux__) && defined(QSV_HAS_VA)
