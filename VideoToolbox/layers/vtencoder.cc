// VideoToolbox 编码器后端(vtenc):补全 macOS 硬件编码能力。
//
// 数据通路:FillFrame(宿主/raw 帧) -> 池中 CVPixelBuffer(逐行拷贝) ->
// VTCompressionSessionEncodeFrame(异步) -> 压缩回调把 AVCC 访问单元
// (关键帧前附参数集)转成 Annex-B ES 流入队 -> GetFrame() 阻塞取出。
// 与 VTDecoder 对称,均以回调驱动,isAsync() = true。
//
// --codec jpeg 走同一条会话通路,但输出语义是图像而非视频:VideoToolbox 的
// JPEG 编码器一个 sample 就是一条完整 JFIF 码流,不拆前缀、不附参数集,会话
// 属性也只有 Quality 一项(GOP/码率/帧率对单张图没有意义)。
//
// --codec prores 也是同一条会话通路,但两点和视频码型相反:它的每个 sample 都
// 自带一个 32 位长度计数,首尾相接就是元素流,不转 Annex-B;而它的源池必须声明
// video-range —— 实测同一份 NV12 走 full-range 池只有 31.9 dB,走 video-range 池
// 55.0 dB(编码器的时序属性同样只剩帧率一项,见 Initialize)。

#include "vtencoder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>

