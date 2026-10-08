// Capability introspection for the "qsv" backend.
//
// libvpl has no hardware capability enumeration API (unlike CUDA); codec
// support is negotiated at session/component init time against the loaded
// Intel media driver. The provider reports the fixed component surface and
// whether the libvpl dispatcher is loadable on this host.

#include <iostream>
#include <string>
#include <vector>

#include "capability.h"
#include "qsv_common.h"

namespace halcodec {
namespace qsv {

class QSVCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "qsv"; }

    std::vector<std::string> getDeviceNames() const override {
        // libvpl does not enumerate GPUs; the dispatcher picks the first Intel
        // adapter. Report nothing.
        return {};
    }

    void showDecoderCapability() const override {
        std::cout << "  hardware decode via libvpl: "
                     "H.264/HEVC/AV1/VP9/VP8/VC1/MPEG2/JPEG "
                     "(driver-dependent, negotiated at init)"
                  << std::endl;
    }

    void showEncoderCapability() const override {
        QSVRuntime runtime;
        bool loaded = runtime.init();
        std::cout << "  hardware encode via libvpl: H.264/HEVC/AV1/JPEG on Intel iGPU "
                     "(VP9/MPEG2 encode not implemented on this hardware)"
                  << (loaded ? "" : " (libvpl not loadable on this host)")
                  << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, qsv, QSVCapsProvider);

} // namespace qsv
} // namespace halcodec