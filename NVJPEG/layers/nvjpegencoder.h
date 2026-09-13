#ifndef NVJPEGENCODER_H
#define NVJPEGENCODER_H

#include <cuda.h>
#include <nvjpeg.h>
#include <string>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

namespace halcodec {
namespace nvjpeg {

class NVJPEGEncoder : public Encoder {
public:
    NVJPEGEncoder();
    ~NVJPEGEncoder() = default;

    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& input) override;
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;

    std::string getName() const override { return "nvjpegenc"; }

    static int dev_malloc(void **p, size_t s) { return (int)cudaMalloc(p, s); }

    static int dev_free(void *p) { return (int)cudaFree(p); }

    static int host_malloc(void** p, size_t s, unsigned int f) { return (int)cudaHostAlloc(p, s, f); }

    static int host_free(void* p) { return (int)cudaFreeHost(p); }

private:
    std::string format_;
    bool pipeline_ = false;
    nvjpegHandle_t nvjpegHandle_;

    nvjpegEncoderState_t encoderState_;
    nvjpegInputFormat_t inputfmt_;
    nvjpegEncoderParams_t encode_params_;

    cudaStream_t stream_;

    uint8_t* dev_data_ = nullptr;       // single input image uploaded to device
    int img_width_ = 0;
    int img_height_ = 0;
    nvjpegChromaSubsampling_t subsampling_ = NVJPEG_CSS_420;

    bool bitstreamReady_ = false;       // one encoded JPEG pending in GetFrame()

    CUcontext cuContext_ = nullptr;
};

} // namespace nvjpeg
} // namespace halcodec
#endif // NVJPEGENCODER_H