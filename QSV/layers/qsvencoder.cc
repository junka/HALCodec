#include "qsvencoder.h"

#include <cstring>
#include <cstdlib>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "qsv_common.h"
#include "registry.h"

namespace halcodec {
namespace qsv {

namespace {

// Output bitstream buffer for one encoded access unit. The encoder writes into
// Data[DataOffset .. DataOffset+DataLength]; we copy out and reset after each
// SyncOperation so the same buffer is reused for the next frame.
constexpr mfxU32 kBitstreamBytes = 2 * 1024 * 1024;
constexpr mfxU32 kSyncTimeoutMs = 1000;
// How many EncodeFrameAsync submissions to keep in flight before forcing a
// synchronization. Deeper pipelining raises throughput by overlapping host
// input copy with GPU encoding of prior frames.
constexpr size_t kMaxInFlight = 4;
// Size of the rotating bitstream pool. Each in-flight submission gets its own
// buffer so outputs never collide while we defer synchronization.
constexpr size_t kBitstreamPool = kMaxInFlight + 1;

// Align a dimension up to a multiple of 16 (oneVPL requires Width/Height
// aligned to macroblocks for hardware encode).
mfxU16 Align16(int v) {
    return static_cast<mfxU16>((v + 15) & ~15);
}

// Writes a tightly-packed NV12 CodecFrame (Y: w*h, interleaved UV: w*h/2) into
// a mapped internal-memory encode surface, pitch-aware. Returns MFX_ERR_NONE
// on success. The caller must have Map'd the surface for write already.
mfxStatus FillSurfaceFromFrame(mfxFrameSurface1* surf, const CodecFrame& in) {
    mfxU16 w = surf->Info.CropW;
    mfxU16 h = surf->Info.CropH;
    mfxU16 pitch = surf->Data.Pitch;
    if (w != in.width || h != in.height) {
        return MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;
    }
    size_t yBytes = static_cast<size_t>(w) * h;
    size_t uvBytes = static_cast<size_t>(w) * (h / 2);
    if (in.size < yBytes + uvBytes) {
        return MFX_ERR_NOT_ENOUGH_BUFFER;
    }
    // Y plane.
    for (mfxU16 i = 0; i < h; i++) {
        std::memcpy(surf->Data.Y + static_cast<size_t>(pitch) * i,
                    in.data + static_cast<size_t>(w) * i, w);
    }
    // Interleaved UV plane.
    const uint8_t* srcUV = in.data + yBytes;
    for (mfxU16 i = 0; i < h / 2; i++) {
        std::memcpy(surf->Data.UV + static_cast<size_t>(pitch) * i,
                    srcUV + static_cast<size_t>(w) * i, w);
    }
    return MFX_ERR_NONE;
}

// Copies the encoded access unit out of the bitstream into a malloc'd
// CodecFrame and resets the bitstream (DataOffset=0, DataLength=0) so the
// same buffer is reused for the next frame. The caller owns the returned
// frame's buffer and must free() it (wired via release).
CodecFrame TakeBitstream(mfxBitstream& bs) {
    CodecFrame frame;
    frame.size = bs.DataLength;
    frame.format = PixelFormat::Unknown; // encoded elementary stream
    frame.data = static_cast<uint8_t*>(std::malloc(frame.size ? frame.size : 1));
    if (frame.data && frame.size) {
        std::memcpy(frame.data, bs.Data + bs.DataOffset, frame.size);
    }
    uint8_t* owned = frame.data;
    frame.release = [owned]() { std::free(owned); };
    bs.DataOffset = 0;
    bs.DataLength = 0;
    return frame;
}

} // namespace

// PIMPL keeps the libvpl session handle, the worker thread, and the
// synchronization primitives out of the public header.
//
// Async model: the caller drives input via FillFrame() (queue a raw frame) and
// SignalInputComplete() (mark EOF). An internal worker thread pulls frames,
// drives MFXVideoENCODE_EncodeFrameAsync keeping several submissions in flight,
// batch-synchronizes completed syncpoints, and pushes encoded CodecFrames onto
// packets_. GetFrame() blocks on packets_ being non-empty or the worker having
// finished. This lets the hardware pipeline submissions deep instead of
// serializing on each frame's completion.
class QSVEncoder::Impl {
public:
    QSVRuntime runtime;
    mfxSession session = nullptr;
    bool inited = false;

    mfxVideoParam par{};
    // Rotating pool of output bitstreams so deferred (batched) synchronization
    // never lets two in-flight outputs share a buffer.
    std::vector<mfxBitstream> bsPool;
    size_t bsNext = 0;  // next pool slot to hand to EncodeFrameAsync

