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
        // The IDE header (nvmedia_ide.h) lists H264/HEVC/VC1/MPEG1/MPEG2/MPEG4/
        // MJPEG/VP8/VP9/AV1, but the shipped libnvmedia_ide_parser runtime
        // rejects every codec except H.264 and HEVC at NvMediaParserCreate
        // ("Codec Not supported"). That rejection is a board/lib limit, not a
        // framework one: NVIDIA's own nvm_ide_sci sample fails identically.
        // Report what the runtime actually honors so codecinfo isn't misleading.
        std::cout << "  NvMediaIDE hardware decode: H.264, HEVC (verified); "
                     "MPEG-1/2/4, VC-1, VP8, VP9, MJPEG, AV1 are in the header "
                     "but rejected by the parser runtime on this DRIVE build"
                  << std::endl;
    }

    void showEncoderCapability() const override {
        // NvMediaIEP creates H.264 and HEVC encoders on this board. AV1 and VP9
        // have headers + sample configs but NvMediaIEPCreate fails at runtime
        // (despite the T234-class SoC), so they are not advertised as working.
        std::cout << "  NvMediaIEP hardware encode: H.264, HEVC (verified); "
                     "AV1/VP9 in header but NvMediaIEPCreate fails on this build"
                  << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, nvmedia, NvMediaCapsProvider);

} // namespace nvmedia
} // namespace halcodec