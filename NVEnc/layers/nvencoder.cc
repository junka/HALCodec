#include <vector>
#include <algorithm>
#include <string>
#include <regex>

#include "nvencoder.h"

#include "frame.h"
#include "registry.h"

namespace halcodec {
namespace nvenc {

// NVENC encoding pipeline. Raw frames are delivered by the caller via
// FillFrame(const CodecFrame&); the encoder converts them onto the device and
// hands the resulting elementary-stream packets out via GetFrame().

bool NVEncoder::Initialize(const CodecParams& params) {
    if (!cudaCtx_.create(params.deviceIndex)) {
        std::cerr << "NVEncoder: failed to create CUDA context for device "
                  << params.deviceIndex << std::endl;
        return false;
    }
    auto cudaCtx = cudaCtx_.get();
    int width = params.width;
    int height = params.height;
    if (width <= 0 || height <= 0) {
        std::string input = params.inputs.empty() ? "" : params.inputs[0];
        std::regex pattern(R"((\d+)[xX](\d+))");
        std::smatch match;
        if (std::regex_search(input, match, pattern)) {
            width = std::stoi(match[1].str());
            height = std::stoi(match[2].str());
        } else {
            std::cerr << "Failed to match resolution in: " << input << std::endl;
            return false;
        }
    }
    std::string format = "nv12";
    switch (params.inputFormat) {
        case PixelFormat::I420: format = "iyuv"; break;
        case PixelFormat::NV12: format = "nv12"; break;
        case PixelFormat::YUV444P: format = "yuv444"; break;
        default: break;
    }
    auto eFormat = [](std::string format) {
        std::vector<std::string> bufferFormatStr = {
            "iyuv", "nv12", "yv12", "yuv444", "p010", "yuv444p16", "bgra", "bgra10", "ayuv", "abgr", "abgr10",
#if NVENCAPI_MAJOR_VERSION > 12
            "nv16", "p210"
#endif
        };
        NV_ENC_BUFFER_FORMAT inFormat[] = {
            NV_ENC_BUFFER_FORMAT_IYUV,
            NV_ENC_BUFFER_FORMAT_NV12,
            NV_ENC_BUFFER_FORMAT_YV12,
            NV_ENC_BUFFER_FORMAT_YUV444,
            NV_ENC_BUFFER_FORMAT_YUV420_10BIT,
            NV_ENC_BUFFER_FORMAT_YUV444_10BIT,
            NV_ENC_BUFFER_FORMAT_ARGB,
            NV_ENC_BUFFER_FORMAT_ARGB10,
            NV_ENC_BUFFER_FORMAT_AYUV,
            NV_ENC_BUFFER_FORMAT_ABGR,
            NV_ENC_BUFFER_FORMAT_ABGR10,
#if NVENCAPI_MAJOR_VERSION > 12
            NV_ENC_BUFFER_FORMAT_NV16,
            NV_ENC_BUFFER_FORMAT_P210,
#endif
        };
        auto it = std::find(bufferFormatStr.begin(), bufferFormatStr.end(), format);
        if (it != bufferFormatStr.end()) {
            return inFormat[it - bufferFormatStr.begin()];
        }
        return NV_ENC_BUFFER_FORMAT_UNDEFINED;
    }(format);

#if NVENCAPI_MAJOR_VERSION > 12
    encoder_ = std::make_unique<NvEncoderCuda>(cudaCtx, width, height, eFormat, 3, false, false, false);
#else
    encoder_ = std::make_unique<NvEncoderCuda>(cudaCtx, width, height, eFormat);
#endif

    NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
    NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };

    initializeParams.encodeConfig = &encodeConfig;
    NvEncoderInitParam encodeCLIOptions("-codec h264 -preset p1 -tuninginfo hq -fps 1 -rc vbr -bitrate 10M");
    encoder_->CreateDefaultEncoderParams(&initializeParams, encodeCLIOptions.GetEncodeGUID(), encodeCLIOptions.GetPresetGUID(), encodeCLIOptions.GetTuningInfo());
    encodeCLIOptions.SetInitParams(&initializeParams, eFormat);

    encoder_->CreateEncoder(&initializeParams);
    return true;
}

void NVEncoder::Finalize() {
    encoder_->DestroyEncoder();
}

std::string NVEncoder::getName() const {
    return "nvenc";
}

bool NVEncoder::FillFrame(const CodecFrame& in) {
    if (in.size == 0) {
        // End-of-stream marker: flush the encoder; buffered packets are then
        // drained through GetFrame().
        encoder_->EndEncode(vPacket_);
        return true;
    }
    const NvEncInputFrame* encoderInputFrame = encoder_->GetNextInputFrame();
    NvEncoderCuda::CopyToDeviceFrame(cudaCtx_.get(),
        in.data, 0,
        (CUdeviceptr)encoderInputFrame->inputPtr,
        (int)encoderInputFrame->pitch,
        encoder_->GetEncodeWidth(),
        encoder_->GetEncodeHeight(),
        CU_MEMORYTYPE_HOST,
        encoderInputFrame->bufferFormat,
        encoderInputFrame->chromaOffsets,
        encoderInputFrame->numChromaPlanes);

    encoder_->EncodeFrame(vPacket_);
    return true;
}

bool NVEncoder::GetFrame(CodecFrame& out) {
    if (vPacket_.empty()) {
        return false;
    }
    out.data = vPacket_[0].frame.data();
    out.size = vPacket_[0].frame.size();
    out.width = encoder_->GetEncodeWidth();
    out.height = encoder_->GetEncodeHeight();
    out.format = PixelFormat::Unknown; // encoded elementary stream
    out.release = nullptr;             // vPacket_ owns the data
    vPacket_.erase(vPacket_.begin());
    return true;
}

HALCODEC_CONNECT(Encoder, nvenc, NVEncoder);

} // namespace nvenc
} // namespace halcodec