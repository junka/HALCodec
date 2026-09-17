#ifndef NVMEDIA_LAYERS_NVMEDIAENCODER_H
#define NVMEDIA_LAYERS_NVMEDIAENCODER_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

#include "nvmedia_iep.h"

namespace halcodec {
namespace nvmedia {

// NvMediaIEP hardware encoder backend for NVIDIA DRIVE OS (Linux aarch64).
//
// Synchronous model: FillFrame() uploads the raw semi-planar YUV frame into a
// pooled NvSciBufObj surface, submits it with NvMediaIEPFeedFrame(), waits on
// the per-frame EOF fence, then drains the pending encoded bytes into an
// internal packet queue consumed by GetFrame(). The end of the stream is
// signaled by a zero-size frame, which drains the encoder until
// NVMEDIA_STATUS_NONE_PENDING.
class NvMediaEncoder : public halcodec::Encoder {
public:
    NvMediaEncoder() = default;

    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& in) override;
    bool GetFrame(CodecFrame& out) override;
    bool SignalInputComplete() override { return true; }
    void Finalize() override;
    std::string getName() const override;

private:
    struct Surface {
        NvSciBufObj buf = nullptr;
        NvSciSyncFence eofFence{};  // EOF fence of the encode using this buffer
        bool pending = false;       // encode submitted, fence not yet consumed
    };

    bool SetupSync();
    bool Drain();

    NvMediaIEP* encoder_ = nullptr;
    NvMediaIEPType iepType_ = NVMEDIA_IMAGE_ENCODE_H264;
    NvMediaEncoderInstanceId instanceId_ = NVMEDIA_ENCODER_INSTANCE_0;
    NvSciBufModule bufModule_ = nullptr;
    NvSciSyncModule syncModule_ = nullptr;
    NvSciSyncCpuWaitContext cpuWaitContext_ = nullptr;
    NvSciSyncObj eofSyncObj_ = nullptr;
    NvSciSyncObj preSyncObj_ = nullptr;
    std::vector<Surface> pool_;
    uint32_t width_ = 0;    // 16-byte aligned coded width
    uint32_t height_ = 0;   // 16-byte aligned coded height
    uint32_t frameRateNum_ = 30;
    uint32_t frameRateDen_ = 1;
    uint32_t nextSurface_ = 0;  // round-robin cursor over pool_
    int64_t frameIndex_ = 0;
    std::deque<CodecFrame> outQueue_;
    bool eof_ = false;
    bool initialized_ = false;
};

} // namespace nvmedia
} // namespace halcodec

#endif // NVMEDIA_LAYERS_NVMEDIAENCODER_H