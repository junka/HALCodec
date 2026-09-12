#include "nvdevice.h"

#include <iostream>
#include <string>

#include "registry.h"

namespace halcodec {
namespace nvenc {

NVDevice::NVDevice() {
    int ret = cuInit(0);
    if (ret != CUDA_SUCCESS) {
        std::cerr << "cuInit error" << std::endl;
    }
}

NVDevice::~NVDevice() {
    destroyCudaContext();
}

void NVDevice::createCudaContext(int idx, unsigned int flags) {
    int ret = cuDeviceGet(&cuDevice_, idx);
    if (ret != CUDA_SUCCESS) {
        std::cerr << "cuDeviceGet error" << std::endl;
        return;
    }
    char szDeviceName[80];
    ret = cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cerr << "cuDeviceGetName error" << std::endl;
        return;
    }
    name_ = std::string(szDeviceName);
    ret = cuCtxCreate(&cuContext_, flags, cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cerr << "cuCtxCreate error" << std::endl;
        return;
    }
}

void NVDevice::destroyCudaContext() {
    if (cuContext_) {
        CUresult ret = cuCtxDestroy(cuContext_);
        if (ret != CUDA_SUCCESS) {
            std::cerr << "cuCtxDestroy error" << std::endl;
        }
        cuContext_ = nullptr;
    }
}

int NVDevice::getNumDevices() const {
    int ngpu = 0;
    cuDeviceGetCount(&ngpu);
    return ngpu;
}

void NVDevice::createCtx(int idx) {
    id_ = idx;
    createCudaContext(idx, 0);
}

void NVDevice::destroyCtx() {
    destroyCudaContext();
}

CUcontext NVDevice::getCtx() const {
    return cuContext_;
}

bool NVDevice::isCodecSupported(cudaVideoCodec codec,
                                cudaVideoChromaFormat chromaFormat,
                                int bitDepth) {
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

HALCODEC_CONNECT(Device, nvidia, NVDevice);

} // namespace nvenc
} // namespace halcodec