// vt_fourcc.h — the codec and pixel-format spellings this backend uses, written
// as the integers they are.
//
// Apple names both by a FourCharCode, and the named constant only exists in the
// SDK that introduced it: kCMVideoCodecType_AV1 needs the macOS 14 SDK,
// kCVPixelFormatType_16VersatileBayer a newer one still. A capability table that
// spells its rows with those constants therefore fails to *compile* on an older
// SDK even though the value is a plain integer and the machine it runs on may
// well support the format. So every code below is a literal, and nothing about
// support is decided at compile time: the runtime probe answers per machine,
// which is the only answer that is true.
//
// Codes whose named constants have existed since long ago (the ProRes tiers,
// HEVC, JPEG, the 8-bit and 10-bit bi-planar YUV pixel formats) keep their long
// names in the codec paths that read them -- every SDK this backend can
// realistically build against has them. Anything Apple named later ('av01' came
// with the macOS 14 SDK, 'sv22'/'sv44'/'bp16' later still) is here, because both
// the data path and the capability table need it.
//
// The bottom half of the file is the other half of the same vocabulary: the
// mappings from HAL's own names (a codec string, a PixelFormat, a profile string)
// onto these codes, shared between the encoder and the capability probe so the
// probe cannot claim a format the data path would not have chosen.
#ifndef VIDEOTOOLBOX_LAYERS_VT_FOURCC_H
#define VIDEOTOOLBOX_LAYERS_VT_FOURCC_H

#include <string>

#include <CoreMedia/CMFormatDescription.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <VideoToolbox/VTCompressionProperties.h>

#include "frame.h"

