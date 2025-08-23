#ifndef LAYERS_VTDECODER_H_
#define LAYERS_VTDECODER_H_ 

#include <memory>
#include <string>

#include <VideoToolbox/VideoToolbox.h>
#include "decoder.h"

namespace halcodec { 
namespace vtbox {

class VTDecoder : public Decoder {
public:
    VTDecoder() = default;

    void Initialize(std::string inputfile) override;
    int FillinFrame() override;
    void Finalize() override;
    uint8_t* GetFrame(int *framesize) override;
    void ReleaseFrame(uint8_t **pFrame) override;
    std::string getName() const override { return "vtbox"; }

    static bool Register() {
        halcodec::Decoder::RegisterDecoder("vtbox", []() {
            return std::make_unique<VTDecoder>();
        });
        return true;
    }
private:
    VTDecompressionSessionRef decompressionSession;
    CMVideoFormatDescriptionRef formatDescription;
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
