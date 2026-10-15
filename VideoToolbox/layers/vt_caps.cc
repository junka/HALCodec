// VideoToolbox capability introspection for the "vtbox" backend.
//
// VideoToolbox has no device enumeration API, and decoders are probed per
// codec via VTIsHardwareDecodeSupported; the encoder section lists the real
// hardware encoders via VTCopyVideoEncoderList.

#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "capability.h"
#include "h264au.h"
#include "vt_probe_streams.h"
#include "vtdecoder.h"

namespace halcodec {
namespace vtbox {

namespace {

std::string FourCCToString(OSType cc) {
    char s[5];
    s[0] = static_cast<char>((cc >> 24) & 0xFF);
    s[1] = static_cast<char>((cc >> 16) & 0xFF);
    s[2] = static_cast<char>((cc >> 8) & 0xFF);
    s[3] = static_cast<char>(cc & 0xFF);
    s[4] = '\0';
    return std::string(s);
}

// Appends the string value of `key` to `out`; returns false if absent.
bool AppendDictString(CFDictionaryRef dict, CFStringRef key, CFMutableStringRef out) {
    CFTypeRef v = CFDictionaryGetValue(dict, key);
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) {
        return false;
    }
    CFStringAppend(out, static_cast<CFStringRef>(v));
    return true;
}

// Decodes one embedded Annex-B probe stream through the real VTDecoder path
// and returns the HAL pixel format of the frame that comes back, or
// PixelFormat::Unknown when the hardware refuses the format (session error,
// no frame out). `paramTypes` lists the parameter-set NAL types the probe's
// extradata carries (VPS/SPS/PPS for HEVC, SPS/PPS for H.264).
//
// This is the honest way to answer "what would decoding produce": the same
// feed/complete/drain path a real decode takes, on a stream whose format is
// known exactly. A session-creation probe cannot do this -- VideoToolbox
// happily opens a session for a format whose frames it then downgrades.
PixelFormat ProbeDecode(const unsigned char* stream, unsigned int len,
                        const char* codec,
                        std::initializer_list<uint8_t> paramTypes) {
    std::vector<Nal> nals = ScanNals(stream, len, 64);
    // ScanNals stores Nal.type in the H.264 convention (payload[0] & 0x1F) no
    // matter the codec; the HEVC paths recompute the real 6-bit type from the
    // byte. The probe does the same, so {32,33,34} can mean VPS/SPS/PPS.
    const bool hevc = std::string(codec) == "hevc";
    auto typeOf = [hevc](const Nal& n) -> uint8_t {
        return hevc ? static_cast<uint8_t>((n.payload[0] >> 1) & 0x3F) : n.type;
    };
    std::vector<uint8_t> extradata;
    for (size_t i = 0; i < nals.size(); i++) {
        if (std::find(paramTypes.begin(), paramTypes.end(), typeOf(nals[i])) ==
            paramTypes.end()) {
            continue;
        }
        std::vector<uint8_t> avcc = BuildAvcc(nals, i, i + 1);
        extradata.insert(extradata.end(), avcc.begin(), avcc.end());
    }
    if (extradata.empty()) {
        return PixelFormat::Unknown;
    }

    VTDecoder dec;
    CodecParams params;
    params.codec = codec;
    params.extradata = extradata;
    if (!dec.Initialize(params)) {
        return PixelFormat::Unknown;
    }
    dec.FillInput(stream, len);
    dec.SignalInputComplete();
    CodecFrame frame;
    PixelFormat fmt = PixelFormat::Unknown;
    while (dec.GetFrame(frame)) {
        fmt = frame.format;
    }
    dec.Finalize();
    return fmt;
}

} // namespace

class VTCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "vtbox"; }

    std::vector<std::string> getDeviceNames() const override {
        // VideoToolbox is a system-wide backend; there is no device
        // enumeration API, so nothing is reported.
        return {};
    }

    void showDecoderCapability() const override {
        // VideoToolbox has no enumeration API for decoders, but
        // VTIsHardwareDecodeSupported probes hardware decode availability per
        // codec (public since macOS 10.13). Probe the full video codec set the
        // platform may see in media; unsupported fourCCs safely report false.
        struct Entry {
            CMVideoCodecType type;
            const char* name;
        };
        const Entry codecs[] = {
            { kCMVideoCodecType_MPEG1Video,    "MPEG-1" },
            { kCMVideoCodecType_MPEG2Video,    "MPEG-2" },
            { kCMVideoCodecType_MPEG4Video,    "MPEG-4 Part 2" },
            { kCMVideoCodecType_H263,          "H.263" },
            { kCMVideoCodecType_H264,          "H.264" },
            { kCMVideoCodecType_HEVC,          "HEVC" },
            { kCMVideoCodecType_HEVCWithAlpha, "HEVC w/ alpha" },
            { kCMVideoCodecType_JPEG,          "JPEG" },
            { kCMVideoCodecType_VP9,           "VP9" },
            { kCMVideoCodecType_AV1,           "AV1" },
            { 'apco',                          "ProRes 422 Proxy" },
            { 'apcs',                          "ProRes 422 LT" },
            { 'apcn',                          "ProRes 422" },
            { 'apch',                          "ProRes 422 HQ" },
            { 'ap4h',                          "ProRes 4444" },
            { 'ap4x',                          "ProRes 4444 XQ" },
        };
        bool any = false;
        for (const auto& c : codecs) {
            if (!VTIsHardwareDecodeSupported(c.type)) {
                continue;
            }
            std::cout << "  " << std::left << std::setw(17) << c.name
                      << ": hw decode supported" << std::endl;
            any = true;
        }
        if (!any) {
            std::cout << "  (no hardware decode support detected)" << std::endl;
        }
    }

    // Structured decoder capability, answered by decoding embedded probe
    // streams and reading what comes back. See ProbeDecode above for why the
    // answer goes through a real decode.
    CapabilityProvider::CapsStatus queryDecoderCaps(
        const std::string& codec,
        std::vector<CapabilityProvider::DecodeCapsEntry>& out) const override {
        if (codec == "h264") {
            // The probe stream is Constrained Baseline; the hardware path it
            // exercises is the one every 4:2:0 8-bit profile shares.
            PixelFormat fmt = ProbeDecode(kH264_420_8Stream, kH264_420_8StreamLen,
                                          "h264", {7, 8});
            if (fmt == PixelFormat::Unknown) {
                return CapsStatus::Unknown;
            }
            out.push_back({"Baseline", 8, 420, {fmt}});
            return CapsStatus::Available;
        }
        if (codec == "hevc") {
            // The 4:2:0 8-bit probe doubles as the sanity check: when even
            // that refuses, the probe mechanism is broken and the honest
            // answer is Unknown -- not an empty table.
            PixelFormat base = ProbeDecode(kHevc_420_8Stream, kHevc_420_8StreamLen,
                                           "hevc", {32, 33, 34});
            if (base == PixelFormat::Unknown) {
                return CapsStatus::Unknown;
            }
            out.push_back({"Main", 8, 420, {base}});
            struct Spec {
                const unsigned char* stream;
                unsigned int len;
                const char* profile;
                int depth;
                int chroma;
            };
            const Spec specs[] = {
                {kHevc_420_10Stream, kHevc_420_10StreamLen, "Main 10", 10, 420},
                {kHevc_422_8Stream, kHevc_422_8StreamLen, "Main 4:2:2", 8, 422},
                {kHevc_422_10Stream, kHevc_422_10StreamLen, "Main 4:2:2 10", 10, 422},
            };
            for (const auto& s : specs) {
                PixelFormat fmt = ProbeDecode(s.stream, s.len, "hevc", {32, 33, 34});
                // A refused probe here is a truthful "not supported"; it would
                // only be suspect if the sanity entry had refused too, which
                // is handled above.
                if (fmt == PixelFormat::Unknown) {
                    continue;
                }
                out.push_back({s.profile, s.depth, s.chroma, {fmt}});
            }
            return CapsStatus::Available;
        }
        // jpeg/av1/prores have no probe stream embedded yet; the honest
        // answer for them is Unknown until one is.
        (void)out;
        return CapsStatus::Unknown;
    }

    void showEncoderCapability() const override {
        CFArrayRef encoders = nullptr;
        OSStatus status = VTCopyVideoEncoderList(nullptr, &encoders);
        if (status != noErr || !encoders) {
            std::cout << "  (VTCopyVideoEncoderList failed: " << status << ")" << std::endl;
            return;
        }

        CFIndex count = CFArrayGetCount(encoders);
        if (count == 0) {
            std::cout << "  (no hardware encoders reported)" << std::endl;
            CFRelease(encoders);
            return;
        }

        for (CFIndex i = 0; i < count; i++) {
            CFDictionaryRef dict =
                static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(encoders, i));
            CFMutableStringRef line = CFStringCreateMutable(kCFAllocatorDefault, 0);
            CFStringAppend(line, CFSTR("    "));

            CFTypeRef codecType = CFDictionaryGetValue(dict, kVTVideoEncoderList_CodecType);
            if (codecType && CFGetTypeID(codecType) == CFNumberGetTypeID()) {
                OSType cc = 0;
                CFNumberGetValue(static_cast<CFNumberRef>(codecType),
                                 kCFNumberSInt32Type, &cc);
                CFStringAppendFormat(line, nullptr, CFSTR("codec=%s  "),
                                     FourCCToString(cc).c_str());
            }
            CFStringRef nameKey = CFDictionaryContainsKey(dict,
                              kVTVideoEncoderList_DisplayName)
                                  ? kVTVideoEncoderList_DisplayName
                                  : kVTVideoEncoderList_EncoderName;
            if (!AppendDictString(dict, nameKey, line)) {
                AppendDictString(dict, kVTVideoEncoderList_EncoderID, line);
            }

            char buf[512];
            if (CFStringGetCString(line, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                std::cout << buf << std::endl;
            }
            CFRelease(line);
        }
        CFRelease(encoders);
    }
};

HALCODEC_CONNECT(CapabilityProvider, vtbox, VTCapsProvider);

} // namespace vtbox
} // namespace halcodec