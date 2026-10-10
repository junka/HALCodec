#include "nvdecoder.h"

#include <cuda.h>
#include <map>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "frame.h"
#include "registry.h"
#include "nvidia_caps.h"

namespace halcodec {
namespace nvenc {

namespace {

// Maps the NVDEC output surface format to the unified PixelFormat.
PixelFormat toHalFormat(cudaVideoSurfaceFormat surfaceFormat) {
    switch (surfaceFormat) {
        case cudaVideoSurfaceFormat_NV12: return PixelFormat::NV12;
        case cudaVideoSurfaceFormat_P016: return PixelFormat::P016;
        case cudaVideoSurfaceFormat_YUV444: return PixelFormat::YUV444P;
        case cudaVideoSurfaceFormat_YUV444_16Bit: return PixelFormat::YUV444P10LE;
#if NVENCAPI_MAJOR_VERSION > 12
        case cudaVideoSurfaceFormat_NV16: return PixelFormat::NV16;
        case cudaVideoSurfaceFormat_P216: return PixelFormat::P210;
#endif
        default: return PixelFormat::Unknown;
    }
}

// Maps a codec name string ("h264"/"hevc"/...) to an NVDEC codec id, used in
// explicit-feeding mode where no FFmpeg demuxer provides the codec type.
cudaVideoCodec codecFromName(const std::string& name) {
    if (name == "h264")  return cudaVideoCodec_H264;
    if (name == "hevc")  return cudaVideoCodec_HEVC;
#if NVENCAPI_MAJOR_VERSION > 12
    if (name == "av1")   return cudaVideoCodec_AV1;
#endif
    if (name == "jpeg" || name == "mjpeg") return cudaVideoCodec_JPEG;
    if (name == "mpeg1") return cudaVideoCodec_MPEG1;
    if (name == "mpeg2") return cudaVideoCodec_MPEG2;
    if (name == "mpeg4") return cudaVideoCodec_MPEG4;
    if (name == "vc1" || name == "wmv3") return cudaVideoCodec_VC1;
    if (name == "vp8")   return cudaVideoCodec_VP8;
    if (name == "vp9")   return cudaVideoCodec_VP9;
    return cudaVideoCodec_NumCodecs;
}

} // namespace

// Async feed path for explicit-feeding mode. NvDecoder::Decode is synchronous
// and returns the count of newly decoded frames; this wrapper runs it on an
// internal worker thread so the caller's FillInput() returns immediately. Each
// decoded frame is copied out of NvDecoder's locked-frame pool (which it
// rotates on the next Decode) into a malloc'd CodecFrame, queued for GetFrame.
class NVDecoder::AsyncFeed {
public:
    AsyncFeed(NVDecoder* owner, NvDecoder* dec, CUcontext ctx)
        : owner_(owner), decoder_(dec), cudaCtx_(ctx) {}

    ~AsyncFeed() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            eof_ = true;
            finished_ = true;
            cvInput_.notify_all();
            cvFrames_.notify_all();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        while (!frames_.empty()) {
            if (frames_.front().release) frames_.front().release();
            frames_.pop();
        }
    }

    void start() {
        worker_ = std::thread([this] { run(); });
    }

    // Append caller data; wakes the worker. Caller owns `data` until return.
    void append(const uint8_t* data, size_t size) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            inBuf_.insert(inBuf_.end(), data, data + size);
        }
        cvInput_.notify_one();
    }

    void signalEOF() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            eof_ = true;
        }
        cvInput_.notify_one();
    }

    bool getFrame(CodecFrame& out) {
        std::unique_lock<std::mutex> lk(mu_);
        cvFrames_.wait(lk, [this] {
            return finished_ || workerError_ || !frames_.empty();
        });
        if (workerError_ && frames_.empty()) return false;
        if (frames_.empty()) return false;
        out = std::move(frames_.front());
        frames_.pop();
        return true;
    }