namespace halcodec {
namespace vtbox {

namespace {

// 从 CodecParams 映射码型;未知/空默认 H.264。
CMVideoCodecType MapCodecType(const std::string& codec) {
    if (codec == "hevc" || codec == "h265") {
        return kCMVideoCodecType_HEVC;
    }
    if (codec == "jpeg" || codec == "mjpeg") {
        return kCMVideoCodecType_JPEG;
    }
    if (codec == "prores") {
        // ProRes's bitrate tiers are selected by profile, so this is only the
        // default tier (ProRes 422); Initialize replaces it with whatever
        // --profile asks for.
        return kCMVideoCodecType_AppleProRes422;
    }
    return kCMVideoCodecType_H264;
}

bool IsProResType(CMVideoCodecType type) {
    return type == kCMVideoCodecType_AppleProRes422Proxy ||
           type == kCMVideoCodecType_AppleProRes422LT ||
           type == kCMVideoCodecType_AppleProRes422 ||
           type == kCMVideoCodecType_AppleProRes422HQ ||
           type == kCMVideoCodecType_AppleProRes4444 ||
           type == kCMVideoCodecType_AppleProRes4444XQ;
}

// The bitrate tier is the only choice a ProRes encoder offers, so the profile
// field carries it. Empty means the default, ProRes 422.
bool MapProResFlavour(const std::string& flavour, CMVideoCodecType* out) {
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
// Initialize 拒绝。默认分支不能兜底:一个拼错的 "main42210" 若被当成 Main 收下,
// 码流会静默变成 8-bit 4:2:0,而 profile 正是这次要断言的东西。
CFStringRef MapProfileLevel(CMVideoCodecType codecType,
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
    }
    if (profile == "baseline") {
        return kVTProfileLevel_H264_Baseline_AutoLevel;
    }
    if (profile == "high") {
        return kVTProfileLevel_H264_High_AutoLevel;
    }
    return kVTProfileLevel_H264_Main_AutoLevel;
}

// 源像素缓冲的 fourcc:位宽由输入格式决定。10-bit 走 FullRange 变体,与本文件
// 8-bit NV12 已经用 FullRange 一致;两种 10-bit fourcc(420v/420r)实测都能建会话
// 出帧,差别只在 range 语义。
//
// ProRes 例外:它的样本天生是 video-range,把同一份源声明成 full-range 会让编码
// 器按 1.164·(v-64) 重标定,实测同一份 NV12 从 55.0 dB 掉到 31.9 dB。所以 ProRes
// 一律取 VideoRange 变体,10-bit 的右对齐样本(ffmpeg 的 p010le/p210le 就是这个
// 排布)直接拷进去即可,不需要移位。
OSType SourcePixelFormat(PixelFormat format, CMVideoCodecType codecType) {
    const bool proRes = IsProResType(codecType);
    switch (format) {
        case PixelFormat::P010:
            return proRes ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
                          : kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
        case PixelFormat::P210:
            // 10-bit 4:2:2,16-bit 存储 —— 正是 ProRes 解码回来的那个 fourcc。
            return kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange;
        case PixelFormat::NV12:
            return proRes ? kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
                          : kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
        default:
            // 平面 I420:只有 VideoRange 变体,不需要按码型分叉。
            return kCVPixelFormatType_420YpCbCr8Planar;
    }
}

} // namespace

bool VTEncoder::Initialize(const CodecParams& params) {
    width_ = params.width;
    height_ = params.height;
    if (width_ <= 0 || height_ <= 0) {
        std::cerr << "VTEncoder: invalid dimensions " << width_ << "x"
                  << height_ << std::endl;
        return false;
    }

    codecType_ = MapCodecType(params.codec);
    // JPEG 是图像编码:没有 GOP、码率、帧率、profile 这些时序概念,一次提交
    // 出一张完整 JFIF 码流。会话属性因此只下发质量一项。
    const bool isJpeg = (codecType_ == kCMVideoCodecType_JPEG);
    // ProRes 是帧内编码:会话属性只剩帧率一项,码型由 --profile 的码率档决定。
    const bool isProRes = IsProResType(codecType_);
    if (isProRes && !MapProResFlavour(params.encode.profile, &codecType_)) {
        std::cerr << "VTEncoder: unknown ProRes flavour \""
                  << params.encode.profile
                  << "\"; use proxy/lt/standard/hq/4444/xq" << std::endl;
        return false;
    }

    // 输入像素格式:I420/NV12(8-bit)与 P010(10-bit,16-bit 存储;含未指定,
    // 按 I420 处理)。P210 只对 ProRes 开放(见下)。
    switch (params.inputFormat) {
        case PixelFormat::NV12:
            inputFormat_ = PixelFormat::NV12;
            break;
        case PixelFormat::P010:
            inputFormat_ = PixelFormat::P010;
            break;
        case PixelFormat::P210:
            // 10-bit 4:2:2 既是 ProRes 的码型,也是 HEVC Main 4:2:2 10 的源池:
            // 实测 sv22 池配 Main42210 出 78458 B、配 Main10 出 60417 B(同一噪声
            // 源)。4:2:0 的池会丢掉一半色度样本,所以除这两家之外仍然拒绝。
            // 具体哪个 profile 合法见下面。
            if (!isProRes && codecType_ != kCMVideoCodecType_HEVC) {
                std::cerr << "VTEncoder: P210 input is only wired up for ProRes "
                             "and HEVC" << std::endl;
                return false;
            }
            inputFormat_ = PixelFormat::P210;
            break;
        case PixelFormat::P016:
            // 12-bit 没有可用的编码通道:VideoToolbox 的 HEVC profile 常量只到
            // Main10 与 Main42210(核对 SDK 头文件),12-bit 无处表达。ProRes
            // 4444 确实是 12-bit,但它的 16-bit 源池按 10-bit 取值(sv22 实测如此),
            // 所以 12-bit 宿主帧在这里同样没有落点。
            std::cerr << "VTEncoder: 12-bit input has no VideoToolbox profile to "
                         "encode it with; use P010 for 10-bit" << std::endl;
            return false;
        case PixelFormat::I420:
        case PixelFormat::Unknown:
            inputFormat_ = PixelFormat::I420;
            break;
        default:
            std::cerr << "VTEncoder: unsupported input format ("
                      << static_cast<int>(params.inputFormat)
                      << "); only I420/NV12/P010/P210 are supported" << std::endl;
            return false;
    }

    // profile 与源位深必须配对,而且要在建会话之前定下来:实测 8-bit 源池配
    // Main10 会被 VideoToolbox 接受,产出 profile_idc=2 却只有 8-bit 精度的码流;
    // 10-bit 源池配 Main 则让 PrepareToEncodeFrames 回 kVTParameterErr,之后每帧
    // 只在压缩回调里报错。两种错都不惊动 Initialize。
    std::string profile = params.encode.profile;
    if (isJpeg) {
        // JPEG has no profile vocabulary in VideoToolbox: setting an H.264 or
        // HEVC ProfileLevel on an image session would be meaningless, so an
        // explicit one is refused rather than silently dropped.
        if (!profile.empty()) {
            std::cerr << "VTEncoder: JPEG has no profiles, ignoring request for \""
                      << profile << '"' << std::endl;
            return false;
        }
    } else if (isProRes) {
        // The profile field already carried the bitrate tier, and ProRes has no
        // ProfileLevel property to set with it.
        profile.clear();
        // ProRes is intra-only at a fixed tier, which leaves frame rate as the
        // only property its session takes. Measured on an apcn session: setting
        // Quality, AverageBitRate, MaxKeyFrameInterval or DataRateLimits returns
        // kVTPropertyNotSupportedErr (-12900) one at a time, while the bulk
        // VTSessionSetProperties answers the same keys with noErr and does
        // nothing -- so a request that reached the session would vanish without
        // a trace. Refuse it here instead.
        std::string asked;
        if (params.encode.bitrateKbps >= 0) {
            asked += " --bitrate";
        }
        if (params.encode.maxBitrateKbps >= 0) {
            asked += " --maxbitrate";
        }
        if (params.encode.quality >= 0) {
            asked += " --quality";
        }
        if (params.encode.gopLength >= 0) {
            asked += " --gop";
        }
        if (params.encode.lowDelay) {
            asked += " --lowdelay";
        }
        if (!asked.empty()) {
            std::cerr << "VTEncoder: ProRes takes none of" << asked
                      << "; its quality is the bitrate tier, chosen with "
                         "--profile (proxy/lt/standard/hq/4444/xq)" << std::endl;
            return false;
        }
    } else if (codecType_ == kCMVideoCodecType_HEVC) {
        // profile 与源池的搭配必须在建会话之前定下来:VideoToolbox 对配错的反应
        // 一律只出现在压缩回调里(-12902/-12905 加 0 字节),Initialize 与
        // EncodeFrame 都回 noErr。下面每条都是实测(RequireHardware, 320x240),
        // 源池按 SourcePixelFormat 取:NV12 -> 420f,I420 -> 8-bit planar,
        // P010 -> xf20,P210 -> sv22。
        const bool tenBit =
            inputFormat_ == PixelFormat::P010 || inputFormat_ == PixelFormat::P210;
        if (profile.empty()) {
            // 输入自己说明位深与码型时不必让调用方指定。
            if (inputFormat_ == PixelFormat::P210) {
                profile = "main422-10";
            } else if (tenBit) {
                profile = "main10";
            }
        }
        if (!profile.empty() && !MapProfileLevel(codecType_, profile)) {
            std::cerr << "VTEncoder: unknown HEVC profile \"" << profile
                      << "\"; the hardware offers main, main10, main422-10, "
                         "main-mono, main-mono-10" << std::endl;
            return false;
        }
        const bool needsTenBit = profile == "main10" || profile == "main422-10" ||
                                 profile == "main-mono-10";
        if (needsTenBit && !tenBit) {
            // 实测:8-bit 源池配 Main10 会被会话接受,吐出 profile_idc=2 却只有
            // 8-bit 精度的码流 —— 静默降级,所以拒绝而不是照做。
            std::cerr << "VTEncoder: profile " << profile
                      << " needs a 10-bit input (-f p010 or -f p210), not "
                         "8-bit" << std::endl;
            return false;
        }
        if (inputFormat_ == PixelFormat::P210 && profile != "main422-10") {
            // 只有 Main 4:2:2 10 会把 4:2:2 源当 4:2:2 编:实测同一 sv22 源配
            // Main10 出 60417 B(4:2:0 的尺寸,色度被减半),配 Monochrome/
            // Monochrome10 则只在回调里回 -12905 加 0 字节。
            std::cerr << "VTEncoder: P210 input keeps its 4:2:2 chroma only with "
                         "profile main422-10, not \"" << profile << '"' << std::endl;
            return false;
        }
    } else if (inputFormat_ == PixelFormat::P010) {
        std::cerr << "VTEncoder: 10-bit input is only wired up for HEVC"
                  << std::endl;
        return false;
    }

    if (params.encode.frameRateNum > 0) {
        fpsN_ = params.encode.frameRateNum;
        fpsD_ = params.encode.frameRateDen > 0 ? params.encode.frameRateDen : 1;
    }

    // 源像素缓冲属性决定编码器内部缓冲池的格式。交给 Session 自建池
    // (VTCompressionSessionGetPixelBufferPool),保证与编码器要求一致;
    // 附加空 IOSurface 字典允许硬件路径使用 IOSurface 作为后备存储。
    CFDictionaryRef sourceAttrs = nullptr;
    {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        OSType pixFmt = SourcePixelFormat(inputFormat_, codecType_);
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault,
                                         kCFNumberSInt32Type, &pixFmt);
        CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, num);
        CFRelease(num);
        CFDictionaryRef emptyDict = CFDictionaryCreate(
            kCFAllocatorDefault, nullptr, nullptr, 0,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey,
                             emptyDict);
        CFRelease(emptyDict);
        sourceAttrs = attrs;
    }

