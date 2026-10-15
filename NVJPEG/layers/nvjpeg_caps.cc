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
#include <utility>
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

    // 结构化解码能力查询。真值来源：nvjpegdecoder.cc 的实际输出路径——
    // NVJPEGDecoder::Initialize 的 outputfmt_ 选择 + GetFrame 的
    // (outputfmt_, 子采样) -> PixelFormat 逐分支映射（host 与 zero-copy 两条
    // 路径一致），不是任何运行时 API——NVJPEG 没有逐组合的能力查询，但该
    // 映射在代码里逐分支确定，可直接照实声明，不依赖运行时 GPU 存在与否。
    //
    // 只声明代码能确证输出格式的组合：
    //   - YUV 输出（默认路径）：4:2:0 -> I420；4:4:4 -> YUV444P。4:2:2 产出
    //     planar 4:2:2 字节，HAL 无对应 PixelFormat（解码层把它标成 YUV444P
    //     但字节不是 4:4:4），故 4:2:2 条目不列任何 YUV 格式；
    //   - RGB/BGR 输出：三平面产出，代码对子采样无条件映射，任意输入类
    //     别均可产出；
    //   - Y 输出：单平面 GRAY，对任意输入类别均成立；
    //   - 灰度输入走 YUV 输出只产出单个 Y 平面却被标成 YUV444P（字节不符），
    //     故灰度条目只声明经确证的 GRAY。
    // 8-bit 以上的 JPEG（12-bit）不在解码器代码路径内，不声明。
    CapsStatus queryDecoderCaps(const std::string& codec,
                                std::vector<DecodeCapsEntry>& out) const override {
        if (codec != "jpeg" && codec != "mjpeg") {
            // NVJPEG 只解 JPEG：对其余 codec 这是权威的"完全不支持"。
            return CapsStatus::Available;  // out 保持为空
        }
        const struct {
            const char* profile;
            int chroma;
            std::vector<PixelFormat> formats;
        } kJpegEntries[] = {
            {"Baseline 4:2:0", 420,
             {PixelFormat::I420, PixelFormat::RGB, PixelFormat::BGR, PixelFormat::GRAY}},
            {"Baseline 4:2:2", 422,
             {PixelFormat::RGB, PixelFormat::BGR, PixelFormat::GRAY}},
            {"Baseline 4:4:4", 444,
             {PixelFormat::YUV444P, PixelFormat::RGB, PixelFormat::BGR, PixelFormat::GRAY}},
            {"Baseline Grayscale", 0, {PixelFormat::GRAY}},
        };
        for (const auto& src : kJpegEntries) {
            DecodeCapsEntry entry;
            entry.profile = src.profile;
            entry.bitDepth = 8;
            entry.chroma = src.chroma;
            entry.outputFormats = src.formats;
            out.push_back(std::move(entry));
        }
        return CapsStatus::Available;
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