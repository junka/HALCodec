// camera_source_v4l2 — Linux V4L2(USB/UVC)相机源实现。
//
// 从 hal_camera_v4l2 泛化而来:V4L2 mmap 捕获路径 + YUYV→NV12 CPU 转换,
// Grab 返回 host NV12 CodecFrame。缓冲归源内部所有(nv12Buf 成员),release 为空。
// MJPG 输入仍不支持(需 QSV JPEG 解码→NV12,未接线),同旧应用行为。
//
// 编译门控:仅 Linux(使用 <linux/videodev2.h>);由 app/CMakeLists.txt 决定。

#include "camera_source_factory.h"

#include <cerrno>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

// Wrap a V4L2 ioctl: retry on EINTR.
int Xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

// V4L2 FourCC for the requested pixel format string.
uint32_t V4l2Fourcc(const std::string& fmt) {
    if (fmt == "yuyv" || fmt == "yuyv422") return V4L2_PIX_FMT_YUYV;
    if (fmt == "mjpg" || fmt == "mjpeg")   return V4L2_PIX_FMT_MJPEG;
    return 0;
}

// A single mmap'd V4L2 capture buffer.
struct CaptureBuffer {
    void*  start = nullptr;
    size_t length = 0;
};

// RAII V4L2 capture session. Opens the device, negotiates format + frame rate,
// mmaps N buffers, and streams. YUYV only for now (NV12 conversion done in
// V4L2CameraSource::Grab); MJPG is negotiated but Grab rejects it until a JPEG
// decode path exists.
class V4l2Capture {
public:
    bool open(const std::string& device, uint32_t fourcc,
              int width, int height, int fps, uint32_t bufferCount);
    void close();

    // Dequeue one captured frame into `buf` (a pointer into the mmap'd buffer).
    // Returns false on error. The caller must call release() before the next
    // capture. The fd is opened blocking, so DQBUF blocks until a frame lands.
    bool get(uint8_t*& data, size_t& size);
    void release();

    uint32_t width() const  { return width_; }
    uint32_t height() const { return height_; }
    uint32_t fourcc() const { return fourcc_; }

private:
    int fd_ = -1;
    std::vector<CaptureBuffer> buffers_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fourcc_ = 0;
    uint32_t current_ = 0;
    bool streaming_ = false;
};

bool V4l2Capture::open(const std::string& device, uint32_t fourcc,
                       int width, int height, int fps, uint32_t bufferCount) {
    // O_NONBLOCK:让 DQBUF 在无帧时返回 EAGAIN,便于 Grab 轮询探测 RequestStop。
    fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "hal_cam: cannot open " << device << ": "
                  << std::strerror(errno) << "\n";
        return false;
    }
    // Every failure path below must release the fd and any mmap'd buffers
    // already allocated; close() handles STREAMOFF (no-op before STREAMON) +
    // munmap of populated buffers + closing the fd.
    auto fail = [&]() { close(); return false; };
    // Check capabilities.
    v4l2_capability cap{};
    if (Xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "hal_cam: VIDIOC_QUERYCAP failed\n";
        return fail();
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "hal_cam: " << device << " is not a video capture device\n";
        return fail();
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::cerr << "hal_cam: " << device << " does not support streaming\n";
        return fail();
    }
    // Negotiate format.
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (Xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "hal_cam: VIDIOC_S_FMT failed: "
                  << std::strerror(errno) << "\n";
        return fail();
    }
    if (fmt.fmt.pix.pixelformat != fourcc) {
        std::cerr << "hal_cam: device refused format "
                  << fourcc << " (got " << fmt.fmt.pix.pixelformat << ")\n";
        return fail();
    }
    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;
    fourcc_ = fmt.fmt.pix.pixelformat;
    // Frame rate (v4l2_streamparm).
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps > 0 ? fps : 30;
    Xioctl(fd_, VIDIOC_S_PARM, &parm);  // best-effort; not all UVC cams honor it

    // Request mmap buffers.
    v4l2_requestbuffers req{};
    req.count = bufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (Xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "hal_cam: VIDIOC_REQBUFS failed: "
                  << std::strerror(errno) << "\n";
        return fail();
    }
    if (req.count < 2) {
        std::cerr << "hal_cam: insufficient buffers (" << req.count << ")\n";
        return fail();
    }
    buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (Xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "hal_cam: VIDIOC_QUERYBUF " << i << " failed\n";
            return fail();
        }
        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd_, buf.m.offset);
        if (buffers_[i].start == MAP_FAILED) {
            std::cerr << "hal_cam: mmap " << i << " failed\n";
            return fail();
        }
    }
    // Queue all buffers and start streaming.
    for (uint32_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (Xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "hal_cam: VIDIOC_QBUF " << i << " failed\n";
            return fail();
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (Xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "hal_cam: VIDIOC_STREAMON failed: "
                  << std::strerror(errno) << "\n";
        return fail();
    }
    streaming_ = true;
    return true;
}

bool V4l2Capture::get(uint8_t*& data, size_t& size) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (Xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        // EAGAIN (non-blocking, no frame ready) is the normal polling case —
        // stay quiet so the Grab loop's 10 ms backoff isn't drowned in logs.
        if (errno != EAGAIN) {
            std::cerr << "hal_cam: VIDIOC_DQBUF failed: "
                      << std::strerror(errno) << "\n";
        }
        return false;
    }
    if (buf.index >= buffers_.size()) {
        // Defensive: requeue the dequeued buffer so the driver doesn't lose it.
        Xioctl(fd_, VIDIOC_QBUF, &buf);
        return false;
    }
    current_ = buf.index;
    data = static_cast<uint8_t*>(buffers_[buf.index].start);
    size = buf.bytesused;
    return true;
}

