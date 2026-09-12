#include "amfdecoder.h"

#include <cstring>
#include <string>

#include <core/Buffer.h>
#include <core/PropertyStorageEx.h>
#include <components/VideoDecoderUVD.h>

#include "registry.h"

namespace halcodec {
namespace amd {

// NOTE: frame data path (FillinFrame/GetFrame) is not implemented yet; the
// adaper currently registers "amfdec" and brings up a real AMF hardware
// decoder component, mirroring the nvenc stub level.

bool AMFDecoder::Initialize(const CodecParams& params) {
    if (params.inputs.empty()) {
        std::cerr << "AMFDecoder: no input specified" << std::endl;
        return false;
    }
    if (!context_.init(runtime_)) {
        return false;
    }

    // UVD decoder component, selected from the unified codec field. H.264 is
    // the only constant exported by the 1.4.36 headers; the HEVC/VP9/AV1
    // component IDs are the stable AMD plugin registration names (same ones
    // ffmpeg's amf decoder uses).
    amf::AMFContext* ctx = context_.get();
    const wchar_t* component = AMFVideoDecoderUVD_H264_AVC;
    if (params.codec == "jpeg") {
        component = AMFVideoDecoderUVD_MJPEG;
    } else if (params.codec == "hevc") {
        component = L"AMFVideoDecoderUVD_HEVC";
    } else if (params.codec == "av1") {
        component = L"AMFVideoDecoderUVD_AV1";
    } else if (params.codec == "vp9") {
        component = L"AMFVideoDecoderUVD_VP9";
    }
    AMF_RESULT res = runtime_.factory()->CreateComponent(ctx, component, &decoder_);
    if (res != AMF_OK) {
        std::cerr << "AMFDecoder: CreateComponent(" << params.codec << ") failed: "
                  << res << std::endl;
        return false;
    }

    // Feed SPS/PPS from the unified extradata when present (same channel the
    // vtbox backend consumes). AMF accepts AVCC (length-prefixed) or Annex-B.
    if (!params.extradata.empty()) {
        amf::AMFBuffer* extra = nullptr;
        res = ctx->AllocBuffer(amf::AMF_MEMORY_HOST, params.extradata.size(), &extra);
        if (res != AMF_OK) {
            std::cerr << "AMFDecoder: alloc extradata buffer failed: " << res << std::endl;
            decoder_->Terminate();
            decoder_->Release();
            decoder_ = nullptr;
            return false;
        }
        std::memcpy(extra->GetNative(), params.extradata.data(), params.extradata.size());
        res = decoder_->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, extra);
        extra->Release();
        if (res != AMF_OK) {
            std::cerr << "AMFDecoder: set extradata failed: " << res << std::endl;
            decoder_->Terminate();
            decoder_->Release();
            decoder_ = nullptr;
            return false;
        }
    }

    // Dimension hints (0, 0) — the decoder derives them from the stream.
    res = decoder_->Init(amf::AMF_SURFACE_NV12, 0, 0);
    if (res != AMF_OK) {
        std::cerr << "AMFDecoder: Init failed: " << res << std::endl;
        decoder_->Terminate();
        decoder_->Release();
        decoder_ = nullptr;
        return false;
    }
    std::cout << "AMFDecoder: UVD H.264 component initialized" << std::endl;
    return true;
}

int AMFDecoder::FillinFrame() {
    // Data path TODO: demux params.inputs[0], feed AMFBytes/AMFDataStream into
    // the decoder via SubmitInput, and drain via QueryOutput.
    return 0;
}

void AMFDecoder::Finalize() {
    if (decoder_) {
        decoder_->Terminate();
        decoder_->Release();
        decoder_ = nullptr;
    }
    context_.destroy();
}

bool AMFDecoder::GetFrame(CodecFrame&) {
    return false;
}

std::string AMFDecoder::getName() const {
    return "amfdec";
}

HALCODEC_CONNECT(Decoder, amfdec, AMFDecoder);

} // namespace amd
} // namespace halcodec