    // 现代 SDK 已无 VTCompressionOutputCallbackRecord,直接传函数指针与 refcon。
    OSStatus status = VTCompressionSessionCreate(
        kCFAllocatorDefault, width_, height_, codecType_, nullptr, sourceAttrs,
        nullptr, CompressionCallback, this, &session_);
    CFRelease(sourceAttrs);
    if (status != noErr) {
        std::cerr << "VTEncoder: failed to create compression session: "
                  << status << std::endl;
        return false;
    }

    // 编码参数一并在 VTSessionSetProperties 批量下发。
    CFMutableDictionaryRef settings = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    if (isJpeg) {
        // 质量是唯一能调的:0..1 的浮点,由 EncodeConfig::quality(1..100)换算。
        // 与 nvjpegenc 一致地默认 75。
        int quality = params.encode.quality;
        if (quality < 0) {
            quality = 75;
        }
        quality = std::max(1, std::min(100, quality));
        const Float32 q = static_cast<Float32>(quality) / 100.0f;
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault,
                                         kCFNumberFloat32Type, &q);
        CFDictionarySetValue(settings, kVTCompressionPropertyKey_Quality, num);
        CFRelease(num);
    } else {
        // ProRes 也走这里:上面已经把码率/GOP/低延迟这些它不认的旋钮拒掉了,
        // 能下发的就只剩帧率。
        CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type,
                                         &fpsN_);
        CFDictionarySetValue(settings,
                             kVTCompressionPropertyKey_ExpectedFrameRate, num);
        CFRelease(num);

        if (params.encode.gopLength > 0) {
            num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type,
                                 &params.encode.gopLength);
            CFDictionarySetValue(settings,
                                 kVTCompressionPropertyKey_MaxKeyFrameInterval,
                                 num);
            CFRelease(num);
        }

        if (params.encode.bitrateKbps > 0) {
            // 码率单位是 bit/s,输入是 kbps。
            SInt64 bps = static_cast<SInt64>(params.encode.bitrateKbps) * 1000;
            num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bps);
            CFDictionarySetValue(settings,
                                 kVTCompressionPropertyKey_AverageBitRate, num);
            CFRelease(num);
        }

        if (params.encode.lowDelay) {
            CFDictionarySetValue(settings, kVTCompressionPropertyKey_RealTime,
                                 kCFBooleanTrue);
            // 低延迟通常不允许 B 帧重排。
            CFDictionarySetValue(settings,
                                 kVTCompressionPropertyKey_AllowFrameReordering,
                                 kCFBooleanFalse);
        }
    }

    status = VTSessionSetProperties(session_, settings);
    CFRelease(settings);
    if (status != noErr) {
        std::cerr << "VTEncoder: failed to set session properties: " << status
                  << std::endl;
        Finalize();
        return false;
    }

    // Profile/Level:有显式取值(或 10-bit 推导值)才覆盖,否则用 VideoToolbox 默认。
    // JPEG 没有 profile 维度,那种请求已在建会话前拒掉了;ProRes 的 profile 字段
    // 装的是码率档,已换成码型,这里不再下发(它也没有 ProfileLevel 属性)。
    if (!profile.empty()) {
        CFStringRef profileLevel = MapProfileLevel(codecType_, profile);
        status = VTSessionSetProperty(
            session_, kVTCompressionPropertyKey_ProfileLevel, profileLevel);
        if (status != noErr) {
            std::cerr << "VTEncoder: failed to set profile " << status
                      << std::endl;
            Finalize();
            return false;
        }
    }

    return true;
}

