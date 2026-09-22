// camera_source_av — macOS AVFoundation 相机源实现。
//
// AVCaptureSession + AVCaptureDeviceInput + AVCaptureVideoDataOutput,像素格式
// kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange(即 NV12)。回调跑在专用串行
// dispatch queue,把 CMSampleBuffer 排队交给 Grab;Grab 内把 CVImageBuffer 两个
// 平面(各自带硬件行填充、且不保证连续)拷平为紧凑 NV12(与 V4L2 源一致,vtenc
// 的 copyFrameToPixelBuffer 也要求 data + w*h 的紧凑布局),然后立即解锁归还采样
// 缓冲。因此 release 为空,缓冲归源内部所有,下次 Grab/Stop 前有效。
//
// 权限:首次运行会弹系统授权弹窗(requestAccessForMediaType 同步等待);打包为
// .app 需在 Info.plist 加 NSCameraUsageDescription,命令行直接运行不受影响。
//
// 编译门控:仅 APPLE;本文件为 Objective-C++(.mm),链接 AVFoundation /
// CoreMedia / CoreVideo / CoreFoundation 框架(同 vtbox_layers 风格)。

#include "camera_source_factory.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

namespace {

// 后续通过函数指针把采集回调转发进 C++ 源(集合器内不持 C++ 类型)。
class AVFCameraSource;
void ForwardSample(void* ctx, CMSampleBufferRef sampleBuffer);

} // namespace

// AVCaptureVideoDataOutput 的 delegate:在采集专用队列上把采样缓冲转交 C++ 源。
@interface HALCamSampleCollector : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic) void* ctx;
@end

@implementation HALCamSampleCollector
- (void)captureOutput:(AVCaptureOutput*)captureOutput
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)connection {
    (void)captureOutput;
    (void)connection;
    if (_ctx) {
        ForwardSample(_ctx, sampleBuffer);
    }
}
@end

namespace {

// AVFoundation 相机源:Open 时完成权限→设备→输入→输出搭建,Start 时启动会话,
// Grab 从内部帧队列取一帧并拷平为紧凑 NV12。
class AVFCameraSource : public halcodec::CameraSource {
public:
    AVFCameraSource(const std::string& deviceName, int fps)
        : deviceName_(deviceName), fps_(fps) {}

    ~AVFCameraSource() override { Stop(); }

    bool Open() override {
        // 1) 权限(iOS/macOS TCC)。同步等待,避免采集线程与授权弹窗时序交错。
        __block bool granted = false;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL ok) {
            granted = ok ? true : false;
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);   // ARC 管理,无需显式释放
        if (!granted) {
            std::cerr << "hal_cam: camera permission denied (NSCameraUsageDescription"
                      << " needed when packaged as .app)\n";
            return false;
        }

        // 2) 设备:--device 作为名称子串过滤;空则默认(通常内建 FaceTime 摄像头)。
        AVCaptureDeviceDiscoverySession* ds =
            [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
                                      mediaType:AVMediaTypeVideo
                                       position:AVCaptureDevicePositionUnspecified];
        AVCaptureDevice* device = nullptr;
        for (AVCaptureDevice* d in ds.devices) {
            if (deviceName_.empty() ||
                [[d localizedName] rangeOfString:
                    [NSString stringWithUTF8String:deviceName_.c_str()]].location
                    != NSNotFound) {
                device = d;
                break;
            }
        }
        if (!device) {
            if (!deviceName_.empty()) {
                std::cerr << "hal_cam: no capture device matching '"
                          << deviceName_ << "'\n";
            } else {
                std::cerr << "hal_cam: no video capture device found\n";
            }
            return false;
        }
        // 3) 帧率(尽力而为):设备帧率仅支持离散值,取离请求最近的;失败不阻断采集。
        NSError* lockErr = nullptr;
        if ([device lockForConfiguration:&lockErr]) {
            @try {
                const double target = fps_ > 0 ? static_cast<double>(fps_) : 30.0;
                AVFrameRateRange* picked = nullptr;
                double bestDelta = DBL_MAX;
                for (AVFrameRateRange* r
                        in device.activeFormat.videoSupportedFrameRateRanges) {
                    double delta = std::fabs(r.maxFrameRate - target);
                    if (delta < bestDelta) {
                        bestDelta = delta;
                        picked = r;
                    }
                }
                if (picked) {
                    // 用 range 自身的时长属性(CMTime 逐分母精确匹配,不复算)。
                    [device setActiveVideoMinFrameDuration:picked.minFrameDuration];
                    [device setActiveVideoMaxFrameDuration:picked.maxFrameDuration];
                    fps_ = static_cast<int>(std::lround(picked.maxFrameRate));
                }
            } @catch (NSException* e) {
                std::cerr << "hal_cam: set frame duration failed: "
                          << [[e reason] UTF8String] << "\n";
            }
            [device unlockForConfiguration];
        } else if (lockErr) {
            std::cerr << "hal_cam: lockForConfiguration failed: "
                      << [[lockErr localizedDescription] UTF8String] << "\n";
        }

