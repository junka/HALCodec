#ifndef NVENC_LAYERS_NVDEVICE_H
#define NVENC_LAYERS_NVDEVICE_H

#include <string>

#include <cuda.h>

#include "nvcuvid.h"
#include "cuviddec.h"
#include "nvEncodeAPI.h"

#include "device.h"

namespace halcodec {
namespace nvenc {

constexpr const char *kCodecNames[] = {
    "MPEG1", "MPEG2", "MPEG4", "VC1", "H264", "JPEG",
    "H264_SVC", "H264_MVC", "HEVC", "VP8", "VP9", "AV1",
    "YUV420", "YV12", "NV12", "YUYV", "UYVY"
};
constexpr const char *kChromaFormat[] = { "4:0:0", "4:2:0", "4:2:2", "4:4:4" };

class NVDevice : public halcodec::Device {
public:
    NVDevice();
    ~NVDevice();

    void showDecoderCapability() const override;
    void showEncoderCapability() const override;

    static bool isCodecSupported(cudaVideoCodec codec,
                                 cudaVideoChromaFormat chromaFormat,
                                 int bitDepth);

    int getNumDevices() const override;
    void createCtx(int idx) override;
    void destroyCtx() override;

    CUcontext getCtx() const;

private:
    void createCudaContext(int idx, unsigned int flags);
    void destroyCudaContext();

    CUcontext cuContext_ = nullptr;
    CUdevice cuDevice_ = 0;
};

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVDEVICE_H