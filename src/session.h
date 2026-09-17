#ifndef SRC_SESSION_H
#define SRC_SESSION_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "codec_config.h"
#include "decoder.h"
#include "encoder.h"
#include "frame.h"

namespace halcodec {

// Export the Session class from the core shared library despite the core's
// hidden default visibility: the apps link against halcodec_core and call
// these methods across the .so boundary (unlike Encoder/Decoder, which are
// header-only templates/inlines).
#if defined(__GNUC__) || defined(__clang__)
#define HALCODEC_API __attribute__((visibility("default")))
#else
#define HALCODEC_API
#endif

// A Session owns a set of independent codec streams of one kind (all decoders
// or all encoders) backed by the same vendor implementation. It is the
// multi-stream entry point above the per-stream Encoder/Decoder HAL: each
// stream is a fully independent async backend (its own worker thread and
// hardware pipeline), and the Session merely coordinates creation, feeding,
// and draining across them.
//
// Streams are addressed by index in creation order (0..N-1). Per-stream
// Feed/SignalEOF/GetFrame map directly onto the underlying async backend, so
// callers retain fine-grained control; GetAnyFrame drains in completion order
// across all streams for fan-in use cases (e.g. many inputs -> one muxer).
class HALCODEC_API Session {
public:
    enum class Kind { Decode, Encode };

    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Create N independent streams of the given backend kind and name. Each
    // stream is Initialize()'d with its own CodecParams copy. Returns the
    // number of streams actually created (0 on total failure; partial success
    // is not kept — all or nothing). backendName e.g. "qsvdec" / "qsvenc".
    size_t open(Kind kind, const std::string& backendName,
                const std::vector<CodecParams>& perStreamParams);

    // Number of live streams.
    size_t streamCount() const;

    // --- Decode streams ---

    // Feed one chunk of compressed Annex-B data to decode stream `idx`.
    // Returns the backend's FillInput result (<0 = unsupported).
    int feedDecode(size_t idx, const uint8_t* data, size_t size);

    // Mark decode stream `idx` as EOF (no more input).
    bool signalDecodeEOF(size_t idx);

    // Block until decode stream `idx` emits a frame, or return false when the
    // stream is finished. The caller owns `out` and must call out.release().
    bool getDecodeFrame(size_t idx, CodecFrame& out);

    // --- Encode streams ---

    // Feed one raw frame to encode stream `idx`. A zero-size frame is the
    // implicit EOS marker (same contract as Encoder::FillFrame).
    bool feedEncode(size_t idx, const CodecFrame& in);

    // Mark encode stream `idx` as EOF.
    bool signalEncodeEOF(size_t idx);

    // Block until encode stream `idx` emits an encoded packet, or return false
    // when the stream is finished.
    bool getEncodeFrame(size_t idx, CodecFrame& out);

    // --- Fan-in drain ---

    // Drain one decoded frame from whichever decode stream has one ready
    // first (blocks until any stream produces or all are finished). Returns
    // false when every decode stream is done. On success, `outStream` is set
    // to the producing stream index.
    bool getAnyDecodeFrame(CodecFrame& out, size_t& outStream);

    // Drain one encoded packet from whichever encode stream is ready first.
    bool getAnyEncodeFrame(CodecFrame& out, size_t& outStream);

    // Release all streams and their resources.
    void close();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace halcodec

#endif // SRC_SESSION_H
