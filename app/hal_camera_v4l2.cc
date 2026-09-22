// hal_camera_v4l2 — USB/UVC camera → QSV (oneVPL) encode on x86.
//
// The x86 counterpart to hal_camera (which is DRIVE-OS SIPL → NvMedia). A
// standard V4L2 USB camera (e.g. a laptop's Chicony integrated UVC) is opened
// via the V4L2 mmap capture path, each captured frame is converted to NV12 on
// the CPU, and fed host-memory into a QsvEncoder instance (the oneVPL/MFX
// hardware encoder on the Intel iGPU) which writes an H.264 elementary stream.
//
// Why CPU color conversion and not zero-copy: UVC cameras expose YUYV (raw,
// low-res) or MJPG (compressed, high-res), not NV12. QsvEncoder's host input
// path wants tightly-packed NV12. A DMABUF → VA-surface zero-copy path would
// need the camera to export DMABUF and QSV to import it as a VA surface in the
// encoder's format, which UVC + the QSV host path don't directly support; the
// conversion is cheap relative to encode and keeps this app dependency-free
// (no libswscale, no VA interop). The QsvEncoder itself still runs the encode
// on the iGPU — only the YUYV→NV12 detile is on the CPU.
//
// MJPG input path: when --input-format mjpg, frames arrive as JPEG. The QSV
// decoder (MFX_CODEC_JPEG) is not wired here yet; for now the app requires
// yuyv. MJPG→NV12 via QSV JPEG decode is the natural follow-on.
//
// Usage:
//   hal_camera_v4l2 -B qsv -c h264 -o out.h264 --bitrate 2000 --fps 30
//       --device /dev/video0 --input-format yuyv
//       --capture-width 640 --capture-height 480 --capture-fps 30
//       [--duration-frames N]
//
// Runtime env (QSV must find libmfx-gen + the iHD VA driver):
//   ONEVPL_SEARCH_PATH=<repo>/QSV/vpl-gpu-rt-24.4.4/opt/intel/media/lib64 \
//   LIBVA_DRIVERS_PATH=/lib/x86_64-linux-gnu/dri LIBVA_DRIVER_NAME=iHD \
//   LD_LIBRARY_PATH=build:build/app ./build/app/hal_camera_v4l2 ...

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "encoder.h"
#include "encode_config.h"
#include "parse_cli.h"
#include "plugin_loader.h"

#include "codec_config.h"
#include "frame.h"

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
// ConvertYuyvToNv12); MJPG is negotiated but the capture loop rejects it until
// a JPEG decode path exists.
class V4l2Capture {
public:
    bool open(const std::string& device, uint32_t fourcc,
              int width, int height, int fps, uint32_t bufferCount);
    void close();

