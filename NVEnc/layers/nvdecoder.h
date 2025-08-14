#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include <memory>
#include <vector>

#include "nvdevice.h"
#include "decoder.h"
#include "NvDecoder.h"

namespace halcodec {
namespace nvenc {

class NVDecoder : public halcodec::Decoder {

public:
    NVDecoder() = default;

    friend std::unique_ptr<halcodec::Decoder> halcodec::Decoder::Create(const std::string&); 

    void Initialize() override;
    void FillinFrame(const std::vector<uint8_t>& rawData) override;
    void Finalize() override;
    void GetFrame() override;
    std::string getName() const override;


    static bool Register() {
        halcodec::Decoder::RegisterDecoder("nvdec", []() {
            return std::make_unique<NVDecoder>();
        });
        return true;
    }

private:
    NVDevice device_;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H