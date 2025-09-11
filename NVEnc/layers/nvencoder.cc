#include <vector>
#include "nvencoder.h"

#include <regex>

namespace halcodec {
namespace nvenc {


NVEncoder::NVEncoder() {
}

void NVEncoder::Initialize(std::string input, std::string format) {
    finput_.open(input, std::ifstream::in | std::ifstream::binary);
    if (!finput_) {
        std::ostringstream err;
        err << "Unable to open input file: " << input << std::endl;
        throw std::invalid_argument(err.str());
    }
    device_ = std::make_unique<NVDevice>();
    if (!device_) {
        std::cout << "unable to create device" << std::endl;
        return;
    }
    int width;
    int height;
    std::regex pattern(R"((\d+)[xX](\d+))");
    std::smatch match;
    if (std::regex_search(input, match, pattern)) {
        width = std::stoi(match[1].str());
        height = std::stoi(match[2].str());
    } else {
        std::cerr << "Failed to match resolution in: " << input << std::endl;
    }
    device_->createCtx(0);
    auto cudaCtx = device_->getCtx();
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
}

void NVEncoder::Finalize() {
    encoder_->DestroyEncoder();
    finput_.close();
}

int NVEncoder::FillData() {
    int nFrameSize = encoder_->GetFrameSize();
#if NVENCAPI_MAJOR_VERSION > 12
    uint32_t enableMVHEVC = encoder_->IsMVHEVC();
    if (enableMVHEVC) {
        nFrameSize = nFrameSize << 1;
    }
#endif
    std::unique_ptr<uint8_t[]> pHostFrame(new uint8_t[nFrameSize]);
    int viewID = 0;
    int nFrame = 0;
    printf("frame size %d\n", nFrameSize);
    std::streamsize nRead = finput_.read(reinterpret_cast<char*>(pHostFrame.get()), nFrameSize).gcount();
    if (nRead == nFrameSize) {
        const NvEncInputFrame* encoderInputFrame = encoder_->GetNextInputFrame();
        NvEncoderCuda::CopyToDeviceFrame(device_->getCtx(),
            pHostFrame.get() + viewID * nFrameSize, 0,
            (CUdeviceptr)encoderInputFrame->inputPtr,
            (int)encoderInputFrame->pitch,
            encoder_->GetEncodeWidth(),
            encoder_->GetEncodeHeight(),
            CU_MEMORYTYPE_HOST,
            encoderInputFrame->bufferFormat,
            encoderInputFrame->chromaOffsets,
            encoderInputFrame->numChromaPlanes);

        encoder_->EncodeFrame(vPacket_);
        printf("EncodeFrame\n");
    } else {
        printf("end\n");
        encoder_->EndEncode(vPacket_);
    }
    nFrame += (int)vPacket_.size();
    return nFrame;
}

uint8_t* NVEncoder::GetFrame(int *framesize, int *height, int *width, int *n_chan) {
    int idx = 0;
    *width = encoder_->GetEncodeWidth();
    *height = encoder_->GetEncodeHeight();
    *n_chan = 3;
    printf("w %d, h %d\n", *width, *height);
    return vPacket_[idx].data();
}

void NVEncoder::ReleaseFrame(uint8_t **pFrame) {

}


static bool registered = []() -> bool {
    NVEncoder::Register();
    return true;
}();


} // namespace layers
} // namespace nvenc
