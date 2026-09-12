#include "qsvencoder.h"

#include <memory>
#include <string>

#include "qsv_common.h"
#include "registry.h"

namespace halcodec {
namespace qsv {

class QSVEncoder::Impl {
public:
    QSVRuntime runtime;
    mfxSession session = nullptr;
    bool inited = false;

    ~Impl() {
        if (inited && session) {
            runtime.encodeTerminate(session);
        }
        if (session) {
            runtime.close(session);
        }
    }
};

bool QSVEncoder::Initialize(const CodecParams& params) {
    if (impl_) {
        return false; // already initialized
    }
    impl_ = new Impl;
    if (!impl_->runtime.init()) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    if (impl_->runtime.initEx(&impl_->session) != MFX_ERR_NONE) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    if (params.width <= 0 || params.height <= 0) {
        std::cerr << "QSVEncoder: width/height required in CodecParams" << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    // NOTE: frame data path (FillFrame/GetFrame) is not implemented yet; the
    // adapter currently registers "qsvenc" and brings up a real libvpl session
    // + encoder component, mirroring the nvenc stub level.
    mfxVideoParam par{};
    par.mfx.CodecId = mapCodec(params.codec);
    par.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_QUALITY;
    par.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    par.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    par.mfx.FrameInfo.Width = static_cast<mfxU16>(params.width);
    par.mfx.FrameInfo.Height = static_cast<mfxU16>(params.height);
    par.mfx.FrameInfo.FrameRateExtN = 30;
    par.mfx.FrameInfo.FrameRateExtD = 1;
    par.mfx.TargetKbps = 8000; // ~8 Mbps
    par.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    mfxStatus sts = impl_->runtime.encodeInit(impl_->session, &par);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: MFXVideoENCODE_Init failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->inited = true;
    std::cout << "QSVEncoder: libvpl session up, codec=" << params.codec
              << " " << params.width << "x" << params.height << std::endl;
    return true;
}

bool QSVEncoder::FillFrame(const CodecFrame&) {
    // Data path TODO: wrap a CodecFrame as an mfxFrameSurface1 (NV12) and
    // submit via MFXVideoENCODE_EncodeFrameAsync.
    return false;
}

bool QSVEncoder::GetFrame(CodecFrame&) {
    return false;
}

void QSVEncoder::Finalize() {
    delete impl_;
    impl_ = nullptr;
}

std::string QSVEncoder::getName() const {
    return "qsvenc";
}

HALCODEC_CONNECT(Encoder, qsvenc, QSVEncoder);

} // namespace qsv
} // namespace halcodec