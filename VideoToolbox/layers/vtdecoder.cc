#include "vtdecoder.h"

#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace halcodec {
namespace vtbox {

namespace {

// Deallocator for the per-access-unit copies owned by CMBlockBuffers. Runs
// when the block buffer is fully released, i.e. after VideoToolbox has
// finished decoding the sample.
void FreeBlockBufferData(void*, void* block, size_t) {
    free(block);
}

// Annex-B NAL scanner. Every NAL except the last is complete (delimited by
// the following start code); the final NAL may continue inside the next feed
// chunk and is therefore held pending until its end is seen. `maxNals` bounds
// the scan so a caller that only wants a few access units never walks the
// whole pending buffer: NALs are only cut off *between* units, so a truncated
// scan simply lacks the NAL that would close the last access unit.
struct Nal {
    size_t startPos;  // offset of the NAL's start code in the buffer
    const uint8_t* payload;
    size_t len;
    uint8_t type;
};

std::vector<Nal> ScanNals(const uint8_t* data, size_t size,
                          size_t maxNals) {
    std::vector<Nal> nals;
    size_t i = 0;
    while (i + 3 <= size && nals.size() < maxNals) {
        if (data[i] != 0 || data[i + 1] != 0) {
            ++i;
            continue;
        }
        size_t sc = 3;
        if (data[i + 2] == 1) {
            sc = 3;
        } else if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
            sc = 4;
        } else {
            ++i;
            continue;
        }
        const uint8_t* payload = data + i + sc;
        size_t j = i + sc;
        // A 3-byte start code can begin at size-3, so the bound is inclusive.
        // When no next start code is found the NAL runs to the end of the
        // buffer: stopping the scan loop at its bound instead (the old
        // `j + 3 < size` form) cut the last NAL of the stream short, and
        // VideoToolbox rejected the truncated AU with
        // kVTVideoDecoderBadDataErr (-12909).
        bool nextStartFound = false;
        while (j + 3 <= size) {
            if (data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 ||
                 (j + 4 <= size && data[j + 2] == 0 && data[j + 3] == 1))) {
                nextStartFound = true;
                break;
            }
            ++j;
        }
        if (!nextStartFound) {
            j = size;
        }
        const size_t len = j - (i + sc);
        nals.push_back(
            {i, payload, len,
             static_cast<uint8_t>(len > 0 ? payload[0] & 0x1F : 0)});
        i = j;
    }
    return nals;
}

// Builds one AVCC access unit ([len:4][nalu]...) from nals[begin, end).
std::vector<uint8_t> BuildAvcc(const std::vector<Nal>& nals,
                               size_t begin, size_t end) {
    size_t total = 0;
    for (size_t k = begin; k < end; ++k) {
        total += nals[k].len + 4;
    }
    std::vector<uint8_t> au;
    au.reserve(total);
    for (size_t k = begin; k < end; ++k) {
        const uint32_t len = static_cast<uint32_t>(nals[k].len);
        au.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        au.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        au.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        au.push_back(static_cast<uint8_t>(len & 0xFF));
        au.insert(au.end(), nals[k].payload, nals[k].payload + nals[k].len);
    }
    return au;
}

// One access unit as a half-open range over the NAL vector plus its display
// order, so the caller decides *after* grouping which units to build (and can
// leave the rest for the next pump).
struct Au {
    size_t firstNal;
    size_t endNal;
    int64_t key;  // negative when the picture order is unknown
};

