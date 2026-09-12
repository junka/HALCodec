#include "vtdecoder.h"

#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <iostream>

namespace halcodec {
namespace vtbox {

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

void VTDecoder::Finalize() {
    if (decompressionSession) {
        VTDecompressionSessionInvalidate(decompressionSession);
        decompressionSession = nullptr;
    }
    if (formatDescription) {
        CFRelease(formatDescription);
        formatDescription = nullptr;
    }
}

int VTDecoder::FillinFrame() {
    return 0;
}

bool VTDecoder::GetFrame(CodecFrame& out) {
    (void)out;
    return false;
}

bool VTDecoder::decodeFrame(const uint8_t* data, size_t size) {
    if (!decompressionSession) {
        std::cerr << "Decompression session is not initialized." << std::endl;
        return false;
    }

    CMBlockBufferRef blockBuffer = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        (void*)data,
        size,
        kCFAllocatorNull,
        nullptr,
        0,
        size,
        0,
        &blockBuffer
    );

    if (status != noErr) {
        std::cerr << "Failed to create block buffer: " << status << std::endl;
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

    CFRelease(blockBuffer);

    if (status != noErr) {
        std::cerr << "Failed to create sample buffer: " << status << std::endl;
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

    if (status != noErr) {
        std::cerr << "Failed to decode frame: " << status << std::endl;
        return false;
    }

    return true;
}

void VTDecoder::DecompressionCallback(
    void* decompressionOutputRefCon,
    void* sourceFrameRefCon,
    OSStatus status,
    VTDecodeInfoFlags infoFlags,
    CVImageBufferRef imageBuffer,
    CMTime presentationTimeStamp,
    CMTime presentationDuration
) {
    (void)decompressionOutputRefCon;
    (void)sourceFrameRefCon;
    (void)infoFlags;
    (void)presentationDuration;
    if (status != noErr) {
        std::cerr << "Decode callback error: " << status << std::endl;
        return;
    }

    if (imageBuffer) {
        // Handle the decoded frame (imageBuffer)
        std::cout << "Decoded frame at PTS: " << CMTimeGetSeconds(presentationTimeStamp) << std::endl;
    }
}

HALCODEC_CONNECT(Decoder, vtbox, VTDecoder);

} // namespace vtbox
} // namespace halcodec