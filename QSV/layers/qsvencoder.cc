#include "qsvencoder.h"

#include <cstring>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "qsv_common.h"
#include "metrics.h"
#include "registry.h"

// ONEVPL_EXPERIMENTAL (defined via CMake) gates the mfxSurfaceInterface /
// mfxMemoryInterface layouts used by the zero-copy ImportFrameSurface path.

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
    // -z/--zero-copy: input frames arrive as FrameLocality::OneVPLSurface
    // (an exported mfxSurfaceHeader* from a decoder session). The encoder is
    // initialized with IN_VIDEO_MEMORY and ImportFrameSurface's each input
    // into this session (no host memcpy). Off => IN_SYSTEM_MEMORY host path.
    bool zeroCopy = false;
    // Decode session's VADisplay (opaque void*), SetHandle'd before encodeInit
    // so the encode session shares the decoder's VA display. Required for
    // ImportFrameSurface to succeed (vaDisplay match). Null in host mode.
    void* vaDisplay = nullptr;
    // Cached encode-session memory interface for ImportFrameSurface. Fetched
    // once after encodeInit via MFXVideoCORE_GetHandle(MFX_HANDLE_MEMORY_INTERFACE).
    mfxMemoryInterface* memIface = nullptr;

    // Runtime observation counters (Layer 1) + Layer-2 vendor stat cache.
    // Registered into the process-wide metrics registry at Initialize so
    // snapshotStreams() can copy it out; unregistered in ~Impl. The worker
    // mutates the counters under mu (same lock the pipeline already holds).
    StreamStats stats_{};

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
                // Layer-1 counters: one encoded access unit emitted, with its
                // byte size. Updated under mu (caller holds it).
                stats_.framesOut++;
                stats_.bytesOut += packets.back().size;
            } else {
                p.bs->DataOffset = 0;
                p.bs->DataLength = 0;
            }
        }
        pending.clear();
        stats_.pendingAsync = 0;
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
                // A zero-size HOST frame is the implicit end-of-stream marker
                // (the sync-backend contract). Device-resident frames
                // (OneVPLSurface) legitimately have size==0 because their host
                // size is unknown until downloaded, so the locality check keeps
                // them from being swallowed as EOF. The remaining queued frames
                // (if any) are still submitted first on subsequent iterations.
                if (input.size == 0 && input.locality == FrameLocality::Host) {
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

    // Submits one raw frame. Host path: get surface, copy NV12 in,
    // EncodeFrameAsync. Zero-copy path: ImportFrameSurface the exported
    // mfxSurfaceHeader* into this session and feed the imported surface
    // straight to EncodeFrameAsync (no Map, no memcpy). Neither path
    // synchronizes per-frame; both batch synchronization when kMaxInFlight
    // submissions are outstanding. Returns false on a fatal submit error.
    bool submitFrame(const CodecFrame& input) {
        mfxSession s = session;
        mfxBitstream* bs = nullptr;
        mfxSyncPoint syncp{};
        mfxStatus sts;

        // Layer-1 input counter: one frame submitted. For the host path the
        // byte count is the raw NV12 size; for the zero-copy device path it is
        // 0 (no host bytes). Updated here (before the lock) since framesIn is
        // only read under mu by snapshotStreams; an occasional torn read of a
        // uint64 is acceptable for observation.
        stats_.framesIn++;
        if (input.locality == FrameLocality::Host) {
            stats_.bytesIn += input.size;
        }

        mfxFrameSurface1* surf = nullptr;
        bool importedSurface = false;
        if (input.locality == FrameLocality::OneVPLSurface) {
            // Zero-copy device input. Import the decoder's exported header
            // into this encode session — ImportFrameSurface returns a new
            // mfxFrameSurface1* backed by the same video memory (shared mode)
            // or a runtime-staged copy (copy mode, if sharing unsupported).
            // Either way we hand the imported surface to EncodeFrameAsync with
            // no host memcpy.
            if (!memIface) {
                std::cerr << "QSVEncoder: zero-copy input but no memory "
                             "interface (encodeInit path skipped it?)\n";
                return false;
            }
            if (!input.device.vplExportedHeader) {
                std::cerr << "QSVEncoder: OneVPLSurface frame has no header\n";
                return false;
            }
            mfxSurfaceHeader* header = static_cast<mfxSurfaceHeader*>(
                input.device.vplExportedHeader);
            // The decoder's Export set SurfaceFlags = EXPORT_SHARED (0x100),
            // which the import-side check_import_flags() rejects (it only
            // accepts IMPORT_SHARED / IMPORT_COPY / DEFAULT). Rewrite to
            // IMPORT_SHARED before importing: the runtime then maps the shared
            // native handle directly (true zero-copy) and backfills the
            // resulted flag — IMPORT_SHARED if it shared, IMPORT_COPY if it
            // had to fall back. Without this rewrite ImportFrameSurface -4's.
            //
            // Copy the header to a stack-local struct first: the exported
            // header is the decoder's refcounted mfxSurfaceVAAPI object, and
            // mutating its flags in place would corrupt the decoder's view
            // (and break a later re-import of the same header). The runtime's
            // import path only reads the header fields — it does not AddRef or
            // Release the import header itself — so a plain copy is safe.
            mfxSurfaceVAAPI importCopy{};
            importCopy.SurfaceInterface.Header = *header;
            importCopy.SurfaceInterface.Header.SurfaceFlags =
                MFX_SURFACE_FLAG_IMPORT_SHARED;
            // Copy the VA handles (vaDisplay / vaSurfaceID) from the exported
            // mfxSurfaceVAAPI. The header pointer above is the first member of
            // an mfxSurfaceVAAPI, so reinterpret to reach the VA fields.
            {
                auto src = reinterpret_cast<mfxSurfaceVAAPI*>(
                    input.device.vplExportedHeader);
                importCopy.vaDisplay = src->vaDisplay;
                importCopy.vaSurfaceID = src->vaSurfaceID;
            }
            mfxSurfaceHeader* importHeader =
                reinterpret_cast<mfxSurfaceHeader*>(&importCopy);
            sts = memIface->ImportFrameSurface(memIface,
                MFX_SURFACE_COMPONENT_ENCODE, importHeader, &surf);
            if (sts != MFX_ERR_NONE || !surf) {
                std::cerr << "QSVEncoder: ImportFrameSurface failed: "
                          << sts << "\n";
                return false;
            }
            importedSurface = true;
        } else {
            // Host path: stage the NV12 pixels into a runtime-allocated encode
            // surface.
            sts = runtime.getSurfaceForEncode(s, &surf);
            if (sts != MFX_ERR_NONE) {
                std::cerr << "QSVEncoder: GetSurfaceForEncode failed: "
                          << sts << "\n";
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
                std::cerr << "QSVEncoder: input frame incompatible: "
                          << fillSts << "\n";
                surf->FrameInterface->Release(surf);
                return false;
            }
            if (unmapSts != MFX_ERR_NONE) {
                std::cerr << "QSVEncoder: surface Unmap failed: "
                          << unmapSts << "\n";
                surf->FrameInterface->Release(surf);
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> lk(mu);
            // Keep the pipeline bounded: if too many submissions are in flight,
            // synchronize the oldest before submitting more.
            if (pending.size() >= kMaxInFlight) {
                drainPendingLocked();
            }
            bs = nextBitstreamLocked();
        }
        sts = runtime.encodeFrameAsync(s, surf, bs, &syncp);
        // The encoder holds its own reference once submitted; release ours.
        // For the imported (zero-copy) surface this drops the import-side ref;
        // the underlying shared memory stays alive via the decoder's exported
        // header until the caller Releases the CodecFrame.
        surf->FrameInterface->Release(surf);
        (void)importedSurface;

        if (sts == MFX_ERR_NONE) {
            std::lock_guard<std::mutex> lk(mu);
            pending.push_back({syncp, bs});
            stats_.pendingAsync = static_cast<int>(pending.size());
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
                stats_.pendingAsync = static_cast<int>(pending.size());
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
        // Unregister from the metrics registry before tearing down the session,
        // so snapshotStreams() never sees a stale pointer. The stats_ struct
        // itself stays valid until the Impl is destroyed (after this dtor).
        unregisterStreamStats(&stats_);
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
    impl_->zeroCopy = params.zeroCopy;
    // QSV zero-copy requires the encode session to share the decode session's
    // VADisplay: the exported surface carries the decoder's vaDisplay, and
    // ImportFrameSurface rejects (-4) any surface whose vaDisplay != the encode
    // session's own VADisplay. The decoder propagates its VADisplay via
    // CodecParams::sharedDeviceHandle (an opaque void*); SetHandle it before
    // Init so CheckOrInitDisplay() reuses it instead of opening its own.
    impl_->vaDisplay = params.sharedDeviceHandle;
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
    // Share the decode session's VADisplay immediately after session creation
    // (zero-copy only) — before encodeQuery/encodeInit. Those touch the core,
    // whose GetHWType() lazily inits the session's own VADisplay via
    // CheckOrInitDisplay(); once that runs, SetHandle refuses with -16
    // (MFX_ERR_UNDEFINED_BEHAVIOR). Sharing the decoder's VADisplay is required
    // for ImportFrameSurface: the imported surface's vaDisplay must equal the
    // encode session's own, else -4.
    if (impl_->zeroCopy) {
        if (!impl_->vaDisplay) {
            std::cerr << "QSVEncoder: zero-copy requested but no shared "
                         "VADisplay in CodecParams (decoder did not propagate "
                         "MFX_HANDLE_VA_DISPLAY)\n";
            delete impl_;
            impl_ = nullptr;
            return false;
        }
        // The decode session's VADisplay must be set on this fresh encode
        // session before any core call (encodeQuery/encodeInit) — those touch
        // the core, whose GetHWType() lazily inits the session's own VADisplay
        // via CheckOrInitDisplay(); once that runs, SetHandle refuses (-16).
        // Sharing the decoder's VADisplay is required for ImportFrameSurface:
        // the imported surface's vaDisplay must equal the encode session's own.
        mfxStatus sh = impl_->runtime.setHandle(impl_->session,
            MFX_HANDLE_VA_DISPLAY, impl_->vaDisplay);
        if (sh != MFX_ERR_NONE) {
            std::cerr << "QSVEncoder: SetHandle(VA_DISPLAY) failed: "
                      << sh << std::endl;
            delete impl_;
            impl_ = nullptr;
            return false;
        }
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
    // IN_VIDEO_MEMORY for the zero-copy path (frames arrive as device
    // surfaces); IN_SYSTEM_MEMORY for the host path (frames arrive as CPU
    // buffers and are memcpy'd into a runtime-allocated surface).
    par.IOPattern = impl_->zeroCopy
        ? MFX_IOPATTERN_IN_VIDEO_MEMORY
        : MFX_IOPATTERN_IN_SYSTEM_MEMORY;

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
    // Fetch the memory interface once for the zero-copy ImportFrameSurface
    // path. Only meaningful in zero-copy mode, but fetching unconditionally
    // would also be harmless; gate it to surface a clear error if the runtime
    // lacks GetHandle while zero-copy was requested.
    if (impl_->zeroCopy) {
        mfxHDL hdl = nullptr;
        sts = impl_->runtime.getHandle(impl_->session,
                                       MFX_HANDLE_MEMORY_INTERFACE, &hdl);
        if (sts != MFX_ERR_NONE || !hdl) {
            std::cerr << "QSVEncoder: GetHandle(MEMORY_INTERFACE) failed: "
                      << sts << " (zero-copy requires libvpl >= 2.10)"
                      << std::endl;
            delete impl_;
            impl_ = nullptr;
            return false;
        }
        impl_->memIface = static_cast<mfxMemoryInterface*>(hdl);
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
    // Populate the stream stats identity + Layer-2-lite (session-configured)
    // fields, then register so snapshotStreams() sees this stream. The worker
    // starts after this and begins mutating framesIn/framesOut/bytesOut.
    impl_->stats_.backend = "qsvenc";
    impl_->stats_.codec = params.codec;
    impl_->stats_.width = params.width;
    impl_->stats_.height = params.height;
    impl_->stats_.zeroCopy = impl_->zeroCopy;
    impl_->stats_.targetBitrateKbps = ec.bitrateKbps;
    impl_->stats_.frameRateNum = ec.frameRateNum;
    impl_->stats_.frameRateDen = ec.frameRateDen;
    // wallStartSecs = steady-clock seconds at register time. StreamStats::
    // wallSeconds() (in metrics.cc) computes nowSecs() - wallStartSecs using
    // the same steady_clock, so the two agree on an epoch. We set it directly
    // here rather than via a setter to keep StreamStats a plain data struct.
    {
        auto tp = std::chrono::steady_clock::now();
        impl_->stats_.wallStartSecs =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                tp.time_since_epoch()).count();
    }
    registerStreamStats(&impl_->stats_);
    impl_->startWorker();
    std::cout << "QSVEncoder: async session up, codec=" << params.codec << " "
              << params.width << "x" << params.height
              << (impl_->zeroCopy ? " (zero-copy)" : "") << std::endl;
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
