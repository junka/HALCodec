#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include <memory>
#include <vector>

#include "decoder.h"

#include "nvdevice.h"
#include "NvDecoder.h"
#include "FFmpegDemuxer.h"


namespace halcodec {
namespace nvenc {

class NVDecoder : public halcodec::Decoder {

public:
    NVDecoder() = default;

    void Initialize(std::string inputfile) override;
    int FillinFrame() override;
    void Finalize() override;
    uint8_t* GetFrame(int *framesize) override;
    void ReleaseFrame(uint8_t **pFrame) override;
    std::string getName() const override;

    static bool Register() {
        halcodec::Decoder::RegisterDecoder("nvdec", []() {
            return std::make_unique<NVDecoder>();
        });
        return true;
    }

private:
    std::unique_ptr<NVDevice> device_;
    std::unique_ptr<NvDecoder> decoder_;
    std::unique_ptr<FFmpegDemuxer> demuxer_;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H