// VideoToolbox capability introspection for the "vtbox" backend.
//
// VideoToolbox has no device enumeration API, and decoders are probed per
// codec via VTIsHardwareDecodeSupported; the encoder section lists the real
// hardware encoders via VTCopyVideoEncoderList.

#include <VideoToolbox/VideoToolbox.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "capability.h"

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