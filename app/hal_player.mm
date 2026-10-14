// hal_player — macOS VT 硬解 + AVSampleBufferDisplayLayer 零拷贝渲染.
// 数据流:Annex-B → NAL 切分(含 codec 检测) → Picture 分组(AU) → AVCC 打包
// → VTDecompressionSessionDecodeFrame(硬解) → VT callback → dispatch 主队列
// → CVPixelBuffer → CMSampleBuffer → AVSampleBufferDisplayLayer.enqueue

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <iomanip>

#include <unistd.h>
#include <mach/mach_time.h>

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreImage/CoreImage.h>
#import <AppKit/AppKit.h>

namespace {

// ---- 高精度计时 ----
static inline uint64_t NowNs() { return mach_absolute_time(); }
static inline double NsToSec(uint64_t ns) {
    static mach_timebase_info_data_t info;
    static int once = (mach_timebase_info(&info), 1);
    (void)once;
    return (double)(ns * info.numer) / (double)info.denom / 1e9;
}

struct Nal { size_t start; size_t size; int type; };  // type >= 0
struct Au { int start; int end; };

// 第一遍:只扫 parameter sets 来定 codec
CMVideoCodecType DetectCodec(const std::vector<uint8_t>& data) {
    size_t i = 0, n = data.size();
    while (i + 3 < n) {
        size_t sc = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) sc = 3;
        else if (i + 3 < n && data[i] == 0 && data[i + 1] == 0 &&
                 data[i + 2] == 0 && data[i + 3] == 1) sc = 4;
        if (!sc) { ++i; continue; }
        size_t start = i + sc;
        // H.264:nal_type = b0.b4 (5-bit); HEVC:nal_type = b1.b6 (6-bit)
        uint8_t b = data[start];
        uint8_t h264t = b & 0x1F;
        uint8_t hevct = (b >> 1) & 0x3F;
        if (h264t == 7 || h264t == 8) return kCMVideoCodecType_H264;
        if (hevct >= 32 && hevct <= 34) return kCMVideoCodecType_HEVC;  // VPS/SPS/PPS
        ++i;
    }
    return kCMVideoCodecType_H264;  // fallback
}

std::vector<Nal> SplitAnnexb(const std::vector<uint8_t>& data, CMVideoCodecType codec) {
    std::vector<Nal> out;
    size_t i = 0, n = data.size();
    while (i + 3 < n) {
        size_t sc = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) sc = 3;
        else if (i + 3 < n && data[i] == 0 && data[i + 1] == 0 &&
                 data[i + 2] == 0 && data[i + 3] == 1) sc = 4;
        if (!sc) { ++i; continue; }
        size_t start = i + sc;
        size_t j = start + 1;
        for (; j + 3 < n; ++j) {
            if (data[j] != 0 || data[j + 1] != 0) continue;
            if (data[j + 2] == 1 || (j + 3 < n && data[j + 2] == 0 && data[j + 3] == 1)) break;
        }
        uint8_t b = data[start];
        int nalType = (codec == kCMVideoCodecType_H264) ? (b & 0x1F) : ((b >> 1) & 0x3F);
        out.push_back({start, j - start, nalType});
        i = j;
    }
    return out;
}

struct ParamSets { int spsIdx=-1, ppsIdx=-1, vpsIdx=-1; };
ParamSets FindParamSets(const std::vector<Nal>& nals, CMVideoCodecType codec) {
    ParamSets ps;
    if (codec == kCMVideoCodecType_H264) {
        for (int i=0; i<(int)nals.size(); i++) {
            if (nals[i].type == 7 && ps.spsIdx < 0) ps.spsIdx = i;
            else if (nals[i].type == 8 && ps.ppsIdx < 0) ps.ppsIdx = i;
        }
    } else {
        for (int i=0; i<(int)nals.size(); i++) {
            int t = nals[i].type;
            if (t == 33 && ps.spsIdx < 0) ps.spsIdx = i;
            else if (t == 34 && ps.ppsIdx < 0) ps.ppsIdx = i;
            else if (t == 32 && ps.vpsIdx < 0) ps.vpsIdx = i;
        }
    }
    return ps;
}

