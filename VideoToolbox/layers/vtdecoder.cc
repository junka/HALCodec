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

// One access unit as a half-open range over the NAL vector, so the caller
// decides *after* grouping which units to build (and can leave the rest for
// the next pump).
struct Au {
    size_t firstNal;
    size_t endNal;
};

// Groups NALs into access units and reports their ranges. The AU boundary rule
// matches FFmpeg's h264_mp4toannexb: a new AU starts at a NAL that follows a
// VCL NAL and is itself VCL or a preamble (SEI/SPS/PPS/AUD, types 6..9). The
// AU still open at the end is emitted only when `closeOpen` (the input ended,
// so its trailing NAL is complete); otherwise the caller holds its bytes back
// for the next feed.
std::vector<Au> GroupAvcc(const std::vector<Nal>& nals, bool closeOpen) {
    std::vector<Au> aus;
    size_t auBegin = 0;
    bool hasVcl = false;
    for (size_t idx = 0; idx < nals.size(); ++idx) {
        const Nal& nal = nals[idx];
        const bool vcl = nal.type >= 1 && nal.type <= 5;
        const bool preamble = nal.type == 6 || nal.type == 7 ||
                              nal.type == 8 || nal.type == 9;
        if (idx > 0 && hasVcl && (vcl || preamble)) {
            aus.push_back({auBegin, idx});
            auBegin = idx;
            hasVcl = vcl;
        } else if (vcl) {
            hasVcl = true;
        }
    }
    if (closeOpen && hasVcl) {
        aus.push_back({auBegin, nals.size()});
    }
    return aus;
}

// Feeds an access unit's parameter sets to the picture order front end and
// returns the display-order key of the picture it starts, or -1 when the
// stream's order cannot be established yet (no SPS/PPS seen, an unsupported
// pic_order_cnt_type, an unparsable slice header).
int64_t PictureKey(H264Poc& poc, const std::vector<Nal>& nals, size_t begin,
                   size_t end) {
    int64_t key = -1;
    for (size_t k = begin; k < end; ++k) {
        const Nal& nal = nals[k];
        const bool vcl = nal.type >= 1 && nal.type <= 5;
        if (!vcl) {
            poc.FeedParameterSet(nal.payload, nal.len);
            continue;
        }
        if (key >= 0) {
            continue;  // a further slice of the same picture
        }
        int64_t value = 0;
        if (poc.ClassifySlice(nal.payload, nal.len, &value)
                != H264Poc::Slice::Unknown) {
            key = value;
        }
    }
    return poc.usable() ? key : -1;
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

    // Scan only roughly the NALs needed for the available room -- an AU is a
    // frame plus its optional SPS/PPS/SEI prefix -- rather than the whole
    // buffer, which may hold the rest of the stream.
    const size_t maxNals = 4 * room + 8;
    const std::vector<Nal> nals = ScanNals(data, size, maxNals);
    const bool inputEof = IsInputDone();
    // A scan that stopped at maxNals may be missing the NAL that closes the
    // last AU, so only reaching the end of the input makes it safe to emit.
    const bool scanTruncated = nals.size() == maxNals;
    if (nals.empty()) {
        // No complete start code yet (a 3/4-byte start code may itself straddle
        // the chunk boundary); at end of input the leftovers are trailing
        // garbage, and keeping them would leave the stream looking unfinished.
        if (inputEof && !scanTruncated) {
            pendingPos_ += size;
        }
        return;
    }

    const std::vector<Au> aus =
        GroupAvcc(nals, /*closeOpen=*/inputEof && !scanTruncated);

    size_t consumedNals = 0;
    for (const Au& au : aus) {
        if (!ReserveSlot()) {
            break;
        }
        const int64_t key = PictureKey(poc_, nals, au.firstNal, au.endNal);
        submitAvccAu(BuildAvcc(nals, au.firstNal, au.endNal), key);
        consumedNals = au.endNal;
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

bool VTDecoder::ReserveSlot() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (liveFrames_ >= kMaxLiveFrames) {
        return false;
    }
    ++liveFrames_;
    return true;
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