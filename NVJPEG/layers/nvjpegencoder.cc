#include "nvjpegencoder.h"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <nvjpeg.h>

#include "frame.h"
#include "registry.h"

namespace halcodec {
namespace nvjpeg {

NVJPEGEncoder::NVJPEGEncoder() {
    int ret = cuInit(0);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuInit error" << std::endl;
    }
}


bool NVJPEGEncoder::Initialize(const CodecParams& params) {
    std::string input = params.inputs.empty() ? "" : params.inputs[0];

    // Map the unified input format; raw images are delivered per-frame via
    // FillFrame(), not read from an input path.
    format_ = "yuv";
    switch (params.inputFormat) {
        case PixelFormat::RGB: format_ = "rgb"; break;
        case PixelFormat::GRAY: format_ = "yuv"; break; // grey input, subsampling decided per image
        case PixelFormat::BGR:
            format_ = (input.size() > 4 && input.substr(input.size() - 4) == ".bmp")
                          ? "bmp" : "bgr";
            break;
        default: break;
    }

    CUdevice cuDevice_ = 0;
    int idx = 0;
    int ret = cuDeviceGet(&cuDevice_, idx);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGet error" << std::endl;
        return false;
    }
    char szDeviceName[80];
    ret = cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGetName error" << std::endl;
        return false;
    }
    ret = cuCtxCreate(&cuContext_, 0, cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuCtxCreate error" << std::endl;
        return false;
    }
    // Create the CUDA stream BEFORE any nvjpeg call: nvjpegCreateEx and the
    // state/params creation below all reference stream_, so it must be valid
    // first. (Previously this was at the end of Initialize, leaving stream_
    // uninitialized during nvjpeg setup — undefined behavior / segfault.)
    ret = cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking);
    if (ret != cudaSuccess) {
        std::cerr << "Fail to create cuda stream" << std::endl;
        return false;
    }
    cudaDeviceProp props;
    ret = cudaGetDeviceProperties(&props, 0);
    if (ret != cudaSuccess) {
        std::cout << "get device properties error" << std::endl;
        return false;
    }
    printf("Using GPU %d (%s, %d SMs, %d th/SM max, CC %d.%d, ECC %s)\n",
         0, props.name, props.multiProcessorCount,
         props.maxThreadsPerMultiProcessor, props.major, props.minor,
         props.ECCEnabled ? "on" : "off");
    nvjpegDevAllocator_t dev_allocator = {&dev_malloc, &dev_free};
    nvjpegPinnedAllocator_t pinned_allocator ={&host_malloc, &host_free};
    ret = nvjpegCreateEx(NVJPEG_BACKEND_GPU_HYBRID, &dev_allocator,
            &pinned_allocator, 0,  &nvjpegHandle_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegCreateEx %d\n", ret);
    }

    ret = nvjpegEncoderStateCreate(nvjpegHandle_, &encoderState_, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderStateCreate %d\n", ret);
        return false;
    }
    ret = nvjpegEncoderParamsCreate(nvjpegHandle_, &encode_params_, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsCreate %d\n", ret);
        return false;
    }
    ret = nvjpegEncoderParamsSetQuality(encode_params_, 75, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetQuality %d\n", ret);
        return false;
    }
    ret = nvjpegEncoderParamsSetEncoding(encode_params_, NVJPEG_ENCODING_BASELINE_DCT, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetEncoding %d\n", ret);
        return false;
    }
    ret = nvjpegEncoderParamsSetOptimizedHuffman(encode_params_, 1, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetOptimizedHuffman %d\n", ret);
        return false;
    }
    ret = nvjpegEncoderParamsSetSamplingFactors(encode_params_, NVJPEG_CSS_420, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetSamplingFactors %d\n", ret);
        return false;
    }

    return true;
}

