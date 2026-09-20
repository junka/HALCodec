#ifndef NVENC_LAYERS_NVDECODER_H
#define NVENC_LAYERS_NVDECODER_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
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
    ~NVDecoder() override;

    bool Initialize(const CodecParams& params) override;
    int FillInput(const uint8_t* data, size_t size) override;
    bool SignalInputComplete() override;
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    bool isAsync() const override;
    std::string getName() const override;

private:
    // PIMPL holds the worker thread + synchronization so the header's public
    // surface stays minimal. Only needed in feedMode (async path).
    class AsyncFeed;
    std::unique_ptr<AsyncFeed> feed_;

    CUDAContext cudaCtx_;
    std::unique_ptr<NvDecoder> decoder_;
    std::unique_ptr<FFmpegDemuxer> demuxer_;

    // Lifetime bookkeeping for the NVDEC device frames handed out in zero-copy
    // mode (defined in the .cc). NvDecoder::GetLockedFrame() takes a buffer out
    // of the SDK's frame pool and ~NvDecoder only frees what is still pooled,
    // so a checked-out device frame is owned by nobody until it is given back.
    // This pool is that owner. Held by shared_ptr because every emitted frame's
    // release() captures it: it must outlive the frames the application holds.
    // Declared after cudaCtx_ so it is destroyed before the context it may free
    // buffers with.
    class DeviceFramePool;
    std::shared_ptr<DeviceFramePool> framePool_;

    // True when constructed without an input file: the caller feeds compressed
    // data via FillInput() (see Initialize()). In this mode the decoder runs
    // asynchronously (isAsync() == true) with an internal worker thread.
    bool feedMode_ = false;

    // True when params.zeroCopy asked for device-resident output: NvDecoder is
    // built with bUseDeviceFrame/bDeviceFramePitched and GetFrame() emits
    // FrameLocality::CudaDevice frames (pitched, no host copy) instead of
    // malloc'd host frames. Consumers either feed them straight to a
    // device-frame encoder (nvenc) or call DownloadToHost() on demand.
    bool zeroCopy_ = false;

    // Shared tail of the device-frame emit path (feed and sync modes alike):
    // describes `locked` as a pitched CUDA device frame with a release() that
    // returns it to the pool.
    void EmitDeviceFrame(CodecFrame& out, uint8_t* locked, int64_t pts) const;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVDECODER_H