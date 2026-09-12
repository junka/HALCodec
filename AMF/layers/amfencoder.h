#ifndef AMF_LAYERS_AMFENCODER_H
#define AMF_LAYERS_AMFENCODER_H

#include <memory>
#include <string>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

#include "amf_common.h"

namespace halcodec {
namespace amd {

class AMFEncoder : public halcodec::Encoder {
public:
    AMFEncoder() = default;

    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& input) override;
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;
    std::string getName() const override;

private:
    AMFRuntime runtime_;
    AMFContextHelper context_;
    amf::AMFComponent* encoder_ = nullptr;
};

} // namespace amd
} // namespace halcodec

#endif // AMF_LAYERS_AMFENCODER_H