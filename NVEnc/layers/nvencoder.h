#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include "encoder.h"

namespace halcodec {
namespace nvenc {

class NVEncoder : public halcodec::Encoder {
public:
    NVEncoder();
    ~NVEncoder();

    void Initialize();
    void EncodeFrame();
    void Finalize();

};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H