    std::mutex mu;
    std::condition_variable cvInput;   // worker waits for new input / EOF
    std::condition_variable cvPackets; // GetFrame waits for packets / done
    bool eof = false;       // SignalInputComplete called
    bool finished = false;  // worker has drained the encoder
    bool workerError = false;
    std::queue<CodecFrame> inFrames;  // raw input awaiting submission
    std::queue<CodecFrame> packets;   // encoded access units for GetFrame
    std::thread worker;

    // Pending EncodeFrameAsync submissions awaiting synchronization. Each
    // carries the syncpoint and the pool slot whose bitstream holds its output.
    struct Pending {
        mfxSyncPoint sync;
        mfxBitstream* bs;
    };
    std::vector<Pending> pending;

    void startWorker() {
        worker = std::thread([this] { run(); });
    }

    // Synchronizes and copies out all pending submissions into packets_.
    // Caller holds mu. The freed pool slots become available again.
    void drainPendingLocked() {
        for (auto& p : pending) {
            if (!p.sync) {
                continue;
            }
            mfxStatus sts = MFX_WRN_IN_EXECUTION;
            while (sts == MFX_WRN_IN_EXECUTION) {
                sts = runtime.syncOperation(session, p.sync, kSyncTimeoutMs);
            }
            if (sts != MFX_ERR_NONE) {
                std::cerr << "QSVEncoder: SyncOperation failed: " << sts << "\n";
                p.bs->DataOffset = 0;
                p.bs->DataLength = 0;
                continue;
            }
            if (p.bs->DataLength > 0) {
                packets.push(TakeBitstream(*p.bs));
            } else {
                p.bs->DataOffset = 0;
                p.bs->DataLength = 0;
            }
        }
        pending.clear();
        cvPackets.notify_all();
    }

    // Hands out the next free bitstream slot. Caller holds mu.
    mfxBitstream* nextBitstreamLocked() {
        mfxBitstream* bs = &bsPool[bsNext];
        bsNext = (bsNext + 1) % bsPool.size();
        bs->DataOffset = 0;
        bs->DataLength = 0;
        return bs;
    }

    void run() {
        while (true) {
            CodecFrame input;
            bool gotFrame = false;
            {
                std::unique_lock<std::mutex> lk(mu);
                cvInput.wait(lk, [this] {
                    return eof || workerError || !inFrames.empty();
                });
                if (workerError) goto out;
                if (!inFrames.empty()) {
                    input = std::move(inFrames.front());
                    inFrames.pop();
                    gotFrame = true;
                } else if (eof) {
                    // EOF with no queued frames: drain the encoder and finish.
                    lk.unlock();
                    if (!drainEncoder()) {
                        std::lock_guard<std::mutex> lk2(mu);
                        workerError = true;
                        cvPackets.notify_all();
                        goto out;
                    }
                    std::lock_guard<std::mutex> lk2(mu);
                    drainPendingLocked();
                    finished = true;
                    cvPackets.notify_all();
                    goto out;
                } else {
                    continue;
                }
            }

            if (gotFrame) {
                // A zero-size frame is the implicit end-of-stream marker (the
                // same contract sync backends use). Treat it as EOF rather than
                // submitting an empty surface; the remaining queued frames
                // (if any) are still submitted first on subsequent iterations.
                if (input.size == 0) {
                    std::lock_guard<std::mutex> lk(mu);
                    eof = true;
                } else if (!submitFrame(input)) {
                    std::lock_guard<std::mutex> lk(mu);
                    workerError = true;
                    cvPackets.notify_all();
                    goto out;
                }
                if (input.release) {
                    input.release();
                }
            }
        }
    out:
        {
            std::lock_guard<std::mutex> lk(mu);
            finished = true;
            cvPackets.notify_all();
        }
    }

    // Submits one raw frame: get surface, copy NV12 in, EncodeFrameAsync. Does
    // NOT synchronize per-frame; batches synchronization when kMaxInFlight
    // submissions are outstanding. Returns false on a fatal submit error.
    bool submitFrame(const CodecFrame& input) {
        mfxSession s = session;
        mfxFrameSurface1* surf = nullptr;
        mfxStatus sts = runtime.getSurfaceForEncode(s, &surf);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "QSVEncoder: GetSurfaceForEncode failed: " << sts << "\n";
            return false;
        }
        sts = surf->FrameInterface->Map(surf, MFX_MAP_WRITE);
        if (sts != MFX_ERR_NONE) {
            std::cerr << "QSVEncoder: surface Map failed: " << sts << "\n";
            surf->FrameInterface->Release(surf);
            return false;
        }
        mfxStatus fillSts = FillSurfaceFromFrame(surf, input);
        mfxStatus unmapSts = surf->FrameInterface->Unmap(surf);
        if (fillSts != MFX_ERR_NONE) {
            std::cerr << "QSVEncoder: input frame incompatible: " << fillSts << "\n";
            surf->FrameInterface->Release(surf);
            return false;
        }
        if (unmapSts != MFX_ERR_NONE) {
            std::cerr << "QSVEncoder: surface Unmap failed: " << unmapSts << "\n";
            surf->FrameInterface->Release(surf);
            return false;
        }