bool VTEncoder::FillFrame(const CodecFrame& in) {
    // size == 0 是 EOF 标记,走 flush 语义。
    if (in.size == 0) {
        return SignalInputComplete();
    }
    if (!session_) {
        return false;
    }
    // 编码会话尺寸固定,输入尺寸必须一致(hal_enc 的 BMP 路径按文件尺寸
    // 更新 in 尺寸,这里显式拒绝不匹配)。
    if (in.width > 0 &&
        (in.width != width_ || in.height != height_)) {
        std::cerr << "VTEncoder: frame size " << in.width << "x" << in.height
                  << " does not match session " << width_ << "x" << height_
                  << std::endl;
        return false;
    }

    CVPixelBufferRef pixelBuffer = nullptr;
    OSStatus status = CVPixelBufferPoolCreatePixelBuffer(
        kCFAllocatorDefault, VTCompressionSessionGetPixelBufferPool(session_),
        &pixelBuffer);
    if (status != noErr || !pixelBuffer) {
        std::cerr << "VTEncoder: failed to create pixel buffer: " << status
                  << std::endl;
        return false;
    }

    if (!copyFrameToPixelBuffer(in, pixelBuffer)) {
        CVPixelBufferRelease(pixelBuffer);
        return false;
    }

    // PTS = frameIndex / fps;每帧时长 = 1 / fps。
    CMTime pts = CMTimeMake(frameIndex_ * static_cast<int64_t>(fpsD_), fpsN_);
    CMTime duration = CMTimeMake(fpsD_, fpsN_);

    status = VTCompressionSessionEncodeFrame(session_, pixelBuffer, pts,
                                             duration, nullptr, nullptr,
                                             nullptr);
    ++frameIndex_;
    CVPixelBufferRelease(pixelBuffer);
    if (status != noErr) {
        std::cerr << "VTEncoder: encode frame failed: " << status << std::endl;
        return false;
    }
    return true;
}