void NVJPEGEncoder::Finalize() {
    if (dev_data_) {
        cudaFree(dev_data_);
        dev_data_ = nullptr;
    }
    cudaStreamDestroy(stream_);
    nvjpegEncoderParamsDestroy(encode_params_);
    nvjpegEncoderStateDestroy(encoderState_);
    nvjpegDestroy(nvjpegHandle_);
    cuCtxDestroy(cuContext_);
}

bool NVJPEGEncoder::FillFrame(const CodecFrame& in) {
    // EOS marker is a zero-size HOST frame; device frames legitimately have
    // size==0 (host size unknown until downloaded), so the locality check keeps
    // them from being swallowed as end-of-stream.
    if (in.size == 0 && in.locality == FrameLocality::Host) {
        // End-of-stream marker: nvjpeg encodes per image, nothing to flush.
        return false;
    }
    if (in.width <= 0 || in.height <= 0) {
        std::cerr << "NVJPEGEncoder: frame has no valid dimensions" << std::endl;
        return false;
    }

    if (cudaStreamSynchronize(stream_) != cudaSuccess) {
        std::cerr << "Fail to sync cuda stream" << std::endl;
        return false;
    }

    img_width_ = in.width;
    img_height_ = in.height;

    const bool deviceSrc = in.locality == FrameLocality::CudaDevice;
    nvjpegImage_t imgdesc;
    if (deviceSrc) {
        // Zero-copy: the frame is already in device memory. nvjpeg's encoder
        // wants a per-plane nvjpegImage_t; a multi-plane CudaDevice frame
        // (nvjpeg decoder output, Y/U/V as separate allocations) maps directly
        // onto it with no copy. A single-block CudaDevice frame (NVDEC's
        // contiguous NV12) cannot be fed to nvjpegEncodeYUV without first
        // splitting it into planes, which is not supported here.
        if (in.device.cudaNumPlanes <= 0) {
            std::cerr << "NVJPEGEncoder: single-block device frame not supported "
                         "(nvjpeg needs per-plane layout)" << std::endl;
            return false;
        }
        for (int c = 0; c < in.device.cudaNumPlanes; c++) {
            imgdesc.channel[c] = reinterpret_cast<unsigned char*>(
                in.device.cudaPlanes[c]);
            imgdesc.pitch[c] = in.device.cudaPitches[c];
        }
        for (int c = in.device.cudaNumPlanes; c < NVJPEG_MAX_COMPONENT; c++) {
            imgdesc.channel[c] = nullptr;
            imgdesc.pitch[c] = 0;
        }
        dev_data_ = nullptr;  // borrowing the decoder's buffers, not allocating
    } else {
        // Host path: upload the whole image as-is (yuv/rgb/bgr/rgbi/bgri).
        cudaError_t err = cudaMalloc((void**)&dev_data_, in.size);
        if (err != cudaSuccess) {
            std::cout << "error for cuda malloc" << std::endl;
            return false;
        }
        err = cudaMemcpy(dev_data_, in.data, in.size, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            cudaFree(dev_data_);
            dev_data_ = nullptr;
            std::cout << "error for cuda copy" << std::endl;
            return false;
        }
        imgdesc = {
            {
                dev_data_,
                dev_data_ + (size_t)img_width_ * img_height_,
                dev_data_ + (size_t)img_width_ * img_height_ * 3 / 2,
                dev_data_ + (size_t)img_width_ * img_height_ * 3
            },
            {
                (unsigned int)((inputfmt_ == NVJPEG_INPUT_RGBI || inputfmt_ == NVJPEG_INPUT_BGRI) ? img_width_ * 3 : img_width_),
                (unsigned int)img_width_,
                (unsigned int)img_width_,
                (unsigned int)img_width_
            }
        };
    }

    // Infer chroma subsampling. For host frames the payload size disambiguates
    // 420/422/444/...; for device frames size is 0, so derive from the pixel
    // format instead.
    subsampling_ = NVJPEG_CSS_420;
    if (deviceSrc) {
        switch (in.format) {
            case PixelFormat::YUV444P: subsampling_ = NVJPEG_CSS_444; break;
            case PixelFormat::I420:    subsampling_ = NVJPEG_CSS_420; break;
            case PixelFormat::GRAY:    subsampling_ = NVJPEG_CSS_GRAY; break;
            default: break;
        }
    } else if (in.size == (size_t)img_height_ * img_width_ * 3) {
        subsampling_ = NVJPEG_CSS_444;
    } else if (in.size == (size_t)img_height_ * img_width_ * 2) {
        subsampling_ = NVJPEG_CSS_440;
    } else if (in.size == (size_t)img_height_ * img_width_ * 3 / 2) {
        subsampling_ = NVJPEG_CSS_420;
    } else if (in.size == (size_t)img_height_ * img_width_ * 5 / 4) {
        subsampling_ = NVJPEG_CSS_411;
    } else if (in.size == (size_t)img_height_ * img_width_ * 9 / 8) {
        subsampling_ = NVJPEG_CSS_410;
    }

    //default bmp file should be BGR and 444
    if (format_ == "bmp" || format_ == "bgri") {
        inputfmt_ = NVJPEG_INPUT_BGRI;
    } else if (format_ == "rgbi") {
        inputfmt_ = NVJPEG_INPUT_RGBI;
    } else if (format_ == "rgb") {
        inputfmt_ = NVJPEG_INPUT_RGB;
    } else if (format_ == "bgr") {
        inputfmt_ = NVJPEG_INPUT_BGR;
    }

    if (cudaDeviceSynchronize() != cudaSuccess) {
        return false;
    }

    // Apply the inferred subsampling and encode the single image.
    nvjpegStatus_t ret =
        nvjpegEncoderParamsSetSamplingFactors(encode_params_, subsampling_, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        std::cout << "fail to nvjpegEncoderParamsSetSamplingFactors " << ret << std::endl;
        return false;
    }
    if (format_ == "yuv") {
        ret = nvjpegEncodeYUV(nvjpegHandle_, encoderState_, encode_params_, &imgdesc,
                              subsampling_, img_width_, img_height_, stream_);
    } else {
        ret = nvjpegEncodeImage(nvjpegHandle_, encoderState_, encode_params_, &imgdesc,
                                inputfmt_, img_width_, img_height_, stream_);
    }
    if (ret != NVJPEG_STATUS_SUCCESS) {
        std::cout << "fail to nvjpegEncode "
                  << (format_ == "yuv" ? "YUV" : "Image") << " " << ret << std::endl;
        return false;
    }
    bitstreamReady_ = true;
    return true;
}

bool NVJPEGEncoder::GetFrame(CodecFrame& out) {
    if (!bitstreamReady_) {
        return false;
    }
    size_t total_size = 0;
    nvjpegStatus_t ret = nvjpegEncodeRetrieveBitstream(nvjpegHandle_, encoderState_,
                                                       nullptr, &total_size, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        std::cout << "fail to retrieve bitstream for probe " << ret << std::endl;
        return false;
    }
    uint8_t* combined_frame = static_cast<uint8_t*>(malloc(total_size));
    ret = nvjpegEncodeRetrieveBitstream(nvjpegHandle_, encoderState_, combined_frame,
                                        &total_size, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        free(combined_frame);
        std::cout << "fail to retrieve bitstream" << ret << std::endl;
        return false;
    }
    out.data = combined_frame;
    out.size = total_size;
    out.width = img_width_;
    out.height = img_height_;
    out.format = PixelFormat::Unknown; // JPEG bitstream, not a raw pixel format
    out.release = [combined_frame]() {
        free(combined_frame);
    };
    bitstreamReady_ = false;
    return true;
}

HALCODEC_CONNECT(Encoder, nvjpegenc, NVJPEGEncoder);

} // namespace nvjpeg
} // namespace halcodec