        mfxBitstream* bs = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu);
            // Keep the pipeline bounded: if too many submissions are in flight,
            // synchronize the oldest before submitting more.
            if (pending.size() >= kMaxInFlight) {
                drainPendingLocked();
            }
            bs = nextBitstreamLocked();
        }
        mfxSyncPoint syncp{};
        sts = runtime.encodeFrameAsync(s, surf, bs, &syncp);
        // The encoder holds its own reference once submitted; release ours.
        surf->FrameInterface->Release(surf);

        if (sts == MFX_ERR_NONE) {
            std::lock_guard<std::mutex> lk(mu);
            pending.push_back({syncp, bs});
        } else if (sts == MFX_ERR_MORE_DATA) {
            // Encoder buffered the frame; output comes with a later submission.
            // The bitstream slot was untouched; release it back implicitly by
            // leaving it reset (nextBitstreamLocked resets on handout).
            std::lock_guard<std::mutex> lk(mu);
            // No pending entry: nothing to synchronize for this submission.
        } else {
            std::cerr << "QSVEncoder: EncodeFrameAsync error: " << sts << "\n";
            return false;
        }
        return true;
    }

    // Drains delayed frames by submitting NULL surfaces until the encoder
    // returns MFX_ERR_MORE_DATA. Synchronizes each emitted output. Returns
    // false on a fatal drain error.
    bool drainEncoder() {
        mfxSession s = session;
        while (true) {
            mfxBitstream* bs = nullptr;
            {
                std::lock_guard<std::mutex> lk(mu);
                if (pending.size() >= kMaxInFlight) {
                    drainPendingLocked();
                }
                bs = nextBitstreamLocked();
            }
            mfxSyncPoint syncp{};
            mfxStatus sts = runtime.encodeFrameAsync(s, nullptr, bs, &syncp);
            if (sts == MFX_ERR_NONE) {
                std::lock_guard<std::mutex> lk(mu);
                pending.push_back({syncp, bs});
            } else if (sts == MFX_ERR_MORE_DATA || sts == MFX_ERR_NOT_ENOUGH_BUFFER) {
                // Encoder fully flushed.
                break;
            } else {
                std::cerr << "QSVEncoder: drain EncodeFrameAsync error: " << sts << "\n";
                return false;
            }
        }
        return true;
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lk(mu);
            eof = true;
            finished = true; // force worker exit if still looping
            cvInput.notify_all();
            cvPackets.notify_all();
        }
        if (worker.joinable()) {
            worker.join();
        }
        while (!inFrames.empty()) {
            if (inFrames.front().release) {
                inFrames.front().release();
            }
            inFrames.pop();
        }
        while (!packets.empty()) {
            if (packets.front().release) {
                packets.front().release();
            }
            packets.pop();
        }
        if (inited && session) {
            runtime.encodeTerminate(session);
        }
        if (session) {
            runtime.close(session);
        }
        for (auto& bs : bsPool) {
            if (bs.Data) {
                std::free(bs.Data);
            }
        }
    }
};