// Groups NALs into access units and reports their ranges, walking them the way
// a decoder's picture state machine would: parameter sets are fed and every VCL
// NAL classified, exactly once and in stream order.
//
// The boundary rule matches FFmpeg's h264_mp4toannexb (a preamble of
// SEI/SPS/PPS/AUD after a picture starts a new unit) with the one thing that
// file-oriented rule leaves out: a picture can be split into several slices, and
// every slice after the first reports first_mb_in_slice != 0. H264Poc surfaces
// that as a continuation, and those slices stay in the unit they belong to --
// handing VideoToolbox a lone secondary slice yields no picture at all.
//
// Only nals[0, limitNals) is looked at, because state a caller has not consumed
// must not be advanced. The unit still open at the limit is returned only when
// `closeOpen` (the input ended, so no further slice of that picture can exist);
// otherwise its bytes are held for the next feed.
std::vector<Au> GroupAUs(H264Poc& poc, const std::vector<Nal>& nals,
                         size_t limitNals, bool closeOpen) {
    std::vector<Au> aus;
    size_t auBegin = 0;
    int64_t auKey = -1;
    bool hasVcl = false;
    const size_t limit = std::min(limitNals, nals.size());
    for (size_t idx = 0; idx < limit; ++idx) {
        const Nal& nal = nals[idx];
        const bool vcl = nal.type >= 1 && nal.type <= 5;
        int64_t key = -1;
        bool continuation = false;
        bool preamble = false;
        if (vcl) {
            continuation = poc.ClassifySlice(nal.payload, nal.len, &key) ==
                           H264Poc::Slice::Continuation;
        } else {
            poc.FeedParameterSet(nal.payload, nal.len);
            preamble = nal.type >= 6 && nal.type <= 9;
        }
        // Where the current picture ends: any NAL that starts a new one.
        if (hasVcl && (vcl ? !continuation : preamble)) {
            aus.push_back({auBegin, idx, auKey});
            auBegin = idx;
            auKey = -1;
            hasVcl = false;
        }
        if (vcl) {
            // The unit's primary slice carries its display order; a lone
            // continuation (a picture whose start was already submitted, which a
            // well-formed stream never lets happen) keeps the key it reports so
            // it still lands beside its sibling frames.
            if (!hasVcl && poc.usable()) {
                auKey = key;
            }
            hasVcl = true;
        }
    }
    if (closeOpen && hasVcl) {
        aus.push_back({auBegin, limit, auKey});
    }
    return aus;
}

// Advances the picture-order state over nals[0, limit) exactly as GroupAUs does
// over the same range: parameter sets fed, every VCL NAL classified once and in
// stream order. Used to commit the state after a group planned against a copy.
void FeedPocRange(H264Poc& poc, const std::vector<Nal>& nals, size_t limit) {
    for (size_t idx = 0; idx < std::min(limit, nals.size()); ++idx) {
        const Nal& nal = nals[idx];
        if (nal.type >= 1 && nal.type <= 5) {
            int64_t key = 0;
            poc.ClassifySlice(nal.payload, nal.len, &key);
        } else {
            poc.FeedParameterSet(nal.payload, nal.len);
        }
    }
}

} // namespace

bool VTDecoder::Initialize(const CodecParams& params) {
    // Parse H.264 parameter sets from extradata (AVCC layout:
    // [len:4][nalu][len:4][nalu] ...). SPS (type 7) and PPS (type 8) are fed
    // to VideoToolbox to build the format description.
    const uint8_t* parameterSetPointers[2] = { nullptr, nullptr };
    size_t parameterSetSizes[2] = { 0, 0 };
    size_t parameterSetCount = 0;

    const auto& ext = params.extradata;
    size_t pos = 0;
    while (pos + 4 <= ext.size() && parameterSetCount < 2) {
        uint32_t naluLen = (static_cast<uint32_t>(ext[pos]) << 24) |
                           (static_cast<uint32_t>(ext[pos + 1]) << 16) |
                           (static_cast<uint32_t>(ext[pos + 2]) << 8) |
                           static_cast<uint32_t>(ext[pos + 3]);
        pos += 4;
        if (pos + naluLen > ext.size()) {
            break;
        }
        uint8_t naluType = ext[pos] & 0x1F;
        if (naluType == 7 && parameterSetCount == 0) {
            parameterSetPointers[0] = ext.data() + pos;
            parameterSetSizes[0] = naluLen;
            parameterSetCount = 1;
        } else if (naluType == 8 && parameterSetCount == 1) {
            parameterSetPointers[1] = ext.data() + pos;
            parameterSetSizes[1] = naluLen;
            parameterSetCount = 2;
        }
        pos += naluLen;
    }

    if (parameterSetCount == 0) {
        std::cerr << "VTDecoder: no SPS/PPS in extradata" << std::endl;
        return false;
    }

    // The same parameter sets tell the picture order front end how this stream
    // counts display order, which an elementary-stream feed carries no other
    // way. In-band copies seen later by PumpInput simply re-parse them.
    for (size_t i = 0; i < parameterSetCount; ++i) {
        poc_.FeedParameterSet(parameterSetPointers[i], parameterSetSizes[i]);
    }
    reorderDelay_.store(poc_.usable() ? poc_.reorderDelay() : 0,
                        std::memory_order_relaxed);

    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault,
        static_cast<size_t>(parameterSetCount),
        parameterSetPointers,
        parameterSetSizes,
        4,
        &formatDescription
    );

    if (status != noErr) {
        std::cerr << "Failed to create format description: " << status << std::endl;
        return false;
    }

    VTDecompressionOutputCallbackRecord callback;
    callback.decompressionOutputCallback = DecompressionCallback;
    callback.decompressionOutputRefCon = this;

    status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        formatDescription,
        nullptr,
        nullptr,
        &callback,
        &decompressionSession
    );

    if (status != noErr) {
        std::cerr << "Failed to create decompression session: " << status << std::endl;
        return false;
    }

    return true;
}

