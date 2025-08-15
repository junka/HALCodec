#ifndef NVENC_LAYERS_NVDEVICE_H
#define NVENC_LAYERS_NVDEVICE_H

#include <string>
#include <iostream>
#include <iomanip>
#include <cuda.h>

#include "nvcuvid.h"
#include "cuviddec.h"

#include "device.h"

namespace halcodec {
namespace nvenc {

constexpr const char *kCodecNames[] = {
    "MPEG1", "MPEG2", "MPEG4", "VC1", "H264", "JPEG",
    "H264_SVC", "H264_MVC", "HEVC", "VP8", "VP9", "AV1",
    "YUV420", "YV12", "NV12", "YUYV", "UYVY"
};
constexpr const char *kChromaFormat[] = { "4:0:0", "4:2:0", "4:2:2", "4:4:4" };


class NVDevice : public halcodec::Device{
private:
    void createCudaContext(int idx, unsigned int flags);
    void destroyCudaContext();
public:
    NVDevice() {
        int ret = cuInit(0);
        if (ret != CUDA_SUCCESS) {
            std::cout << "cuInit error" << std::endl;
        }
    };
    ~NVDevice() {
        destroyCudaContext();
    };
    static bool Register() {
        halcodec::Device::RegisterDevice("nvidia", []() {
            return std::make_unique<NVDevice>();
        });
        return true;
    }

    void showDecoderCapability() override;

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

    int getNumDevices() override {
        int ngpu;
        cuDeviceGetCount(&ngpu);
        return ngpu;
    }

    void createCtx(int idx) override {
        id_ = idx;
        createCudaContext(idx, 0);
    }

    void destroyCtx() override {
        destroyCudaContext();
    }

    CUcontext getCtx() {
        return cuContext_;
    }

private:
    CUcontext cuContext_ = nullptr;
    CUdevice cuDevice_ = 0;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVDEVICE_H