namespace halcodec {
namespace vtbox {

constexpr OSType FourccOf(char a, char b, char c, char d) {
    return (static_cast<OSType>(a) << 24) | (static_cast<OSType>(b) << 16) |
           (static_cast<OSType>(c) << 8) | static_cast<OSType>(d);
}

// Codec types, as CMVideoFormatDescriptionCreate names them.
constexpr OSType kFccMpeg1 = FourccOf('m', 'p', '1', 'v');
constexpr OSType kFccMpeg2 = FourccOf('m', 'p', '2', 'v');
constexpr OSType kFccMpeg4 = FourccOf('m', 'p', '4', 'v');
constexpr OSType kFccH263 = FourccOf('h', '2', '6', '3');
constexpr OSType kFccH264 = FourccOf('a', 'v', 'c', '1');
constexpr OSType kFccHevc = FourccOf('h', 'v', 'c', '1');
constexpr OSType kFccHevcAlpha = FourccOf('m', 'u', 'x', 'a');
constexpr OSType kFccDolbyVisionHevc = FourccOf('d', 'v', 'h', '1');
constexpr OSType kFccDepthHevc = FourccOf('d', 'e', 'p', 'h');
constexpr OSType kFccDisparityHevc = FourccOf('d', 'i', 's', 'h');
constexpr OSType kFccJpeg = FourccOf('j', 'p', 'e', 'g');
constexpr OSType kFccJpegOpenDml = FourccOf('d', 'm', 'b', '1');
constexpr OSType kFccJpegXl = FourccOf('j', 'x', 'l', 'c');
constexpr OSType kFccVp8 = FourccOf('v', 'p', '0', '8');
constexpr OSType kFccVp9 = FourccOf('v', 'p', '0', '9');
constexpr OSType kFccAv1 = FourccOf('a', 'v', '0', '1');
constexpr OSType kFccProRes422Proxy = FourccOf('a', 'p', 'c', 'o');
constexpr OSType kFccProRes422Lt = FourccOf('a', 'p', 'c', 's');
constexpr OSType kFccProRes422 = FourccOf('a', 'p', 'c', 'n');
constexpr OSType kFccProRes422Hq = FourccOf('a', 'p', 'c', 'h');
constexpr OSType kFccProRes4444 = FourccOf('a', 'p', '4', 'h');
constexpr OSType kFccProRes4444Xq = FourccOf('a', 'p', '4', 'x');
constexpr OSType kFccProResRaw = FourccOf('a', 'p', 'r', 'n');
constexpr OSType kFccProResRawHq = FourccOf('a', 'p', 'r', 'h');

// Pixel formats newer SDKs name, and that the decoder therefore has to ask for
// and recognise by value: 'sv22'/'sv44' are Apple's bi-planar 16-bit 4:2:2 and
// 4:4:4 layouts, 'bp16' the single-plane 16-bit Bayer grid a ProRes RAW picture
// decodes into.
constexpr OSType kPfSv22 = FourccOf('s', 'v', '2', '2');
constexpr OSType kPfSv44 = FourccOf('s', 'v', '4', '4');
constexpr OSType kPfBp16 = FourccOf('b', 'p', '1', '6');

// The mappings from HAL names onto those codes, shared because two callers need
// exactly the same answer: the encoder, which builds its session from them, and
// the capability probe, whose claim is only honest while it exercises the same
// codec type, source pixel format and profile the encoder would. A probe with
// its own copy of a table drifts, and a drifted table advertises a capability the
// data path does not have.
//
// From CodecParams; unknown or empty means H.264.
inline CMVideoCodecType MapCodecType(const std::string& codec) {
    if (codec == "hevc" || codec == "h265") {
        return kCMVideoCodecType_HEVC;
    }
    if (codec == "jpeg" || codec == "mjpeg") {
        return kCMVideoCodecType_JPEG;
    }
    if (codec == "h263") {
        return kCMVideoCodecType_H263;
    }
    if (codec == "prores") {
        // ProRes's bitrate tiers are selected by profile, so this is only the
        // default tier (ProRes 422); the encoder replaces it with whatever
        // --profile asks for.
        return kCMVideoCodecType_AppleProRes422;
    }
    return kCMVideoCodecType_H264;
}

inline bool IsProResType(CMVideoCodecType type) {
    return type == kCMVideoCodecType_AppleProRes422Proxy ||
           type == kCMVideoCodecType_AppleProRes422LT ||
           type == kCMVideoCodecType_AppleProRes422 ||
           type == kCMVideoCodecType_AppleProRes422HQ ||
           type == kCMVideoCodecType_AppleProRes4444 ||
           type == kCMVideoCodecType_AppleProRes4444XQ;
}

// The bitrate tier is the only choice a ProRes encoder offers, so the profile
// field carries it. Empty means the default, ProRes 422.
inline bool MapProResFlavour(const std::string& flavour, CMVideoCodecType* out) {
    if (flavour.empty() || flavour == "standard" || flavour == "422") {
        *out = kCMVideoCodecType_AppleProRes422;
    } else if (flavour == "proxy") {
        *out = kCMVideoCodecType_AppleProRes422Proxy;
    } else if (flavour == "lt") {
        *out = kCMVideoCodecType_AppleProRes422LT;
    } else if (flavour == "hq") {
        *out = kCMVideoCodecType_AppleProRes422HQ;
    } else if (flavour == "4444") {
        *out = kCMVideoCodecType_AppleProRes4444;
    } else if (flavour == "xq") {
        *out = kCMVideoCodecType_AppleProRes4444XQ;
    } else {
        return false;
    }
    return true;
}

// profile 字符串 -> VideoToolbox ProfileLevel 常量(自动 level)。
//
// HEVC 在这里只认 SDK 提供的那五个 profile(SDK 头文件全集:Main、Main10、
// Main42210、Monochrome、Monochrome10,没有 12-bit),认不出来就返回 null 让
// 调用方拒绝。默认分支不能兜底:一个拼错的 "main42210" 若被当成 Main 收下,
// 码流会静默变成 8-bit 4:2:0,而 profile 正是这次要断言的东西。
inline CFStringRef MapProfileLevel(CMVideoCodecType codecType,
                                   const std::string& profile) {
    if (codecType == kCMVideoCodecType_HEVC) {
        if (profile.empty() || profile == "main") {
            return kVTProfileLevel_HEVC_Main_AutoLevel;
        }
        if (profile == "main10") {
            return kVTProfileLevel_HEVC_Main10_AutoLevel;
        }
        if (profile == "main422-10") {
            return kVTProfileLevel_HEVC_Main42210_AutoLevel;
        }
        if (profile == "main-mono") {
            return kVTProfileLevel_HEVC_Monochrome_AutoLevel;
        }
        if (profile == "main-mono-10") {
            return kVTProfileLevel_HEVC_Monochrome10_AutoLevel;
        }
        return nullptr;
    } else if (codecType == kCMVideoCodecType_H263) {
        // VT 只给了三种 Profile/Level: Profile0 Level10(默认), Profile0 Level45,
        // 与 Profile3 Level45(H.263+ 的高级 profile)。用户给 level 即可。
        if (profile == "profile0-level10" || profile == "level10" ||
            profile.empty()) {
            return kVTProfileLevel_H263_Profile0_Level10;
        }
        if (profile == "profile0-level45" || profile == "level45") {
            return kVTProfileLevel_H263_Profile0_Level45;
        }
        if (profile == "profile3-level45" || profile == "level45-pro" ||
            profile == "pro") {
            return kVTProfileLevel_H263_Profile3_Level45;
        }
        return nullptr;
    } else {
        // H.264 —— 默认 Main AutoLevel,空或拼错都走这里,不会 silent 降级。
        if (profile == "baseline") {
            return kVTProfileLevel_H264_Baseline_AutoLevel;
        }
        if (profile == "high") {
            return kVTProfileLevel_H264_High_AutoLevel;
        }
        return kVTProfileLevel_H264_Main_AutoLevel;
    }
}

// 源像素缓冲的 fourcc:位宽由输入格式决定。10-bit 走 FullRange 变体,与 8-bit
// NV12 已经用 FullRange 一致;两种 10-bit fourcc(420v/420r)实测都能建会话
// 出帧,差别只在 range 语义。
//
// ProRes 例外:它的样本天生是 video-range,把同一份源声明成 full-range 会让编码
// 器按 1.164·(v-64) 重标定,实测同一份 NV12 从 55.0 dB 掉到 31.9 dB。所以 ProRes
// 一律取 VideoRange 变体,10-bit 的右对齐样本(ffmpeg 的 p010le/p210le 就是这个
// 排布)直接拷进去即可,不需要移位。
inline OSType SourcePixelFormat(PixelFormat format, CMVideoCodecType codecType) {
    const bool proRes = IsProResType(codecType);
    switch (format) {
        case PixelFormat::P010:
            return proRes ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
                          : kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
        case PixelFormat::P210:
            // 10-bit 4:2:2,16-bit 存储 —— 正是 ProRes 解码回来的那个 fourcc。
            return kPfSv22;
        case PixelFormat::NV12:
            return proRes ? kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
                          : kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
        default:
            // 平面 I420:只有 VideoRange 变体,不需要按码型分叉。
            return kCVPixelFormatType_420YpCbCr8Planar;
    }
}

} // namespace vtbox
} // namespace halcodec

#endif // VIDEOTOOLBOX_LAYERS_VT_FOURCC_H