private:
    NVDecoder* owner_ = nullptr;
    NvDecoder* decoder_;
    CUcontext cudaCtx_ = nullptr;

    std::mutex mu_;
    std::condition_variable cvInput_;
    std::condition_variable cvFrames_;
    std::vector<uint8_t> inBuf_;  // Annex-B bytes awaiting Decode
    std::queue<CodecFrame> frames_;
    bool eof_ = false;
    bool finished_ = false;
    bool workerError_ = false;
    std::thread worker_;

    // Fills `out` from one newly decoded NvDecoder frame (pitch-aware): a
    // device-resident CodecFrame in zero-copy mode, otherwise a tightly-packed
    // malloc'd host frame. Returns false if no frame was available. The CUDA
    // context must be current on this thread.
    bool takeOneFrame(CodecFrame& out) {
        int64_t pts = 0;
        uint8_t* locked = decoder_->GetLockedFrame(&pts);
        if (!locked) {
            return false;
        }
        if (owner_->zeroCopy_) {
            // Hand the pitched device buffer out as-is; no CPU round-trip.
            owner_->EmitDeviceFrame(out, locked, pts);
            return true;
        }
        // Host mode: copy the locked frame out as before.
        return copyOutHostFrame(out, locked, pts);
    }

    // Copies one newly decoded host-mode locked frame into a tightly-packed
    // malloc'd CodecFrame (pitch-aware), for the host (non-zero-copy) path.
    bool copyOutHostFrame(CodecFrame& out, const uint8_t* locked, int64_t pts) {
        int w = decoder_->GetWidth();
        int h = decoder_->GetHeight();
        int pitch = decoder_->GetDeviceFramePitch();
        int bpp = decoder_->GetBitDepth() > 8 ? 2 : 1;
        int lumaBytes = w * bpp;
        int lumaLines = h;
        int chromaLines = decoder_->GetHeight() / 2;  // 4:2:0
        int numChromaPlanes = 2;  // NV12/P016/YUV444 carry chroma planes
        size_t frameSize = static_cast<size_t>(decoder_->GetFrameSize());
        // NVDEC host-memory output packs luma (w*bpp per line) then chroma
        // planes; when pitch == lumaBytes the whole frame is contiguous.
        out.data = static_cast<uint8_t*>(std::malloc(frameSize));
        if (!out.data) {
            return false;
        }
        if (pitch == lumaBytes) {
            std::memcpy(out.data, locked, frameSize);
        } else {
            // Pitched host output: copy each plane row-by-row from `pitch`
            // stride into a tightly-packed destination.
            size_t dstOff = 0;
            size_t srcOff = 0;
            auto copyPlane = [&](int lines) {
                for (int i = 0; i < lines; i++) {
                    std::memcpy(out.data + dstOff + static_cast<size_t>(lumaBytes) * i,
                                locked + srcOff + static_cast<size_t>(pitch) * i,
                                lumaBytes);
                }
                dstOff += static_cast<size_t>(lumaBytes) * lines;
                srcOff += static_cast<size_t>(pitch) * lines;
            };
            copyPlane(lumaLines);
            for (int p = 0; p < numChromaPlanes; p++) {
                copyPlane(chromaLines);
            }
        }
        out.size = frameSize;
        out.width = w;
        out.height = h;
        out.format = toHalFormat(decoder_->GetOutputFormat());
        out.strides[0] = static_cast<size_t>(w);
        out.pts = pts;
        uint8_t* owned = out.data;
        out.release = [owned]() { std::free(owned); };
        return true;
    }

    void run() {
        // Make the CUDA context current on the worker thread before any
        // cuvid/CUDA call.
        if (cudaCtx_) {
            cuCtxSetCurrent(cudaCtx_);
        }
        std::vector<uint8_t> chunk;
        while (true) {
            {
                std::unique_lock<std::mutex> lk(mu_);
                cvInput_.wait(lk, [this] {
                    return eof_ || workerError_ || !inBuf_.empty();
                });
                if (workerError_) return;
                if (inBuf_.empty()) {
                    // EOF with no data left: flush, then finish.
                    if (eof_) {
                        flushLocked();
                        finished_ = true;
                        cvFrames_.notify_all();
                        return;
                    }
                    continue;
                }
                chunk.swap(inBuf_);
            }

            // Synchronous decode of the accumulated chunk. Decode returns the
            // number of frames now available via GetLockedFrame.
            int n = 0;
            try {
                n = decoder_->Decode(chunk.data(), static_cast<int>(chunk.size()));
            } catch (...) {
                std::lock_guard<std::mutex> lk(mu_);
                workerError_ = true;
                cvFrames_.notify_all();
                return;
            }
            chunk.clear();

            // Pull every frame Decode produced into our own buffers.
            std::lock_guard<std::mutex> lk(mu_);
            for (int i = 0; i < n; i++) {
                CodecFrame f;
                if (takeOneFrame(f)) {
                    frames_.push(std::move(f));
                }
            }
            if (n > 0) {
                cvFrames_.notify_all();
            }
        }
    }

    // Called with mu_ held on EOF: send ENDOFSTREAM, drain remaining frames.
    void flushLocked() {
        // Decode(nullptr, 0) maps to CUVID_PKT_ENDOFSTREAM and flushes buffered
        // frames. Must run on the worker thread (CUDA context current).
        int n = 0;
        try {
            n = decoder_->Decode(nullptr, 0);
        } catch (...) {
            workerError_ = true;
            return;
        }
        for (int i = 0; i < n; i++) {
            CodecFrame f;
            if (takeOneFrame(f)) {
                frames_.push(std::move(f));
            }
        }
        cvFrames_.notify_all();
    }
};