bool VTEncoder::SignalInputComplete() {
    if (!session_) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            eof_ = true;
        }
        cv_.notify_all();
        return true;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (eof_) {
            return true; // 幂等:eos 可能已由 FillFrame(size==0) 触发
        }
    }
    // 同步等待所有在途帧编码完成(输出经回调已入队),再置 EOF,避免
    // GetFrame 在帧仍在途时提前结束。与 VTDecoder 的
    // WaitForAsynchronousFrames 模式对应。
    VTCompressionSessionCompleteFrames(session_, kCMTimeInvalid);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        eof_ = true;
    }
    cv_.notify_all();
    return true;
}

bool VTEncoder::GetFrame(CodecFrame& out) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this] { return !outQueue_.empty() || eof_; });
    if (outQueue_.empty()) {
        return false; // EOF 且无更多输出
    }
    out = std::move(outQueue_.front());
    outQueue_.pop_front();
    return true;
}

void VTEncoder::Finalize() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        eof_ = true;
        for (auto& f : outQueue_) {
            if (f.release) {
                f.release();
            }
        }
        outQueue_.clear();
    }
    cv_.notify_all();
    if (session_) {
        VTCompressionSessionInvalidate(session_);
        CFRelease(session_);
        session_ = nullptr;
    }
}

VTEncoder::~VTEncoder() {
    Finalize();
}