int VTDecoder::FillInput(const uint8_t* data, size_t size) {
    CompactPending();
    pending_.insert(pending_.end(), data, data + size);
    PumpInput();
    return QueuedFrames();
}

// Drops the input bytes already turned into access units. Rewriting the buffer
// only once at least half of it has been consumed keeps a caller that feeds a
// whole file before taking any frame linear instead of quadratic.
void VTDecoder::CompactPending() {
    if (pendingPos_ == 0) {
        return;
    }
    if (pendingPos_ < pending_.size() - pendingPos_) {
        return;
    }
    pending_.erase(pending_.begin(),
                   pending_.begin() +
                       static_cast<std::ptrdiff_t>(pendingPos_));
    pendingPos_ = 0;
}

// Submits as many complete access units as the live-frame window has room for
// and leaves the rest of pending_ untouched. It never blocks: a full window
// simply consumes no input, and GetFrame() pumps again once it has handed a
// frame (and therefore a slot) to the caller.
void VTDecoder::PumpInput() {
    const size_t room = FreeSlots();
    if (!decompressionSession || room == 0) {
        return;
    }
    const uint8_t* data = pending_.data() + pendingPos_;
    const size_t size = pending_.size() - pendingPos_;
    if (size == 0) {
        return;
    }

    // Scan bound: enough NALs to fill the window with pictures, with slack for
    // pictures split into slices (x264 goes up to 8) -- the walk has to see the
    // NAL that starts the picture *after* the last unit it emits in order to
    // close that unit. Scanning the whole buffer instead would walk the rest of
    // the stream on every pump.
    constexpr size_t kMaxScanNals = 1024;
    const bool inputEof = IsInputDone();
    std::vector<Nal> nals;
    std::vector<Au> aus;
    size_t maxNals = 8 * room + 64;
    bool scanTruncated = false;
    for (;;) {
        nals = ScanNals(data, size, maxNals);
        // A scan that stopped at maxNals may be missing the NAL that closes the
        // last AU, so only reaching the end of the input makes it safe to emit.
        scanTruncated = nals.size() == maxNals;
        // Grouping advances the picture-order state and only the units submitted
        // here may do that, so plan against a copy and commit the real state over
        // the consumed range afterwards.
        H264Poc trial = poc_;
        aus = GroupAUs(trial, nals, nals.size(),
                       /*closeOpen=*/inputEof && !scanTruncated);
        // A picture sliced deeper than the bound leaves nothing to close it, so
        // this pump would submit no unit and the stream could never advance;
        // widen the scan until it yields one. The cap bounds the work that a
        // stream without any picture boundary can cause on every pump.
        if (!aus.empty() || nals.empty() || !scanTruncated ||
            maxNals >= kMaxScanNals) {
            break;
        }
        maxNals *= 2;
    }
    if (nals.empty()) {
        // No complete start code yet (a 3/4-byte start code may itself straddle
        // the chunk boundary); at end of input the leftovers are trailing
        // garbage, and keeping them would leave the stream looking unfinished.
        if (inputEof) {
            pendingPos_ += size;
        }
        return;
    }
    if (aus.empty() && inputEof) {
        // The scan bound cut through a single picture too deep to see the end
        // of. No further input can complete it, so submit the slices seen
        // instead of stranding them behind a picture that will never close.
        H264Poc trial = poc_;
        aus = GroupAUs(trial, nals, nals.size(), /*closeOpen=*/true);
    }
    // Units beyond the window's room wait for the next pump, which regroups
    // them from their first NAL along with the input this scan did not reach.
    if (aus.size() > room) {
        aus.resize(room);
    }

    size_t consumedNals = 0;
    if (!aus.empty()) {
        ReserveSlots(aus.size());
        for (const Au& au : aus) {
            submitAvccAu(BuildAvcc(nals, au.firstNal, au.endNal), au.key);
            consumedNals = au.endNal;
        }
        FeedPocRange(poc_, nals, consumedNals);
    }
    // The callback needs the delay but must not touch poc_, whose state only
    // the feed path owns.
    reorderDelay_.store(poc_.usable() ? poc_.reorderDelay() : 0,
                        std::memory_order_relaxed);

    // At end of input whatever the grouping left over can never become an
    // access unit either -- it is parameter sets or filler after the last
    // picture -- so drop it instead of holding the stream open on it.
    if (inputEof && !scanTruncated && aus.empty()) {
        pendingPos_ += size;
        return;
    }

    // Unconsumed input starts at the first NAL of the first AU that was not
    // submitted, which after all AUs are submitted is the still-open AU (or
    // the end of the buffer once that one was closed by the EOF flush).
    pendingPos_ += consumedNals < nals.size()
                       ? nals[consumedNals].startPos
                       : size;
}

