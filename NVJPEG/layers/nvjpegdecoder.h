#ifndef NVJPEGDECODER_H
#define NVJPEGDECODER_H

#include <cuda.h>
#include <nvjpeg.h>
#include <vector>
#include <string>
#include <stdexcept>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

namespace halcodec {
namespace nvjpeg {

class NVJPEGDecoder : public Decoder {
public:
    NVJPEGDecoder();
    ~NVJPEGDecoder() = default;

    bool Initialize(const CodecParams& params) override;

    int PullFrames() override;

    void Finalize() override;

    std::string getName() const override { return "nvjpeg"; }

    bool GetFrame(CodecFrame& out) override;

private:
    void create_decouple_api();
    void destroy_deouple_api();

    static int dev_malloc(void **p, size_t s) { return (int)cudaMalloc(p, s); }

    static int dev_free(void *p) { return (int)cudaFree(p); }

    static int host_malloc(void** p, size_t s, unsigned int f) { return (int)cudaHostAlloc(p, s, f); }

    static int host_free(void* p) { return (int)cudaFreeHost(p); }

private:
    int batch_size_ = 1;
    std::vector<std::string> file_names_;
    std::vector<std::string>::iterator file_iter_;
    bool pipeline_ = false;
    nvjpegHandle_t nvjpegHandle_;
    nvjpegJpegState_t jpegState_;
    nvjpegOutputFormat_t outputfmt_;
    nvjpegJpegDecoder_t decoder_;
    cudaStream_t stream_;

    //decouple
    nvjpegJpegState_t decoupled_state_;
    nvjpegBufferPinned_t pinned_buffers_[2]; // 2 buffers for pipelining
    nvjpegBufferDevice_t device_buffer_;
    nvjpegJpegStream_t  jpeg_streams_[2]; //  2 streams for pipelining
    nvjpegDecodeParams_t decode_params_;

    std::vector<std::vector<char> > data_;
    std::vector<size_t> file_len_;
    std::vector<nvjpegImage_t> out_;
    std::vector<nvjpegImage_t> isz_;

    std::vector<int> img_widths_;
    std::vector<int> img_heights_;

    std::vector<nvjpegChromaSubsampling_t> subsamplings_;

    int num_decoded = 0;

    // Reused host buffer for combined frame output (avoids per-frame malloc).
    uint8_t* combined_host_ = nullptr;
    size_t combined_cap_ = 0;

    CUcontext cuContext_ = nullptr;
};

} // namespace nvjpeg
} // namespace halcodec

#endif // NVJPEGDECODER_H