#include "vtdecoder.h"

#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CMFormatDescriptionBridge.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

#include "av1desc.h"
#include "h264au.h"
#include "jpegdesc.h"
#include "proresdesc.h"

namespace halcodec {
namespace vtbox {

namespace {

// Deallocator for the per-access-unit copies owned by CMBlockBuffers. Runs
// when the block buffer is fully released, i.e. after VideoToolbox has
// finished decoding the sample.
void FreeBlockBufferData(void*, void* block, size_t) {
    free(block);
}

// A pixel format type as the four characters Apple names it by, so a rejected
// buffer can be identified in the log by what VideoToolbox calls it.
std::string FourCC(OSType cc) {
    char text[5] = {static_cast<char>(cc >> 24), static_cast<char>(cc >> 16),
                    static_cast<char>(cc >> 8), static_cast<char>(cc), '\0'};
    return text;
}

// ... and back again, for the formats this SDK has no constant for. The
// decoder's 10-bit output ('pf20') is one of them.
constexpr OSType FourccOf(char a, char b, char c, char d) {
    return (static_cast<OSType>(a) << 24) | (static_cast<OSType>(b) << 16) |
           (static_cast<OSType>(c) << 8) | static_cast<OSType>(d);
}

// One row of the decoder's packed 10-bit plane to one row of 16-bit samples.
//
// 'pf20' stores three 10-bit samples per little-endian 32-bit word and pads
// every row to a 64-byte pitch -- 448 bytes for a 320-wide picture, neither the
// 640 a 16-bit row would need nor a multiple of the packed row, so the tail of
// each row is padding and the row has to be decoded rather than copied.
//
// The packed samples are the bare 10-bit numbers, while HAL's P010 (like
// ffmpeg's p010le, and like the 'xf20' encode pool this repository already
// fills) carries them in the top ten bits of each 16-bit word: a decoded luma
// of 832 has to leave as 53248. Unpacked this way the output matches a reference
// decode byte for byte, on luma and on both interleaved chroma components.
void UnpackPacked10Row(const uint8_t* src, size_t srcBytes, size_t samples,
                       uint8_t* dst) {
    size_t produced = 0;
    for (size_t off = 0; off + 4 <= srcBytes && produced < samples; off += 4) {
        uint32_t word = 0;
        // The buffer is host memory on this machine's byte order, which is the
        // order the samples are packed in.
        memcpy(&word, src + off, sizeof(word));
        for (int i = 0; i < 3 && produced < samples; ++i, ++produced) {
            uint16_t value =
                static_cast<uint16_t>(((word >> (10 * i)) & 0x3FF) << 6);
            memcpy(dst + produced * sizeof(value), &value, sizeof(value));
        }
    }
}

// 'sv44' parks Cb and Cr in one plane, two 16-bit samples wide per luma sample
// (measured: plane 1's row is 4*w bytes, the even slot Cb and the odd slot Cr).
// HAL's planar 4:4:4 keeps them in separate planes, so a row is split rather than
// copied. The samples pass through untouched: the buffer is already at 16-bit
// scale, the same numbers ffmpeg's yuv444p16le carries for the same codestream
// (53237 against 53232 for one luma sample, which is decoder rounding rather than
// a rescaling the output path should compensate for).
void SplitCbCrRow(const uint8_t* src, size_t samples, uint8_t* dstCb,
                  uint8_t* dstCr) {
    for (size_t i = 0; i < samples; ++i) {
        memcpy(dstCb + i * sizeof(uint16_t), src + i * 4, sizeof(uint16_t));
        memcpy(dstCr + i * sizeof(uint16_t), src + i * 4 + 2, sizeof(uint16_t));
    }
}

// A Bayer grid says nothing on its own about which sensel is which colour, or
// what the numbers mean: VideoToolbox carries that as buffer attachments. Only
// the three a consumer cannot do without are read here -- the phase order and the
// two levels the samples are scaled between -- which is also all of the raw
// metadata HAL's frame model has a place for. Measured on Apple's ProRes RAW
// material, where they arrive as pattern 0 (RGGB), black 256 and white 61568.
void ReadBayerAttachments(CVPixelBufferRef pb, BayerInfo* out) {
    auto number = [pb](CFStringRef key, double* value) {
        CFTypeRef attachment = CVBufferCopyAttachment(pb, key, nullptr);
        if (!attachment) {
            return false;
        }
        const bool got = CFGetTypeID(attachment) == CFNumberGetTypeID() &&
                         CFNumberGetValue(static_cast<CFNumberRef>(attachment),
                                          kCFNumberFloat64Type, value);
        CFRelease(attachment);
        return got;
    };

    double pattern = -1;
    if (number(kCVPixelBufferVersatileBayerKey_BayerPattern, &pattern) &&
        pattern >= 0 && pattern <= 3) {
        out->pattern = static_cast<BayerPattern>(pattern);
    }
    double level = 0;
    if (number(kCVPixelBufferProResRAWKey_BlackLevel, &level)) {
        out->blackLevel = static_cast<uint16_t>(level);
    }
    if (number(kCVPixelBufferProResRAWKey_WhiteLevel, &level)) {
        out->whiteLevel = static_cast<uint16_t>(level);
    }
}

} // namespace

bool VTDecoder::Initialize(const CodecParams& params) {
    const auto& ext = params.extradata;
    if (ext.empty()) {
        std::cerr << "VTDecoder: no extradata provided" << std::endl;
        return false;
    }
    
    // Detect codec from the first NAL in extradata (AVCC layout: the first NAL
    // starts 4 bytes in). HEVC carries a 2-byte header with a 6-bit type at
    // (b0 >> 1) & 0x3F -- VPS/SPS/PPS are 32/33/34; H.264 has a 1-byte header
    // with a 5-bit type at b0 & 0x1F -- SPS/PPS are 7/8. Only a 32/33/34 under
    // the HEVC reading means HEVC: an H.264 SPS byte (0x67) maps to 51, never
    // into that range, so the test is unambiguous.
    //
    // JPEG and ProRes are decided first: their leading bytes are checked before
    // the NAL sniff, because a codestream's fourth and fifth bytes are whatever
    // the producer put there and could read as a parameter set type.
    isProRes_ = params.codec == "prores" || params.codec == "appleprores" ||
                LooksLikeProRes(ext.data(), ext.size());
    if (isProRes_) {
        return InitializeProRes(params);
    }
    isJPEG_ = params.codec == "jpeg" || params.codec == "mjpeg" ||
              LooksLikeJpeg(ext.data(), ext.size());
    if (isJPEG_) {
        return InitializeJPEG(params);
    }

    bool isHEVC = false;
    if (ext.size() >= 5) {
        const uint8_t hevcType = static_cast<uint8_t>((ext[4] >> 1) & 0x3F);
        if (hevcType == 32 || hevcType == 33 || hevcType == 34) {
            isHEVC = true;
        }
    }

    // AV1 is decided before that sniff, not after it: an extradata that opens
    // with an in-band sequence header (0a 0b ...) has a 0x00 at the offset the
    // NAL reading uses, which reads as neither HEVC nor H.264 and would send the
    // stream to the H.264 path. The caller's codec name is authoritative when it
    // gives one; otherwise a leading temporal delimiter announces the stream.
    isAV1_ = params.codec == "av1" || IsAv1ObuStream(ext.data(), ext.size());

    isHEVC_ = isHEVC;  // Store for PumpInput to use

    if (isAV1_) {
        return InitializeAV1(params);
    }
    if (isHEVC) {
        return InitializeHEVC(params);
    } else {
        return InitializeH264(params);
    }
}

bool VTDecoder::InitializeH264(const CodecParams& params) {
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
        std::cerr << "Failed to create H.264 format description: " << status << std::endl;
        return false;
    }

