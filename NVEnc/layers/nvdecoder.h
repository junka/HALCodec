#ifndef NVENC_LAYERS_NVENCODER_H
#define NVENC_LAYERS_NVENCODER_H

#include "nvdevice.h"
#include "decoder.h"
#include "NvDecoder.h"

namespace halcodec {
namespace nvenc {

class NVDecoder : public NvDecoder, public halcodec::Decoder {
private:
    NVDevice device_;
public:
    NVDecoder() = delete;
    ~NVDecoder();

    void Initialize();
    void FillinFrame();
    void Finalize();
    void GetFrame();

};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVENCODER_H