        // 4) 尺寸:以当前 activeFormat 的格式描述为准(会话未启动也可查询)。
        CMVideoDimensions dims =
            CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
        w_ = static_cast<uint32_t>(dims.width);
        h_ = static_cast<uint32_t>(dims.height);
        if (w_ == 0 || h_ == 0) {
            std::cerr << "hal_cam: empty activeFormat dimensions\n";
            return false;
        }

        // 5) 输入 + 会话 + 输出。
        NSError* err = nullptr;
        AVCaptureDeviceInput* input =
            [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
        if (!input) {
            std::cerr << "hal_cam: AVCaptureDeviceInput failed: "
                      << [[err localizedDescription] UTF8String] << "\n";
            return false;
        }
        session_ = [[AVCaptureSession alloc] init];
        if (![session_ canAddInput:input]) {
            std::cerr << "hal_cam: cannot add video input\n";
            return false;
        }
        [session_ addInput:input];

        output_ = [[AVCaptureVideoDataOutput alloc] init];
        // NV12(420YpCbCr8BiPlanarVideoRange)直出;丢弃迟到帧保持实时性。
        [output_ setVideoSettings:@{
            (id)kCVPixelBufferPixelFormatTypeKey :
                @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        }];
        [output_ setAlwaysDiscardsLateVideoFrames:YES];
        if (![session_ canAddOutput:output_]) {
            std::cerr << "hal_cam: cannot add video output\n";
            return false;
        }
        sampleQueue_ =
            dispatch_queue_create("halcam.capture.sample", DISPATCH_QUEUE_SERIAL);
        collector_ = [[HALCamSampleCollector alloc] init];
        [collector_ setCtx:this];
        [output_ setSampleBufferDelegate:collector_ queue:sampleQueue_];
        [session_ addOutput:output_];

        // 限制输出连接帧率(与第 3 步双保险;连接属性同样可能抛异常)。
        @try {
            AVCaptureConnection* conn =
                [output_ connectionWithMediaType:AVMediaTypeVideo];
            CMTime t = CMTimeMake(1, fps_ > 0 ? fps_ : 30);
            if (conn.supportsVideoMinFrameDuration) {
                [conn setVideoMinFrameDuration:t];
            }
            if (conn.supportsVideoMaxFrameDuration) {
                [conn setVideoMaxFrameDuration:t];
            }
        } @catch (NSException*) {
            // 帧率设置失败不阻断采集,设备以其默认帧率出帧。
        }

        nv12Buf_.assign(static_cast<size_t>(w_) * h_ * 3 / 2, 0);
        std::cout << "hal_cam: av source [" << [[device localizedName] UTF8String]
                  << "] " << w_ << "x" << h_ << " @" << fps_ << " fps(请求)\n";
        return true;
    }

    bool Prepare() override { return true; }   // 无需第二阶段

    bool Start() override {
        if (!session_) return false;
        [session_ startRunning];
        started_ = true;
        return true;
    }

    void RequestStop() override { requestedStop_.store(true); }