    return CreateSession();
}

bool VTDecoder::InitializeHEVC(const CodecParams& params) {
    // Parse HEVC parameter sets from extradata (AVCC layout).
    // Order: VPS (type 32), SPS (type 33), PPS (type 34).
    const uint8_t* vpsPointer = nullptr;
    size_t vpsSize = 0;
    const uint8_t* spsPointer = nullptr;
    size_t spsSize = 0;
    const uint8_t* ppsPointer = nullptr;
    size_t ppsSize = 0;
    
    const auto& ext = params.extradata;
    size_t pos = 0;
    while (pos + 4 <= ext.size()) {
        uint32_t naluLen = (static_cast<uint32_t>(ext[pos]) << 24) |
                           (static_cast<uint32_t>(ext[pos + 1]) << 16) |
                           (static_cast<uint32_t>(ext[pos + 2]) << 8) |
                           static_cast<uint32_t>(ext[pos + 3]);
        pos += 4;
        if (pos + naluLen > ext.size()) {
            break;
        }
        // HEVC NAL type lives in bits 1-6 of the first header byte.
        uint8_t naluType = static_cast<uint8_t>((ext[pos] >> 1) & 0x3F);
        if (naluType == 32) {
            vpsPointer = ext.data() + pos;
            vpsSize = naluLen;
        } else if (naluType == 33) {
            spsPointer = ext.data() + pos;
            spsSize = naluLen;
        } else if (naluType == 34) {
            ppsPointer = ext.data() + pos;
            ppsSize = naluLen;
        }
        pos += naluLen;
    }
    
    if (!spsPointer || !ppsPointer) {
        std::cerr << "VTDecoder: missing SPS/PPS in HEVC extradata" << std::endl;
        return false;
    }
    
    // Build parameter set arrays for VideoToolbox.
    // VPS is optional for CreateFromHEVCParameterSets but usually present.
    const uint8_t* psPointers[3];
    size_t psSizes[3];
    size_t psCount = 0;
    
    if (vpsPointer) {
        psPointers[psCount] = vpsPointer;
        psSizes[psCount] = vpsSize;
        ++psCount;
    }
    psPointers[psCount] = spsPointer;
    psSizes[psCount] = spsSize;
    ++psCount;
    psPointers[psCount] = ppsPointer;
    psSizes[psCount] = ppsSize;
    ++psCount;

    OSStatus status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
        kCFAllocatorDefault,
        psCount,
        psPointers,
        psSizes,
        4,
        nullptr,  // extensions
        &formatDescription
    );