bool VTEncoder::copyFrameToPixelBuffer(const CodecFrame& in,
                                       CVPixelBufferRef pixelBuffer) {
    const size_t w = static_cast<size_t>(width_);
    const size_t h = static_cast<size_t>(height_);
    const size_t halfW = w / 2;
    const size_t halfH = h / 2;

    // 平面布局(样本宽度由格式决定):
    //   NV12 -> Y(w*h) + UV(w * h/2);I420 -> Y + U + V;
    //   P010 -> 每样本 2 字节:Y(w*h*2) + 交织 UV(w*h/2*2);
    //   P210 -> 每样本 2 字节,色度只水平减半:Y(w*h*2) + UV(w*h*2)。
    const size_t sampleBytes = inputFormat_ == PixelFormat::P010 ||
                                       inputFormat_ == PixelFormat::P210
                                   ? 2
                                   : 1;
    const size_t chromaRows = inputFormat_ == PixelFormat::P210 ? h : h / 2;
    const size_t totalBytes = (w * h + w * chromaRows) * sampleBytes;
    if (in.size < totalBytes) {
        std::cerr << "VTEncoder: input frame too small (" << in.size
                  << " < " << totalBytes << ")" << std::endl;
        return false;
    }

    struct Plane {
        size_t srcOff;   // 源数据内偏移
        size_t rowBytes; // 每行有意义的像素字节数
        size_t rows;
    };
    Plane planes[3];
    int numPlanes = 0;
    if (inputFormat_ == PixelFormat::P210) {
        // 实测 sv22 池的两个平面都是 bytesPerRow = w*2、h 行,和宿主 P210 的
        // 排布一一对应(4:2:2 只在水平方向减半,色度和亮度同高)。
        planes[0] = {0, w * sampleBytes, h};
        planes[1] = {w * h * sampleBytes, w * sampleBytes, h};
        numPlanes = 2;
    } else if (inputFormat_ == PixelFormat::P010) {
        // 实测 320 宽的 10-bit 池缓冲 bytesPerRow = 640 = w*2(两平面同),
        // 色度 h/2 行,与宿主 P010 排布一致;更宽的尺寸由下面的 stride 取小处理。
        planes[0] = {0, w * sampleBytes, h};
        planes[1] = {w * h * sampleBytes, w * sampleBytes, halfH};
        numPlanes = 2;
    } else if (inputFormat_ == PixelFormat::NV12) {
        planes[0] = {0, w, h};
        planes[1] = {w * h, w, halfH};
        numPlanes = 2;
    } else {
        planes[0] = {0, w, h};
        planes[1] = {w * h, halfW, halfH};
        planes[2] = {w * h + halfW * halfH, halfW, halfH};
        numPlanes = 3;
    }

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    for (int p = 0; p < numPlanes; ++p) {
        const uint8_t* src = in.data + planes[p].srcOff;
        // 行距:调用方给出则用之,否则按紧凑排布。
        const size_t srcStride =
            in.strides[p] > 0 ? in.strides[p] : planes[p].rowBytes;
        uint8_t* dst = static_cast<uint8_t*>(
            CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, p));
        const size_t dstStride =
            CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, p);
        // 目的行有硬件对齐的 padding,逐行拷贝即可;取两者较小避免越界。
        const size_t copyBytes = std::min(srcStride, dstStride);
        for (size_t r = 0; r < planes[p].rows; ++r) {
            memcpy(dst + r * dstStride, src + r * srcStride, copyBytes);
        }
    }
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    return true;
}

void VTEncoder::CompressionCallback(void* refcon, void* sourceFrameRefCon,
                                    OSStatus status,
                                    VTEncodeInfoFlags infoFlags,
                                    CMSampleBufferRef sampleBuffer) {
    auto* self = static_cast<VTEncoder*>(refcon);
    (void)sourceFrameRefCon;
    (void)infoFlags;
    if (status != noErr || !sampleBuffer) {
        // 回调是会话属性配错时唯一的报错出口:那种情况下 EncodeFrame 仍返回
        // noErr,失败只在这里体现,静默丢掉就等于整条流没有输出。
        std::cerr << "VTEncoder: encoded frame came back with status " << status
                  << std::endl;
        return;
    }

    // 关键帧判断:同步帧没有 kCMSampleAttachmentKey_NotSync 附件;
    // 非同步帧该附件为 kCFBooleanTrue。
    bool isKeyframe = true;
    CFArrayRef attachments =
        CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        CFDictionaryRef dict = static_cast<CFDictionaryRef>(
            CFArrayGetValueAtIndex(attachments, 0));
        if (dict && CFGetTypeID(dict) == CFDictionaryGetTypeID()) {
            CFTypeRef notSync =
                CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync);
            if (notSync && CFGetTypeID(notSync) == CFBooleanGetTypeID()) {
                isKeyframe = !CFBooleanGetValue(
                    static_cast<CFBooleanRef>(notSync));
            }
        }
    }

    self->emitSample(sampleBuffer, isKeyframe);
}

