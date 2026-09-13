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

// Maps a codec name string ("h264"/"hevc"/...) to an NVDEC codec id, used in
// explicit-feeding mode where no FFmpeg demuxer provides the codec type.
cudaVideoCodec codecFromName(const std::string& name) {
    if (name == "h264")  return cudaVideoCodec_H264;
    if (name == "hevc")  return cudaVideoCodec_HEVC;
#if NVENCAPI_MAJOR_VERSION > 12
    if (name == "av1")   return cudaVideoCodec_AV1;
#endif
    if (name == "mpeg2") return cudaVideoCodec_MPEG2;
    if (name == "mpeg4") return cudaVideoCodec_MPEG4;
    if (name == "vp8")   return cudaVideoCodec_VP8;
    if (name == "vp9")   return cudaVideoCodec_VP9;
    return cudaVideoCodec_NumCodecs;
}

} // namespace

bool NVDecoder::Initialize(const CodecParams& params) {
    Rect cropRect = {};
    Dim resizeDim = {};
    if (params.inputs.empty()) {
        // Explicit-feeding mode: no internal source; the caller delivers
        // compressed data via FillInput() / SignalInputComplete().
        feedMode_ = true;
        if (!cudaCtx_.create(params.deviceIndex)) {
            std::cerr << "NVDecoder: failed to create CUDA context for device "
                      << params.deviceIndex << std::endl;
            return false;
        }
        auto cudaCtx = cudaCtx_.get();
        cudaVideoCodec codec =
            codecFromName(params.codec.empty() ? "h264" : params.codec);
        if (codec == cudaVideoCodec_NumCodecs) {
            std::cerr << "NVDecoder: unknown codec: " << params.codec << std::endl;
            return false;
        }
#if NVENCAPI_MAJOR_VERSION > 12
        decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
            codec, false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
        decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
            codec, false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
        decoder_->SetOperatingPoint(0, false);
        return true;
    }

    if (!cudaCtx_.create(params.deviceIndex)) {
        std::cerr << "NVDecoder: failed to create CUDA context for device "
                  << params.deviceIndex << std::endl;
        return false;
    }
    auto cudaCtx = cudaCtx_.get();

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

int NVDecoder::FillInput(const uint8_t* data, size_t size) {
    if (!feedMode_ || !decoder_) {
        return -1;
    }
    // NvDecoder::Decode is synchronous: it returns the number of frames
    // decoded from this chunk. The caller should feed whole access units so
    // decoder internal state is not left with a partial NAL at chunk ends.
    return decoder_->Decode(data, static_cast<int>(size));
}

bool NVDecoder::SignalInputComplete() {
    if (!feedMode_ || !decoder_) {
        return false;
    }
    // A null payload with zero size maps to CUVID_PKT_ENDOFSTREAM, flushing
    // any buffered frames out so GetFrame() can drain them.
    decoder_->Decode(nullptr, 0);
    return true;
}

int NVDecoder::PullFrames() {
    if (feedMode_) {
        // No internal source in explicit-feeding mode.
        return 0;
    }
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