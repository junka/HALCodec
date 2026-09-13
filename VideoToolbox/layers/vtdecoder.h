#ifndef LAYERS_VTDECODER_H_
#define LAYERS_VTDECODER_H_

#include <memory>
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
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    std::string getName() const override { return "vtbox"; }

private:
    VTDecompressionSessionRef decompressionSession = nullptr;
    CMVideoFormatDescriptionRef formatDescription = nullptr;

    static void DecompressionCallback(void* refcon,
        void* sourceFrameRefCon,
        OSStatus status,
        VTDecodeInfoFlags infoFlags,
        CVPixelBufferRef pixelBuffer,
        CMTime presentationTimeStamp,
        CMTime presentationDuration);

    bool decodeFrame(const uint8_t* data, size_t size);
};

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_VTDECODER_H_