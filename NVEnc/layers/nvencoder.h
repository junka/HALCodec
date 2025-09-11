#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include <fstream>
#include <string>

#include "encoder.h"
#include "nvdevice.h"

#include "NvEncoder/NvEncoderCuda.h"
#include "NvEncoderCLIOptions.h"

namespace halcodec {
namespace nvenc {

class NVEncoder : public halcodec::Encoder {
public:
    NVEncoder();
    ~NVEncoder() = default;
    void Initialize(std::string input, std::string format) override;
    void Finalize() override;

    std::string getName() const override {return "nvenc";}


    int FillData() override;

    uint8_t* GetFrame(int *framesize, int *height, int *width, int *n_chan) override;

    void ReleaseFrame(uint8_t **pFrame) override;

    static bool Register() {
        halcodec::Encoder::RegisterEncoder("nvenc", []() {
            return std::make_unique<NVEncoder>();
        });
        return true;
    }
private:
    std::unique_ptr<NVDevice> device_;
    std::unique_ptr<NvEncoderCuda> encoder_;

    std::vector<std::vector<uint8_t>> vPacket_;

    std::ifstream finput_;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H