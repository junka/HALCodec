// Capability introspection for the "nvmedia" backend (NVIDIA DRIVE OS).
//
// NvMedia has no host-side device enumeration API; the DRIVE OS SoC is a
// fixed hardware target. Decoder/encoder surfaces are reported statically:
// the NvMediaIDE decode engine and the NvMediaIEP encode engine.

#include <iostream>
#include <string>
#include <vector>

#include "capability.h"

namespace halcodec {
namespace nvmedia {

class NvMediaCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "nvmedia"; }

    std::vector<std::string> getDeviceNames() const override {
        // Single-SoC platform; no enumeration API.
        return {};
    }

    void showDecoderCapability() const override {
        std::cout << "  NvMediaIDE hardware decode: H.264/HEVC/MPEG-2/MPEG-4/"
                     "VC-1/VP8/VP9/MJPEG (AV1 on T234+), DRIVE OS"
                  << std::endl;
    }

    void showEncoderCapability() const override {
        std::cout << "  NvMediaIEP hardware encode: H.264/HEVC/VP9/AV1 "
                     "(DRIVE OS)"
                  << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, nvmedia, NvMediaCapsProvider);

} // namespace nvmedia
} // namespace halcodec