    bool Grab(halcodec::CodecFrame& frame) override {
        CMSampleBufferRef sampleBuffer = nullptr;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return pending_ != nullptr || requestedStop_.load(); });
            if (requestedStop_.load() && !pending_) return false;
            sampleBuffer = pending_;
            pending_ = nullptr;
        }
        CVImageBufferRef image = CMSampleBufferGetImageBuffer(sampleBuffer);
        const bool locked =
            CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess;
        if (locked) {
            // Y / UV 平面可能带行填充且不连续:逐行拷平到紧凑 NV12。
            const uint8_t* y = static_cast<const uint8_t*>(
                CVPixelBufferGetBaseAddressOfPlane(image, 0));
            const uint8_t* uv = static_cast<const uint8_t*>(
                CVPixelBufferGetBaseAddressOfPlane(image, 1));
            const size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(image, 0);
            const size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(image, 1);
            const size_t w = w_;
            const size_t h = h_;
            uint8_t* dst = nv12Buf_.data();
            for (size_t r = 0; r < h; ++r) {
                std::memcpy(dst + r * w, y + r * yStride, w);
            }
            dst += w * h;
            for (size_t r = 0; r < h / 2; ++r) {
                std::memcpy(dst + r * w, uv + r * uvStride, w);
            }
            CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
        }
        CFRelease(sampleBuffer);
        if (!locked) {
            return false;
        }

        frame.data = nv12Buf_.data();
        frame.size = nv12Buf_.size();
        frame.width = static_cast<int>(w_);
        frame.height = static_cast<int>(h_);
        frame.format = halcodec::PixelFormat::NV12;
        frame.strides = {w_, w_, 0, 0};   // 紧凑,两平面行距均为 w
        frame.locality = halcodec::FrameLocality::Host;
        frame.release = nullptr;          // 已拷平并解锁,vtenc 在 FillFrame 内即刻拷贝
        return true;
    }

    void Stop() override {
        requestedStop_.store(true);
        if (!started_ && !session_) return;   // 从未 Open
        if (session_) {
            if (started_) [session_ stopRunning];
            if (output_ && sampleQueue_) {
                [output_ setSampleBufferDelegate:nil queue:sampleQueue_];
            }
        }
        if (sampleQueue_) {
            sampleQueue_ = nullptr;   // ARC 管理,置空即释放
        }
        collector_ = nullptr;
        output_ = nullptr;
        session_ = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (pending_) {
                CFRelease(pending_);
                pending_ = nullptr;
            }
        }
        started_ = false;
    }

    uint32_t width()  const override { return w_; }
    uint32_t height() const override { return h_; }

    // 采集回调(专用串行队列):保留采样缓冲排队;若前帧未被消费则让其先走
    // (实时流,丢帧优于反压)。
    void PushSample(CMSampleBufferRef sampleBuffer) {
        CMSampleBufferRef keep = (CMSampleBufferRef)CFRetain(sampleBuffer);
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (pending_) CFRelease(pending_);
            pending_ = keep;
        }
        cv_.notify_one();
    }

private:
    std::string deviceName_;
    int fps_ = 30;
    uint32_t w_ = 0;
    uint32_t h_ = 0;
    std::vector<uint8_t> nv12Buf_;

    AVCaptureSession* session_ = nullptr;            // ARC 管理
    AVCaptureVideoDataOutput* output_ = nullptr;     // 强持,delegate 生命周期安全
    HALCamSampleCollector* collector_ = nullptr;
    dispatch_queue_t sampleQueue_ = nullptr;

    std::mutex mu_;
    std::condition_variable cv_;
    CMSampleBufferRef pending_ = nullptr;            // CF 管理,需手动 CFRelease
    std::atomic<bool> requestedStop_{false};
    std::atomic<bool> started_{false};
};

void ForwardSample(void* ctx, CMSampleBufferRef sampleBuffer) {
    static_cast<AVFCameraSource*>(ctx)->PushSample(sampleBuffer);
}

} // namespace

namespace camera {

bool CreateAVF(const CommandLineParser& cli,
               std::vector<std::unique_ptr<halcodec::CameraSource>>& out) {
    // --device 复用为摄像头名子串过滤;v4l2 的默认设备节点 /dev/video0 在
    // macOS 上无意义,视为未指定(用默认摄像头)。
    std::string device = cli.getV4l2Device();
    if (device == "/dev/video0") device = "";
    auto src = std::make_unique<AVFCameraSource>(device, cli.getV4l2Fps());
    if (!src->Open()) {
        return false;
    }
    if (!src->Prepare()) {
        return false;
    }
    out.push_back(std::move(src));
    return true;
}

} // namespace camera