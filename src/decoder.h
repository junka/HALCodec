#ifndef SRC_DECODER_H
#define SRC_DECODER_H

#include <memory>
#include <string>

#include "codec_config.h"
#include "frame.h"
#include "registry.h"

namespace halcodec {

class Decoder {
protected:
    Decoder() = default;

public:
    virtual ~Decoder() = default;

    using Creator = Registry<Decoder>::Creator;

    // Factory: instantiate a registered decoder by name (e.g. "nvdec",
    // "nvjpeg", "vtbox").
    static std::unique_ptr<Decoder> Create(const std::string& type) {
        return Registry<Decoder>::Create(type);
    }

    static bool Register(const std::string& type, Creator creator) {
        return Registry<Decoder>::Register(type, std::move(creator));
    }

    // Initialize the decoder with the given configuration. Returns true on
    // success, false otherwise.
    virtual bool Initialize(const CodecParams& params) { (void)params; return false; }

    // Fill input data and decode. Returns the number of frames decoded in
    // this batch (0 = exhausted, negative = error).
    virtual int FillinFrame() { return -1; }

    // Retrieve the next decoded frame. Returns true when a frame is
    // available; the caller owns the CodecFrame object and must invoke
    // out.release() to free the underlying buffer.
    virtual bool GetFrame(CodecFrame& out) { (void)out; return false; }

    // Finalize the decoding process and release resources.
    virtual void Finalize() {}

    virtual std::string getName() const { return ""; }
};

} // namespace halcodec

#endif // SRC_DECODER_H