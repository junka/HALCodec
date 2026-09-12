#include "nvdecoder.h"

#include <iostream>
#include <string>

#include "frame.h"
#include "registry.h"

namespace halcodec {
namespace nvenc {

namespace {

// Maps the NVDEC output surface format to the unified PixelFormat.
PixelFormat toHalFormat(cudaVideoSurfaceFormat surfaceFormat) {
    switch (surfaceFormat) {
        case cudaVideoSurfaceFormat_NV12: return PixelFormat::NV12;
        case cudaVideoSurfaceFormat_P016: return PixelFormat::P016;
        case cudaVideoSurfaceFormat_YUV444: return PixelFormat::YUV444P;
        case cudaVideoSurfaceFormat_YUV444_16Bit: return PixelFormat::YUV444P10LE;
#if NVENCAPI_MAJOR_VERSION > 12
        case cudaVideoSurfaceFormat_NV16: return PixelFormat::NV16;
        case cudaVideoSurfaceFormat_P216: return PixelFormat::P210;
#endif
        default: return PixelFormat::Unknown;
    }
}

} // namespace

bool NVDecoder::Initialize(const CodecParams& params) {
    if (params.inputs.empty()) {
        std::cerr << "NVDecoder: no input specified" << std::endl;
        return false;
    }
    if (!cudaCtx_.create(params.deviceIndex)) {
        std::cerr << "NVDecoder: failed to create CUDA context for device "
                  << params.deviceIndex << std::endl;
        return false;
    }
    auto cudaCtx = cudaCtx_.get();

    Rect cropRect = {};
    Dim resizeDim = {};

    demuxer_ = std::make_unique<FFmpegDemuxer>(params.inputs[0].c_str());
#if NVENCAPI_MAJOR_VERSION > 12
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
        FFmpeg2NvCodecId(demuxer_->GetVideoCodec()),
        false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
        FFmpeg2NvCodecId(demuxer_->GetVideoCodec()),
        false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
    decoder_->SetOperatingPoint(0, false);
    return true;
}

int NVDecoder::FillinFrame() {
    uint8_t* pVideo = nullptr;
    int nVideoBytes = 0;
    int nFrame = 0;
    do {
        demuxer_->Demux(&pVideo, &nVideoBytes);
        nFrame = decoder_->Decode(pVideo, nVideoBytes);
    } while (nFrame == 0 && nVideoBytes > 0);
    return nFrame;
}

void NVDecoder::Finalize() {
    std::cout << decoder_->GetVideoInfo();
    decoder_ = nullptr;
    demuxer_ = nullptr;
}

std::string NVDecoder::getName() const {
    return "nvdec";
}

bool NVDecoder::GetFrame(CodecFrame& out) {
    int64_t pts = 0;
    out.data = decoder_->GetLockedFrame(&pts);
    if (!out.data) {
        return false;
    }
    out.size = decoder_->GetFrameSize();
    out.width = decoder_->GetWidth();
    out.height = decoder_->GetHeight();
    out.format = toHalFormat(decoder_->GetOutputFormat());
    out.strides[0] = decoder_->GetDeviceFramePitch();
    out.pts = pts;
    // NVDEC owns the locked frame buffer; the caller must not free it.
    // Unlocking happens on the next locked-frame rotation inside NvDecoder.
    out.release = nullptr;
    return true;
}

HALCODEC_CONNECT(Decoder, nvdec, NVDecoder);

} // namespace nvenc
} // namespace halcodec