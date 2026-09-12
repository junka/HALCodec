#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include <fstream>
#include <string>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

#include "cuda_context.h"

#include "NvEncoder/NvEncoderCuda.h"
#include "NvEncoderCLIOptions.h"

namespace halcodec {
namespace nvenc {

class NVEncoder : public halcodec::Encoder {
public:
    NVEncoder() = default;
    ~NVEncoder() = default;
    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& in) override;
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;
    std::string getName() const override;

private:
    CUDAContext cudaCtx_;
    std::unique_ptr<NvEncoderCuda> encoder_;

    std::vector<std::vector<uint8_t>> vPacket_;

    std::ifstream finput_;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H