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

    // Explicitly feed one chunk of compressed data (Annex-B elementary stream
    // or already demuxed packet). Returns the number of frames that can be
    // synchronously drained via GetFrame():
    //   >= 0  frames available now (0 = accepted, frames may be produced
    //         asynchronously; drain with GetFrame());
    //   -1    this backend does not support explicit feeding.
    // The default implementation reports "not supported".
    virtual int FillInput(const uint8_t* data, size_t size) {
        (void)data;
        (void)size;
        return -1;
    }

    // Signals that no more input will be fed (flush / EOF marker). Async
    // backends use this to release a blocked GetFrame, sync backends may
    // trigger a final decoder flush. Returns false when unsupported.
    virtual bool SignalInputComplete() { return false; }

    // Consume the internal input source (file / directory captured in
    // CodecParams). Returns the number of frames decoded in this batch
    // (0 = exhausted, negative = error).
    virtual int PullFrames() { return -1; }

    // True when the backend decodes asynchronously: GetFrame() then blocks
    // until a frame is available or the stream ends (returns false only when
    // no more frames will ever be produced).
    virtual bool isAsync() const { return false; }

    // Retrieve the next decoded frame. For synchronous backends this returns
    // true while a frame is available; for async backends (isAsync() == true)
    // it blocks until a frame is available or the stream is complete, and
    // returns false only when decoding is definitively finished. The caller
    // owns the CodecFrame object and must invoke out.release() to free the
    // underlying buffer.
    virtual bool GetFrame(CodecFrame& out) { (void)out; return false; }

    // Finalize the decoding process and release resources.
    virtual void Finalize() {}

    virtual std::string getName() const { return ""; }
};

} // namespace halcodec

#endif // SRC_DECODER_H