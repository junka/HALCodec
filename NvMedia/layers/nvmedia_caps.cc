// Capability introspection for the "nvmedia" backend (NVIDIA DRIVE OS).
//
// NvMedia has no host-side device enumeration API; the DRIVE OS SoC is a
// fixed hardware target. Decoder/encoder surfaces are reported statically:
// the NvMediaIDE decode engine and the NvMediaIEP encode engine.

#include <iostream>
#include <string>
#include <utility>
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
        // MJPEG/VP8/VP9/AV1, but the board's system libnvmedia_ide_parser runtime
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

    // Structured decode caps. NvMedia has no decoder-capability query API
    // (nvmedia_ide.h exposes creation only; NvMedia2DGetCapabilities belongs
    // to the 2D engine, not the IDE decoder), so this states only what the
    // code itself confirms: every output path in nvmediadecoder.cc hardcodes
    // PixelFormat::NV12, and the deployed parser runtime accepts only H.264
    // and HEVC (see showDecoderCapability). Those two codecs therefore get a
    // single 8-bit 4:2:0 -> NV12 entry each; other bit depths / subsamplings
    // have no confirmed output (the layer never queries the surface bit
    // depth, so a Main 10 stream's fate on the NV12 path is unmeasured), and
    // every other codec answers Unknown. Available-with-no-entries would be
    // a false "decodes none of it" verdict for a backend that demonstrably
    // decodes -- not an option here.
    CapsStatus queryDecoderCaps(const std::string& codec,
                                std::vector<DecodeCapsEntry>& out) const override {
        if (codec != "h264" && codec != "hevc") {
            return CapsStatus::Unknown;
        }
        DecodeCapsEntry entry;
        entry.profile = (codec == "h264") ? "High" : "Main";
        entry.bitDepth = 8;
        entry.chroma = 420;
        entry.outputFormats.push_back(PixelFormat::NV12);
        out.push_back(std::move(entry));
        return CapsStatus::Available;
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