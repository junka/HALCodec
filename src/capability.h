#ifndef SRC_CAPABILITY_H
#define SRC_CAPABILITY_H

#include <memory>
#include <string>
#include <vector>

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

    static std::unique_ptr<CapabilityProvider> Create(const std::string& type) {
        return Registry<CapabilityProvider>::Create(type);
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
};

} // namespace halcodec

#endif // SRC_CAPABILITY_H