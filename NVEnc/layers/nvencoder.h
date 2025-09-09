#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include "encoder.h"

namespace halcodec {
namespace nvenc {

class NVEncoder : public halcodec::Encoder {
public:
    NVEncoder();
    ~NVEncoder() = default;

    // void Initialize(std::string input, std::string format) override;
    // void EncodeFrame() override;
    // void Finalize() override;

};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H