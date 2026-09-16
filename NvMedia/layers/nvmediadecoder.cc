#include "nvmediadecoder.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "registry.h"

namespace halcodec {
namespace nvmedia {

namespace {

// Map the unified codec string ("h264"/"hevc"/...) to a NvMediaVideoCodec.
// Returns static_cast<NvMediaVideoCodec>(-1) for unknown codecs.
NvMediaVideoCodec MapCodec(const std::string& codec) {
    if (codec == "h264") {
        return NVMEDIA_VIDEO_CODEC_H264;
    }
    if (codec == "hevc") {
        return NVMEDIA_VIDEO_CODEC_HEVC;
    }
    if (codec == "av1") {
        return NVMEDIA_VIDEO_CODEC_AV1;
    }
    if (codec == "jpeg") {
        return NVMEDIA_VIDEO_CODEC_MJPEG;
    }
    if (codec == "vp9") {
        return NVMEDIA_VIDEO_CODEC_VP9;
    }
    if (codec == "mpeg2") {
        return NVMEDIA_VIDEO_CODEC_MPEG2;
    }
    return static_cast<NvMediaVideoCodec>(-1);
}

} // namespace

bool NvMediaDecoder::Initialize(const CodecParams& params) {
    // GetVersion -> Create, mirroring videodemo.c (GetVersion then
    // NvMediaIDECreate). No NvSci registration is needed at create time.
    NvMediaVersion version{};
    if (NvMediaIDEGetVersion(&version) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: NvMediaIDEGetVersion failed" << std::endl;
        return false;
    }

    NvMediaVideoCodec codec = MapCodec(params.codec);
    if (codec == static_cast<NvMediaVideoCodec>(-1)) {
        std::cerr << "NvMediaDecoder: unsupported codec: " << params.codec
                  << std::endl;
        return false;
    }

    // NvMediaIDECreate requires width/height > 0; decode usually has no
    // resolution yet, so fall back to a default.
    uint16_t w = (params.width > 0) ? static_cast<uint16_t>(params.width) : 1920u;
    uint16_t h = (params.height > 0) ? static_cast<uint16_t>(params.height) : 1080u;

    decoder_ = NvMediaIDECreate(
        codec,                                  // NvMediaVideoCodec
        w, h,                                   // width, height
        16u,                                    // maxReferences
        25ull * 1024 * 1024,                    // maxBitstreamSize (25 MB)
        4u,                                     // inputBuffering (1..8)
        0u,                                     // flags
        NVMEDIA_DECODER_INSTANCE_0);            // instance id
    if (!decoder_) {
        std::cerr << "NvMediaDecoder: NvMediaIDECreate(" << params.codec
                  << ") failed" << std::endl;
        return false;
    }
    std::cout << "NvMediaDecoder: IDE " << params.codec
              << " decoder created (stub)" << std::endl;
    return true;
}

int NvMediaDecoder::PullFrames() {
    // Data-path TODO: demux params.inputs[0], feed each picture via
    // NvMediaBitstreamBuffer + NvMediaIDEDecoderRender(...) and drain with
    // NvMediaIDEGetFrameDecodeStatus (see samples/nvmedia_6x/ide/videodemo.c
    // cbDecodePicture). For now report an exhausted stream so the synchronous
    // hal_dec loop terminates cleanly.
    return 0;
}

void NvMediaDecoder::Finalize() {
    if (decoder_) {
        NvMediaIDEDestroy(decoder_);
        decoder_ = nullptr;
    }
}

bool NvMediaDecoder::GetFrame(CodecFrame&) {
    // Data-path TODO: convert the decoded NvMedia surface to a CodecFrame.
    return false;
}

std::string NvMediaDecoder::getName() const {
    return "nvmedia";
}

HALCODEC_CONNECT(Decoder, nvmedia, NvMediaDecoder);

} // namespace nvmedia
} // namespace halcodec