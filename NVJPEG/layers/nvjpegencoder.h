#ifndef NVJPEGENCODER_H
#define NVJPEGENCODER_H

#include <cuda.h>
#include <nvjpeg.h>
#include <vector>
#include <string>
#include <memory>

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
    int batch_size_ = 1;
    std::string format_;
    std::vector<std::string> file_names_;
    std::vector<std::string>::iterator file_iter_;
    bool pipeline_ = false;
    nvjpegHandle_t nvjpegHandle_;

    nvjpegEncoderState_t encoderState_;
    nvjpegInputFormat_t inputfmt_;

    cudaStream_t stream_;

    //decouple
    nvjpegJpegState_t decoupled_state_;
    nvjpegBufferPinned_t pinned_buffers_[2]; // 2 buffers for pipelining
    nvjpegBufferDevice_t device_buffer_;
    nvjpegJpegStream_t  jpeg_streams_[2]; //  2 streams for pipelining
    nvjpegEncoderParams_t encode_params_;


    std::vector<uint8_t *> dev_data_;
    std::vector<size_t> file_len_;
    std::vector<nvjpegImage_t> out_;
    std::vector<nvjpegImage_t> isz_;

    std::vector<int> img_widths_;
    std::vector<int> img_heights_;

    std::vector<nvjpegChromaSubsampling_t> subsamplings_;

    int num_decoded = 0;

    CUcontext cuContext_ = nullptr;
};

} // namespace nvjpeg
} // namespace halcodec
#endif // NVJPEGENCODER_H