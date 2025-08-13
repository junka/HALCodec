#ifndef NVENC_LAYERS_NVDEVICE_H
#define NVENC_LAYERS_NVDEVICE_H

#include <string>
#include <iostream>
#include <iomanip>
#include <cuda.h>
#include "nvcuvid.h"
#include "cuviddec.h"

namespace halcodec {
namespace nvenc {

constexpr const char *kCodecNames[] = {
    "MPEG1", "MPEG2", "MPEG4", "VC1", "H264", "JPEG",
    "H264_SVC", "H264_MVC", "HEVC", "VP8", "VP9", "AV1",
    "YUV420", "YV12", "NV12", "YUYV", "UYVY"
};
constexpr const char *kChromaFormat[] = { "4:0:0", "4:2:0", "4:2:2", "4:4:4" };


class NVDevice {
private:
    CUcontext cuContext_ = nullptr;
    CUdevice cuDevice_ = 0;
    int iGpu_ = 0;
    char szDeviceName_[80];
public:
    NVDevice(int iGpu) : iGpu_(iGpu) {
        createCudaContext(0);
    };
    ~NVDevice() {
        destroyCudaContext();
    };
    std::string getName() {
        return szDeviceName_;
    }

public:
    void createCudaContext(unsigned int flags);
    void destroyCudaContext();

    void showDecoderCapability();

    static bool isCodecSupported(cudaVideoCodec codec, cudaVideoChromaFormat chromaFormat, int bitDepth)
    {
        CUVIDDECODECAPS decodeCaps = {};
        decodeCaps.eCodecType = codec;
        decodeCaps.eChromaFormat = chromaFormat;
        decodeCaps.nBitDepthMinus8 = bitDepth - 8;

        CUresult ret = cuvidGetDecoderCaps(&decodeCaps);
        if (ret != CUDA_SUCCESS) {
            std::cout << "cuvidGetDecoderCaps error" << std::endl;
            return false;
        }
        return decodeCaps.bIsSupported;
    }

    CUcontext getCtx() {
        return cuContext_;
    }
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVDEVICE_H