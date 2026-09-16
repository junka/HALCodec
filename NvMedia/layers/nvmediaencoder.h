#ifndef NVMEDIA_LAYERS_NVMEDIAENCODER_H
#define NVMEDIA_LAYERS_NVMEDIAENCODER_H

#include <string>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

#include "nvmedia_iep.h"

namespace halcodec {
namespace nvmedia {

// NvMediaIEP hardware encoder backend for NVIDIA DRIVE OS (Linux aarch64).
//
// NOTE: initialization-only stub. A real encode pipeline requires
// NvMediaIEPInit with a reconciled NvSciBufAttrList (input surface) and a
// full encode-init parameter struct, then NvMediaIEPFeedFrame/GetBits.
class NvMediaEncoder : public halcodec::Encoder {
public:
    NvMediaEncoder() = default;

    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& in) override;
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;
    std::string getName() const override;

private:
    NvMediaIEP* encoder_ = nullptr;
};

} // namespace nvmedia
} // namespace halcodec

#endif // NVMEDIA_LAYERS_NVMEDIAENCODER_H