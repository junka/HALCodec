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
        // MJPEG/VP8/VP9/AV1, but the nv118 system libnvmedia_ide_parser runtime
        // (a reduced /usr/lib build) rejects every codec except H.264 and HEVC
        // at NvMediaParserCreate ("Codec Not supported"). That rejection is a
        // lib-version limit, not a hardware one: the same DRIVE-OS 7.0.3.0 SDK
        // lib-target (full build) accepts all 11 codecs when loaded instead.
        // Report what the deployed runtime lib actually honors so codecinfo
        // isn't misleading.
        std::cout << "  NvMediaIDE hardware decode: H.264, HEVC (verified); "
                     "MPEG-1/2/4, VC-1, VP8, VP9, MJPEG, AV1 are in the header "
                     "but rejected by the deployed parser runtime on this "
                     "DRIVE build (a full-build SDK lib unlocks them)"
                  << std::endl;
    }

    void showEncoderCapability() const override {
        // NvMediaIEP creates H.264 and HEVC encoders on this board's deployed
        // lib. AV1 has a header + sample config and is accepted by the full
        // SDK lib build (not the reduced system one); VP9 encode fails in both.
        std::cout << "  NvMediaIEP hardware encode: H.264, HEVC (verified); "
                     "AV1 accepted only by the full-build SDK lib (not wired); "
                     "VP9 fails at NvMediaIEPCreate on this build"
                  << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, nvmedia, NvMediaCapsProvider);

} // namespace nvmedia
} // namespace halcodec