// Device-frame pool for zero-copy mode. NvDecoder::GetLockedFrame() erases a
// frame buffer from the SDK's internal pool and ~NvDecoder only cuMemFree()s
// the buffers still in m_vpFrame, so a checked-out device frame would be owned
// by nobody. This pool owns exactly the checked-out set: it hands each buffer
// back to NvDecoder via UnlockFrame() once the last reference (the emitted
// CodecFrame's release()) goes away, and force-frees whatever is still
// outstanding at teardown.
//
// Destruction order matters: ~NvDecoder pushes m_cuContext, so buffers must be
// freed and returned before the decoder (and the CUDA context) go away. That is
// why NVDecoder::Finalize() releases framePool_ explicitly rather than leaving
// it to member destruction order.
class NVDecoder::DeviceFramePool {
public:
    DeviceFramePool(NvDecoder* dec, CUcontext ctx) : decoder_(dec), cudaCtx_(ctx) {}
    ~DeviceFramePool() { releaseAll(); }

    DeviceFramePool(const DeviceFramePool&) = delete;
    DeviceFramePool& operator=(const DeviceFramePool&) = delete;

    // Takes one reference to a checked-out frame buffer.
    void adopt(uint8_t* p) {
        std::lock_guard<std::mutex> lk(mu_);
        ++owned_[p];
    }

    // Drops one reference; returns the buffer to NvDecoder on the last one.
    void put(uint8_t* p) {
        NvDecoder* dec = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = owned_.find(p);
            if (it == owned_.end()) {
                return;  // already returned or force-freed at teardown
            }
            if (--it->second == 0) {
                owned_.erase(it);
                dec = decoder_;
            }
        }
        if (!dec) {
            return;
        }
        CUcontext prev = nullptr;
        bool pushed = cudaCtx_ && cuCtxPushCurrent(cudaCtx_) == CUDA_SUCCESS;
        uint8_t* frames[1] = {p};
        uint8_t** fp = frames;
        dec->UnlockFrame(fp);
        if (pushed) {
            cuCtxPopCurrent(&prev);
        }
    }

private:
    // Frees every buffer still checked out (the decoder is gone or going away,
    // so UnlockFrame is not an option) and detaches from the decoder.
    void releaseAll() {
        std::vector<uint8_t*> doomed;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (const auto& kv : owned_) {
                doomed.push_back(kv.first);
            }
            owned_.clear();
            decoder_ = nullptr;
        }
        if (doomed.empty()) {
            return;
        }
        CUcontext prev = nullptr;
        bool pushed = cudaCtx_ && cuCtxPushCurrent(cudaCtx_) == CUDA_SUCCESS;
        for (uint8_t* p : doomed) {
            cuMemFree(reinterpret_cast<CUdeviceptr>(p));
        }
        if (pushed) {
            cuCtxPopCurrent(&prev);
        }
    }

    std::mutex mu_;
    NvDecoder* decoder_ = nullptr;
    CUcontext cudaCtx_ = nullptr;
    std::map<uint8_t*, int> owned_;
};