void V4l2Capture::release() {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = current_;
    Xioctl(fd_, VIDIOC_QBUF, &buf);  // requeue; best-effort
}

void V4l2Capture::close() {
    if (streaming_) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        Xioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    for (auto& b : buffers_) {
        if (b.start && b.start != MAP_FAILED) munmap(b.start, b.length);
    }
    buffers_.clear();
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

// Convert a tightly-packed YUYV (YUY2) frame to tightly-packed NV12 (从
// hal_camera_v4l2 平移)。`dst` 必须可容纳 w*h*3/2 字节;`src` 为 w*h*2 字节。
void ConvertYuyvToNv12(const uint8_t* src, uint8_t* dst,
                       uint32_t w, uint32_t h) {
    uint8_t* yDst = dst;
    uint8_t* uvDst = dst + static_cast<size_t>(w) * h;
    for (uint32_t row = 0; row < h; row += 2) {
        // Two source Y rows -> two Y rows.
        for (uint32_t sub = 0; sub < 2; ++sub) {
            const uint8_t* s = src + static_cast<size_t>(row + sub) * w * 2;
            uint8_t* y = yDst + static_cast<size_t>(row + sub) * w;
            for (uint32_t p = 0; p < w; p += 2) {
                y[p]     = s[0];
                y[p + 1] = s[2];
                s += 4;
            }
        }
        // One UV row shared by the two Y rows (from the first source row).
        const uint8_t* s = src + static_cast<size_t>(row) * w * 2;
        uint8_t* uv = uvDst + static_cast<size_t>(row / 2) * w;
        for (uint32_t p = 0; p < w; p += 2) {
            uv[0] = s[1];   // U
            uv[1] = s[3];   // V
            s += 4;
            uv += 2;
        }
    }
}

// V4L2 CameraSource:Grab 内部完成 YUYV→NV12 转换,输出 host NV12 CodecFrame。
//
// 缓冲池:异步 encoder(qsvenc)的 FillFrame 只是把 CodecFrame 入队,worker 线程
// 之后才 memcpy 上传到 MFX surface 并在完成后触发 frame.release。若 Grab 复用单
// 缓冲,下一帧的 YUYV→NV12 会覆盖 worker 仍在读取的内存 → 花屏。故每帧从池中取
// 一个独立 NV12 缓冲,release 把它归还池;池大小限定在途帧数(异步背压)。同步
// encoder(nvmedia)在 FillFrame 内即消费完毕,hal_cam 会在 FillFrame 返回后立即
// 调 release 归还,池大小同样足够。
class V4L2CameraSource : public halcodec::CameraSource {
public:
    bool Open() override {
        uint32_t fourcc = V4l2Fourcc(fourccStr_);
        if (fourcc == 0) {
            std::cerr << "hal_cam: unsupported --input-format '"
                      << fourccStr_ << "' (use yuyv or mjpg)\n";
            return false;
        }
        if (fourcc == V4L2_PIX_FMT_MJPEG) {
            std::cerr << "hal_cam: MJPG capture not yet supported (needs "
                      << "QSV JPEG decode → NV12). Use --input-format yuyv.\n";
            return false;
        }
        if (!cap_.open(device_, fourcc, captureWidth_, captureHeight_,
                       captureFps_, 4)) {
            return false;
        }
        w_ = cap_.width();
        h_ = cap_.height();
        // NV12 chroma subsampling and the YUYV→NV12 converter both require
        // even dimensions; reject odd w/h early instead of producing a
        // misaligned NV12 buffer the encoder would reject or corrupt.
        if ((w_ | h_) & 1) {
            std::cerr << "hal_cam: odd dimensions " << w_ << "x" << h_
                      << " not supported (need even w/h for NV12)\n";
            return false;
        }
        // Allocate the conversion buffer pool.
        const size_t nv12Size = static_cast<size_t>(w_) * h_ * 3 / 2;
        poolStorage_.assign(kPoolSize, std::vector<uint8_t>(nv12Size));
        freePool_.clear();
        freePool_.reserve(kPoolSize);
        for (auto& b : poolStorage_) freePool_.push_back(&b);
        return true;
    }

    bool Prepare() override { return true; }   // V4L2 无需第二阶段
    bool Start()  override { return true; }    // STREAMON 已在 Open 完成

    // 默认阻塞 fd;RequestStop 需要 DQBUF 可中断,故置非阻塞 + 轮询探测(与
    // SIPL 源 5s 超时同类手法)。
    void RequestStop() override { requestedStop_.store(true); }

    bool Grab(halcodec::CodecFrame& frame) override {
        uint8_t* frameData = nullptr;
        size_t frameSize = 0;
        while (!requestedStop_.load()) {
            if (!cap_.get(frameData, frameSize)) {
                // EAGAIN(无帧就绪)或瞬时错误:短暂退避后重试,避免 100% CPU 空转。
                // RequestStop 后由循环条件退出。
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (frameSize < static_cast<size_t>(w_) * h_ * 2) {
                std::cerr << "hal_cam: short frame (" << frameSize << ")\n";
                cap_.release();
                continue;
            }
            // 取一个空闲 NV12 缓冲;全部在途时阻塞(异步背压)。
            std::vector<uint8_t>* nv12 = nullptr;
            {
                std::unique_lock<std::mutex> lk(poolMu_);
                poolCv_.wait(lk, [&] { return !freePool_.empty(); });
                nv12 = freePool_.back();
                freePool_.pop_back();
            }
            ConvertYuyvToNv12(frameData, nv12->data(), w_, h_);
            cap_.release();

            frame.data = nv12->data();
            frame.size = nv12->size();
            frame.width = static_cast<int>(w_);
            frame.height = static_cast<int>(h_);
            frame.format = halcodec::PixelFormat::NV12;
            frame.locality = halcodec::FrameLocality::Host;
            // 归还缓冲:同步 encoder 由 hal_cam 在 FillFrame 后立即调;异步
            // encoder(qsvenc)由 worker 在 memcpy 上传后调。poolStorage_ 生命周期
            // 覆盖整个源(Stop 前 worker 必已 drain),捕获 &freePool_/互斥量安全。
            frame.release = [this, nv12]() {
                std::lock_guard<std::mutex> lk(poolMu_);
                freePool_.push_back(nv12);
                poolCv_.notify_one();
            };
            return true;
        }
        return false;
    }

    void Stop() override {
        cap_.close();
        // 等待并清空池:Grab 已不再运行,但异步 encoder 的 worker 可能仍持有最后
        // 几帧的 release 回调。poolStorage_ 在此函数返回后析构,故这里无需也不能
        // 主动 join worker —— hal_cam 在 Stop() 之前已 Finalize() 编码器(worker
        // 已退出)。仅重置池状态即可。
    }

    uint32_t width()  const override { return w_; }
    uint32_t height() const override { return h_; }

    V4L2CameraSource(const std::string& device, const std::string& fmt,
                     int w, int h, int fps)
        : device_(device), fourccStr_(fmt),
          captureWidth_(w), captureHeight_(h), captureFps_(fps) {}

private:
    V4l2Capture cap_;
    std::string device_;
    std::string fourccStr_;
    int captureWidth_ = 640;
    int captureHeight_ = 480;
    int captureFps_ = 30;
    uint32_t w_ = 0;
    uint32_t h_ = 0;
    std::atomic<bool> requestedStop_{false};

    // NV12 转换缓冲池(每帧独立缓冲,避免异步 encoder worker 读旧帧)。
    static constexpr size_t kPoolSize = 8;  // 在途帧上限(异步背压)
    std::vector<std::vector<uint8_t>> poolStorage_;
    std::vector<std::vector<uint8_t>*> freePool_;
    std::mutex poolMu_;
    std::condition_variable poolCv_;
};

} // namespace

namespace camera {

bool CreateV4L2(const CommandLineParser& cli,
                std::vector<std::unique_ptr<halcodec::CameraSource>>& out) {
    auto src = std::make_unique<V4L2CameraSource>(
        cli.getV4l2Device(), cli.getV4l2InputFormat(),
        cli.getV4l2Width(), cli.getV4l2Height(), cli.getV4l2Fps());
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