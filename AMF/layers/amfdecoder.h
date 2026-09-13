#ifndef AMF_LAYERS_AMFDECODER_H
#define AMF_LAYERS_AMFDECODER_H

#include <memory>
#include <string>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

#include "amf_common.h"

namespace halcodec {
namespace amd {

class AMFDecoder : public halcodec::Decoder {
public:
    AMFDecoder() = default;

    bool Initialize(const CodecParams& params) override;
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    std::string getName() const override;

private:
    AMFRuntime runtime_;
    AMFContextHelper context_;
    amf::AMFComponent* decoder_ = nullptr;
};

} // namespace amd
} // namespace halcodec

#endif // AMF_LAYERS_AMFDECODER_H