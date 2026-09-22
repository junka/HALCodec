// NVJPEG 解码/编码器能力自省("nvjpeg" CapabilityProvider)。
//
// NVJPEG 只有 JPEG 系格式,没有可枚举的 codec 探针 API;该库加载成功
// 本身即代表宿主机提供 CUDA + NVJPEG(NVIDIA GPU),因此能力列表固定为
// JPEG/JPEG-LS 的硬件解码与 JPEG 硬件编码,并列出可用 CUDA 设备。
// NVENC/NVDEC 的视频 codec 能力由 "nvidia"(nvidia_caps.cc) 负责。

#include <cuda_runtime_api.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "capability.h"

namespace halcodec {
namespace nvjpeg {

class NVJPEGCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "nvjpeg"; }

    std::vector<std::string> getDeviceNames() const override {
        std::vector<std::string> names;
        int ngpu = 0;
        if (cudaGetDeviceCount(&ngpu) != cudaSuccess) {
            return names;
        }
        for (int i = 0; i < ngpu; ++i) {
            cudaDeviceProp prop;
            if (cudaGetDeviceProperties(&prop, i) == cudaSuccess) {
                names.emplace_back(prop.name);
            }
        }
        return names;
    }

    void showDecoderCapability() const override {
        int ngpu = 0;
        if (cudaGetDeviceCount(&ngpu) != cudaSuccess || ngpu == 0) {
            std::cout << "  (no CUDA device available)" << std::endl;
            return;
        }
        // NVJPEG 固定支持 JPEG;JPEG-LS 解码自 NVJPEG 2.0+。
        std::cout << "  " << std::left << std::setw(17) << "JPEG"
                  << ": hw decode supported" << std::endl;
        std::cout << "  " << std::left << std::setw(17) << "JPEG-LS"
                  << ": hw decode supported" << std::endl;
    }

    void showEncoderCapability() const override {
        int ngpu = 0;
        if (cudaGetDeviceCount(&ngpu) != cudaSuccess || ngpu == 0) {
            std::cout << "  (no CUDA device available)" << std::endl;
            return;
        }
        std::cout << "  " << std::left << std::setw(17) << "JPEG"
                  << ": hw encode supported" << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, nvjpeg, NVJPEGCapsProvider);

} // namespace nvjpeg
} // namespace halcodec