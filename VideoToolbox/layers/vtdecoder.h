#ifndef LAYERS_VTDECODER_H_
#define LAYERS_VTDECODER_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <VideoToolbox/VideoToolbox.h>
#include "codec_config.h"
#include "decoder.h"
#include "frame.h"
#include "h264poc.h"
#include "hevcpoc.h"

namespace halcodec {
namespace vtbox {

class VTDecoder : public Decoder {
public:
    VTDecoder() = default;

    bool Initialize(const CodecParams& params) override;
    int FillInput(const uint8_t* data, size_t size) override;
    bool SignalInputComplete() override;
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    bool isAsync() const override { return true; }
    std::string getName() const override { return "vtbox"; }

private:
    // Internal helpers for codec-specific initialization.
    bool InitializeH264(const CodecParams& params);
    bool InitializeHEVC(const CodecParams& params);
    bool InitializeAV1(const CodecParams& params);
    bool InitializeJPEG(const CodecParams& params);
    bool InitializeProRes(const CodecParams& params);
    // Opens the session. `requestedFormat` asks VideoToolbox for a particular
    // output pixel format instead of the one it would pick; 0 (the default for
    // every stream but a RAW one) leaves the choice to it.
    bool CreateSession(OSType requestedFormat = 0);
    
    VTDecompressionSessionRef decompressionSession = nullptr;
    CMVideoFormatDescriptionRef formatDescription = nullptr;

    // Queue of decoded frames in presentation order, filled by FlushReorder()
    // and drained by GetFrame(). Guarded by mtx_; cv_ wakes GetFrame().
    //
    // The queue is bounded indirectly, by the live-frame window below: the
    // callback always pushes immediately, and the feed path stops submitting
    // once the window is full. Peak memory therefore follows the window, not
    // the length of the stream -- one window slot is one full frame.
    std::deque<CodecFrame> frameQ_;
    // Frames the callback has taken but not yet ordered, keyed by the picture
    // order count computed at submit time. VideoToolbox returns an elementary
    // stream in decode order, so B-frame output has to be reordered here rather
    // than by the caller. Empty when the stream's picture order is unknown (a
    // non-parsable or unsupported parameter set), in which case the callback
    // queues frames directly and they come out in decode order as before.
    std::deque<std::pair<int64_t, CodecFrame>> reorder_;
    std::mutex mtx_;
    std::condition_variable cv_;
    // Set by SignalInputComplete(); GetFrame only treats the stream as
    // finished once the queue has drained and no sample is in flight.
    bool inputDone_ = false;

    // H.264 picture order front end. Used by the feed path only (PumpInput
    // classifies each access unit before submitting it), so it needs no lock;
    // the callback only reads the delay, which is mirrored into reorderDelay_.
    H264Poc poc_;
    HEVCPoc hevcPoc_;  // HEVC counterpart, used when codec is HEVC
    bool isHEVC_ = false;  // true if current stream is HEVC, false for H.264
    // True for an AV1 OBU stream. No picture order front end comes with it: an
    // OBU carries no order count the way an H.264/HEVC slice header carries a POC,
    // and VideoToolbox hands AV1 frames back in the order they were submitted
    // (measured on a libsvtav1 stream), so the callback queues them directly.
    bool isAV1_ = false;
    // True for a JPEG codestream. Like AV1 it needs no picture order front end --
    // one codestream is one image, so there is nothing to reorder -- and unlike
    // every video codec it has no parameter set to open a session with: the
    // picture size comes out of the frame header markers instead (jpegdesc).
    bool isJPEG_ = false;
    // True for a ProRes element stream: a concatenation of self-delimiting
    // pictures, each opening with its own byte count (proresdesc). Intra-only, so
    // no picture order front end, and no parameter set either -- the frame header
    // carries the picture size the session opens on.
    bool isProRes_ = false;
    // True for a ProRes RAW picture ('prrf'), which is a ProRes stream in every
    // structural sense and a different kind of frame: the decoder hands back a
    // Bayer sensel grid plus the metadata that says how to turn it into colour,
    // not a YUV picture, so the session is asked for that output explicitly.
    bool isProResRaw_ = false;
    std::atomic<int> reorderDelay_{0};