void VTEncoder::emitSample(CMSampleBufferRef sampleBuffer, bool isKeyframe) {
    CMBlockBufferRef dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!dataBuffer) {
        return;
    }
    const size_t len = CMBlockBufferGetDataLength(dataBuffer);
    if (len == 0) {
        return;
    }

    if (codecType_ == kCMVideoCodecType_JPEG || IsProResType(codecType_)) {
        // JPEG 与 ProRes 都原样透传:一个 sample 就是一条完整码流 —— 一张 JFIF
        // 图,或一帧自带 32 位长度计数的 ProRes(首尾相接即成元素流,不需要转
        // Annex-B,也没有参数集要前置)。用 CopyDataBytes 而不是 GetDataPointer,
        // 是因为块缓冲不连续时后者会失败,而一条码流完全可能被拆成几块。
        std::vector<uint8_t> out(len);
        if (CMBlockBufferCopyDataBytes(dataBuffer, 0, len, out.data()) != noErr) {
            std::cerr << "VTEncoder: could not read the encoded image"
                      << std::endl;
            return;
        }
        queueEncoded(out, sampleBuffer);
        return;
    }

    char* avcc = nullptr;
    size_t avccLen = 0;
    OSStatus status =
        CMBlockBufferGetDataPointer(dataBuffer, 0, nullptr, &avccLen, &avcc);
    if (status != noErr || !avcc || avccLen == 0) {
        return;
    }

    std::vector<uint8_t> out;
    const auto appendStartCode = [&out]() {
        out.insert(out.end(), {0x00, 0x00, 0x00, 0x01});
    };
    const auto appendNalu = [&out, &appendStartCode](const uint8_t* data,
                                                     size_t size) {
        appendStartCode();
        out.insert(out.end(), data, data + size);
    };

    // 关键帧:参数集(SPS/PPS;HEVC 还多 VPS)不出现在样本数据里,需要从
    // format description 取出后先写入流。返回的数据是 AVCC 布局:每条
    // 参数集由 [naluHeaderSize 字节长度前缀][NAL] 组成,可能多条连排。
    if (isKeyframe) {
        CMFormatDescriptionRef desc =
            CMSampleBufferGetFormatDescription(sampleBuffer);
        if (desc) {
            const bool hevc = (codecType_ == kCMVideoCodecType_HEVC);
            for (size_t i = 0;; ++i) {
                const uint8_t* ps = nullptr;
                size_t psSize = 0, count = 0;
                int naluHeader = 0;
                OSStatus s =
                    hevc
                        ? CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                              desc, i, &ps, &psSize, &count, &naluHeader)
                        : CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                              desc, i, &ps, &psSize, &count, &naluHeader);
                if (s != noErr) {
                    break;
                }
                // naluHeader 异常时按典型值 4 处理。
                if (naluHeader <= 0 || naluHeader > 4) {
                    naluHeader = 4;
                }
                size_t off = 0;
                size_t emitted = 0;
                while (off + static_cast<size_t>(naluHeader) <= psSize) {
                    uint32_t len = 0;
                    for (int b = 0; b < naluHeader; ++b) {
                        len = (len << 8) | ps[off + b];
                    }
                    if (len == 0 ||
                        off + static_cast<size_t>(naluHeader) + len > psSize) {
                        break;
                    }
                    appendNalu(ps + off + naluHeader, len);
                    off += static_cast<size_t>(naluHeader) + len;
                    ++emitted;
                }
                if (emitted == 0 && psSize > 0) {
                    // 前缀解析失败(非 AVCC 布局),整段按一个 NAL 输出兜底。
                    appendNalu(ps, psSize);
                }
            }
        }
    }

    // AVCC([len:4][nalu]...) -> Annex-B(00 00 00 01 + nalu)。
    size_t pos = 0;
    while (pos + 4 <= len) {
        uint32_t naluLen = (static_cast<uint32_t>(static_cast<uint8_t>(avcc[pos])) << 24) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(avcc[pos + 1])) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(avcc[pos + 2])) << 8) |
                           static_cast<uint32_t>(static_cast<uint8_t>(avcc[pos + 3]));
        pos += 4;
        if (pos + naluLen > len) {
            break;
        }
        appendNalu(reinterpret_cast<const uint8_t*>(avcc + pos), naluLen);
        pos += naluLen;
    }
    if (out.empty()) {
        return;
    }
    queueEncoded(out, sampleBuffer);
}

// 把一条已编码的码流(视频帧的 Annex-B AU 或一张完整 JFIF 图)拷成独立缓冲入队。
void VTEncoder::queueEncoded(const std::vector<uint8_t>& out,
                             CMSampleBufferRef sampleBuffer) {
    auto* buf = static_cast<uint8_t*>(malloc(out.size()));
    if (!buf) {
        return;
    }
    memcpy(buf, out.data(), out.size());

    CodecFrame frame;
    frame.data = buf;
    frame.size = out.size();
    frame.width = width_;
    frame.height = height_;
    frame.format = PixelFormat::Unknown; // 编码后的元素流
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    frame.pts = CMTIME_IS_VALID(pts)
                    ? static_cast<int64_t>(CMTimeGetSeconds(pts) * 1000.0)
                    : 0;
    frame.release = [buf]() { free(buf); };

    {
        std::lock_guard<std::mutex> lk(mtx_);
        outQueue_.push_back(std::move(frame));
    }
    cv_.notify_one();
}

HALCODEC_CONNECT(Encoder, vtenc, VTEncoder);

} // namespace vtbox
} // namespace halcodec