// Capability introspection for the "amf" backend.
//
// AMF has no hardware capability enumeration API (neither decode nor encode,
// unlike CUDA/cuvidGetDecoderCaps). The provider reports the fixed component
// surface the VCE encoder / UVD decoder expose and whether the AMF runtime is
// loadable on this host.

#include <iostream>
#include <string>
#include <vector>

#include "capability.h"

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
        AMFRuntime runtime;
        bool loaded = runtime.init();
        std::cout << "  VCE/AV1 hardware encode: H.264/HEVC/AV1 on AMD GPU"
                  << (loaded ? "" : " (AMF runtime not loadable on this host)")
                  << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, amf, AMFCapsProvider);

} // namespace amd
} // namespace halcodec