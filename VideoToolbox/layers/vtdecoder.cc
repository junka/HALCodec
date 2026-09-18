#include "vtdecoder.h"

#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
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
// chunk and is therefore held pending until its end is seen.
struct Nal {
    size_t startPos;  // offset of the NAL's start code in the buffer
    const uint8_t* payload;
    size_t len;
    uint8_t type;
};

std::vector<Nal> ScanNals(const uint8_t* data, size_t size) {
    std::vector<Nal> nals;
    size_t i = 0;
    while (i + 3 < size) {
        if (data[i] != 0 || data[i + 1] != 0) {
            ++i;
            continue;
        }
        size_t sc = 3;
        if (data[i + 2] == 1) {
            sc = 3;
        } else if (data[i + 2] == 0 && data[i + 3] == 1) {
            sc = 4;
        } else {
            ++i;
            continue;
        }
        const uint8_t* payload = data + i + sc;
        size_t j = i + sc;
        while (j + 3 < size) {
            if (data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 ||
                 (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
                break;
            }
            ++j;
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

// Groups NALs into access units, appending each completed AU to `aus`. The
// AU boundary rule matches FFmpeg's h264_mp4toannexb: a new AU starts at a
// NAL that follows a VCL NAL and is itself VCL or a preamble
// (SEI/SPS/PPS/AUD, types 6..9). Reports in `openAuStart` the index of the
// first NAL of the AU still open at the end. The open AU is emitted only
// when `isFinal` (the stream ended, so its trailing NAL is complete);
// otherwise it is held back for the next feed chunk.
void GroupAvcc(const std::vector<Nal>& nals, bool isFinal,
               std::vector<std::vector<uint8_t>>* aus, size_t* openAuStart) {
    size_t auBegin = 0;
    bool hasVcl = false;
    for (size_t idx = 0; idx < nals.size(); ++idx) {
        const Nal& nal = nals[idx];
        const bool vcl = nal.type >= 1 && nal.type <= 5;
        const bool preamble = nal.type == 6 || nal.type == 7 ||
                              nal.type == 8 || nal.type == 9;
        if (idx > 0 && hasVcl && (vcl || preamble)) {
            aus->push_back(BuildAvcc(nals, auBegin, idx));
            auBegin = idx;
            hasVcl = vcl;
        } else if (vcl) {
            hasVcl = true;
        }
    }
    if (isFinal && hasVcl) {
        aus->push_back(BuildAvcc(nals, auBegin, nals.size()));
    }
    *openAuStart = (isFinal && hasVcl) ? nals.size() : auBegin;
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
    // Append to the unterminated tail of the previous chunk, so a NAL that
    // straddles a chunk boundary is reassembled before AU grouping. These
    // chunks are fixed-size reads from the CLI feed path, so a NAL can end
    // mid-chunk; splitting each chunk in isolation would truncate its slices.
    std::vector<uint8_t> combined;
    if (!pending_.empty()) {
        combined.reserve(pending_.size() + size);
        combined.insert(combined.end(), pending_.begin(), pending_.end());
        combined.insert(combined.end(), data, data + size);
    } else {
        combined.assign(data, data + size);
    }

    const std::vector<Nal> nals = ScanNals(combined.data(), combined.size());
    if (nals.empty()) {
        // No complete start code yet (a 3/4-byte start code may itself
        // straddle the chunk boundary); hold everything for the next chunk.
        pending_ = std::move(combined);
        return 0;
    }

    std::vector<std::vector<uint8_t>> aus;
    size_t openAuStart = 0;
    GroupAvcc(nals, /*isFinal=*/false, &aus, &openAuStart);
    for (const auto& au : aus) {
        submitAvccAu(au);
    }
    // The open AU ends in a possibly incomplete NAL (everything before
    // nals[openAuStart] was emitted above); keep its bytes for the next
    // chunk to complete.
    pending_.assign(combined.data() + nals[openAuStart].startPos,
                    combined.data() + combined.size());
    return 0;  // async: frames are produced in the decompression callback
}

bool VTDecoder::SignalInputComplete() {
    // Flush the AU held back across chunk boundaries: no more input is
    // coming, so the trailing NAL is now definitively complete.
    if (!pending_.empty()) {
        const std::vector<Nal> nals =
            ScanNals(pending_.data(), pending_.size());
        if (!nals.empty() && decompressionSession) {
            std::vector<std::vector<uint8_t>> aus;
            size_t openAuStart = 0;
            GroupAvcc(nals, /*isFinal=*/true, &aus, &openAuStart);
            for (const auto& au : aus) {
                submitAvccAu(au);
            }
        }
        pending_.clear();
    }
    // Wait for every submitted sample to be decoded (the decompression
    // callback flushes into frameQ_ before this returns), so that EOF does
    // not race with frames still in flight.
    if (decompressionSession) {
        VTDecompressionSessionWaitForAsynchronousFrames(decompressionSession);
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        eof_ = true;
    }
    cv_.notify_all();
    return true;
}

void VTDecoder::Finalize() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        eof_ = true;
        for (auto& f : frameQ_) {
            if (f.release) {
                f.release();
            }
        }
        frameQ_.clear();
    }
    cv_.notify_all();
    pending_.clear();
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
    cv_.wait(lk, [this] { return !frameQ_.empty() || eof_; });
    if (frameQ_.empty()) {
        return false;
    }
    out = std::move(frameQ_.front());
    frameQ_.pop_front();
    return true;
}

bool VTDecoder::decodeFrameAsync(uint8_t* data, size_t size) {
    if (!decompressionSession || !data || size == 0) {
        free(data);
        return false;
    }

    CMBlockBufferCustomBlockSource customBlockSource{0, nullptr,
                                                     FreeBlockBufferData,
                                                     nullptr};
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

    status = VTDecompressionSessionDecodeFrame(
        decompressionSession,
        sampleBuffer,
        0,
        nullptr,
        nullptr
    );
    CFRelease(sampleBuffer);
    return status == noErr;
}

void VTDecoder::submitAvccAu(const std::vector<uint8_t>& au) {
    auto* copy = static_cast<uint8_t*>(malloc(au.size()));
    if (!copy) {
        return;
    }
    memcpy(copy, au.data(), au.size());
    decodeFrameAsync(copy, au.size());  // takes ownership of `copy`
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
    (void)sourceFrameRefCon;
    (void)infoFlags;
    (void)presentationDuration;
    if (status != noErr || !imageBuffer) {
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

        std::lock_guard<std::mutex> lk(self->mtx_);
        self->frameQ_.push_back(std::move(frame));
    }
    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    self->cv_.notify_one();
}

HALCODEC_CONNECT(Decoder, vtbox, VTDecoder);

} // namespace vtbox
} // namespace halcodec