#ifndef QSV_LAYERS_QSV_COMMON_H
#define QSV_LAYERS_QSV_COMMON_H

// Shared libvpl (Intel Quick Sync Video / oneVPL) runtime plumbing for the
// QSV backend. libvpl ships as a dispatcher shared library (libvpl.so.2 on
// Linux): we dlopen it, mirroring how the AMF backend loads libamfrt64, so
// there is no static link dependency. A valid libvpl + Intel media driver
// (iHD/i965 on Linux) must exist on the machine where the binary runs.

#include <dlfcn.h>

#include <iostream>
#include <string>

#include <vpl/mfx.h>

namespace halcodec {
namespace qsv {

class QSVRuntime {
public:
    bool init() {
        handle_ = dlopen("libvpl.so.2", RTLD_NOW | RTLD_GLOBAL);
        if (!handle_) {
            handle_ = dlopen("libvpl.so", RTLD_NOW | RTLD_GLOBAL);
        }
        if (!handle_) {
            std::cerr << "QSV: failed to dlopen libvpl.so.2 (" << dlerror() << ")"
                      << std::endl;
            return false;
        }
        dlerror();
        // Session creation (oneVPL 2.x path, runtime version-decoupled).
        load_ = reinterpret_cast<MFXLoadFn>(dlsym(handle_, "MFXLoad"));
        unload_ = reinterpret_cast<MFXUnloadFn>(dlsym(handle_, "MFXUnload"));
        createSession_ =
            reinterpret_cast<MFXCreateSessionFn>(dlsym(handle_, "MFXCreateSession"));
        queryVersion_ =
            reinterpret_cast<MFXQueryVersionFn>(dlsym(handle_, "MFXQueryVersion"));
        // Legacy fallback (kept for dispatchers without MFXLoad).
        initEx_ = reinterpret_cast<MFXInitExFn>(dlsym(handle_, "MFXInitEx"));
        close_ = reinterpret_cast<MFXCloseFn>(dlsym(handle_, "MFXClose"));
        // Decode data path.
        decodeInit_ = reinterpret_cast<MFXVideoDECODEInitFn>(dlsym(handle_, "MFXVideoDECODE_Init"));
        decodeHeader_ = reinterpret_cast<MFXVideoDECODEDecodeHeaderFn>(
            dlsym(handle_, "MFXVideoDECODE_DecodeHeader"));
        decodeTerminate_ =
            reinterpret_cast<MFXVideoDECODETerminateFn>(dlsym(handle_, "MFXVideoDECODE_Close"));
        decodeFrameAsync_ = reinterpret_cast<MFXVideoDECODEDecodeFrameAsyncFn>(
            dlsym(handle_, "MFXVideoDECODE_DecodeFrameAsync"));
        getSurfaceForDecode_ = reinterpret_cast<MFXMemoryGetSurfaceForDecodeFn>(
            dlsym(handle_, "MFXMemory_GetSurfaceForDecode"));
        syncOperation_ = reinterpret_cast<MFXVideoCORESyncOperationFn>(
            dlsym(handle_, "MFXVideoCORE_SyncOperation"));
        decodeGetVideoParam_ = reinterpret_cast<MFXVideoDECODEGetVideoParamFn>(
            dlsym(handle_, "MFXVideoDECODE_GetVideoParam"));
        encodeInit_ = reinterpret_cast<MFXVideoENCODEInitFn>(dlsym(handle_, "MFXVideoENCODE_Init"));
        encodeTerminate_ =
            reinterpret_cast<MFXVideoENCODETerminateFn>(dlsym(handle_, "MFXVideoENCODE_Close"));
        if (!createSession_ || !close_ || !decodeInit_ || !encodeInit_
                || !decodeFrameAsync_ || !syncOperation_ || !decodeTerminate_
                || !encodeTerminate_) {
            std::cerr << "QSV: required libvpl symbols not found" << std::endl;
            return false;
        }
        return true;
    }

    // oneVPL 2.x path: MFXLoad enumerates available implementations and picks
    // one (iHD/i965 hardware on this host), MFXCreateSession opens it. Unlike
    // the legacy MFXInitEx, the requested API version is negotiated at runtime
    // (MFXQueryVersion reports what the dispatcher actually supports), so a
    // binary built against libvpl-2.17 headers runs against libvpl-2.9.
    mfxStatus createSession(mfxSession* session) const {
        mfxLoader loader = nullptr;
        mfxStatus sts;
        if (load_ && createSession_) {
            loader = load_();
            if (!loader) {
                std::cerr << "QSV: MFXLoad returned null" << std::endl;
                return MFX_ERR_UNKNOWN;
            }
            // oneVPL 2.x: MFXCreateSession(loader, implIdx=0, &session). The
            // impl index selects which discovered implementation to use; 0 is
            // the first (hardware) one enumerated by MFXLoad.
            sts = createSession_(loader, 0, session);
            if (sts != MFX_ERR_NONE) {
                std::cerr << "QSV: MFXCreateSession failed: " << sts << std::endl;
                unload_(loader);
                return sts;
            }
            if (queryVersion_) {
                mfxVersion v{};
                if (queryVersion_(*session, &v) == MFX_ERR_NONE) {
                    std::cerr << "QSV: libvpl runtime API " << v.Major << "."
                              << v.Minor << std::endl;
                }
            }
            // Keep the loader alive for the session's lifetime (oneVPL allows
            // MFXUnload after MFXCreateSession; the session holds its own ref).
            unload_(loader);
            return MFX_ERR_NONE;
        }
        // Legacy fallback: request a conservative 2.1 to maximize compatibility
        // with older dispatchers that reject higher compiled versions.
        mfxInitParam par{};
        par.Implementation = MFX_IMPL_HARDWARE_ANY;
        par.Version.Major = 2;
        par.Version.Minor = 1;
        par.ExternalThreads = 0;
        sts = initEx_(par, session);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "QSV: MFXInitEx failed: " << sts << std::endl;
        }
        return sts;
    }

