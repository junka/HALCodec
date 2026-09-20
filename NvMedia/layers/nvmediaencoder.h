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
#include "nvmedia_common_encode.h"

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

    // Zero-copy fast path: encode a caller-owned device frame directly. The
    // external NvSciBufObj (produced by e.g. the NvMedia zero-copy decoder) is
    // registered with IEP on first use and fed without the CPU stage buffer /
    // NvSciBufObjPutPixels upload. Returns false if the surface is rejected —
    // the caller then falls back to the host path. The frame stays owned by the
    // caller (same contract as the host path): FeedDeviceFrame waits for the
    // encode's EOF fence before returning, so the caller can release it
    // immediately afterwards.
    bool FeedDeviceFrame(const CodecFrame& in);

    NvMediaIEP* encoder_ = nullptr;
    NvMediaIEPType iepType_ = NVMEDIA_IMAGE_ENCODE_H264;
    NvMediaEncoderInstanceId instanceId_ = NVMEDIA_ENCODER_INSTANCE_0;
    NvSciBufModule bufModule_ = nullptr;
    NvSciSyncModule syncModule_ = nullptr;
    NvSciSyncCpuWaitContext cpuWaitContext_ = nullptr;
    NvSciSyncObj eofSyncObj_ = nullptr;
    NvSciSyncObj preSyncObj_ = nullptr;
    std::vector<Surface> pool_;
    // External (borrowed) NvSciBufObjs registered with IEP for the zero-copy
    // path. Registration is per-object and expensive, so it happens once per
    // distinct buffer; the objects are owned by the producer, so Finalize()
    // only unregisters them (never frees).
    std::vector<NvSciBufObj> externalBufs_;
    uint32_t width_ = 0;    // 16-byte aligned coded width
    uint32_t height_ = 0;   // 16-byte aligned coded height
    // Real geometry of the allocated input surfaces, read back from the
    // reconciled NvSciBufObj after the pool is built. The allocator pads each
    // plane's pitch (320 -> 512 here), and NvSciBufObjPutPixels rejects a
    // plane whose srcPtrSizes entry is below pitch * aligned height, so the
    // host upload must use these rather than width_/height_.
    uint32_t planePitch_[2] = {0, 0};  // [0] luma, [1] chroma, in bytes
    uint32_t planeHeight_[2] = {0, 0}; // [0] luma, [1] chroma, aligned rows
    uint32_t surfaceLumaW_ = 0;        // reported luma plane width
    uint32_t surfaceLumaH_ = 0;        // reported luma plane height
    // HW-aligned plane heights read back from the reconciled surface. The
    // allocator pads each plane's height (240 -> 256 luma, 120 -> 128 chroma),
    // and NvSciBufObjPutPixels rejects a srcPtrSizes entry smaller than the
    // aligned plane, so the staging buffer must cover the full aligned height
    // even though the encoder only consumes the coded region.
    uint32_t alignedHeight_[2] = {0, 0};
    uint32_t frameRateNum_ = 30;
    uint32_t frameRateDen_ = 1;
    // Rate-control parameters configured via NvMediaIEPSetConfiguration and
    // mirrored into each per-frame NvMediaEncodePicParamsH264.rcParams. The
    // image_encoder sample populates picParams.rcParams from the config on
    // every frame; an all-zero rcParams with CBR configured makes FeedFrame
    // return NVMEDIA_STATUS_ERROR on this DRIVE build.
    NvMediaEncodeRCParams rcParams_{};
    uint32_t nextSurface_ = 0;  // round-robin cursor over pool_
    int64_t frameIndex_ = 0;
    std::deque<CodecFrame> outQueue_;
    bool eof_ = false;
    bool initialized_ = false;
};

} // namespace nvmedia
} // namespace halcodec

#endif // NVMEDIA_LAYERS_NVMEDIAENCODER_H