    // Slots in flight: samples submitted to VideoToolbox plus decoded frames
    // still waiting in frameQ_ or in the reorder window. A slot is freed when
    // GetFrame() hands its frame over, so a caller that drains as it feeds never
    // blocks the decoder and a caller that feeds a whole stream merely leaves
    // input queued in front of the window (compressed bytes, orders of magnitude
    // cheaper than the frame queue it protects).
    //
    // Half of the budget is reserved for VideoToolbox to keep working with and
    // half for the reorder window, which is why the delay it holds back is
    // capped at kMaxLiveFrames / 2 = 16 pictures: a compliant H.264 stream never
    // reorders deeper than that (MaxDpbFrames <= 16), including the deep-B
    // presets that hold a dozen consecutive B frames.
    static constexpr size_t kMaxLiveFrames = 32;
    size_t liveFrames_ = 0;

    // Input that has not become a submitted access unit yet: everything from
    // pendingPos_ on, kept because the live-frame window was full or because
    // the trailing NAL has no end yet (feed chunks are fixed-size reads, so a
    // NAL can straddle a chunk boundary and splitting each chunk in isolation
    // would truncate its slices).
    std::vector<uint8_t> pending_;
    size_t pendingPos_ = 0;

    // Submits as many complete access units as the live-frame window allows.
    void PumpInput();
    // The AV1 feed path, reached from PumpInput: cuts pending_ into temporal
    // units and submits each one's raw OBU bytes.
    void PumpAv1Input();
    // The JPEG feed path: submits one codestream once its end-of-image marker
    // has arrived (feed chunks are fixed-size, so a large image straddles them).
    void PumpJpegInput();
    // The ProRes feed path: submits pictures whose own 32-bit count has arrived
    // in full. No grouping, no order key -- every picture is a keyframe.
    void PumpProResInput();
    // Drops already-consumed input bytes, rewriting the buffer only once at
    // least half of it has been consumed.
    void CompactPending();
    // Moves frames whose display slot has settled from reorder_ to frameQ_.
    // With `final` the stream is over and the whole window is drained. Needs
    // mtx_.
    void FlushReorder(bool final);

    // Window bookkeeping, all under mtx_.
    int QueuedFrames();
    bool IsInputDone();
    size_t FreeSlots();
    // Takes `n` slots for units about to be submitted. PumpInput never plans more
    // than FreeSlots() reported and only that thread reserves, so this cannot
    // fail; the other threads (GetFrame, the callback) only ever release.
    void ReserveSlots(size_t n);
    void ReleaseSlot();

    static void DecompressionCallback(void* refcon,
        void* sourceFrameRefCon,
        OSStatus status,
        VTDecodeInfoFlags infoFlags,
        CVPixelBufferRef pixelBuffer,
        CMTime presentationTimeStamp,
        CMTime presentationDuration);

    // Submits one access unit for async decoding. Takes ownership of `data`:
    // it is freed automatically when VideoToolbox finishes with the sample
    // (via the block buffer's custom deallocator), or immediately on failure.
    // `displayKey` is the picture's order count, or negative when unknown; it
    // comes back to the callback untouched, which is how a frame is matched to
    // the position it should be inserted at.
    bool decodeFrameAsync(uint8_t* data, size_t size, int64_t displayKey);

    // Submits one access unit for async decoding. The `au` bytes are copied into
    // an independent buffer (whose ownership passes to decodeFrameAsync) because
    // the source buffer belongs to the caller: H.264/HEVC pass an AVCC access unit
    // they just built, AV1 a range of its own pending_ input. Consumes the
    // live-frame slot that PumpInput reserved for it when the sample is not
    // accepted, since no callback will ever release it.
    void submitAu(const uint8_t* au, size_t size, int64_t displayKey);
};

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_VTDECODER_H_