    if (status != noErr) {
        std::cerr << "Failed to create HEVC format description: " << status << std::endl;
        return false;
    }

    // Feed parameter sets to the HEVC POC front end for display-order reordering.
    if (vpsPointer) {
        hevcPoc_.FeedParameterSet(vpsPointer, vpsSize);
    }
    hevcPoc_.FeedParameterSet(spsPointer, spsSize);
    hevcPoc_.FeedParameterSet(ppsPointer, ppsSize);
    reorderDelay_.store(hevcPoc_.usable() ? hevcPoc_.reorderDelay() : 0,
                        std::memory_order_relaxed);
    
    return CreateSession();
}

bool VTDecoder::InitializeAV1(const CodecParams& params) {
    // AV1 has no parameter-set call to open a session with: CoreMedia offers no
    // CreateFromAV1ParameterSets, so the description is built as the container
    // stores it -- an `av01` sample entry carrying an `av1C` record -- and handed
    // to the big-endian image description bridge.
    const auto& ext = params.extradata;

    // The extradata is a raw OBU stream and its sequence header OBUs are what a
    // container keeps as av1C config OBUs. Anything else in the range belongs to
    // a picture or carries no description, so it is not what the record is built
    // from and is dropped.
    std::vector<uint8_t> configObus;
    const std::vector<Av1Obu> obus = ScanObus(ext.data(), ext.size(), 32);
    for (const Av1Obu& obu : obus) {
        if (obu.type != kAv1ObuSequenceHeader) {
            continue;
        }
        configObus.insert(configObus.end(), ext.data() + obu.start,
                          ext.data() + obu.start + obu.size);
    }
    if (configObus.empty()) {
        std::cerr << "VTDecoder: no sequence header OBU in AV1 extradata"
                  << std::endl;
        return false;
    }

    // The first header describes the stream the session opens on; VideoToolbox
    // checks the record built from it against the in-band headers of the frames,
    // and refuses the session when the two disagree.
    Av1SequenceHeader header;
    if (!ParseAv1SequenceHeader(configObus.data(), configObus.size(), &header)) {
        std::cerr << "VTDecoder: could not parse the AV1 sequence header"
                  << std::endl;
        return false;
    }
    if (header.unsupported) {
        std::cerr << "VTDecoder: " << header.unsupported << std::endl;
        return false;
    }

    const std::vector<uint8_t> entry =
        BuildAv01SampleEntry(header, configObus.data(), configObus.size());
    OSStatus status = CMVideoFormatDescriptionCreateFromBigEndianImageDescriptionData(
        kCFAllocatorDefault,
        entry.data(),
        entry.size(),
        kCFStringEncodingMacRoman,
        kCMImageDescriptionFlavor_ISOFamily,
        &formatDescription
    );
    if (status != noErr) {
        std::cerr << "Failed to create AV1 format description: " << status
                  << std::endl;
        return false;
    }

    return CreateSession();
}

