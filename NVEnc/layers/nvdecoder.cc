#include "nvdecoder.h"

#include <cuda.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "frame.h"
#include "registry.h"

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
    if (name == "mpeg2") return cudaVideoCodec_MPEG2;
    if (name == "mpeg4") return cudaVideoCodec_MPEG4;
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
    AsyncFeed(NvDecoder* dec, CUcontext ctx)
        : decoder_(dec), cudaCtx_(ctx) {}

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

    // Copies one decoded frame out of NvDecoder's host-memory locked pool into
    // a tightly-packed malloc'd CodecFrame (pitch-aware). Returns false if no
    // frame was available. The CUDA context must be current on this thread.
    bool takeOneFrame(CodecFrame& out) {
        int64_t pts = 0;
        uint8_t* locked = decoder_->GetLockedFrame(&pts);
        if (!locked) {
            return false;
        }
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
#if NVENCAPI_MAJOR_VERSION > 12
        decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
            codec, false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
        decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
            codec, false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
        decoder_->SetOperatingPoint(0, false);
        feed_ = std::make_unique<AsyncFeed>(decoder_.get(), cudaCtx);
        feed_->start();
        std::cout << "NVDecoder: async feed session up, codec=" << params.codec
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
#if NVENCAPI_MAJOR_VERSION > 12
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
        FFmpeg2NvCodecId(demuxer_->GetVideoCodec()),
        false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, false,
        FFmpeg2NvCodecId(demuxer_->GetVideoCodec()),
        false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
    decoder_->SetOperatingPoint(0, false);
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

bool NVDecoder::GetFrame(CodecFrame& out) {
    if (feedMode_ && feed_) {
        return feed_->getFrame(out);
    }
    // Sync (demuxer) mode: non-blocking pull of one locked frame.
    int64_t pts = 0;
    out.data = decoder_->GetLockedFrame(&pts);
    if (!out.data) {
        return false;
    }
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