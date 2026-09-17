#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

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
    ~NVEncoder() override;
    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& in) override;
    bool SignalInputComplete() override;
    bool isAsync() const override { return true; }
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;
    std::string getName() const override;

private:
    // PIMPL holding the worker thread + queues, keeping the public header
    // free of <thread>/<mutex> beyond the includes above.
    class AsyncPipe;
    std::unique_ptr<AsyncPipe> pipe_;

    CUDAContext cudaCtx_;
    std::unique_ptr<NvEncoderCuda> encoder_;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H