bool VTDecoder::InitializeJPEG(const CodecParams& params) {
    // A JPEG states its picture size in a frame header marker, and CoreMedia has
    // no call that builds a description out of JPEG bytes: the description is just
    // a codec type plus dimensions, and every codestream byte travels in the
    // sample. So the header has to be read here, before a session can open.
    const auto& ext = params.extradata;
    JpegCodestream stream;
    if (!FindJpegCodestream(ext.data(), ext.size(), &stream)) {
        std::cerr << "VTDecoder: no JPEG frame header in the input" << std::endl;
        return false;
    }
    // Only what has been measured against a reference decoder is accepted. A
    // gray or 4:4:4 image decodes into a buffer that is not the two-plane NV12
    // the output path hands back, and pretending otherwise would mislabel its
    // pixels rather than refuse the stream.
    if (stream.precision != 8 || !stream.subsampled420) {
        std::cerr << "VTDecoder: JPEG with precision " << stream.precision
                  << " and " << stream.componentCount
                  << " components is not supported; only 8-bit 4:2:0 is"
                  << std::endl;
        return false;
    }

    OSStatus status = CMVideoFormatDescriptionCreate(
        kCFAllocatorDefault, kCMVideoCodecType_JPEG, stream.width, stream.height,
        nullptr, &formatDescription);
    if (status != noErr) {
        std::cerr << "Failed to create JPEG format description: " << status
                  << std::endl;
        return false;
    }

    return CreateSession();
}

bool VTDecoder::InitializeProRes(const CodecParams& params) {
    // A ProRes picture has no parameter set to build a description from, and the
    // description's dimensions are not checked against the stream: measured, a
    // session created with the wrong size comes back with a buffer of the
    // *described* size, so the picture it hands over is cropped or padded rather
    // than rejected. The frame header is therefore the only honest source of the
    // size, and it has to be read before a session opens.
    const auto& ext = params.extradata;
    ProresFrame frame;
    if (!ReadProresFrame(ext.data(), ext.size(), &frame)) {
        std::cerr << "VTDecoder: no ProRes frame header in the input"
                  << std::endl;
        return false;
    }
    isProResRaw_ = frame.isRaw;
    // A RAW picture decodes to a Bayer grid rather than to YUV, and the session
    // has to be told which raw layout to produce: left to itself it picks its own
    // four-plane one ('b16q'), which HAL has no format for. The documented
    // single-plane 16-bit grid ('bp16', kCVPixelFormatType_16VersatileBayer) is
    // asked for instead, and measured on 4112x2176 material it costs nothing --
    // 78.06 fps against the native layout's 77.68.
    const OSType requestedFormat =
        isProResRaw_ ? kCVPixelFormatType_16VersatileBayer : 0;
    // Neither choice below pins the flavour: the codec type in the description is
    // not checked against the bitstream's own -- measured, an apcn description
    // decodes a 4:4:4 picture into 'sv44' just the same, and these RAW frames
    // decode identically described as 'aprn' -- so what the frame header says is
    // enough, and the flavour lives in the frame.
    // 4:4:4 decodes into a bi-planar 16-bit 'sv44' picture; 4:2:2 lands on
    // 'sv22'. Both are named by the output path.
    OSType codecType = 0;
    if (isProResRaw_) {
        // ProRes RAW HQ, the tag the containers carrying this material use. No
        // piece of RAW material exists here that says which of the two RAW
        // flavours its bytes are, and the two decode the same.
        codecType = kCMVideoCodecType_AppleProResRAWHQ;
    } else {
        codecType = frame.is444 ? kCMVideoCodecType_AppleProRes4444
                                : kCMVideoCodecType_AppleProRes422;
    }

    OSStatus status = CMVideoFormatDescriptionCreate(
        kCFAllocatorDefault, codecType, frame.width,
        frame.height, nullptr, &formatDescription);
    if (status != noErr) {
        std::cerr << "Failed to create ProRes format description: " << status
                  << std::endl;
        return false;
    }

    return CreateSession(requestedFormat);
}

