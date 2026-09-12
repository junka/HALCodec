#ifndef NVENC_LAYERS_NVDECODER_H
#define NVENC_LAYERS_NVDECODER_H

#include <memory>
#include <vector>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

#include "cuda_context.h"
#include "NvDecoder.h"
#include "FFmpegDemuxer.h"

namespace halcodec {
namespace nvenc {

class NVDecoder : public halcodec::Decoder {
public:
    NVDecoder() = default;

    bool Initialize(const CodecParams& params) override;
    int FillinFrame() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    std::string getName() const override;

private:
    CUDAContext cudaCtx_;
    std::unique_ptr<NvDecoder> decoder_;
    std::unique_ptr<FFmpegDemuxer> demuxer_;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVDECODER_H