// Moves every frame whose display slot can no longer be contested out of the
// reorder window and into the output queue. Needs mtx_.
void VTDecoder::FlushReorder(bool final) {
    // A picture is settled once the window holds more frames than the stream's
    // own reorder depth: a later frame with a smaller order count would have to
    // come from outside the decoder's picture buffer, which the parameter sets
    // bound. `final` drops the bound entirely -- the stream has ended, so
    // nothing smaller is coming at all.
    //
    // The depth is also capped at half the live-frame window: frames parked here
    // still hold their window slot, and a window the reorder buffer can saturate
    // would stop submissions and starve VideoToolbox of work.
    size_t delay = final ? 0
                         : static_cast<size_t>(reorderDelay_.load(
                               std::memory_order_relaxed));
    delay = std::min(delay, kMaxLiveFrames / 2);
    while (!reorder_.empty() && reorder_.size() > delay) {
        frameQ_.push_back(std::move(reorder_.front().second));
        reorder_.pop_front();
    }
}

int VTDecoder::QueuedFrames() {
    std::lock_guard<std::mutex> lk(mtx_);
    return static_cast<int>(frameQ_.size());
}

bool VTDecoder::IsInputDone() {
    std::lock_guard<std::mutex> lk(mtx_);
    return inputDone_;
}

size_t VTDecoder::FreeSlots() {
    std::lock_guard<std::mutex> lk(mtx_);
    return liveFrames_ < kMaxLiveFrames ? kMaxLiveFrames - liveFrames_ : 0;
}

void VTDecoder::ReserveSlots(size_t n) {
    std::lock_guard<std::mutex> lk(mtx_);
    liveFrames_ += n;
}

void VTDecoder::ReleaseSlot() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (liveFrames_ > 0) {
        --liveFrames_;
    }
}

bool VTDecoder::SignalInputComplete() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        inputDone_ = true;
    }
    // Flush what the window still allows, including the trailing AU that no
    // further input would have completed. Deliberately does not wait for the
    // in-flight samples: those frames are owed to the caller, and releasing
    // here while the window is full would block until the caller drains --
    // which only GetFrame() can do.
    PumpInput();
    cv_.notify_all();
    return true;
}