bool VTDecoder::CreateSession(OSType requestedFormat) {
    VTDecompressionOutputCallbackRecord callback;
    callback.decompressionOutputCallback = DecompressionCallback;
    callback.decompressionOutputRefCon = this;

    // Asking for a destination format is the only way to name the pixel layout
    // the callback will get. A fourcc the decoder cannot produce is refused here
    // rather than silently ignored: -12910 (kVTVideoDecoderUnsupportedDataFormatErr).
    CFMutableDictionaryRef destinationAttributes = nullptr;
    if (requestedFormat) {
        destinationAttributes = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFNumberRef formatNumber = CFNumberCreate(
            kCFAllocatorDefault, kCFNumberSInt32Type, &requestedFormat);
        CFDictionarySetValue(destinationAttributes,
                             kCVPixelBufferPixelFormatTypeKey, formatNumber);
        CFRelease(formatNumber);
    }

    OSStatus status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        formatDescription,
        nullptr,
        destinationAttributes,
        &callback,
        &decompressionSession
    );
    if (destinationAttributes) {
        CFRelease(destinationAttributes);
    }

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
    // A ProRes stream is neither a NAL assembly nor an OBU run, and it has its
    // own delimiting: every picture states its byte count.
    if (isProRes_) {
        PumpProResInput();
        return;
    }
    // A JPEG is neither a NAL assembly nor an OBU run, and a single file holds a
    // single image; it has its own scan.
    if (isJPEG_) {
        PumpJpegInput();
        return;
    }
    // AV1 units are not NAL assemblies and carry no picture order, so none of
    // the grouping below applies to them; it has its own scan.
    if (isAV1_) {
        PumpAv1Input();
        return;
    }
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
        if (isHEVC_) {
            HEVCPoc trial = hevcPoc_;
            aus = GroupHevcAUs(trial, nals, nals.size(),
                               /*closeOpen=*/inputEof && !scanTruncated);
        } else {
            H264Poc trial = poc_;
            aus = GroupAUs(trial, nals, nals.size(),
                           /*closeOpen=*/inputEof && !scanTruncated);
        }
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
        if (isHEVC_) {
            HEVCPoc trial = hevcPoc_;
            aus = GroupHevcAUs(trial, nals, nals.size(), /*closeOpen=*/true);
        } else {
            H264Poc trial = poc_;
            aus = GroupAUs(trial, nals, nals.size(), /*closeOpen=*/true);
        }
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
            const std::vector<uint8_t> avcc =
                BuildAvcc(nals, au.firstNal, au.endNal);
            submitAu(avcc.data(), avcc.size(), au.key);
            consumedNals = au.endNal;
        }
        if (isHEVC_) {
            FeedHevcPocRange(hevcPoc_, nals, consumedNals);
        } else {
            FeedPocRange(poc_, nals, consumedNals);
        }
    }
    // The callback needs the delay but must not touch poc_/hevcPoc_, whose state
    // only the feed path owns.
    if (isHEVC_) {
        reorderDelay_.store(hevcPoc_.usable() ? hevcPoc_.reorderDelay() : 0,
                            std::memory_order_relaxed);
    } else {
        reorderDelay_.store(poc_.usable() ? poc_.reorderDelay() : 0,
                            std::memory_order_relaxed);
    }

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

