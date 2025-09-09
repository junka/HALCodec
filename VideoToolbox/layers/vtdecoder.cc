#include "vtdecoder.h"

#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <iostream>

namespace halcodec {
namespace vtbox {
void VTDecoder::Initialize(std::string input, std::string format) {
    const uint8_t* sps = nullptr;
    size_t spsSize = 0;
    const uint8_t* pps = nullptr;
    size_t ppsSize = 0;

    const uint8_t* parameterSetPointers[2] = { sps, pps };
    const size_t parameterSetSizes[2] = { spsSize, ppsSize };

    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault,
        2,
        parameterSetPointers,
        parameterSetSizes,
        4,
        &formatDescription
    );

    if (status != noErr) {
        std::cerr << "Failed to create format description: " << status << std::endl;
        return;
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
        return;
    }

    return;
}

void VTDecoder::Finalize() {

}

void VTDecoder::ReleaseFrame(uint8_t **pFrame) {
    // No-op for VideoToolbox as frames are managed by the framework
}

int VTDecoder::FillinFrame() {
    
    return 0;
}

uint8_t* VTDecoder::GetFrame(int *framesize, int *height, int *width, int *n_chan) {
    uint8_t* frame = nullptr;
    return nullptr;
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
    if (status != noErr) {
        std::cerr << "Decode callback error: " << status << std::endl;
        return;
    }

    if (imageBuffer) {
        // Handle the decoded frame (imageBuffer)
        std::cout << "Decoded frame at PTS: " << CMTimeGetSeconds(presentationTimeStamp) << std::endl;
    }
}


static bool registered = []() -> bool {
    VTDecoder::Register();
    return true;
}();

} // namespace vtbox
} // namespace halcodec