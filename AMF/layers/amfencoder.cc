#include "amfencoder.h"

#include <string>

#include <core/PropertyStorageEx.h>
#include <components/VideoEncoderVCE.h>
#include <components/VideoEncoderHEVC.h>
#include <components/VideoEncoderAV1.h>

#include "registry.h"

namespace halcodec {
namespace amd {

// NOTE: frame data path (FillFrame/GetFrame) is not implemented yet; the
// adapter currently registers "amfenc" and brings up a real AMF VCE hardware
// encoder component, mirroring the nvenc stub level.

bool AMFEncoder::Initialize(const CodecParams& params) {
    if (!context_.init(runtime_)) {
        return false;
    }

    // VCE (H.264) component by default; HEVC/AV1 use the HW encoder plugins.
    amf::AMFContext* ctx = context_.get();
    const wchar_t* component = AMFVideoEncoderVCE_AVC;
    if (params.codec == "hevc") {
        component = AMFVideoEncoder_HEVC;
    } else if (params.codec == "av1") {
        component = AMFVideoEncoder_AV1;
    }
    AMF_RESULT res = runtime_.factory()->CreateComponent(ctx, component, &encoder_);
    if (res != AMF_OK) {
        std::cerr << "AMFEncoder: CreateComponent(" << params.codec << ") failed: "
                  << res << std::endl;
        return false;
    }

    // FrameRate default applies to VCE; HEVC/AV1 HW encoders ignore it unless
    // set explicitly. Width/height come from the unified params (mandatory for
    // encode).
    encoder_->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_HIGH_QUALITY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET,
                          AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_MAIN);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, AMFConstructRate(30, 1));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, (amf_int64)(8 * 1000000));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, (amf_int64)(8 * 1000000));

    int width = params.width > 0 ? params.width : 1280;
    int height = params.height > 0 ? params.height : 720;
    if (params.width <= 0 || params.height <= 0) {
        std::cerr << "AMFEncoder: width/height required in CodecParams" << std::endl;
    }
    res = encoder_->Init(amf::AMF_SURFACE_NV12, width, height);
    if (res != AMF_OK) {
        std::cerr << "AMFEncoder: Init failed: " << res << std::endl;
        encoder_->Terminate();
        encoder_->Release();
        encoder_ = nullptr;
        return false;
    }
    std::cout << "AMFEncoder: VCE H.264 component initialized (" << width << "x"
              << height << ")" << std::endl;
    return true;
}

bool AMFEncoder::FillFrame(const CodecFrame&) {
    // Data path TODO: wrap a CodecFrame as an AMFSurface (NV12) and submit via
    // SubmitInput, then drain AMFBitstream output through QueryOutput.
    return false;
}

bool AMFEncoder::GetFrame(CodecFrame&) {
    return false;
}

void AMFEncoder::Finalize() {
    if (encoder_) {
        encoder_->Terminate();
        encoder_->Release();
        encoder_ = nullptr;
    }
    context_.destroy();
}

std::string AMFEncoder::getName() const {
    return "amfenc";
}

// 数据通路未实现(FillFrame/GetFrame 均为 TODO),先不注册,否则 hal_*
// 默认 encoder backend 会静默选中空壳编码器输出 0 帧。caps 仍注册。
// 数据通路落地后再放开。
// HALCODEC_CONNECT(Encoder, amfenc, AMFEncoder);

} // namespace amd
} // namespace halcodec