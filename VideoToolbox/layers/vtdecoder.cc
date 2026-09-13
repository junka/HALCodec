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

// Annex-B access-unit splitter with AVCC output. VideoToolbox H.264 format
// descriptions created from parameter sets expect length-prefixed (AVCC)
// samples, so each access unit is emitted as a contiguous buffer of
// [len:4][nalu]... entries (start codes stripped).
// Grouping rule (as in FFmpeg's h264_mp4toannexb): a new AU starts at a
// non-VCL NAL (preamble: SEI/SPS/PPS/AUD) that follows a VCL NAL, or at a
// VCL NAL when the current AU already contains one. NAL types 1..5 are VCL.
// Multi-slice frames (first_mb_in_slice != 0) are not disambiguated; that
// requires rbsp parsing and is out of scope for the CLI feed path.
void SplitAnnexBToAvcc(const uint8_t* data, size_t size,
                       std::vector<std::vector<uint8_t>>* aus) {
    struct Nal {
        const uint8_t* payload;  // first byte after the start code
        size_t len;              // payload length
        uint8_t type;
    };
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
        uint8_t type = payload[0] & 0x1F;
        // Locate the next start code to delimit this NAL.
        size_t j = i + sc;
        while (j + 3 < size) {
            if (data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 ||
                 (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
                break;
            }
            ++j;
        }
        nals.push_back({payload, j - (i + sc), type});
        i = j;
    }

    // Group NALs into AUs, converting each to AVCC on the fly.
    const Nal* auBegin = nullptr;
    size_t auPayloadBytes = 0;
    bool hasVcl = false;
    auto flushAu = [&](const Nal* end) {
        if (!auBegin) {
            return;
        }
        std::vector<uint8_t> au;
        au.reserve(auPayloadBytes + 4 * (end - auBegin));
        for (const Nal* n = auBegin; n != end; ++n) {
            au.push_back(static_cast<uint8_t>((n->len >> 24) & 0xFF));
            au.push_back(static_cast<uint8_t>((n->len >> 16) & 0xFF));
            au.push_back(static_cast<uint8_t>((n->len >> 8) & 0xFF));
            au.push_back(static_cast<uint8_t>(n->len & 0xFF));
            au.insert(au.end(), n->payload, n->payload + n->len);
        }
        aus->push_back(std::move(au));
    };

    for (size_t idx = 0; idx < nals.size(); ++idx) {
        const Nal& nal = nals[idx];
        bool vcl = nal.type >= 1 && nal.type <= 5;
        bool preamble = nal.type == 6 || nal.type == 7 || nal.type == 8 ||
                        nal.type == 9;
        if (auBegin == nullptr) {
            auBegin = &nal;
            auPayloadBytes = nal.len;
            hasVcl = vcl;
            continue;
        }
        if (hasVcl && (vcl || preamble)) {
            flushAu(&nal);
            auBegin = &nal;
            auPayloadBytes = nal.len;
            hasVcl = vcl;
            continue;
        }
        auPayloadBytes += nal.len;
        if (vcl) {
            hasVcl = true;
        }
    }
    // Flush the trailing AU only if it contains picture data.
    if (auBegin && hasVcl) {
        flushAu(nals.data() + nals.size());
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
    std::vector<std::vector<uint8_t>> aus;
    SplitAnnexBToAvcc(data, size, &aus);
    for (auto& au : aus) {
        auto* copy = static_cast<uint8_t*>(malloc(au.size()));
        if (!copy) {
            continue;
        }
        memcpy(copy, au.data(), au.size());
        decodeFrameAsync(copy, au.size());  // takes ownership of `copy`
    }
    return 0;  // async: frames are produced in the decompression callback
}

bool VTDecoder::SignalInputComplete() {
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