// Submits as many complete temporal units as the live-frame window has room for,
// the AV1 counterpart of PumpInput. The scan is shaped the same way because the
// constraints are: fixed-size feed chunks let a unit straddle one, and only a scan
// that reached the delimiter opening the unit *after* the last one it emits bounds
// that unit. What differs is the delimiting itself -- AV1 has no start code to
// resynchronise on, so input that stops parsing as OBUs is not submitted -- and the
// key each unit carries, which is always "unknown": an OBU states no display
// position, and VideoToolbox returns AV1 frames in submission order.
void VTDecoder::PumpAv1Input() {
    const size_t room = FreeSlots();
    if (!decompressionSession || room == 0) {
        return;
    }
    const uint8_t* data = pending_.data() + pendingPos_;
    const size_t size = pending_.size() - pendingPos_;
    if (size == 0) {
        return;
    }

    constexpr size_t kMaxScanObus = 4096;
    const bool inputEof = IsInputDone();
    // OBU bound: enough to fill the window with units, with slack for the tile
    // groups and metadata OBUs a single frame can be split across.
    size_t maxObus = 8 * room + 64;
    std::vector<Av1TemporalUnit> units;
    bool scanTruncated = false;
    for (;;) {
        units = GroupAv1TemporalUnits(data, size, room, maxObus,
                                      /*closeOpen=*/false, &scanTruncated);
        if (!units.empty() || !scanTruncated || maxObus >= kMaxScanObus) {
            break;
        }
        maxObus *= 2;
    }
    if (units.empty() && !inputEof) {
        return;
    }
    if (units.empty()) {
        // No unit closed, and no further delimiter is coming to close the one
        // still open, so submit what there is of it rather than strand it. A unit
        // whose last OBU never arrived in full goes out too: VideoToolbox rejects
        // the sample and the slot it held comes back either way.
        units = GroupAv1TemporalUnits(data, size, room, kMaxScanObus,
                                      /*closeOpen=*/true, &scanTruncated);
        if (units.empty()) {
            // Nothing in the leftovers ever formed a unit. At end of input they
            // are trailing bytes, and keeping them would leave the stream looking
            // unfinished.
            if (!scanTruncated) {
                pendingPos_ += size;
            }
            return;
        }
    }

    ReserveSlots(units.size());
    for (const Av1TemporalUnit& unit : units) {
        submitAu(data + unit.firstByte, unit.endByte - unit.firstByte,
                 /*displayKey=*/-1);
    }
    // Everything up to the end of the last submitted unit is consumed, the bytes
    // ahead of the first delimiter with them -- they belong to no unit.
    pendingPos_ += units.back().endByte;
}

// Submits the pending codestream once it is whole. An image is one sample and
// carries no picture order, so there is nothing to group and nothing to reorder:
// the only thing the feed path has to decide is whether the end of image has
// arrived yet, since a 256 KiB chunk can stop in the middle of the entropy data.
void VTDecoder::PumpJpegInput() {
    const size_t room = FreeSlots();
    if (!decompressionSession || room == 0) {
        return;
    }
    const uint8_t* data = pending_.data() + pendingPos_;
    const size_t size = pending_.size() - pendingPos_;
    if (size == 0) {
        return;
    }
    JpegCodestream stream;
    if (!FindJpegCodestream(data, size, &stream)) {
        // Nothing here can open a sample. With no further input coming the bytes
        // are trailing garbage, and holding them would leave the stream looking
        // unfinished.
        if (IsInputDone()) {
            pendingPos_ += size;
        }
        return;
    }
    if (!stream.complete && !IsInputDone()) {
        return;  // the rest of the image may still be on its way
    }
    ReserveSlots(1);
    submitAu(data + stream.start, stream.size, /*displayKey=*/-1);
    // At end of input an incomplete span runs to the end of the buffer, so the
    // whole remainder is consumed either way.
    pendingPos_ += stream.start + stream.size;
}

