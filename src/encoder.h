#ifndef SRC_ENCODER_H
#define SRC_ENCODER_H

#include <memory>
#include <string>

#include "codec_config.h"
#include "frame.h"
#include "registry.h"

namespace halcodec {

class Encoder {
public:
    Encoder() = default;
    virtual ~Encoder() = default;

    using Creator = Registry<Encoder>::Creator;

    // Factory: instantiate a registered encoder by name (e.g. "nvenc").
    static std::unique_ptr<Encoder> Create(const std::string& type) {
        return Registry<Encoder>::Create(type);
    }

    static bool Register(const std::string& type, Creator creator) {
        return Registry<Encoder>::Register(type, std::move(creator));
    }

    // Initialize the encoder with the given configuration. Returns true on
    // success, false otherwise.
    virtual bool Initialize(const CodecParams& params) { (void)params; return false; }

    // Feed a raw frame into the encoder.
    virtual bool FillFrame(const CodecFrame& in) { (void)in; return false; }

    // Retrieve an encoded frame. Returns true when a frame is available.
    virtual bool GetFrame(CodecFrame& out) { (void)out; return false; }

    // Finalize the encoding process and release resources.
    virtual void Finalize() {}

    virtual std::string getName() const { return ""; }
};

} // namespace halcodec

#endif // SRC_ENCODER_H