// DownloadToHost hook for CudaDevice frames. Copies the pitched NVDEC device
// frame plane-by-plane into a tightly-packed host buffer and flips the frame to
// Host. The context owning the pointer is discovered via
// cuPointerGetAttribute (the app thread may have no context current at all),
// which keeps the hook self-contained. The frame's device frame is returned to
// the decoder pool once the copy has completed.
namespace {
bool DownloadCudaFrame(CodecFrame& f) {
    // NVDEC emits single-block pitched device frames (cudaNumPlanes == 0).
    // nvjpeg emits multi-plane frames (cudaNumPlanes > 0) with its own download
    // hook; leave those for it so the two CudaDevice backends don't fight.
    if (f.locality != FrameLocality::CudaDevice || !f.device.cudaPtr
        || f.device.cudaNumPlanes > 0) {
        return false;
    }
    const int w = f.width;
    const int h = f.height;
    const size_t pitch = f.device.cudaPitch ? f.device.cudaPitch : static_cast<size_t>(w);
    const int bpp = (f.format == PixelFormat::P016 || f.format == PixelFormat::P010
                     || f.format == PixelFormat::P210
                     || f.format == PixelFormat::YUV444P10LE) ? 2 : 1;
    const size_t rowBytes = static_cast<size_t>(w) * bpp;

    // NVDEC stores each plane at a whole luma-height offset (NvDecoder.cpp
    // copies plane i to pDecodedFrame + pitch * lumaHeight * i), so a plane's
    // offset is derived from the luma height, not from accumulated heights.
    struct Plane { size_t srcRow; int lines; };
    std::vector<Plane> planes;
    switch (f.format) {
        case PixelFormat::NV12:
        case PixelFormat::P016:
        case PixelFormat::NV16:
        case PixelFormat::P210:
            planes.push_back({0, h});
            planes.push_back({static_cast<size_t>(h), h / 2});
            if (f.format == PixelFormat::NV16 || f.format == PixelFormat::P210) {
                planes.back().lines = h;
            }
            break;
        case PixelFormat::YUV444P:
        case PixelFormat::YUV444P10LE:
            planes.push_back({0, h});
            planes.push_back({static_cast<size_t>(h), h});
            planes.push_back({static_cast<size_t>(h) * 2, h});
            break;
        default:
            return false;  // layout unknown: leave the frame device-resident
    }

    size_t total = 0;
    for (const Plane& p : planes) {
        total += static_cast<size_t>(p.lines) * rowBytes;
    }
    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(total ? total : 1));
    if (!buffer) {
        return false;
    }

    CUcontext ctx = nullptr;
    CUdeviceptr src = static_cast<CUdeviceptr>(f.device.cudaPtr);
    if (cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, src) != CUDA_SUCCESS
        || !ctx) {
        std::free(buffer);
        return false;
    }
    CUcontext prev = nullptr;
    bool pushed = cuCtxPushCurrent(ctx) == CUDA_SUCCESS;
    size_t dstOff = 0;
    bool ok = true;
    for (const Plane& p : planes) {
        CUDA_MEMCPY2D m = {};
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        m.srcDevice = src + pitch * p.srcRow;
        m.srcPitch = pitch;
        m.dstMemoryType = CU_MEMORYTYPE_HOST;
        m.dstHost = buffer + dstOff;
        m.dstPitch = rowBytes;
        m.WidthInBytes = rowBytes;
        m.Height = static_cast<unsigned>(p.lines);
        if (cuMemcpy2D(&m) != CUDA_SUCCESS) {
            ok = false;
            break;
        }
        dstOff += static_cast<size_t>(p.lines) * rowBytes;
    }
    if (pushed) {
        cuCtxPopCurrent(&prev);
    }
    // The device frame is no longer needed once the copy completed.
    if (f.release) {
        f.release();
        f.release = nullptr;
    }
    if (!ok) {
        std::free(buffer);
        return false;
    }
    f.data = buffer;
    f.size = total;
    f.strides[0] = rowBytes;
    f.strides[1] = rowBytes;
    f.locality = FrameLocality::Host;
    f.device = {};
    f.release = [buffer]() { std::free(buffer); };
    return true;
}

