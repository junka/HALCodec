#include "qsvdecoder.h"

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "qsv_common.h"
#include "registry.h"

namespace halcodec {
namespace qsv {

namespace {

// Bitstream buffer for DecodeHeader + the working bitstream. The FillInput
// path appends into this; the worker consumes it.
constexpr mfxU32 kBitstreamBytes = 4 * 1024 * 1024;
constexpr mfxU32 kSyncTimeoutMs = 1000;

// Copies an NV12 internal-memory surface into a tightly-packed malloc'd buffer
// (Y plane w*h followed by interleaved UV plane w*h/2), pitch-aware. The
// caller owns the returned buffer and must free() it.
uint8_t* CopyNV12(const mfxFrameSurface1* surf) {
    mfxU16 w = surf->Info.CropW;
    mfxU16 h = surf->Info.CropH;
    mfxU16 pitch = surf->Data.Pitch;
    size_t yBytes = static_cast<size_t>(w) * h;
    size_t uvBytes = static_cast<size_t>(w) * (h / 2);
    uint8_t* dst = static_cast<uint8_t*>(std::malloc(yBytes + uvBytes));
    if (!dst) {
        return nullptr;
    }
    for (mfxU16 i = 0; i < h; i++) {
        std::memcpy(dst + static_cast<size_t>(w) * i,
                    surf->Data.Y + static_cast<size_t>(pitch) * i, w);
    }
    uint8_t* dstUV = dst + yBytes;
    for (mfxU16 i = 0; i < h / 2; i++) {
        std::memcpy(dstUV + static_cast<size_t>(w) * i,
                    surf->Data.UV + static_cast<size_t>(pitch) * i, w);
    }
    return dst;
}

} // namespace

// PIMPL keeps the libvpl session handle, the worker thread, and the
// synchronization primitives out of the public header.
//
// Async model: the caller drives input via FillInput() (append into bs_) and
// SignalInputComplete() (mark EOF). An internal worker thread consumes bs_,
// drives MFXVideoDECODE_DecodeFrameAsync, synchronizes completed surfaces, and
// pushes decoded CodecFrames onto frames_. GetFrame() blocks on frames_ being
// non-empty or the worker having finished. This lets the hardware pipeline
// multiple DecodeFrameAsync submissions deep instead of serializing on each.
class QSVDecoder::Impl {
public:
    QSVRuntime runtime;
    mfxSession session = nullptr;
    bool inited = false;     // decodeInit succeeded
    bool headerParsed = false;
    mfxU32 codecId = 0;

    // Working bitstream. FillInput appends into the tail; the worker compacts
    // and consumes from DataOffset.
    mfxBitstream bs{};

    std::mutex mu;
    std::condition_variable cvInput;   // worker waits for new input / EOF
    std::condition_variable cvFrames;  // GetFrame waits for frames / done
    bool eof = false;       // SignalInputComplete called
    bool finished = false;  // worker has drained the decoder
    bool workerError = false;
    std::queue<CodecFrame> frames;
    std::thread worker;

    // Pending surfaces submitted to the decoder but not yet synchronized.
    struct Pending {
        mfxFrameSurface1* surf;
        mfxSyncPoint sync;
    };
    std::vector<Pending> pending;

    void startWorker() {
        worker = std::thread([this] { run(); });
    }

    // Compacts the bitstream (moves unconsumed tail to the front). Caller
    // holds mu.
    void compactLocked() {
        if (bs.DataOffset > 0 && bs.DataLength > 0) {
            std::memmove(bs.Data, bs.Data + bs.DataOffset, bs.DataLength);
        }
        bs.DataOffset = 0;
    }

    // Appends caller data into bs_, growing if needed. Caller holds mu.
    void appendLocked(const uint8_t* data, size_t size) {
        size_t need = bs.DataLength + size;
        if (need > bs.MaxLength) {
            // Grow the buffer to fit, preserving existing data.
            mfxU32 newSize = static_cast<mfxU32>(need);
            mfxU8* p = static_cast<mfxU8*>(std::realloc(bs.Data, newSize));
            if (!p) {
                workerError = true;
                return;
            }
            bs.Data = p;
            bs.MaxLength = newSize;
        }
        std::memcpy(bs.Data + bs.DataLength, data, size);
        bs.DataLength += static_cast<mfxU32>(size);
    }

