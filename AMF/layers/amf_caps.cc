// Capability introspection for the "amf" backend.
//
// AMF has no hardware capability enumeration API (neither decode nor encode,
// unlike CUDA/cuvidGetDecoderCaps). The provider reports the fixed component
// surface the VCE encoder / UVD decoder expose.
//
// Registration is gated on the AMF runtime being loadable: unlike nvenc/
// nvjpeg (whose .so fails to dlopen when its CUDA NEEDED dep is absent, so
// LoadBackends skips them), libamf_layers.so has no link-time dependency on
// libamfrt64.so.1 — it dlopens it lazily in AMFRuntime::init(). That means the
// .so loads on any host and its static initializer would otherwise register
// "amf" everywhere, including Intel-only machines with no AMD hardware, where
// codecinfo would then advertise a backend that can never run. So the
// initializer probes the runtime first and only registers when it loads.

#include <iostream>
#include <string>
#include <vector>

#include "capability.h"
#include "registry.h"

#include "amf_common.h"

namespace halcodec {
namespace amd {

class AMFCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "amf"; }

    std::vector<std::string> getDeviceNames() const override {
        // AMF does not enumerate GPUs; device selection happens through the
        // Vulkan/DX11 context. Report nothing.
        return {};
    }

    void showDecoderCapability() const override {
        std::cout << "  UVD hardware decode: H.264/HEVC/VP9/AV1/MPEG-2 (GPU-dependent, "
                     "no query API)"
                  << std::endl;
    }

    void showEncoderCapability() const override {
        std::cout << "  VCE/AV1 hardware encode: H.264/HEVC/AV1 on AMD GPU"
                  << std::endl;
    }
};

// Register only if the AMF runtime dlopens and initializes. On a host without
// libamfrt64.so.1 (or without an AMD driver) the probe fails and "amf" never
// enters the registry, so codecinfo/hal_* won't list a dead backend.
namespace {
bool registerAmfIfRuntimePresent() {
    AMFRuntime runtime;
    if (!runtime.init()) {
        return false;
    }
    ::halcodec::Registry<::halcodec::CapabilityProvider>::Register(
        "amf", []() { return std::make_unique<AMFCapsProvider>(); });
    return true;
}
static bool amfRegistered = registerAmfIfRuntimePresent();
} // namespace

} // namespace amd
} // namespace halcodec