// Submits as many whole pictures as the live-frame window has room for. ProRes
// needs no scan bound and no grouping pass: a picture's own 32-bit count says
// exactly where it ends, so the split is a walk over lengths and everything a
// feed chunk cannot account for is a tail still on its way. Intra-only, so every
// unit carries no order key and the callback queues frames as they land.
void VTDecoder::PumpProResInput() {
    const size_t room = FreeSlots();
    if (!decompressionSession || room == 0) {
        return;
    }
    const uint8_t* data = pending_.data() + pendingPos_;
    const size_t size = pending_.size() - pendingPos_;
    if (size == 0) {
        return;
    }
    const bool inputEof = IsInputDone();

    size_t pos = 0;
    for (size_t i = 0; i < room; ++i) {
        ProresFrame frame;
        // Either the bytes here are no frame header, or the picture they declare
        // has not fully arrived. Both mean nothing more can be submitted: a
        // truncated frame comes back from VideoToolbox as -12902 with no picture,
        // so at end of input the remainder is dropped rather than handed over --
        // keeping it would leave the stream looking unfinished.
        if (!ReadProresFrame(data + pos, size - pos, &frame) ||
            !frame.complete) {
            if (inputEof) {
                pos = size;
            }
            break;
        }
        ReserveSlots(1);
        submitAu(data + pos, frame.size, /*displayKey=*/-1);
        pos += frame.size;
    }
    pendingPos_ += pos;
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
    hevcPoc_.Reset();
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
        if (!frameQ_.empty()) {
            // The pump's sample was small enough for VideoToolbox to finish it
            // inside the call, so the frame it owed arrived here rather than
            // through a later wake-up. Take it before deciding nothing is left.
            continue;
        }
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
    // A JPEG sample is the codestream itself and a ProRes sample one whole
    // picture, so either has to say how long it is: the block buffer may carry
    // padding a video access unit's length prefixes would otherwise account for.
    // Video samples keep going without size entries, which is how the video paths
    // have always been submitted.
    const bool sizedSample = isJPEG_ || isProRes_;
    const size_t sampleSizeArray[1] = {size};
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
        sizedSample ? 1 : 0,
        sizedSample ? sampleSizeArray : nullptr,
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

void VTDecoder::submitAu(const uint8_t* au, size_t size, int64_t displayKey) {
    auto* copy = static_cast<uint8_t*>(malloc(size));
    if (copy) {
        memcpy(copy, au, size);
    }
    // decodeFrameAsync takes ownership of `copy` and reports failure without
    // ever invoking the callback, so such a sample must give its window slot
    // back itself.
    if (copy && decodeFrameAsync(copy, size, displayKey)) {
        return;
    }
    std::cerr << "VTDecoder: access unit of " << size
              << " bytes was not handed to the session" << std::endl;
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
        std::cerr << "VTDecoder: decoded frame came back with status " << status
                  << std::endl;
        // A sample VideoToolbox rejected never reaches the queue, so it must
        // give its window slot back or the feed path stops making progress.
        self->ReleaseSlot();
        return;
    }

    // The buffer's own fourcc decides both what the bytes are and how many rows
    // its chroma plane has: ProRes comes back as a 16-bit picture whose chroma
    // plane is as tall as the luma one, so the h/2 rows an 8-bit 4:2:0 frame
    // carries would drop half of it. Formats this output path cannot name -- a
    // 10-bit 4:2:2 'p422', say -- are refused instead of being labelled NV12 and
    // handed over with the wrong depth and subsampling.
    const size_t w = CVPixelBufferGetWidth(imageBuffer);
    const size_t h = CVPixelBufferGetHeight(imageBuffer);
    const OSType fourcc = CVPixelBufferGetPixelFormatType(imageBuffer);
    PixelFormat format = PixelFormat::Unknown;
    size_t chromaRows = 0;
    bool packed10 = false;
    bool planar444 = false;
    bool bayerGrid = false;
    if (fourcc == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        fourcc == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        format = PixelFormat::NV12;
        chromaRows = h / 2;
    } else if (fourcc == kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange) {
        format = PixelFormat::P210;
        chromaRows = h;
    } else if (fourcc == kCVPixelFormatType_444YpCbCr16BiPlanarVideoRange) {
        // ProRes 4:4:4. Both planes are as tall as the picture, and the chroma
        // one is twice as wide as the luma one because it holds Cb and Cr for
        // every luma sample.
        format = PixelFormat::YUV444P16LE;
        chromaRows = h;
        planar444 = true;
    } else if (fourcc == kCVPixelFormatType_16VersatileBayer) {
        // ProRes RAW: one plane of 16-bit sensels, the grid at the picture's full
        // size. The OS's own demosaic stage is not reached on this machine --
        // measured, VTRAWProcessingSessionCreate answers -12907 even for a buffer
        // that has just been decoded -- so sensels are as far as a hardware
        // decode goes, and what they mean travels in the attachments.
        format = PixelFormat::BAYER16LE;
        bayerGrid = true;
    } else if (fourcc == FourccOf('p', 'f', '2', '0') ||
               fourcc == FourccOf('p', '4', '2', '0')) {
        // 10-bit 4:2:0. Which of the two a stream lands on is its *range*, not
        // its codec: an x265 Main 10 file comes back as 'p420' while the same
        // picture encoded by the hardware is 'pf20'. Chroma is interleaved Cb:Cr
        // like NV12's, one full row of w samples per h/2 rows, only 10-bit and
        // packed -- see UnpackPacked10Row.
        format = PixelFormat::P010;
        chromaRows = h / 2;
        packed10 = true;
    }
    if (format == PixelFormat::Unknown) {
        // Measured layouts this path still cannot name: 10-bit 4:2:2 ('p422'),
        // greyscale ('L008', 'L010'), and a RAW picture left to pick its own
        // output ('b16q', four planes of one Bayer phase each), which is why the
        // RAW session asks for 'bp16' instead. Handing one of these over under an
        // existing format would be a wrong-labelled frame; the reason is printed.
        std::cerr << "VTDecoder: decoded frame came back as '" << FourCC(fourcc)
                  << "', which HAL has no pixel format for" << std::endl;
        self->ReleaseSlot();
        return;
    }

    // Copy the decoded frame out of the pool buffer so we never depend on its
    // lifetime, then push it onto the frame queue. Planes are read by their own
    // base address and packed one after the other: the hardware parks the chroma
    // plane past alignment padding that is not part of the picture.
    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    size_t row0 = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
    size_t row1 = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 1);
    // A packed 10-bit row, a Bayer row and a 16-bit chroma row are all padded to
    // a 64-byte pitch (measured: 'bp16' gives 8256 bytes a row for a 4112-wide
    // grid whose own row is 8224), which is a different width from the row the HAL
    // format carries, so the output strides are the format's own and those rows
    // move one at a time.
    const size_t outRow0 = (packed10 || planar444 || bayerGrid) ? w * 2 : row0;
    const size_t outRow1 = (packed10 || planar444 || bayerGrid) ? w * 2 : row1;
    size_t plane0Bytes = outRow0 * h;
    size_t plane1Bytes = outRow1 * chromaRows;
    // A planar 4:4:4 picture splits its interleaved chroma into a third plane.
    const size_t plane2Bytes = planar444 ? plane1Bytes : 0;
    auto* buf =
        static_cast<uint8_t*>(malloc(plane0Bytes + plane1Bytes + plane2Bytes));
    if (buf) {
        const uint8_t* src0 = static_cast<const uint8_t*>(
            CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0));
        const uint8_t* src1 = static_cast<const uint8_t*>(
            CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1));
        if (packed10) {
            for (size_t r = 0; r < h; ++r) {
                UnpackPacked10Row(src0 + r * row0, row0, w, buf + r * outRow0);
            }
            for (size_t r = 0; r < chromaRows; ++r) {
                UnpackPacked10Row(src1 + r * row1, row1, w,
                                  buf + plane0Bytes + r * outRow1);
            }
        } else if (planar444) {
            for (size_t r = 0; r < h; ++r) {
                memcpy(buf + r * outRow0, src0 + r * row0, outRow0);
            }
            uint8_t* dstCb = buf + plane0Bytes;
            uint8_t* dstCr = dstCb + plane1Bytes;
            for (size_t r = 0; r < chromaRows; ++r) {
                SplitCbCrRow(src1 + r * row1, w, dstCb + r * outRow1,
                             dstCr + r * outRow1);
            }
        } else if (bayerGrid) {
            // The sensels pass straight through, rows de-padded and nothing else:
            // measured, the grid keeps the black level in its numbers rather than
            // subtracting it (a RAW frame spans 205 to 62108 around the 256 and
            // 61568 levels its attachments name), so a consumer that wants
            // normalised samples does the arithmetic the levels make possible.
            for (size_t r = 0; r < h; ++r) {
                memcpy(buf + r * outRow0, src0 + r * row0, outRow0);
            }
        } else {
            memcpy(buf, src0, plane0Bytes);
            memcpy(buf + plane0Bytes, src1, plane1Bytes);
        }

        CodecFrame frame;
        frame.data = buf;
        frame.size = plane0Bytes + plane1Bytes + plane2Bytes;
        frame.width = static_cast<int>(w);
        frame.height = static_cast<int>(h);
        frame.format = format;
        frame.strides[0] = outRow0;
        // A Bayer grid is one plane: no chroma stride to report.
        frame.strides[1] = bayerGrid ? 0 : outRow1;
        frame.strides[2] = planar444 ? outRow1 : 0;
        if (bayerGrid) {
            // Sensels say which colour they are only together with the phase order
            // and the levels they were captured between, and those live on the
            // buffer, not in its pixels.
            ReadBayerAttachments(imageBuffer, &frame.bayer);
        }
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