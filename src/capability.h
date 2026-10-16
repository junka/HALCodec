#ifndef SRC_CAPABILITY_H
#define SRC_CAPABILITY_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "frame.h"
#include "registry.h"

namespace halcodec {

// Backend self-introspection: device enumeration plus decoder/encoder
// capability printout. Implemented and self-registered per vendor; consumed
// by codecinfo.
//
// Capability data is owned by the codec backends (cuvidGetDecoderCaps,
// nvEncGetEncodeCaps, VTCopyVideoEncoderList, ...), not by any device/context
// abstraction, so this provider replaces the old Device::show*Capability pair.
// A backend reports only what it can truthfully query: VideoToolbox has no
// decoder-capability API, and that is valid, honest output.
class CapabilityProvider {
public:
    virtual ~CapabilityProvider() = default;

    using Creator = Registry<CapabilityProvider>::Creator;

    // A provider is a backend *family* -- one per vendor library (qsv, nvidia,
    // vtbox, ...) -- while the CLI addresses a direction of that family
    // (qsvdec/qsvenc, nvdec/nvenc, nvjpegenc, vtenc). Both spellings name the
    // same provider, so Create() resolves the direction to the family first:
    // otherwise `codecinfo -b qsvdec --decode-caps` fails with "no capability
    // provider" for a backend that is plainly registered. A name that is not a
    // known direction (the family name itself, or a future backend) passes
    // through unchanged.
    static std::string ProviderName(const std::string& backend) {
        static const std::map<std::string, std::string> directionToFamily = {
            {"qsvdec", "qsv"},   {"qsvenc", "qsv"},
            {"nvdec", "nvidia"}, {"nvenc", "nvidia"}, {"nvjpegenc", "nvjpeg"},
            {"vtenc", "vtbox"},
        };
        auto it = directionToFamily.find(backend);
        return it == directionToFamily.end() ? backend : it->second;
    }

    static std::unique_ptr<CapabilityProvider> Create(const std::string& type) {
        return Registry<CapabilityProvider>::Create(ProviderName(type));
    }

    static bool Register(const std::string& type, Creator creator) {
        return Registry<CapabilityProvider>::Register(type, std::move(creator));
    }

    virtual std::string getName() const = 0;

    // Backend-level device enumeration (e.g. CUDA devices). A system-wide
    // backend (VideoToolbox) has no such enumeration and returns an empty
    // vector.
    virtual std::vector<std::string> getDeviceNames() const = 0;

    virtual void showDecoderCapability() const = 0;
    virtual void showEncoderCapability() const = 0;

    // Structured decoder capability for one codec ("h264" / "hevc" / ...),
    // consumed by codecinfo --decode-caps and by the tests' capability gate.
    //
    // Each entry states one input class (profile label plus the numeric
    // bit depth and chroma subsampling a gate matches on) and the pixel
    // formats the decoder actually produces for it. The output side is the
    // load-bearing part: some hardware accepts a stream and silently
    // downgrades it -- VideoToolbox on machines whose HEVC hardware path has
    // no 4:2:2 10-bit output decodes such streams to P010 -- so "decodes
    // Main 4:2:2 10" and "decodes Main 4:2:2 10 into P210" are different
    // claims and only the second one lets a byte-exact test run.
    //
    // CapsStatus::Unknown means the backend cannot answer truthfully (query
    // not implemented, probe broken, data path absent). Callers must fall
    // back to running and seeing; only Available answers gate a test. An
    // Available codec with no entries is a truthful "this backend does not
    // decode it at all".
    //
    // The default returns Unknown so a backend adopts the structured query
    // incrementally; showDecoderCapability() stays the human-readable view.
    enum class CapsStatus {
        Unknown,
        Available,
    };
    struct DecodeCapsEntry {
        std::string profile;   // human-readable input class, e.g. "Main 4:2:2 10"
        int bitDepth = 0;      // bits per component: 8/10/12; 0 = n/a (jpeg)
        int chroma = 0;        // subsampling: 420/422/444; 0 = mono/n-a
        std::vector<PixelFormat> outputFormats;
    };
    virtual CapsStatus queryDecoderCaps(const std::string& codec,
                                        std::vector<DecodeCapsEntry>& out) const {
        (void)codec;
        (void)out;
        return CapsStatus::Unknown;
    }
};

} // namespace halcodec

#endif // SRC_CAPABILITY_H