    // Worker: lazily parses the header (once enough data has arrived), then
    // decodes until EOF + drain complete.
    void run() {
        // Phase 1: accumulate data until DecodeHeader succeeds, then Init.
        while (!headerParsed) {
            std::unique_lock<std::mutex> lk(mu);
            cvInput.wait(lk, [this] { return eof || workerError || bs.DataLength > 0; });
            if (workerError) goto out;
            if (bs.DataLength == 0) {
                // EOF with no data at all.
                if (eof) { workerError = true; }
                goto out;
            }
            {
                compactLocked();
                mfxVideoParam par{};
                par.mfx.CodecId = codecId;
                par.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
                mfxStatus sts = runtime.decodeHeader(session, &bs, &par);
                if (sts == MFX_ERR_MORE_DATA) {
                    if (eof) {
                        std::cerr << "QSVDecoder: EOF before header parsed\n";
                        workerError = true;
                        goto out;
                    }
                    // Need more input; loop waits for FillInput.
                    if (!eof) {
                        // Wait for more data. Release lock and re-loop.
                        continue;
                    }
                } else if (sts != MFX_ERR_NONE) {
                    std::cerr << "QSVDecoder: DecodeHeader failed: " << sts << "\n";
                    workerError = true;
                    goto out;
                } else {
                    sts = runtime.decodeInit(session, &par);
                    if (sts != MFX_ERR_NONE) {
                        std::cerr << "QSVDecoder: decodeInit failed: " << sts << "\n";
                        workerError = true;
                        goto out;
                    }
                    inited = true;
                    headerParsed = true;
                    cvFrames.notify_all();
                }
            }
        }

        // Phase 2: decode loop.
        while (true) {
            mfxBitstream* bsPtr = nullptr;
            {
                std::unique_lock<std::mutex> lk(mu);
                // Consume any data first; if none and not EOF, wait.
                cvInput.wait(lk, [this] {
                    return eof || workerError || bs.DataLength > 0;
                });
                if (workerError) goto out;
                compactLocked();
                if (bs.DataLength > 0) {
                    bsPtr = &bs;
                } else if (eof) {
                    bsPtr = nullptr; // drain mode
                } else {
                    continue;
                }
            }

            mfxFrameSurface1* surfOut = nullptr;
            mfxSyncPoint syncp{};
            mfxStatus sts = runtime.decodeFrameAsync(session, bsPtr, nullptr,
                                                    &surfOut, &syncp);
            switch (sts) {
                case MFX_ERR_NONE: {
                    // Track the surface for batched synchronization.
                    std::lock_guard<std::mutex> lk(mu);
                    pending.push_back({surfOut, syncp});
                    // Synchronize a batch when several are in flight.
                    if (pending.size() >= 4) {
                        drainPendingLocked();
                    }
                    break;
                }
                case MFX_ERR_MORE_DATA: {
                    std::lock_guard<std::mutex> lk(mu);
                    if (eof) {
                        // Flush any remaining pending surfaces, then done.
                        drainPendingLocked();
                        finished = true;
                        cvFrames.notify_all();
                        goto out;
                    }
                    // Need more input; loop back to wait.
                    break;
                }
                case MFX_ERR_MORE_SURFACE:
                case MFX_WRN_DEVICE_BUSY:
                    break;
                default:
                    std::cerr << "QSVDecoder: DecodeFrameAsync error: " << sts << "\n";
                    workerError = true;
                    cvFrames.notify_all();
                    goto out;
            }
        }
    out:
        {
            std::lock_guard<std::mutex> lk(mu);
            finished = true;
            cvFrames.notify_all();
        }
    }