void VTDecoder::Finalize() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // No input can follow a finalize, so a blocked GetFrame() can treat
        // the stream as finished instead of waiting for frames that never
        // come.
        inputDone_ = true;
        for (auto& f : frameQ_) {
            if (f.release) {
                f.release();
            }
        }
        frameQ_.clear();
        for (auto& slot : reorder_) {
            if (slot.second.release) {
                slot.second.release();
            }
        }
        reorder_.clear();
        liveFrames_ = 0;
    }
    cv_.notify_all();
    pending_.clear();
    pendingPos_ = 0;
    poc_.Reset();
    reorderDelay_.store(0, std::memory_order_relaxed);
    if (decompressionSession) {
        VTDecompressionSessionInvalidate(decompressionSession);
        decompressionSession = nullptr;
    }
    if (formatDescription) {
        CFRelease(formatDescription);
        formatDescription = nullptr;
    }
}

int VTDecoder::PullFrames() {
    // VideoToolbox does not demux containers; the feed path (FillInput) is
    // the only supported input mode.
    return 0;
}

bool VTDecoder::GetFrame(CodecFrame& out) {
    std::unique_lock<std::mutex> lk(mtx_);
    for (;;) {
        if (!frameQ_.empty()) {
            out = std::move(frameQ_.front());
            frameQ_.pop_front();
            // The buffer belongs to the caller now: the slot it occupied is
            // free for the held-back input to move into.
            --liveFrames_;
            lk.unlock();
            PumpInput();
            return true;
        }
        // Nothing queued. Anything still owed to the caller is either inside
        // VideoToolbox, held in the reorder window, or held back in pending_ by
        // the window, so pump before deciding the stream is over.
        lk.unlock();
        PumpInput();
        lk.lock();
        if (liveFrames_ == frameQ_.size() + reorder_.size()) {
            // Nothing is in flight, so no callback can wake this wait and any
            // frame parked in the reorder window would never be handed over.
            if (!reorder_.empty()) {
                FlushReorder(inputDone_);
                if (frameQ_.empty()) {
                    // Even the settled prefix is held back by the delay bound,
                    // and the feed has not caught up with the window: deliver
                    // the oldest picture instead of spinning. At end of stream
                    // FlushReorder(true) above already drained everything, so
                    // this is a stalled live feed.
                    frameQ_.push_back(std::move(reorder_.front().second));
                    reorder_.pop_front();
                }
                continue;
            }
            if (inputDone_) {
                return false;
            }
            // No frames anywhere and no sample in flight: the caller is ahead of
            // its own feed, which only it can continue.
        }
        cv_.wait(lk, [this] {
            // A callback is owed (it notifies when a frame reaches either
            // buffer), plus the terminal wake Finalize() raises so a blocked
            // consumer is released when another thread ends the stream.
            return !frameQ_.empty() || (inputDone_ && liveFrames_ == 0);
        });
    }
}

bool VTDecoder::decodeFrameAsync(uint8_t* data, size_t size,
                                int64_t displayKey) {
    if (!decompressionSession || !data || size == 0) {
        free(data);
        return false;
    }

    // Per Apple's advice: CMBlockBufferCustomBlockSource has misaligned function
    // pointers on 64-bit archs. Fill fields via assignment to avoid link-time
    // alignment issues that result from brace initialization landing the struct
    // in a const segment.
    CMBlockBufferCustomBlockSource customBlockSource;
    customBlockSource.version = 0;
    customBlockSource.AllocateBlock = nullptr;
    customBlockSource.FreeBlock = FreeBlockBufferData;
    customBlockSource.refCon = nullptr;
    CMBlockBufferRef blockBuffer = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        data,
        size,
        kCFAllocatorNull,
        &customBlockSource,
        0,
        size,
        0,
        &blockBuffer
    );
    if (status != noErr) {
        free(data);
        return false;
    }

    CMSampleBufferRef sampleBuffer = nullptr;
    status = CMSampleBufferCreate(
        kCFAllocatorDefault,
        blockBuffer,
        true,
        nullptr,
        nullptr,
        formatDescription,
        1,
        0,
        nullptr,
        0,
        nullptr,
        &sampleBuffer
    );
    CFRelease(blockBuffer);  // sampleBuffer retains it; data freed at last release
    if (status != noErr) {
        return false;
    }

    // The key travels to the callback as the source frame reference (+1, so
    // that a picture ordering of 0 is still distinguishable from "unknown").
    void* sourceFrameRefCon = displayKey >= 0
        ? reinterpret_cast<void*>(
              static_cast<intptr_t>(displayKey + 1))
        : nullptr;
    status = VTDecompressionSessionDecodeFrame(
        decompressionSession,
        sampleBuffer,
        0,
        sourceFrameRefCon,
        nullptr
    );
    CFRelease(sampleBuffer);
    return status == noErr;
}

