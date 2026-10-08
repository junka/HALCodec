#include "qsvdecoder.h"

#include <condition_variable>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "qsv_common.h"
#include "metrics.h"
#include "registry.h"

// ONEVPL_EXPERIMENTAL (defined via CMake) gates mfxFrameSurfaceInterface::Export
// and the mfxSurfaceVAAPI concrete struct. It must be defined before <vpl/mfx.h>
// is included (via qsv_common.h) so the Export vtable slot exists in the layout.
// The flag is a compile-time header gate only — the runtime exports the
// function regardless on libvpl >= 2.10.

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

// Downloads a FrameLocality::OneVPLSurface frame to host memory by importing
// the exported mfxSurfaceHeader* back into a session (as a DECODE-component
// surface — any component that supports read works) and Map'ing it. Used by
// DownloadToHost when a consumer needs CPU pixels (hal_dec writing .yuv). The
// device-frame encoder path does NOT call this — it ImportFrameSurface's into
// its own encode session instead.
//
// On success: f.data is a malloc'd tight NV12 buffer (release wired), f.size
// and f.strides set, locality flipped to Host, and the exported header is
// Released (the host buffer is now the sole owner of the pixels). Returns false
// on any failure (f left untouched).
bool DownloadVPLSurfaceToHost(const QSVRuntime& rt, mfxSession sess,
                              CodecFrame& f) {
    mfxHDL hdl = nullptr;
    mfxStatus sts = rt.getHandle(sess, MFX_HANDLE_MEMORY_INTERFACE, &hdl);
    if (sts != MFX_ERR_NONE || !hdl) {
        std::cerr << "QSVDecoder: GetHandle(MEMORY_INTERFACE) failed: "
                  << sts << "\n";
        return false;
    }
    mfxMemoryInterface* memIface = static_cast<mfxMemoryInterface*>(hdl);
    mfxSurfaceHeader* header =
        static_cast<mfxSurfaceHeader*>(f.device.vplExportedHeader);
    mfxFrameSurface1* imported = nullptr;
    // DECODE component gives read access; ENCODE/VPP_INPUT are the other
    // import-capable components. For a pure download, DECODE suffices.
    sts = memIface->ImportFrameSurface(memIface, MFX_SURFACE_COMPONENT_DECODE,
                                       header, &imported);
    if (sts != MFX_ERR_NONE || !imported) {
        std::cerr << "QSVDecoder: ImportFrameSurface failed: " << sts << "\n";
        return false;
    }
    mfxFrameSurfaceInterface* fi = imported->FrameInterface;
    sts = fi->Synchronize(imported, kSyncTimeoutMs);
    if (sts == MFX_WRN_IN_EXECUTION) {
        // retry once with a longer wait
        sts = fi->Synchronize(imported, 5000);
    }
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVDecoder: imported Synchronize failed: " << sts << "\n";
        fi->Release(imported);
        return false;
    }
    sts = fi->Map(imported, MFX_MAP_READ);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVDecoder: imported Map failed: " << sts << "\n";
        fi->Release(imported);
        return false;
    }
    uint8_t* host = CopyNV12(imported);
    fi->Unmap(imported);
    fi->Release(imported);
    if (!host) {
        return false;
    }
    // Flip to host. Release the exported header now that we have a private
    // host copy; the import above did not take ownership of the header's
    // refcount (ImportFrameSurface AddRef's internally and the imported
    // surface's Release balances that).
    mfxSurfaceInterface* siface =
        reinterpret_cast<mfxSurfaceInterface*>(header);
    siface->Release(siface);
    f.data = host;
    f.size = static_cast<size_t>(f.width) * f.height * 3 / 2;
    f.strides[0] = static_cast<size_t>(f.width);
    f.locality = FrameLocality::Host;
    f.device.vplExportedHeader = nullptr;
    uint8_t* owned = host;
    f.release = [owned]() { std::free(owned); };
    return true;
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
    // -z/--zero-copy: GetFrame() hands out the decoded surface's Exported
    // mfxSurfaceHeader* as a FrameLocality::OneVPLSurface frame (no Map/Copy
    // to host). The exported header is a runtime-owned, refcounted object; the
    // frame's release callback decrements it. Consumers either feed it to a
    // device-frame encoder (qsvenc) via ImportFrameSurface or call
    // DownloadToHost() on demand.
    bool zeroCopy = false;
    // The decode session's VADisplay (MFX_HANDLE_VA_DISPLAY), fetched once after
    // decodeInit. In zero-copy mode each OneVPLSurface frame carries this so the
    // encode session can SetHandle it before Init — required for the imported
    // surface's vaDisplay to match the encode session's own VADisplay (otherwise
    // ImportFrameSurface returns -4 on a VAAPI display mismatch).
    void* vaDisplay = nullptr;

    // Runtime observation counters (Layer 1). Registered into the process-wide
    // metrics registry at Initialize so snapshotStreams() can copy it out;
    // unregistered in ~Impl. The worker mutates counters under mu. The decoder
    // has no Layer-2 stat API in oneVPL, so only Layer-1 counters are filled.
    StreamStats stats_{};

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
        // Layer-1 input counter: compressed bytes fed into the decoder.
        stats_.bytesIn += size;
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
                // VIDEO_MEMORY keeps decoded frames in device memory (the QSV
                // zero-copy path); SYSTEM_MEMORY lets the runtime stage them
                // for CPU Map (the host path). The header is re-parsed into
                // the same par the worker later Init's with.
                par.IOPattern = zeroCopy
                    ? MFX_IOPATTERN_OUT_VIDEO_MEMORY
                    : MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
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
                    // Layer-1 identity: geometry is only known after the header
                    // is parsed. Fill it now so snapshotStreams() reports the
                    // real resolution instead of 0x0.
                    stats_.width = par.mfx.FrameInfo.CropW;
                    stats_.height = par.mfx.FrameInfo.CropH;
                    // Cache the decode session's VADisplay for the zero-copy
                    // path: the exported surface's vaDisplay is this handle,
                    // and the encode session must SetHandle the same value
                    // before its Init so import succeeds.
                    if (zeroCopy) {
                        mfxHDL vhdl = nullptr;
                        if (runtime.getHandle(session,
                                MFX_HANDLE_VA_DISPLAY, &vhdl) == MFX_ERR_NONE) {
                            vaDisplay = vhdl;
                        }
                    }
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
                    // Drain mode: pass the (now-empty) bitstream struct, not
                    // nullptr. oneVPL's DecodeFrameAsync treats a non-null
                    // bitstream with DataLength==0 as a drain query and will
                    // emit buffered reorder frames on successive calls;
                    // passing nullptr is not the documented drain contract and
                    // drops the tail.
                    bsPtr = &bs;
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
                    stats_.framesIn++;  // one frame submitted to hardware
                    stats_.pendingAsync = static_cast<int>(pending.size());
                    // Synchronize a batch when several are in flight.
                    if (pending.size() >= 4) {
                        drainPendingLocked();
                    }
                    break;
                }
                case MFX_ERR_MORE_DATA: {
                    std::unique_lock<std::mutex> lk(mu);
                    if (!eof) {
                        // Need more input; loop back to wait.
                        break;
                    }
                    // EOF: the decoder still holds frames in its reorder
                    // buffer. Drain by re-calling with a null bitstream until
                    // it stops emitting surfaces. A single MFX_ERR_MORE_DATA
                    // at EOF does NOT mean the decoder is empty — it means
                    // "no more input", and the next drain call can still
                    // return MFX_ERR_NONE + a delayed surface. The previous
                    // code exited here, dropping every frame still buffered
                    // past the last emitted one (h264 lost 2/15, hevc 4/15,
                    // av1 10/15, vp9 14/15 — the loss scales with reorder
                    // depth). Loop until a drain call returns MORE_DATA with
                    // no surface, which is the true end.
                    bool progressed = true;
                    int drainIters = 0;
                    // Mark the bitstream as end-of-stream so the decoder
                    // flushes its reorder buffer. Without MFX_BITSTREAM_EOS,
                    // libmfx-gen keeps the last N frames buffered waiting for
                    // more data that never comes (h264/hevc lost their tail).
                    // The flag is the documented drain signal.
                    bs.DataFlag |= MFX_BITSTREAM_EOS;
                    while (progressed) {
                        progressed = false;
                        mfxFrameSurface1* drain = nullptr;
                        mfxSyncPoint drainSync{};
                        // Release the lock for the async call; reacquire to
                        // push the surface. Pass the empty bitstream struct
                        // (DataLength==0, EOS flag set) as the drain query.
                        lk.unlock();
                        mfxStatus d = runtime.decodeFrameAsync(
                            session, &bs, nullptr, &drain, &drainSync);
                        lk.lock();
                        drainIters++;
                        if (d == MFX_ERR_NONE && drain) {
                            pending.push_back({drain, drainSync});
                            stats_.framesIn++;
                            progressed = true;
                        } else if (drain) {
                            // Returned an error but allocated a surface; let
                            // drainPendingLocked Release it.
                            pending.push_back({drain, drainSync});
                        }
                        if (pending.size() >= 4) {
                            drainPendingLocked();
                        }
                        if (drainIters > 64) break; // safety
                    }
                    drainPendingLocked();
                    finished = true;
                    cvFrames.notify_all();
                    goto out;
                }
                case MFX_ERR_MORE_SURFACE:
                case MFX_WRN_DEVICE_BUSY:
                    break;
                default:
                    if (eof && bs.DataLength == 0) {
                        // At EOF some codecs (VP9, MPEG2) return MFX_ERR_UNKNOWN
                        // from the final drain call rather than MFX_ERR_MORE_DATA.
                        // Treat that as end-of-stream: flush what's pending and
                        // stop, instead of poisoning the worker and dropping the
                        // tail frames that are still in the pipeline.
                        std::lock_guard<std::mutex> lk(mu);
                        drainPendingLocked();
                        finished = true;
                        cvFrames.notify_all();
                        goto out;
                    }
                    std::cerr << "QSVDecoder: DecodeFrameAsync error: " << sts << "\n";
                    std::lock_guard<std::mutex> lk(mu);
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
            if (zeroCopy) {
                if (!pushZeroCopyFrameLocked(p.surf, fi)) {
                    fi->Release(p.surf);
                } else {
                    // Layer-1 output counter: one device-memory frame emitted.
                    stats_.framesOut++;
                    stats_.localityDevice++;
                }
                // pushZeroCopyFrameLocked AddRef+Export'd the surface; the
                // Export produced an independent refcounted header, so we still
                // drop our reference on the original mfxFrameSurface1 here.
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
            // Wrap the malloc'd buffer in a shared_ptr so that copies of the
            // CodecFrame (e.g. the encoder's async input queue holding a copy
            // while the caller also holds one) don't double-free: each copy's
            // release decrements the shared count, and the last one frees.
            // Without this, hal_transcode's "release as soon as queued" path
            // raced the encoder worker and double-freed the buffer.
            auto owned = std::shared_ptr<uint8_t>(
                static_cast<uint8_t*>(frame.data), std::free);
            frame.release = [owned]() { /* freed when `owned` drops last ref */ };
            fi->Unmap(p.surf);
            fi->Release(p.surf);
            if (frame.data) {
                // Layer-1 output counter: one host-memory frame emitted, with
                // its raw NV12 byte size.
                stats_.framesOut++;
                stats_.localityHost++;
                stats_.bytesOut += frame.size;
                frames.push(std::move(frame));
            }
        }
        pending.clear();
        stats_.pendingAsync = 0;
        cvFrames.notify_all();
    }

    // Zero-copy drain: AddRef the decoded surface (keep it alive past the
    // runtime's internal reuse), Export it to an opaque refcounted
    // mfxSurfaceHeader*, then drop our AddRef — the exported header carries its
    // own reference. The frame's release callback Releases the exported header.
    // Returns true if a frame was pushed. Caller holds mu.
    bool pushZeroCopyFrameLocked(mfxFrameSurface1* surf,
                                 mfxFrameSurfaceInterface* fi) {
        // AddRef so Export (and any in-flight runtime reuse) can't free the
        // surface out from under the exported handle. Export itself does not
        // AddRef the source; it produces an independent refcounted object that
        // keeps the underlying resource alive.
        if (fi->AddRef(surf) != MFX_ERR_NONE) {
            return false;
        }
        // Export descriptor: ask for a shared (zero-copy) VA-API export. The
        // runtime fills SurfaceFlags with the actual mode on success — if it
        // could not share, it may have copied (EXPORT_COPY). Either way the
        // returned header is what the encode side imports.
        mfxSurfaceHeader req{};
        req.SurfaceType = MFX_SURFACE_TYPE_VAAPI;
        req.SurfaceFlags = MFX_SURFACE_FLAG_EXPORT_SHARED;
        mfxSurfaceHeader* exported = nullptr;
        mfxStatus sts = fi->Export(surf, req, &exported);
        // We AddRef'd solely to protect the Export; now that Export has
        // returned (success or failure), the exported header (if any) holds
        // its own ref, so drop ours on the source surface.
        fi->Release(surf);
        if (sts != MFX_ERR_NONE || !exported) {
            std::cerr << "QSVDecoder: surface Export failed: " << sts << "\n";
            return false;
        }
        CodecFrame frame;
        frame.width = surf->Info.CropW;
        frame.height = surf->Info.CropH;
        frame.format = PixelFormat::NV12;
        frame.size = 0;  // device-resident; host size unknown until downloaded
        frame.locality = FrameLocality::OneVPLSurface;
        frame.device.vplExportedHeader = exported;
        frame.device.vplVaDisplay = vaDisplay;
        // The exported mfxSurfaceHeader is actually the first member of an
        // mfxSurfaceInterface (Header), so its Release is reached via the
        // mfxSurfaceInterface vtable starting at the same pointer.
        //
        // The CodecFrame is copied across the decode->encode async boundary
        // (queued into the encoder's inFrames, then released by both the
        // encoder worker AND the transcode loop after FillFrame returns). To
        // survive that double release without a use-after-free on the
        // refcounted runtime object, wrap the Release in a shared_ptr: the
        // underlying mfxSurfaceInterface->Release fires exactly once, when the
        // last copy drops. Mirrors the host-path shared_ptr fix.
        mfxSurfaceInterface* siface =
            reinterpret_cast<mfxSurfaceInterface*>(exported);
        auto guard = std::shared_ptr<void>(nullptr, [siface](void*) {
            siface->Release(siface);
        });
        frame.release = [guard]() { /* Release on last drop */ };
        frames.push(std::move(frame));
        return true;
    }

    ~Impl() {
        // Unregister from the metrics registry before tearing down the session.
        unregisterStreamStats(&stats_);
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
    impl_->zeroCopy = params.zeroCopy;
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
    if (impl_->codecId == 0) {
        std::cerr << "QSVDecoder: unsupported codec '" << params.codec
                  << "' (qsvdec supports h264/hevc/av1/jpeg/vp9/mpeg2)"
                  << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->bs.MaxLength = kBitstreamBytes;
    impl_->bs.Data = static_cast<mfxU8*>(std::calloc(impl_->bs.MaxLength, 1));
    if (!impl_->bs.Data) {
        std::cerr << "QSVDecoder: bitstream alloc failed" << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->bs.CodecId = impl_->codecId;
    // In zero-copy mode, register a download hook so consumers that only
    // understand host memory (e.g. hal_dec writing a .yuv) can materialize the
    // pixels by ImportFrameSurface-ing the exported header back into this
    // decode session and Map'ing it. The hook checks FrameLocality::OneVPLSurface
    // and returns false otherwise, so it coexists with other backends' hooks.
    if (impl_->zeroCopy) {
        QSVRuntime* rt = &impl_->runtime;
        mfxSession sess = impl_->session;
        RegisterVPLSurfaceDownload([rt, sess](CodecFrame& f) -> bool {
            if (f.locality != FrameLocality::OneVPLSurface ||
                !f.device.vplExportedHeader) {
                return false;
            }
            return DownloadVPLSurfaceToHost(*rt, sess, f);
        });
    }
    // The worker lazily parses the header (DecodeHeader) once FillInput has
    // delivered enough data, then calls decodeInit and begins decoding.
    // Populate the stats identity we know at Initialize time (geometry is
    // filled by the worker after decodeHeader) and register so a snapshot
    // taken mid-stream sees this decoder.
    impl_->stats_.backend = "qsvdec";
    impl_->stats_.codec = params.codec;
    impl_->stats_.zeroCopy = impl_->zeroCopy;
    {
        auto tp = std::chrono::steady_clock::now();
        impl_->stats_.wallStartSecs =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                tp.time_since_epoch()).count();
    }
    registerStreamStats(&impl_->stats_);
    impl_->startWorker();
    std::cout << "QSVDecoder: async session up, codec=" << params.codec
              << (impl_->zeroCopy ? " (zero-copy)" : "") << std::endl;
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