CMFormatDescriptionRef BuildFormatDescription(const std::vector<uint8_t>& data,
                                              const std::vector<Nal>& nals,
                                              const ParamSets& ps,
                                              CMVideoCodecType codec) {
    if (codec == kCMVideoCodecType_H264 && ps.spsIdx >= 0 && ps.ppsIdx >= 0) {
        const uint8_t* ptrs[2] = { data.data()+nals[ps.spsIdx].start, data.data()+nals[ps.ppsIdx].start };
        size_t sizes[2] = { nals[ps.spsIdx].size, nals[ps.ppsIdx].size };
        CMFormatDescriptionRef fd = nullptr;
        OSStatus st = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, ptrs, sizes, 4, &fd);
        if (st == noErr) return fd;
    }
    if (codec == kCMVideoCodecType_HEVC && ps.vpsIdx >= 0 && ps.spsIdx >= 0 && ps.ppsIdx >= 0) {
        const uint8_t* ptrs[3] = { data.data()+nals[ps.vpsIdx].start,
                                   data.data()+nals[ps.spsIdx].start,
                                   data.data()+nals[ps.ppsIdx].start };
        size_t sizes[3] = { nals[ps.vpsIdx].size, nals[ps.spsIdx].size, nals[ps.ppsIdx].size };
        CMFormatDescriptionRef fd = nullptr;
        OSStatus st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault, 3, ptrs, sizes, 4, nullptr, &fd);
        if (st == noErr) return fd;
    }
    return nullptr;
}

// Picture 分组:H.264 VCL 1-5 是 picture 起点,HEVC VCL 0-9 和 16-21 是 picture 起点
std::vector<Au> GroupAUs(const std::vector<Nal>& nals, CMVideoCodecType codec) {
    std::vector<Au> aus;
    int curStart = -1;
    for (int i=0; i<(int)nals.size(); i++) {
        int t = nals[i].type;
        bool isVCL;
        if (codec == kCMVideoCodecType_H264) {
            isVCL = (t >= 1 && t <= 5);
        } else {
            // HEVC: VCL = 0-9 (trailing/TSA/STSA/RADL/RASL) + 16-21 (BLA/IDR/CRA)
            isVCL = (t <= 9) || (t >= 16 && t <= 21);
        }
        if (isVCL) {
            if (curStart >= 0) aus.push_back({curStart, i-1});
            curStart = i;
        }
    }
    if (curStart >= 0) aus.push_back({curStart, (int)nals.size()-1});
    return aus;
}

bool IsKeyFrame(const std::vector<Nal>& nals, int picStart, CMVideoCodecType codec) {
    int t = nals[picStart].type;
    return (codec == kCMVideoCodecType_H264) ? (t == 5) : (t == 16 || t == 17);
}

std::vector<uint8_t> BuildAvcc(const std::vector<uint8_t>& data,
                               const std::vector<Nal>& nals,
                               const Au& au, const ParamSets& ps,
                               CMVideoCodecType codec) {
    bool isKey = IsKeyFrame(nals, au.start, codec);
    std::vector<uint8_t> avcc;
    auto push = [&](int ni) {
        uint32_t len = (uint32_t)nals[ni].size;
        avcc.push_back((len>>24)&0xFF); avcc.push_back((len>>16)&0xFF);
        avcc.push_back((len>>8)&0xFF); avcc.push_back(len&0xFF);
        avcc.insert(avcc.end(), data.begin()+nals[ni].start, data.begin()+nals[ni].start+nals[ni].size);
    };
    if (isKey) {
        if (codec == kCMVideoCodecType_HEVC && ps.vpsIdx >= 0 && ps.vpsIdx < au.start) push(ps.vpsIdx);
        if (ps.spsIdx >= 0 && ps.spsIdx < au.start) push(ps.spsIdx);
        if (ps.ppsIdx >= 0 && ps.ppsIdx < au.start) push(ps.ppsIdx);
    }
    for (int i=au.start; i<=au.end; i++) push(i);
    return avcc;
}

