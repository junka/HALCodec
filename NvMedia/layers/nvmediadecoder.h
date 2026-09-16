#ifndef NVMEDIA_LAYERS_NVMEDIADECODER_H
#define NVMEDIA_LAYERS_NVMEDIADECODER_H

#include <string>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

#include "nvmedia_ide.h"

namespace halcodec {
namespace nvmedia {

// NvMediaIDE hardware decoder backend for NVIDIA DRIVE OS (Linux aarch64).
//
// NOTE: initialization-only stub. The data path (parser + surface pool +
// NvMediaIDEDecoderRender) is not implemented yet; PullFrames/GetFrame keep
// the synchronous stub behavior and report an empty stream.
class NvMediaDecoder : public halcodec::Decoder {
public:
    NvMediaDecoder() = default;

    bool Initialize(const CodecParams& params) override;
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    std::string getName() const override;

private:
    NvMediaIDE* decoder_ = nullptr;
};

} // namespace nvmedia
} // namespace halcodec

#endif // NVMEDIA_LAYERS_NVMEDIADECODER_H