    mfxStatus close(mfxSession session) const { return close_(session); }
    mfxStatus decodeInit(mfxSession session, mfxVideoParam* par) const {
        return decodeInit_(session, par);
    }
    mfxStatus decodeHeader(mfxSession session, mfxBitstream* bs, mfxVideoParam* par) const {
        return decodeHeader_(session, bs, par);
    }
    mfxStatus decodeTerminate(mfxSession session) const { return decodeTerminate_(session); }
    mfxStatus decodeFrameAsync(mfxSession session, mfxBitstream* bs,
                               mfxFrameSurface1* surfWork, mfxFrameSurface1** surfOut,
                               mfxSyncPoint* sync) const {
        return decodeFrameAsync_(session, bs, surfWork, surfOut, sync);
    }
    mfxStatus getSurfaceForDecode(mfxSession session, mfxFrameSurface1** surf) const {
        return getSurfaceForDecode_(session, surf);
    }
    mfxStatus syncOperation(mfxSession session, mfxSyncPoint sync, mfxU32 timeout) const {
        return syncOperation_(session, sync, timeout);
    }
    mfxStatus decodeGetVideoParam(mfxSession session, mfxVideoParam* par) const {
        return decodeGetVideoParam_(session, par);
    }
    mfxStatus encodeInit(mfxSession session, mfxVideoParam* par) const {
        return encodeInit_(session, par);
    }
    mfxStatus encodeTerminate(mfxSession session) const { return encodeTerminate_(session); }

private:
    using MFXLoadFn = mfxLoader(MFX_CDECL*)();
    using MFXUnloadFn = void(MFX_CDECL*)(mfxLoader);
    using MFXCreateSessionFn = mfxStatus(MFX_CDECL*)(mfxLoader, mfxU32, mfxSession*);
    using MFXQueryVersionFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxVersion*);
    using MFXInitExFn = mfxStatus(MFX_CDECL*)(mfxInitParam par, mfxSession* session);
    using MFXCloseFn = mfxStatus(MFX_CDECL*)(mfxSession session);
    using MFXVideoDECODEInitFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxVideoParam*);
    using MFXVideoDECODEDecodeHeaderFn = mfxStatus(MFX_CDECL*)(mfxSession,
        mfxBitstream*, mfxVideoParam*);
    using MFXVideoDECODETerminateFn = mfxStatus(MFX_CDECL*)(mfxSession);
    using MFXVideoDECODEDecodeFrameAsyncFn = mfxStatus(MFX_CDECL*)(mfxSession,
        mfxBitstream*, mfxFrameSurface1*, mfxFrameSurface1**, mfxSyncPoint*);
    using MFXMemoryGetSurfaceForDecodeFn = mfxStatus(MFX_CDECL*)(mfxSession,
        mfxFrameSurface1**);
    using MFXVideoCORESyncOperationFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxSyncPoint, mfxU32);
    using MFXVideoDECODEGetVideoParamFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxVideoParam*);
    using MFXVideoENCODEInitFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxVideoParam*);
    using MFXVideoENCODETerminateFn = mfxStatus(MFX_CDECL*)(mfxSession);

    void* handle_ = nullptr;
    MFXLoadFn load_ = nullptr;
    MFXUnloadFn unload_ = nullptr;
    MFXCreateSessionFn createSession_ = nullptr;
    MFXQueryVersionFn queryVersion_ = nullptr;
    MFXInitExFn initEx_ = nullptr;
    MFXCloseFn close_ = nullptr;
    MFXVideoDECODEInitFn decodeInit_ = nullptr;
    MFXVideoDECODEDecodeHeaderFn decodeHeader_ = nullptr;
    MFXVideoDECODETerminateFn decodeTerminate_ = nullptr;
    MFXVideoDECODEDecodeFrameAsyncFn decodeFrameAsync_ = nullptr;
    MFXMemoryGetSurfaceForDecodeFn getSurfaceForDecode_ = nullptr;
    MFXVideoCORESyncOperationFn syncOperation_ = nullptr;
    MFXVideoDECODEGetVideoParamFn decodeGetVideoParam_ = nullptr;
    MFXVideoENCODEInitFn encodeInit_ = nullptr;
    MFXVideoENCODETerminateFn encodeTerminate_ = nullptr;
};

// Maps the unified codec string to an libvpl codec id.
inline mfxU32 mapCodec(const std::string& codec) {
    if (codec == "hevc") return MFX_CODEC_HEVC;
    if (codec == "av1") return MFX_CODEC_AV1;
    if (codec == "jpeg") return MFX_CODEC_JPEG;
    return MFX_CODEC_AVC; // default
}

} // namespace qsv
} // namespace halcodec

#endif // QSV_LAYERS_QSV_COMMON_H