struct DecodeCtx {
    VTDecompressionSessionRef session = nullptr;
    AVSampleBufferDisplayLayer* layer = nil;
    std::atomic<int64_t> frameCount{0};

    // ---- perf ----
    int totalFrames = 0;                      // 期望总帧数 (主线程填入)
    std::vector<uint64_t> submitTs;           // 每帧提交时间戳 (mach_absolute_time)
    std::vector<uint64_t> callbackLatencyNs; // VT callback - submit
    uint64_t firstSubmitNs = 0;
    uint64_t lastCallbackNs = 0;
    std::mutex perfMtx;
    bool benchMode = false;                   // bench 模式跳过 layer
};

static void DecompressionCb(
    void* refCon, void* srcRefCon, OSStatus st, VTDecodeInfoFlags flags,
    CVPixelBufferRef pb, CMTime pts, CMTime duration
) {
    (void)srcRefCon; (void)flags;
    if (st != noErr || !pb) return;
    auto* ctx = static_cast<DecodeCtx*>(refCon);

    // 记录 VT 延迟:frameCount 即当前帧序号 (从 0 开始)
    int64_t idx = ctx->frameCount.load();
    uint64_t now = NowNs();
    {
        std::lock_guard<std::mutex> lk(ctx->perfMtx);
        if (idx < (int64_t)ctx->submitTs.size()) {
            uint64_t lat = now - ctx->submitTs[idx];
            ctx->callbackLatencyNs.push_back(lat);
        }
        ctx->lastCallbackNs = now;
    }

    int w = (int)CVPixelBufferGetWidth(pb);
    int h = (int)CVPixelBufferGetHeight(pb);

    // VT 给的 pts/duration 在 stream 无 timing 时可能无效 (kCMTimeZero).
    // 关键:AVSampleBufferDisplayLayer 的 controlTimebase 绑定 host clock (当前时间 ~1e9s).
    // 所以每一帧的 presentation timestamp 必须落在"现在"附近,layer 才会调度显示.
    static std::atomic<int64_t> s_frameSeq{0};
    static CMClockRef s_hostClock = CMClockGetHostTimeClock();
    static CMTime s_startHostTime = {0, 0};  //懒初始化
    int64_t seq = s_frameSeq.fetch_add(1);
    if (s_startHostTime.timescale == 0)
        s_startHostTime = CMClockGetTime(s_hostClock);
    CMTime frameDur = CMTimeMakeWithSeconds(1.0/30.0, 600);
    CMTime hostPts = CMTimeAdd(s_startHostTime, CMTimeMakeWithSeconds((double)seq / 30.0, 600));

    // ============================================================
    // 带 layer 的渲染路径:先把 NV12 转成 BGRA,彻底避免 biplanar chroma
    // stride 解析错位导致的彩虹条纹!
    // bench 模式下直接用原始 NV12 CVPixelBuffer (零开销)
    // ============================================================
    CVPixelBufferRef pbForLayer = pb;
    CFTypeID origPbType = CVPixelBufferGetTypeID();

    if (ctx->layer && !ctx->benchMode) {
        @autoreleasepool {
            // 用一个 static CIContext 避免每帧创建开销
            static CIContext* ciCtx = nil;
            static dispatch_once_t onceToken;
            dispatch_once(&onceToken, ^{
                ciCtx = [[CIContext alloc] initWithOptions:@{
                    kCIContextUseSoftwareRenderer: @NO
                }];
            });
            CIImage* srcImg = [CIImage imageWithCVPixelBuffer:pb];
            CVPixelBufferRef bgraPB = nullptr;
            CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault,
                (const void*[]){ kCVPixelBufferIOSurfacePropertiesKey },
                (const void*[]){ CFDictionaryCreate(kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks) },
                1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CVPixelBufferCreate(kCFAllocatorDefault, w, h,
                kCVPixelFormatType_32BGRA, attrs, &bgraPB);
            CFRelease(attrs);
            if (bgraPB) {
                [ciCtx render:srcImg toCVPixelBuffer:bgraPB];
                pbForLayer = bgraPB;
            }
        }
    }

    CMFormatDescriptionRef fmt = nullptr;
    OSStatus s2 = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pbForLayer, &fmt);
    if (s2 != noErr || !fmt) {
        if (pbForLayer != pb) CFRelease(pbForLayer);
        ctx->frameCount++; return;
    }

    CMSampleTimingInfo timing = {frameDur, hostPts, hostPts};
    CMSampleBufferRef sample = nullptr;
    s2 = CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, pbForLayer, fmt, &timing, &sample);
    CFRelease(fmt);
    if (s2 != noErr) {
        if (pbForLayer != pb) CFRelease(pbForLayer);
        ctx->frameCount++; return;
    }

    if (ctx->benchMode || !ctx->layer) {
        if (pbForLayer != pb) CFRelease(pbForLayer);
        ctx->frameCount++;
        int64_t fc = ctx->frameCount.load();
        if (fc == 1 || fc % 30 == 0)
            std::cerr << "[VT cb] frame " << fc << " (" << w << "x" << h << ") bench\n";
        CFRelease(sample);
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        if (ctx->layer) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [ctx->layer enqueueSampleBuffer:sample];
#pragma clang diagnostic pop
            ctx->frameCount++;
            int64_t fc = ctx->frameCount.load();
            if (fc == 1 || fc % 30 == 0)
                std::cerr << "[VT cb] frame " << fc << " (" << w << "x" << h << ")\n";
        }
        if (pbForLayer != pb) CFRelease(pbForLayer);
        CFRelease(sample);
    });
}

} // namespace