const bool g_cudaDownloadRegistered = [] {
    RegisterCudaFrameDownload(&DownloadCudaFrame);
    return true;
}();
}  // namespace

bool NVDecoder::Initialize(const CodecParams& params) {
    Rect cropRect = {};
    Dim resizeDim = {};
    if (params.inputs.empty()) {
        // Explicit-feeding mode: no internal source; the caller delivers
        // compressed data via FillInput() / SignalInputComplete(). Runs
        // asynchronously (isAsync() == true) on an internal worker thread.
        feedMode_ = true;
        if (!cudaCtx_.create(params.deviceIndex)) {
            std::cerr << "NVDecoder: failed to create CUDA context for device "
                      << params.deviceIndex << std::endl;
            return false;
        }
        auto cudaCtx = cudaCtx_.get();
        cudaVideoCodec codec =
            codecFromName(params.codec.empty() ? "h264" : params.codec);
        if (codec == cudaVideoCodec_NumCodecs) {
            std::cerr << "NVDecoder: unknown codec: " << params.codec << std::endl;
            return false;
        }
        // Codec-level capability probe: refuse up front if this GPU's NVDEC
        // engine doesn't support the codec (e.g. JPEG on some SKUs), instead
        // of failing deep inside NvDecoder construction. Single JPEG files
        // can still be decoded via the nvjpeg backend in that case.
        if (!nvdecSupportsCodec(params.deviceIndex, codec)) {
            std::cerr << "NVDecoder: codec " << nvdecCodecName(codec)
                      << " not supported by NVDEC on device "
                      << params.deviceIndex
                      << " (for single JPEG images try -b nvjpeg)"
                      << std::endl;
            return false;
        }
        // -z/--zero-copy: NvDecoder allocates pitched device buffers and copies
        // the decoded surface device-to-device instead of into host memory, so
        // GetFrame() hands out FrameLocality::CudaDevice frames with no CPU
        // round-trip.
        zeroCopy_ = params.zeroCopy;
#if NVENCAPI_MAJOR_VERSION > 12
        decoder_ = std::make_unique<NvDecoder>(cudaCtx, zeroCopy_,
            codec, false, zeroCopy_, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
        decoder_ = std::make_unique<NvDecoder>(cudaCtx, zeroCopy_,
            codec, false, zeroCopy_, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
        decoder_->SetOperatingPoint(0, false);
        framePool_ = std::make_shared<DeviceFramePool>(decoder_.get(), cudaCtx);
        feed_ = std::make_unique<AsyncFeed>(this, decoder_.get(), cudaCtx);
        feed_->start();
        std::cout << "NVDecoder: async feed session up, codec=" << params.codec
                  << (zeroCopy_ ? ", zero-copy device frames" : "")
                  << std::endl;
        return true;
    }

    if (!cudaCtx_.create(params.deviceIndex)) {
        std::cerr << "NVDecoder: failed to create CUDA context for device "
                  << params.deviceIndex << std::endl;
        return false;
    }
    auto cudaCtx = cudaCtx_.get();

    demuxer_ = std::make_unique<FFmpegDemuxer>(params.inputs[0].c_str());
    cudaVideoCodec codec = FFmpeg2NvCodecId(demuxer_->GetVideoCodec());
    // Codec-level capability probe (mirrors the feed-mode path): refuse up
    // front if this GPU's NVDEC engine doesn't support the demuxed codec,
    // instead of failing deep inside NvDecoder construction. Single JPEG
    // files can still be decoded via the nvjpeg backend in that case.
    if (!nvdecSupportsCodec(params.deviceIndex, codec)) {
        std::cerr << "NVDecoder: codec " << nvdecCodecName(codec)
                  << " not supported by NVDEC on device "
                  << params.deviceIndex
                  << " (for single JPEG images try -b nvjpeg)"
                  << std::endl;
        return false;
    }
    zeroCopy_ = params.zeroCopy;
#if NVENCAPI_MAJOR_VERSION > 12
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, zeroCopy_,
        codec,
        false, zeroCopy_, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, zeroCopy_,
        codec,
        false, zeroCopy_, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
    decoder_->SetOperatingPoint(0, false);
    framePool_ = std::make_shared<DeviceFramePool>(decoder_.get(), cudaCtx);
    return true;
}

int NVDecoder::FillInput(const uint8_t* data, size_t size) {
    if (!feedMode_ || !feed_) {
        return -1;
    }
    if (data && size > 0) {
        feed_->append(data, size);
    }
    return 0;
}

bool NVDecoder::SignalInputComplete() {
    if (!feedMode_ || !feed_) {
        return false;
    }
    feed_->signalEOF();
    return true;
}

int NVDecoder::PullFrames() {
    if (feedMode_) {
        // Async-only in feed mode; the sync PullFrames contract is not used.
        return 0;
    }
    uint8_t* pVideo = nullptr;
    int nVideoBytes = 0;
    int nFrame = 0;
    do {
        demuxer_->Demux(&pVideo, &nVideoBytes);
        nFrame = decoder_->Decode(pVideo, nVideoBytes);
    } while (nFrame == 0 && nVideoBytes > 0);
    return nFrame;
}

void NVDecoder::Finalize() {
    feed_.reset();  // joins the worker thread first
    // Return or free every device frame still checked out before the decoder
    // (and with it the frame pool inside it) goes away. Anything the
    // application still holds after this point is freed by the pool, not
    // UnlockFrame'd, since the decoder is about to disappear.
    framePool_ = nullptr;
    if (decoder_) {
        std::cout << decoder_->GetVideoInfo();
    }
    decoder_ = nullptr;
    demuxer_ = nullptr;
}

NVDecoder::~NVDecoder() {
    Finalize();
}

std::string NVDecoder::getName() const {
    return "nvdec";
}

bool NVDecoder::isAsync() const {
    // Feed mode runs an async worker; file/demuxer mode stays sync (PullFrames).
    return feedMode_;
}

void NVDecoder::EmitDeviceFrame(CodecFrame& out, uint8_t* locked, int64_t pts) const {
    // Describe a checked-out NVDEC device buffer as a pitched CudaDevice frame.
    // The pool takes a reference so the buffer is not leaked while the
    // application holds the frame; release() gives it back to NvDecoder.
    framePool_->adopt(locked);
    out.data = nullptr;
    out.size = 0;
    out.width = decoder_->GetWidth();
    out.height = decoder_->GetHeight();
    out.format = toHalFormat(decoder_->GetOutputFormat());
    out.pts = pts;
    out.strides[0] = static_cast<size_t>(decoder_->GetDeviceFramePitch());
    out.strides[1] = out.strides[0];
    out.locality = FrameLocality::CudaDevice;
    out.device = {};
    out.device.cudaPtr = reinterpret_cast<uintptr_t>(locked);
    out.device.cudaPitch = out.strides[0];
    std::shared_ptr<DeviceFramePool> pool = framePool_;
    out.release = [pool, locked]() { pool->put(locked); };
}

bool NVDecoder::GetFrame(CodecFrame& out) {
    if (feedMode_ && feed_) {
        return feed_->getFrame(out);
    }
    // Sync (demuxer) mode: non-blocking pull of one locked frame.
    int64_t pts = 0;
    uint8_t* locked = decoder_->GetLockedFrame(&pts);
    if (!locked) {
        return false;
    }
    if (zeroCopy_) {
        EmitDeviceFrame(out, locked, pts);
        return true;
    }
    out.data = locked;
    out.size = decoder_->GetFrameSize();
    out.width = decoder_->GetWidth();
    out.height = decoder_->GetHeight();
    out.format = toHalFormat(decoder_->GetOutputFormat());
    out.strides[0] = decoder_->GetDeviceFramePitch();
    out.pts = pts;
    // NVDEC owns the locked frame buffer; the caller must not free it.
    out.release = nullptr;
    return true;
}

HALCODEC_CONNECT(Decoder, nvdec, NVDecoder);

} // namespace nvenc
} // namespace halcodec