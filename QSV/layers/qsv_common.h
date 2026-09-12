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
        initEx_ = reinterpret_cast<MFXInitExFn>(dlsym(handle_, "MFXInitEx"));
        close_ = reinterpret_cast<MFXCloseFn>(dlsym(handle_, "MFXClose"));
        decodeInit_ = reinterpret_cast<MFXVideoDECODEInitFn>(dlsym(handle_, "MFXVideoDECODE_Init"));
        decodeTerminate_ =
            reinterpret_cast<MFXVideoDECODETerminateFn>(dlsym(handle_, "MFXVideoDECODE_Terminate"));
        encodeInit_ = reinterpret_cast<MFXVideoENCODEInitFn>(dlsym(handle_, "MFXVideoENCODE_Init"));
        encodeTerminate_ =
            reinterpret_cast<MFXVideoENCODETerminateFn>(dlsym(handle_, "MFXVideoENCODE_Terminate"));
        if (!initEx_ || !close_ || !decodeInit_ || !encodeInit_) {
            std::cerr << "QSV: required libvpl symbols not found" << std::endl;
            return false;
        }
        return true;
    }

    // Legacy init path accepted by the libvpl dispatcher; keeps this adapter
    // minimal without the full MFXLoad/MFXCreateSession config dance.
    mfxStatus initEx(mfxSession* session) const {
        mfxInitParam par{};
        par.Implementation = MFX_IMPL_HARDWARE_ANY;
        par.Version.Major = MFX_VERSION_MAJOR;
        par.Version.Minor = MFX_VERSION_MINOR;
        par.ExternalThreads = 0; // internal threading
        mfxStatus sts = initEx_(par, session);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "QSV: MFXInitEx failed: " << sts << std::endl;
        }
        return sts;
    }

    mfxStatus close(mfxSession session) const { return close_(session); }
    mfxStatus decodeInit(mfxSession session, mfxVideoParam* par) const {
        return decodeInit_(session, par);
    }
    mfxStatus decodeTerminate(mfxSession session) const { return decodeTerminate_(session); }
    mfxStatus encodeInit(mfxSession session, mfxVideoParam* par) const {
        return encodeInit_(session, par);
    }
    mfxStatus encodeTerminate(mfxSession session) const { return encodeTerminate_(session); }

private:
    using MFXInitExFn = mfxStatus(MFX_CDECL*)(mfxInitParam par, mfxSession* session);
    using MFXCloseFn = mfxStatus(MFX_CDECL*)(mfxSession session);
    using MFXVideoDECODEInitFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxVideoParam*);
    using MFXVideoDECODETerminateFn = mfxStatus(MFX_CDECL*)(mfxSession);
    using MFXVideoENCODEInitFn = mfxStatus(MFX_CDECL*)(mfxSession, mfxVideoParam*);
    using MFXVideoENCODETerminateFn = mfxStatus(MFX_CDECL*)(mfxSession);

    void* handle_ = nullptr;
    MFXInitExFn initEx_ = nullptr;
    MFXCloseFn close_ = nullptr;
    MFXVideoDECODEInitFn decodeInit_ = nullptr;
    MFXVideoDECODETerminateFn decodeTerminate_ = nullptr;
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