@interface PlayerDelegate : NSObject <NSApplicationDelegate>
- (void)startPlayback;
@end

@implementation PlayerDelegate {
    NSWindow* _window;
    NSView* _contentView;
    AVSampleBufferDisplayLayer* _layer;
    DecodeCtx _ctx;
    std::vector<uint8_t> _fileData;
    std::atomic<bool> _done;
    const char* _filePath;
    bool _bench;
}
- (void)setFilePath:(const char*)path { _filePath = path; }
- (void)setBench:(bool)b { _bench = b; }
- (void)applicationDidFinishLaunching:(NSNotification*)notif { @autoreleasepool { (void)notif; [self startPlayback]; } }

- (void)startPlayback {
    std::ifstream in(_filePath, std::ios::binary | std::ios::ate);
    if (!in) { std::cerr << "hal_player: cannot open " << _filePath << std::endl; [NSApp terminate:nil]; return; }
    std::streamsize sz = in.tellg(); in.seekg(0);
    _fileData.resize(sz); in.read(reinterpret_cast<char*>(_fileData.data()), sz); in.close();
    std::cerr << "[hal_player] file: " << _filePath << " (" << sz << " bytes)\n";

    CMVideoCodecType codec = DetectCodec(_fileData);
    std::cerr << "[hal_player] codec detect: "
              << (codec == kCMVideoCodecType_H264 ? "H.264" : "HEVC") << "\n";

    auto nalus = SplitAnnexb(_fileData, codec);
    std::cerr << "[hal_player] NALs: " << nalus.size() << "\n";
    if (nalus.empty()) { [NSApp terminate:nil]; return; }

    auto ps = FindParamSets(nalus, codec);
    std::cerr << "[hal_player] param sets: sps=" << ps.spsIdx << " pps=" << ps.ppsIdx;
    if (codec == kCMVideoCodecType_HEVC) std::cerr << " vps=" << ps.vpsIdx;
    std::cerr << "\n";

    auto aus = GroupAUs(nalus, codec);
    std::cerr << "[hal_player] picture AUs: " << aus.size() << "\n";

    CMFormatDescriptionRef fmtDesc = BuildFormatDescription(_fileData, nalus, ps, codec);
    if (!fmtDesc) { std::cerr << "format desc failed" << std::endl; [NSApp terminate:nil]; return; }

    VTDecompressionOutputCallbackRecord cbRec;
    cbRec.decompressionOutputCallback = DecompressionCb;
    cbRec.decompressionOutputRefCon = &_ctx;
    VTDecompressionSessionRef sess = nullptr;
    OSStatus st = VTDecompressionSessionCreate(kCFAllocatorDefault, fmtDesc, nullptr, nullptr, &cbRec, &sess);
    if (st != noErr) { std::cerr << "VT session create: " << (int)st << std::endl; [NSApp terminate:nil]; return; }
    _ctx.session = sess;
    _ctx.benchMode = _bench;
    _ctx.totalFrames = (int)aus.size();

    if (_bench) {
        std::cerr << "[hal_player] BENCH MODE — no window, pure VT decode throughput\n";
    } else {
        // Window + Layer
        NSRect frame = NSMakeRect(100, 100, 1280, 720);
        _window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered defer:NO];
        [_window setTitle:[NSString stringWithUTF8String:_filePath]];
        _contentView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 720)];
        _window.contentView = _contentView;
        _contentView.wantsLayer = YES;
        _layer = [AVSampleBufferDisplayLayer layer];
        _layer.frame = _contentView.bounds;
        _layer.videoGravity = AVLayerVideoGravityResizeAspect;
        _layer.backgroundColor = [[NSColor blackColor] CGColor];
        // 关键:给 layer 一个 host timebase,让它知道用 host clock 来 schedule 帧显示.
        // 没设时 layer 无法将 stream PTS 映射到 host 时间,可能跳过所有帧.
        CMTimebaseRef tb = nullptr;
        CMTimebaseCreateWithSourceClock(kCFAllocatorDefault, CMClockGetHostTimeClock(), &tb);
        _layer.controlTimebase = tb;
        if (tb) CFRelease(tb);  // layer 会 retain
        [_contentView.layer addSublayer:_layer];
        _ctx.layer = _layer;
        [_window center];
        [_window makeKeyAndOrderFront:nil];
        std::cerr << "[hal_player] layer bounds="
                  << _layer.bounds.size.width << "x" << _layer.bounds.size.height
                  << ", controlTimebase=" << (_layer.controlTimebase ? "set" : "nil") << "\n";
    }

    // 预分配 submitTs 避免 realloc
    _ctx.submitTs.resize(aus.size(), 0);

    auto ctx = &_ctx;
    auto doneFlag = &_done;
    std::thread([fileDataCopy = _fileData, fmtDesc, nalus = std::move(nalus), aus = std::move(aus), ps, codec, ctx, doneFlag]() {
        int auCount = 0;
        uint64_t t0 = NowNs();
        for (const auto& au : aus) {
            if (doneFlag->load()) break;
            auto avcc = BuildAvcc(fileDataCopy, nalus, au, ps, codec);
            CMBlockBufferRef bb = nullptr;
            OSStatus st2 = CMBlockBufferCreateWithMemoryBlock(
                kCFAllocatorDefault, avcc.data(), avcc.size(), kCFAllocatorNull,
                nullptr, 0, avcc.size(), 0, &bb);
            if (st2 != noErr) continue;
            CMSampleBufferRef sb = nullptr;
            st2 = CMSampleBufferCreate(kCFAllocatorDefault, bb, TRUE, nullptr, nullptr,
                fmtDesc, 1, 0, nullptr, 0, nullptr, &sb);
            CFRelease(bb);
            if (st2 != noErr) continue;
            // 记录提交时间戳
            uint64_t ts = NowNs();
            if (auCount == 0) ctx->firstSubmitNs = ts;
            ctx->submitTs[auCount] = ts;
            VTDecompressionSessionDecodeFrame(ctx->session, sb, 0, nullptr, nullptr);
            CFRelease(sb);
            auCount++;
        }
        std::cerr << "[hal_player] submitted " << auCount << " AUs in "
                  << std::fixed << std::setprecision(2)
                  << NsToSec(NowNs() - t0) * 1000 << " ms\n";
        VTDecompressionSessionFinishDelayedFrames(ctx->session);
        VTDecompressionSessionWaitForAsynchronousFrames(ctx->session);

        // ==== PERF REPORT ====
        int64_t frames = ctx->frameCount.load();
        std::vector<uint64_t> lats;
        uint64_t totalWallNs = 0;
        {
            std::lock_guard<std::mutex> lk(ctx->perfMtx);
            lats = ctx->callbackLatencyNs;
            totalWallNs = ctx->lastCallbackNs - ctx->firstSubmitNs;
        }

        std::cerr << "\n================ PERF ================\n";
        std::cerr << "  decoded frames : " << frames << "/" << ctx->totalFrames << "\n";
        if (totalWallNs > 0 && frames > 0) {
            double wallSec = NsToSec(totalWallNs);
            double fps = frames / wallSec;
            std::cerr << "  total wall time: " << std::fixed << std::setprecision(3) << wallSec << " s\n";
            std::cerr << "  throughput     : " << std::fixed << std::setprecision(1) << fps << " fps\n";
        }
        if (!lats.empty()) {
            std::sort(lats.begin(), lats.end());
            uint64_t sum = 0; for (auto x : lats) sum += x;
            double mean = (double)sum / lats.size();
            std::cerr << "  VT latency     : mean=" << std::fixed << std::setprecision(2) << mean/1e6 << " ms"
                      << "  p50=" << lats[lats.size()*50/100]/1e6 << " ms"
                      << "  p95=" << lats[lats.size()*95/100]/1e6 << " ms"
                      << "  p99=" << lats[lats.size()*99/100]/1e6 << " ms"
                      << "  max=" << lats.back()/1e6 << " ms\n";
        }
        std::cerr << "======================================\n\n";

        doneFlag->store(true);
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:ctx->benchMode ? 0.05 : 0.5];
        });
    }).detach();
    CFRelease(fmtDesc);  // 线程 lambda 捕获了 fmtDesc,ARC 风格自动 retain,这里 release startPlayback 持有的那份
}

- (void)applicationWillTerminate:(NSNotification*)notif {
    (void)notif; _done = true;
    if (_ctx.session) { VTDecompressionSessionInvalidate(_ctx.session); CFRelease(_ctx.session); _ctx.session = nullptr; }
    std::cerr << "[hal_player] terminated, total frames: " << _ctx.frameCount.load() << "\n";
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
@end

int main(int argc, const char* argv[]) {
    if (argc < 2) { std::cerr << "usage: hal_player [--bench] <file.h264|file.hevc>" << std::endl; return 1; }
    bool bench = false;
    const char* path = argv[1];
    if (argc >= 3) {
        if (strcmp(argv[1], "--bench") == 0) { bench = true; path = argv[2]; }
        else if (strcmp(argv[2], "--bench") == 0) { bench = true; }
    } else if (strcmp(argv[1], "--bench") == 0) {
        std::cerr << "usage: hal_player [--bench] <file.h264|file.hevc>" << std::endl; return 1;
    }
    @autoreleasepool {
        PlayerDelegate* del = [[PlayerDelegate alloc] init];
        [del setFilePath:path];
        [del setBench:bench];
        NSApp = [NSApplication sharedApplication];
        [NSApp setDelegate:del];
        [NSApp run];
    }
    return 0;
}
