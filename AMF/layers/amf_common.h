#ifndef AMF_LAYERS_AMF_COMMON_H
#define AMF_LAYERS_AMF_COMMON_H

// Shared AMF runtime/context plumbing for the AMD backend. The AMF SDK is
// loaded at runtime via dlopen (libamfrt64.so.1 on Linux) — there is no
// static link dependency, mirroring how AMD distributes the SDK. The Vegas
// of the unified API stays clean: nothing AMF-specific leaks into src/.

#include <dlfcn.h>

#include <iostream>
#include <string>

#include <core/Factory.h>

namespace halcodec {
namespace amd {

class AMFRuntime {
public:
    bool init() {
        handle_ = dlopen(AMF_DLL_NAMEA, RTLD_NOW | RTLD_GLOBAL);
        if (!handle_) {
            std::cerr << "AMF: failed to dlopen " << AMF_DLL_NAMEA
                      << " (" << dlerror() << ")" << std::endl;
            return false;
        }
        auto initFn = reinterpret_cast<AMFInit_Fn>(dlsym(handle_, "AMFInit"));
        auto queryVersionFn =
            reinterpret_cast<AMFQueryVersion_Fn>(dlsym(handle_, "AMFQueryVersion"));
        if (!initFn || !queryVersionFn) {
            std::cerr << "AMF: runtime symbols not found" << std::endl;
            return false;
        }
        amf_uint64 version = 0;
        if (queryVersionFn(&version) != AMF_OK) {
            std::cerr << "AMF: AMFQueryVersion failed" << std::endl;
            return false;
        }
        if (initFn(AMF_FULL_VERSION, &factory_) != AMF_OK) {
            std::cerr << "AMF: AMFInit failed (runtime version " << version << ")"
                      << std::endl;
            return false;
        }
        return true;
    }

    amf::AMFFactory* factory() const { return factory_; }

private:
    void* handle_ = nullptr;
    amf::AMFFactory* factory_ = nullptr;
};

// RAII wrapper around AMFContext with hardware context creation (Vulkan on
// Linux; the unified API has no context concept, so this stays backend-internal).
class AMFContextHelper {
public:
    bool init(AMFRuntime& runtime) {
        if (runtime.init() != true) {
            return false;
        }
        if (runtime.factory()->CreateContext(&context_) != AMF_OK) {
            std::cerr << "AMF: CreateContext failed" << std::endl;
            return false;
        }
        // InitVulkan lives on AMFContext1 (base interface pin). Pass a null
        // Vulkan device: the runtime creates/uses its own device. On Windows
        // this would be InitDX11(nullptr) instead.
        amf::AMFContext1* context1 = reinterpret_cast<amf::AMFContext1*>(context_);
        if (context1->InitVulkan(nullptr) != AMF_OK) {
            std::cerr << "AMF: InitVulkan failed (no AMD driver?)" << std::endl;
            return false;
        }
        return true;
    }

    amf::AMFContext* get() const { return context_; }

    void destroy() {
        if (context_) {
            context_->Terminate();
            context_->Release();
            context_ = nullptr;
        }
    }

private:
    amf::AMFContext* context_ = nullptr;
};

} // namespace amd
} // namespace halcodec

#endif // AMF_LAYERS_AMF_COMMON_H