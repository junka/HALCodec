#ifndef LAYERS_VTDECODER_H_
#define LAYERS_VTDECODER_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <VideoToolbox/VideoToolbox.h>
#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

namespace halcodec {
namespace vtbox {

class VTDecoder : public Decoder {
public:
    VTDecoder() = default;

    bool Initialize(const CodecParams& params) override;
    int FillInput(const uint8_t* data, size_t size) override;
    bool SignalInputComplete() override;
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    bool isAsync() const override { return true; }
    std::string getName() const override { return "vtbox"; }

private:
    VTDecompressionSessionRef decompressionSession = nullptr;
    CMVideoFormatDescriptionRef formatDescription = nullptr;

    // Queue of decoded frames, filled by the decompression callback and
    // drained by GetFrame(). Guarded by mtx_; cv_ wakes GetFrame().
    std::deque<CodecFrame> frameQ_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool eof_ = false;

    static void DecompressionCallback(void* refcon,
        void* sourceFrameRefCon,
        OSStatus status,
        VTDecodeInfoFlags infoFlags,
        CVPixelBufferRef pixelBuffer,
        CMTime presentationTimeStamp,
        CMTime presentationDuration);

    // Submits one access unit for async decoding. Takes ownership of `data`:
    // it is freed automatically when VideoToolbox finishes with the sample
    // (via the block buffer's custom deallocator), or immediately on failure.
    bool decodeFrameAsync(uint8_t* data, size_t size);
};

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_VTDECODER_H_