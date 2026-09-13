#include "qsvdecoder.h"

#include <memory>
#include <string>

#include "qsv_common.h"
#include "registry.h"

namespace halcodec {
namespace qsv {

// PIMPL keeps the libvpl session handle out of the public header.
class QSVDecoder::Impl {
public:
    QSVRuntime runtime;
    mfxSession session = nullptr;
    bool inited = false;

    ~Impl() {
        if (inited && session) {
            runtime.decodeTerminate(session);
        }
        if (session) {
            runtime.close(session);
        }
    }
};

bool QSVDecoder::Initialize(const CodecParams& params) {
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

    // NOTE: frame data path (FillinFrame/GetFrame) is not implemented yet; the
    // adapter currently registers "qsvdec" and brings up a real libvpl session
    // + decoder component, mirroring the nvenc stub level.
    mfxVideoParam par{};
    par.mfx.CodecId = mapCodec(params.codec);
    par.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    par.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    if (params.width > 0) {
        par.mfx.FrameInfo.Width = static_cast<mfxU16>(params.width);
    }
    if (params.height > 0) {
        par.mfx.FrameInfo.Height = static_cast<mfxU16>(params.height);
    }
    par.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    mfxStatus sts = impl_->runtime.decodeInit(impl_->session, &par);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVDecoder: MFXVideoDECODE_Init failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->inited = true;
    std::cout << "QSVDecoder: libvpl session up, codec=" << params.codec
              << std::endl;
    return true;
}

int QSVDecoder::PullFrames() {
    // Data path TODO: demux params.inputs[0], feed mfxBitstream via
    // MFXVideoDECODE_DecodeHeader/DecodeAsync and drain via GetSurfacePool.
    return 0;
}

void QSVDecoder::Finalize() {
    delete impl_;
    impl_ = nullptr;
}

bool QSVDecoder::GetFrame(CodecFrame&) {
    return false;
}

std::string QSVDecoder::getName() const {
    return "qsvdec";
}

HALCODEC_CONNECT(Decoder, qsvdec, QSVDecoder);

} // namespace qsv
} // namespace halcodec