    // Dequeue one captured frame into `buf` (a pointer into the mmap'd buffer).
    // Returns false on error or stream end. The caller must call release()
    // before the next capture.
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
    fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        std::cerr << "hal_camera_v4l2: cannot open " << device << ": "
                  << std::strerror(errno) << "\n";
        return false;
    }
    // Check capabilities.
    v4l2_capability cap{};
    if (Xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "hal_camera_v4l2: VIDIOC_QUERYCAP failed\n";
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "hal_camera_v4l2: " << device
                  << " is not a video capture device\n";
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::cerr << "hal_camera_v4l2: " << device
                  << " does not support streaming\n";
        return false;
    }
    // Negotiate format.
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (Xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "hal_camera_v4l2: VIDIOC_S_FMT failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (fmt.fmt.pix.pixelformat != fourcc) {
        std::cerr << "hal_camera_v4l2: device refused format "
                  << fourcc << " (got " << fmt.fmt.pix.pixelformat << ")\n";
        return false;
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
        std::cerr << "hal_camera_v4l2: VIDIOC_REQBUFS failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (req.count < 2) {
        std::cerr << "hal_camera_v4l2: insufficient buffers (" << req.count << ")\n";
        return false;
    }
    buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (Xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "hal_camera_v4l2: VIDIOC_QUERYBUF " << i << " failed\n";
            return false;
        }
        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd_, buf.m.offset);
        if (buffers_[i].start == MAP_FAILED) {
            std::cerr << "hal_camera_v4l2: mmap " << i << " failed\n";
            return false;
        }
    }
    // Queue all buffers and start streaming.
    for (uint32_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (Xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "hal_camera_v4l2: VIDIOC_QBUF " << i << " failed\n";
            return false;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (Xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "hal_camera_v4l2: VIDIOC_STREAMON failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    streaming_ = true;
    return true;
}

bool V4l2Capture::get(uint8_t*& data, size_t& size) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (Xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return false;
        std::cerr << "hal_camera_v4l2: VIDIOC_DQBUF failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }
    if (buf.index >= buffers_.size()) return false;
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

// Convert a tightly-packed YUYV (YUY2) frame to tightly-packed NV12.
// YUYV packs two pixels as Y0 U0 Y1 V0 (4 bytes / 2 px). NV12 stores a Y
// plane (w*h) followed by an interleaved UV plane (w * h/2 — one chroma row
// per *two* Y rows). For each 2x2 pixel block we emit:
//   Y out row r:     Y0 Y1 (from source row r)
//   Y out row r+1:   Y0 Y1 (from source row r+1)
//   UV out row r/2:  U V   (one sample per 2px column; taken from the first of
//                   the two source rows, no vertical averaging)
// `dst` must hold w*h*3/2 bytes; `src` holds w*h*2 bytes. w must be even.
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

} // namespace

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    halcodec::LoadBackends();

    std::string encoderBackend = cli.getEncoderBackend();
    if (encoderBackend.empty()) encoderBackend = "qsv";
    // The QSV encoder registers under "qsvenc"; accept the friendlier "qsv"
    // alias too so `-B qsv` does what users expect.
    if (encoderBackend == "qsv") encoderBackend = "qsvenc";
    if (encoderBackend != "qsvenc") {
        std::cerr << "hal_camera_v4l2: only -B qsv (qsvenc) is supported (got '"
                  << encoderBackend << "')\n";
        return 1;
    }

    // --- V4L2 capture setup ---
    uint32_t fourcc = V4l2Fourcc(cli.getV4l2InputFormat());
    if (fourcc == 0) {
        std::cerr << "hal_camera_v4l2: unsupported --input-format '"
                  << cli.getV4l2InputFormat()
                  << "' (use yuyv or mjpg)\n";
        return 1;
    }
    if (fourcc == V4L2_PIX_FMT_MJPEG) {
        std::cerr << "hal_camera_v4l2: MJPG capture not yet supported (needs "
                  << "QSV JPEG decode → NV12). Use --input-format yuyv for now.\n";
        return 1;
    }
    V4l2Capture cap;
    if (!cap.open(cli.getV4l2Device(), fourcc,
                  cli.getV4l2Width(), cli.getV4l2Height(),
                  cli.getV4l2Fps(), 4)) {
        return 1;
    }
    uint32_t w = cap.width();
    uint32_t h = cap.height();
    std::cout << "hal_camera_v4l2: capturing " << w << "x" << h << " "
              << cli.getV4l2InputFormat() << " from " << cli.getV4l2Device()
              << "\n";

    // --- Encoder setup ---
    auto enc = halcodec::Encoder::Create(encoderBackend);
    if (!enc) {
        std::cerr << "hal_camera_v4l2: Encoder::Create(" << encoderBackend
                  << ") failed\n";
        return 1;
    }
    halcodec::CodecParams params;
    params.codec = cli.getCodec();
    params.deviceIndex = cli.getGpuIndex();
    params.width = w;
    params.height = h;
    params.inputFormat = halcodec::PixelFormat::NV12;
    if (!cli.getEncodeConfigFile().empty()) {
        LoadEncodeConfig(cli.getEncodeConfigFile(), params.encode);
    }
    if (!cli.getRateControl().empty()) params.encode.rateControl = cli.getRateControl();
    if (cli.getBitrateKbps() >= 0)    params.encode.bitrateKbps = cli.getBitrateKbps();
    if (cli.getMaxBitrateKbps() >= 0) params.encode.maxBitrateKbps = cli.getMaxBitrateKbps();
    if (cli.getQp() >= 0)             params.encode.qp = cli.getQp();
    if (cli.getGopLength() >= 0)      params.encode.gopLength = cli.getGopLength();
    if (cli.getNumBFrames() >= 0)     params.encode.numBFrames = cli.getNumBFrames();
    if (cli.getFps() > 0)             params.encode.frameRateNum = cli.getFps();
    if (!cli.getProfile().empty())    params.encode.profile = cli.getProfile();
    if (!cli.getLevel().empty())      params.encode.level = cli.getLevel();
    if (cli.getLowDelay())            params.encode.lowDelay = true;
    if (!enc->Initialize(params)) {
        std::cerr << "hal_camera_v4l2: encoder Initialize failed\n";
        return 1;
    }

    // --- Output file ---
    std::string outFile = cli.getOutputFile();
    if (outFile.empty()) outFile = "out.h264";
    std::ofstream fpout(outFile, std::ios::out | std::ios::binary);
    if (!fpout) {
        std::cerr << "hal_camera_v4l2: cannot open output " << outFile << "\n";
        return 1;
    }

    // NV12 conversion + encode loop. The QSV encoder is async: FillFrame
    // returns without blocking, so drain trailing packets periodically and at
    // the end. nv12Buf outlives the drain() call since FillFrame copies into a
    // runtime surface before returning (the host pointer is not retained).
    const size_t nv12Size = static_cast<size_t>(w) * h * 3 / 2;
    std::vector<uint8_t> nv12Buf(nv12Size);
    const bool asyncEnc = enc->isAsync();
    int totalFrames = 0;
    int durationFrames = cli.getDurationFrames();

    auto drain = [&]() {
        halcodec::CodecFrame out;
        while (enc->GetFrame(out)) {
            halcodec::DownloadToHost(out);
            if (out.data && out.size > 0) {
                fpout.write(reinterpret_cast<const char*>(out.data), out.size);
            }
            if (out.release) out.release();
        }
    };

    std::cout << "hal_camera_v4l2: streaming to " << outFile
              << (durationFrames > 0 ? " for " + std::to_string(durationFrames)
                                          + " frames" : " until Ctrl-C")
              << "\n";
    while (durationFrames <= 0 || totalFrames < durationFrames) {
        uint8_t* frameData = nullptr;
        size_t frameSize = 0;
        if (!cap.get(frameData, frameSize)) {
            continue;
        }
        if (frameSize < static_cast<size_t>(w) * h * 2) {
            std::cerr << "hal_camera_v4l2: short frame (" << frameSize << ")\n";
            cap.release();
            continue;
        }
        ConvertYuyvToNv12(frameData, nv12Buf.data(), w, h);
        cap.release();

        halcodec::CodecFrame in;
        in.data = nv12Buf.data();
        in.size = nv12Size;
        in.width = w;
        in.height = h;
        in.format = halcodec::PixelFormat::NV12;
        in.pts = totalFrames;
        if (!enc->FillFrame(in)) {
            std::cerr << "hal_camera_v4l2: FillFrame failed at frame "
                      << totalFrames << "\n";
            break;
        }
        ++totalFrames;
        if (!asyncEnc) drain();
    }

    // Flush: EOS marker + drain trailing packets.
    halcodec::CodecFrame eos;
    eos.width = w;
    eos.height = h;
    eos.format = halcodec::PixelFormat::NV12;
    enc->FillFrame(eos);
    if (asyncEnc) enc->SignalInputComplete();
    drain();

    fpout.close();
    enc->Finalize();
    cap.close();

    std::cout << "hal_camera_v4l2: encoded " << totalFrames << " frames to "
              << outFile << "\n";
    return 0;
}