void VTDecoder::submitAvccAu(const std::vector<uint8_t>& au,
                            int64_t displayKey) {
    auto* copy = static_cast<uint8_t*>(malloc(au.size()));
    if (copy) {
        memcpy(copy, au.data(), au.size());
    }
    // decodeFrameAsync takes ownership of `copy` and reports failure without
    // ever invoking the callback, so such a sample must give its window slot
    // back itself.
    if (copy && decodeFrameAsync(copy, au.size(), displayKey)) {
        return;
    }
    ReleaseSlot();
}

void VTDecoder::DecompressionCallback(
    void* decompressionOutputRefCon,
    void* sourceFrameRefCon,
    OSStatus status,
    VTDecodeInfoFlags infoFlags,
    CVPixelBufferRef imageBuffer,
    CMTime presentationTimeStamp,
    CMTime presentationDuration
) {
    auto* self = static_cast<VTDecoder*>(decompressionOutputRefCon);
    (void)infoFlags;
    (void)presentationDuration;
    // Reattached to the sample at submit time; the raw key needs no lock
    // because only the submit path computes it.
    const int64_t displayKey = sourceFrameRefCon
        ? static_cast<int64_t>(
              reinterpret_cast<intptr_t>(sourceFrameRefCon)) - 1
        : -1;
    if (status != noErr || !imageBuffer) {
        // A sample VideoToolbox rejected never reaches the queue, so it must
        // give its window slot back or the feed path stops making progress.
        self->ReleaseSlot();
        return;
    }

    // Copy the decoded NV12 frame out of the pool buffer so we never depend
    // on its lifetime, then push it onto the frame queue.
    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    size_t w = CVPixelBufferGetWidth(imageBuffer);
    size_t h = CVPixelBufferGetHeight(imageBuffer);
    size_t row0 = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
    size_t row1 = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 1);
    size_t plane0Bytes = row0 * h;
    size_t plane1Bytes = row1 * (h / 2);
    auto* buf = static_cast<uint8_t*>(malloc(plane0Bytes + plane1Bytes));
    if (buf) {
        memcpy(buf, CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0),
               plane0Bytes);
        memcpy(buf + plane0Bytes,
               CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1),
               plane1Bytes);

        CodecFrame frame;
        frame.data = buf;
        frame.size = plane0Bytes + plane1Bytes;
        frame.width = static_cast<int>(w);
        frame.height = static_cast<int>(h);
        frame.format = PixelFormat::NV12;
        frame.strides[0] = row0;
        frame.strides[1] = row1;
        frame.pts = CMTIME_IS_VALID(presentationTimeStamp)
                        ? static_cast<int64_t>(
                              CMTimeGetSeconds(presentationTimeStamp) * 1000.0)
                        : 0;
        frame.release = [buf]() { free(buf); };

        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        // Queue unconditionally: a callback that ever waited for queue room
        // would stall VideoToolbox's own decompression queue, and the feed
        // thread that submits into it is the only one that can drain it. The
        // window in PumpInput is what keeps the frame memory bounded instead.
        {
            std::lock_guard<std::mutex> lk(self->mtx_);
            if (displayKey < 0) {
                // No picture order known: pass the frame straight through in
                // the order VideoToolbox delivered it.
                self->frameQ_.push_back(std::move(frame));
            } else {
                auto where = std::upper_bound(
                    self->reorder_.begin(), self->reorder_.end(), displayKey,
                    [](int64_t key, const std::pair<int64_t, CodecFrame>& slot) {
                        return key < slot.first;
                    });
                self->reorder_.insert(where,
                                      {displayKey, std::move(frame)});
                self->FlushReorder(/*final=*/false);
            }
        }
        self->cv_.notify_one();
    } else {
        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        // No frame reaches the queue, so the slot it reserved is already dead.
        self->ReleaseSlot();
    }
}

HALCODEC_CONNECT(Decoder, vtbox, VTDecoder);

} // namespace vtbox
} // namespace halcodec