bool QSVEncoder::Initialize(const CodecParams& params) {
    if (impl_) {
        return false; // already initialized
    }
    if (params.width <= 0 || params.height <= 0) {
        std::cerr << "QSVEncoder: width/height required in CodecParams" << std::endl;
        return false;
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

    mfxVideoParam& par = impl_->par;
    par.mfx.CodecId = mapCodec(params.codec);
    // Unified EncodeConfig overrides; sentinels fall back to the prior
    // hardcoded defaults (BALANCED / 8 Mbps / CBR / 30 fps).
    const auto& ec = params.encode;
    par.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    if (!ec.preset.empty()) {
        if (ec.preset == "fast" || ec.preset == "speed")
            par.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_SPEED;
        else if (ec.preset == "slow" || ec.preset == "best" || ec.preset == "quality")
            par.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_QUALITY;
        else if (ec.preset == "balanced")
            par.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    }
    par.mfx.TargetKbps = ec.bitrateKbps > 0
        ? static_cast<mfxU16>(ec.bitrateKbps)
        : 8000; // ~8 Mbps default
    // Rate control: CBR is the safe default (libmfx-gen rejects VBR for AVC on
    // Arrow Lake iGPU at submit time). ec.rateControl overrides only when set.
    par.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    if (!ec.rateControl.empty()) {
        if (ec.rateControl == "vbr")      par.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
        else if (ec.rateControl == "cqp") par.mfx.RateControlMethod = MFX_RATECONTROL_CQP;
        else if (ec.rateControl == "icq") par.mfx.RateControlMethod = MFX_RATECONTROL_ICQ;
        // "cbr" or unknown => CBR (default)
    }
    if (ec.qp >= 0) {
        par.mfx.QPI = par.mfx.QPP = par.mfx.QPB = static_cast<mfxU16>(ec.qp);
    }
    // GOP / B-frame: GopPicSize/GopRefDist are direct fields on mfxInfoMFX
    // (no extension buffer needed). GopRefDist=1 => I/P only (no B-frames).
    if (ec.gopLength > 0) {
        par.mfx.GopPicSize = static_cast<mfxU16>(ec.gopLength);
    }
    int bframes = ec.lowDelay ? 0 : ec.numBFrames;
    if (bframes >= 0) {
        par.mfx.GopRefDist = static_cast<mfxU16>(bframes + 1);
    }
    par.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    par.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    par.mfx.FrameInfo.CropW = static_cast<mfxU16>(params.width);
    par.mfx.FrameInfo.CropH = static_cast<mfxU16>(params.height);
    par.mfx.FrameInfo.Width = Align16(params.width);
    par.mfx.FrameInfo.Height = Align16(params.height);
    par.mfx.FrameInfo.FrameRateExtN = ec.frameRateNum > 0
        ? static_cast<mfxU16>(ec.frameRateNum) : 30;
    par.mfx.FrameInfo.FrameRateExtD = ec.frameRateDen > 0
        ? static_cast<mfxU16>(ec.frameRateDen) : 1;
    par.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    par.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    // Validate / clamp parameters against what the implementation supports.
    // MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is benign: the encoder adjusts the
    // structure to the nearest supported config.
    mfxStatus sts = impl_->runtime.encodeQuery(impl_->session, &par, &par);
    if (sts == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        sts = MFX_ERR_NONE;
    }
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: MFXVideoENCODE_Query failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    sts = impl_->runtime.encodeInit(impl_->session, &par);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: MFXVideoENCODE_Init failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    impl_->bsPool.resize(kBitstreamPool);
    for (auto& bs : impl_->bsPool) {
        bs.MaxLength = kBitstreamBytes;
        bs.Data = static_cast<mfxU8*>(std::calloc(bs.MaxLength, 1));
        if (!bs.Data) {
            std::cerr << "QSVEncoder: bitstream alloc failed" << std::endl;
            delete impl_;
            impl_ = nullptr;
            return false;
        }
    }
    impl_->inited = true;
    impl_->startWorker();
    std::cout << "QSVEncoder: async session up, codec=" << params.codec << " "
              << params.width << "x" << params.height << std::endl;
    return true;
}

bool QSVEncoder::FillFrame(const CodecFrame& input) {
    if (!impl_ || !impl_->inited) {
        return false;
    }
    // Frames are queued for the worker; a zero-size frame is interpreted by
    // the worker as the implicit end-of-stream marker (the sync-backend
    // contract). SignalInputComplete() is the explicit async EOF path.
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->inFrames.push(input);
    }
    impl_->cvInput.notify_one();
    return true;
}

bool QSVEncoder::SignalInputComplete() {
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

bool QSVEncoder::GetFrame(CodecFrame& out) {
    if (!impl_) {
        return false;
    }
    std::unique_lock<std::mutex> lk(impl_->mu);
    impl_->cvPackets.wait(lk, [this] {
        return impl_->finished || impl_->workerError ||
               !impl_->packets.empty();
    });
    if (impl_->workerError && impl_->packets.empty()) {
        return false;
    }
    if (impl_->packets.empty()) {
        return false; // finished and drained
    }
    out = std::move(impl_->packets.front());
    impl_->packets.pop();
    return true;
}

void QSVEncoder::Finalize() {
    delete impl_;
    impl_ = nullptr;
}

std::string QSVEncoder::getName() const {
    return "qsvenc";
}

HALCODEC_CONNECT(Encoder, qsvenc, QSVEncoder);

} // namespace qsv
} // namespace halcodec
