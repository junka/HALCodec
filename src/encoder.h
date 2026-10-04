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

    // Feed a raw frame into the encoder. For synchronous backends this blocks
    // until the frame is submitted and may produce output retrievable via
    // GetFrame(); for async backends (isAsync() == true) it returns as soon as
    // the frame is queued for hardware submission, without blocking on the
    // completion of any prior frame. A frame with size == 0 is the
    // end-of-stream marker (flush).
    virtual bool FillFrame(const CodecFrame& in) { (void)in; return false; }

    // True when the backend encodes asynchronously: GetFrame() then blocks
    // until an encoded packet is available or the stream ends (returns false
    // only when no more packets will ever be produced). Async backends pipeline
    // hardware submissions across multiple frames; sync backends drain each
    // submission before returning from FillFrame().
    virtual bool isAsync() const { return false; }

    // Signals that no more input will be fed (flush / EOF marker). Async
    // backends use this to release a blocked GetFrame and emit trailing
    // packets; sync backends may trigger a final encoder flush. Returns false
    // when unsupported. Feeding a zero-size CodecFrame to FillFrame() is the
    // equivalent implicit signal for backends that do not implement this.
    virtual bool SignalInputComplete() { return false; }

    // Retrieve an encoded frame. For synchronous backends this returns true
    // while a packet is available; for async backends (isAsync() == true) it
    // blocks until a packet is available or the stream is complete, and
    // returns false only when encoding is definitively finished. The caller
    // owns the CodecFrame object and must invoke out.release() to free the
    // underlying buffer.
    virtual bool GetFrame(CodecFrame& out) { (void)out; return false; }

    // Finalize the encoding process and release resources.
    virtual void Finalize() {}

    // True when each encoded frame is a standalone output file rather than a
    // piece of one stream: image coders (JPEG) emit a complete codestream per
    // frame, so appending two of them to one file yields an unreadable file.
    // Callers that were handed one output path per input image use this to
    // decide whether to move to the next file after every frame.
    virtual bool oneOutputFilePerFrame() const { return false; }

    virtual std::string getName() const { return ""; }
};

} // namespace halcodec

#endif // SRC_ENCODER_H