    // Synchronizes and copies out all pending surfaces into frames_. Caller
    // holds mu. Surfaces with MFX_WRN_IN_EXECUTION are retried inline.
    void drainPendingLocked() {
        for (auto& p : pending) {
            mfxFrameSurfaceInterface* fi = p.surf->FrameInterface;
            mfxStatus sts = MFX_WRN_IN_EXECUTION;
            while (sts == MFX_WRN_IN_EXECUTION) {
                sts = fi->Synchronize(p.surf, kSyncTimeoutMs);
            }
            if (sts != MFX_ERR_NONE) {
                fi->Release(p.surf);
                continue;
            }
            sts = fi->Map(p.surf, MFX_MAP_READ);
            if (sts != MFX_ERR_NONE) {
                fi->Release(p.surf);
                continue;
            }
            CodecFrame frame;
            frame.width = p.surf->Info.CropW;
            frame.height = p.surf->Info.CropH;
            frame.format = PixelFormat::NV12;
            frame.size = static_cast<size_t>(frame.width) * frame.height * 3 / 2;
            frame.strides[0] = static_cast<size_t>(frame.width);
            frame.data = CopyNV12(p.surf);
            uint8_t* owned = frame.data;
            frame.release = [owned]() { std::free(owned); };
            fi->Unmap(p.surf);
            fi->Release(p.surf);
            if (frame.data) {
                frames.push(std::move(frame));
            }
        }
        pending.clear();
        cvFrames.notify_all();
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lk(mu);
            eof = true;
            finished = true; // force worker exit if still looping
            cvInput.notify_all();
            cvFrames.notify_all();
        }
        if (worker.joinable()) {
            worker.join();
        }
        while (!frames.empty()) {
            if (frames.front().release) {
                frames.front().release();
            }
            frames.pop();
        }
        if (inited && session) {
            runtime.decodeTerminate(session);
        }
        if (session) {
            runtime.close(session);
        }
        if (bs.Data) {
            std::free(bs.Data);
        }
    }
};

bool QSVDecoder::Initialize(const CodecParams& params) {
    if (impl_) {
        return false; // already initialized
    }
    impl_ = new Impl;
    if (!impl_->runtime.init()) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    if (impl_->runtime.createSession(&impl_->session) != MFX_ERR_NONE) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->codecId = mapCodec(params.codec);
    impl_->bs.MaxLength = kBitstreamBytes;
    impl_->bs.Data = static_cast<mfxU8*>(std::calloc(impl_->bs.MaxLength, 1));
    if (!impl_->bs.Data) {
        std::cerr << "QSVDecoder: bitstream alloc failed" << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->bs.CodecId = impl_->codecId;
    // The worker lazily parses the header (DecodeHeader) once FillInput has
    // delivered enough data, then calls decodeInit and begins decoding.
    impl_->startWorker();
    std::cout << "QSVDecoder: async session up, codec=" << params.codec << std::endl;
    return true;
}

int QSVDecoder::FillInput(const uint8_t* data, size_t size) {
    if (!impl_ || size == 0) {
        return 0;
    }
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->appendLocked(data, size);
    }
    impl_->cvInput.notify_one();
    return 0;
}

bool QSVDecoder::SignalInputComplete() {
    if (!impl_) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->eof = true;
    }
    impl_->cvInput.notify_one();
    return true;
}

bool QSVDecoder::GetFrame(CodecFrame& out) {
    if (!impl_) {
        return false;
    }
    std::unique_lock<std::mutex> lk(impl_->mu);
    impl_->cvFrames.wait(lk, [this] {
        return impl_->finished || impl_->workerError ||
               !impl_->frames.empty();
    });
    if (impl_->workerError && impl_->frames.empty()) {
        return false;
    }
    if (impl_->frames.empty()) {
        return false; // finished and drained
    }
    out = std::move(impl_->frames.front());
    impl_->frames.pop();
    return true;
}

int QSVDecoder::PullFrames() {
    // Async-only backend; the sync PullFrames contract is not used.
    return 0;
}

void QSVDecoder::Finalize() {
    delete impl_;
    impl_ = nullptr;
}

std::string QSVDecoder::getName() const {
    return "qsvdec";
}

HALCODEC_CONNECT(Decoder, qsvdec, QSVDecoder);

} // namespace qsv
} // namespace halcodec
