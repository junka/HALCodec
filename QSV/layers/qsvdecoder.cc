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
#if defined(__linux__) && defined(QSV_HAS_VA)
#include "qsv_va_allocator.h"
#endif
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

    // Per-packet feed for VP8/VP9: the worker consumes exactly one IVF packet
    // per DecodeFrameAsync call (libmfx-gen's VP8/VP9 decoders reject bulk-fed
    // multi-packet bitstreams). cvDrained gates FillInput so the worker sees
    // one packet per iteration. Declared unconditionally (no platform guard)
    // because the worker logic references it; only the VA allocator that makes
    // VP8/VP9 actually decode is Linux+libva-gated.
    bool perPacketFeed = false;
    std::condition_variable cvDrained;
    mfxU64 packetSeq = 0;
    mfxU64 curPacketTs = 0;
    bool newPacketArrived = true;

#if defined(__linux__) && defined(QSV_HAS_VA)
    // VP8/VP9 decode VA-surface allocator (Linux+libva only). libmfx-gen's
    // VP8/VP9 P-frame reference handling rejects the new-API
    // GetSurfaceForDecode internal pool with -14/-16; it requires real VA
    // surfaces exposed via a registered mfxFrameAllocator (ffmpeg's
    // AVHWFramesContext recipe). Nullptr/!ready() on non-Linux, libva-less
    // builds, or init failure → the worker falls back to GetSurfaceForDecode
    // (VP8/VP9 stay blocked, no crash; other codecs never use this).
    std::unique_ptr<VaApiAllocator> vaAlloc;
    bool vaAllocReady = false;
#endif

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
        // If the previous packet was fully consumed (DataLength==0) the
        // decoder left DataOffset pointing past the consumed bytes. New data
        // is written at bs.Data+DataLength (=+0), so a stale nonzero
        // DataOffset would make compactLocked()'s memmove copy from the old
        // offset and overwrite the freshly-appended bytes — feeding garbage
        // (a stale keyframe tail) to the decoder on the next packet. Reset
        // DataOffset to 0 whenever the buffer is empty; there's no
        // unconsumed tail to preserve in that case.
        if (bs.DataLength == 0) {
            bs.DataOffset = 0;
        }
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
        // Mark a fresh packet for the per-packet-feed worker so it advances
        // the TimeStamp (a re-feed of the same packet must not).
        newPacketArrived = true;
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
#if defined(__linux__) && defined(QSV_HAS_VA)
                    // VP8/VP9: switch to the VA-surface path. libmfx-gen's
                    // VP8/VP9 P-frame reference handling rejects the
                    // GetSurfaceForDecode internal pool (-14/-16); it needs
                    // real VA surfaces via a registered mfxFrameAllocator
                    // (ffmpeg's AVHWFramesContext recipe). Re-Init with
                    // VIDEO_MEMORY + the VA allocator registered, so
                    // libmfx-gen takes the legacy surface path
                    // (IsExternalFrameAllocator()=true → m_redirect_to_vpl_path=false).
                    if (perPacketFeed) {
                        mfxHDL vhdl = nullptr;
                        if (runtime.getHandle(session,
                                MFX_HANDLE_VA_DISPLAY, &vhdl) == MFX_ERR_NONE
                                && vhdl) {
                            vaAlloc = std::make_unique<VaApiAllocator>();
                            if (vaAlloc->init()) {
                                mfxU16 aw = par.mfx.FrameInfo.Width;
                                mfxU16 ah = par.mfx.FrameInfo.Height;
                                if (vaAlloc->createPool(
                                        reinterpret_cast<VADisplay>(vhdl),
                                        aw, ah, 16, par.mfx.FrameInfo)
                                    && runtime.setFrameAllocator(
                                        session, vaAlloc->allocator())
                                        == MFX_ERR_NONE) {
                                    // Re-Init with VIDEO_MEMORY so decoded
                                    // frames land in VA surfaces the allocator
                                    // owns (not system memory).
                                    runtime.decodeTerminate(session);
                                    par.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
                                    sts = runtime.decodeInit(session, &par);
                                    if (sts == MFX_ERR_NONE) {
                                        vaAllocReady = true;
                                        std::cerr << "QSVDecoder: VP8/VP9 VA-surface "
                                                     "pool ready (" << aw << "x"
                                                  << ah << ", 16 surfaces)\n";
                                    } else {
                                        std::cerr << "QSVDecoder: VA re-Init "
                                                     "failed: " << sts << "\n";
                                    }
                                }
                            }
                        }
                        if (!vaAllocReady) {
                            std::cerr << "QSVDecoder: VA allocator unavailable — "
                                         "VP8/VP9 P-frames will not decode\n";
                            vaAlloc.reset();
                        }
                    }
#endif
                    cvFrames.notify_all();
                }
            }
        }

        // Phase 2: decode loop.
        // eosSignaled is left uninitialized (then assigned) rather than
        // initialized in-line: the switch cases below goto out, and a C++
        // goto cannot cross a non-trivial initialization. A bool is trivially
        // destructible, but the rule still flags `bool x = false;` — using a
        // plain declaration + later assignment keeps the goto legal.
        bool eosSignaled;
        eosSignaled = false;
        while (true) {
            mfxBitstream* bsPtr = nullptr;
            // This lock spans the bitstream setup AND the DecodeFrameAsync
            // call. FillInput() appends into bs (and may realloc bs.Data) under
            // the same lock; if we released it before calling DecodeFrameAsync,
            // FillInput could realloc the buffer out from under the decoder
            // (bsPtr points at bs) or mutate bs.DataLength mid-parse. That race
            // nondeterministically dropped frames on multi-chunk inputs: a
            // 305131-byte stream fed in 256 KiB chunks lost 4/30 frames
            // depending on whether the second chunk's append landed during a
            // decode call. DecodeFrameAsync is asynchronous (it submits and
            // returns, synchronizing later via drainPendingLocked), so holding
            // the lock across it only blocks FillInput for the submit duration.
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
            // Signal end-of-stream to the decoder exactly once, the moment
            // eof is observed. The flag must be on the bitstream for BOTH
            // the final data-carrying call (so the decoder knows no more
            // input follows the bytes it has) and the empty drain calls.
            // Setting it only inside the MORE_DATA branch left a window
            // where the last data-carrying DecodeFrameAsync ran without
            // EOS, and the decoder buffered reorder frames it then
            // nondeterministically refused to flush.
            if (eof && !eosSignaled) {
                bs.DataFlag |= MFX_BITSTREAM_EOS;
                eosSignaled = true;
            }
#if defined(__linux__) && defined(QSV_HAS_VA)
            // VP8/VP9 (perPacketFeed): present the bitstream as exactly one
            // complete frame, the way ffmpeg's qsv_decode does (a fresh
            // mfxBitstream per avpkt: DataLength=MaxLength=size, COMPLETE_FRAME).
            // With the shared 4 MiB buffer libmfx-gen's VP8/VP9 decoders read
            // past the packet boundary and reject even the keyframe.
            if (perPacketFeed && bs.DataLength > 0) {
                bs.MaxLength = bs.DataLength;
                bs.DataFlag |= MFX_BITSTREAM_COMPLETE_FRAME;
                if (newPacketArrived) {
                    curPacketTs = packetSeq++;
                    newPacketArrived = false;
                }
                bs.TimeStamp = curPacketTs;
            }
#endif

            mfxFrameSurface1* surfOut = nullptr;
            mfxSyncPoint syncp{};
            // Provision an output surface via MFXMemory_GetSurfaceForDecode
            // (the oneVPL 2.x recommended decode pattern) rather than passing
            // nullptr. With nullptr the decoder draws from its internal pool,
            // which is sized to AsyncDepth + reorder depth; at end-of-stream
            // that pool can be exhausted and the decoder stops emitting —
            // dropping the last reorder frames (High-profile h264 lost 4/30
            // here even with EOS + empty-bs drain, while ffmpeg's h264_qsv,
            // which provisions surfaces, decoded all 30). Handing the decoder
            // an explicit free surface per call gives it a buffer to write
            // every drain frame into.
            mfxFrameSurface1* workSurf = nullptr;
#if defined(__linux__) && defined(QSV_HAS_VA)
            // VP8/VP9 VA path: hand the decoder a VA-surface from our
            // registered allocator's pool (real VA surface via Data.MemId,
            // no FrameInterface — libmfx-gen takes the legacy path because
            // the allocator is registered). GetSurfaceForDecode would give
            // an internal-pool surface that libmfx-gen's VP8/VP9 P-frame
            // reference handling rejects (-14/-16).
            mfxFrameSurface1* vaWork = nullptr;
            if (vaAllocReady) {
                vaWork = vaAlloc->acquireSurface();
            }
            mfxFrameSurface1* surfArg = vaWork;
            if (!surfArg) {
                mfxStatus gs = runtime.getSurfaceForDecode(session, &workSurf);
                surfArg = (gs == MFX_ERR_NONE && workSurf) ? workSurf : nullptr;
            }
#else
            mfxStatus gs = runtime.getSurfaceForDecode(session, &workSurf);
            mfxFrameSurface1* surfArg = (gs == MFX_ERR_NONE && workSurf)
                ? workSurf : nullptr;
#endif
            mfxStatus sts = runtime.decodeFrameAsync(session, bsPtr, surfArg,
                                                    &surfOut, &syncp);
            // If GetSurface gave us a surface but DecodeFrameAsync did not take
            // ownership of it (returned no output surface or an error), release
            // our reference so the pool reclaims it.
            if (workSurf && surfOut != workSurf) {
                if (workSurf->FrameInterface) {
                    workSurf->FrameInterface->Release(workSurf);
                }
            }
#if defined(__linux__) && defined(QSV_HAS_VA)
            // VA path: if the decoder did NOT take the work surface (no output
            // surface returned, or an error/non-NONE status), return it to the
            // pool so the next iteration can reuse it. VP9's per-packet flow
            // calls DecodeFrameAsync twice per frame: first returns
            // MFX_ERR_MORE_SURFACE (surfOut=null, work surface unused — must be
            // recycled), then returns MFX_ERR_NONE (surfOut==work, drained and
            // recycled in drainPendingLockedKeepAlive). Without recycling on the
            // MORE_SURFACE call every frame would leak one surface and exhaust
            // the 16-surface pool within ~16 frames. VA surfaces have no
            // FrameInterface, so we recycle via the allocator (not Release).
            if (vaWork && (sts != MFX_ERR_NONE || surfOut != vaWork)) {
                vaAlloc->releaseSurface(vaWork);
            }
            (void)workSurf;  // unused on the VA path (no GetSurfaceForDecode)
#endif
            switch (sts) {
                case MFX_ERR_NONE:
                    // Track the surface for batched synchronization. lk is held
                    // (spanning the decode call above).
                    pending.push_back({surfOut, syncp});
                    stats_.framesIn++;  // one frame submitted to hardware
                    stats_.pendingAsync = static_cast<int>(pending.size());
#if defined(__linux__) && defined(QSV_HAS_VA)
                    // VP8/VP9 per-packet path: sync+copy this frame to host
                    // immediately before pulling the next packet (the VA
                    // surface is then free to be reused as a reference for
                    // the next P-frame — but we keep N alive; see
                    // drainPendingLockedKeepAlive).
                    if (perPacketFeed) {
                        drainPendingLockedKeepAlive();
                    } else if (pending.size() >= 4) {
                        drainPendingLocked();
                    }
#else
                    // Synchronize a batch when several are in flight.
                    if (pending.size() >= 4) {
                        drainPendingLocked();
                    }
#endif
                    break;
                case MFX_ERR_MORE_DATA:
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
                    {
                    int drainIters = 0;
                    int emptyInARow = 0;
                    // MFX_BITSTREAM_EOS was set on bs the moment eof was
                    // observed (top of the phase-2 loop), so these drain calls
                    // carry the flag without re-setting it. Loop until two
                    // consecutive surface-less calls: libmfx-gen returns
                    // MFX_ERR_MORE_DATA between delayed reorder frames, so a
                    // single empty call is not the true end.
                    while (emptyInARow < 2 && drainIters < 64) {
                        mfxFrameSurface1* drain = nullptr;
                        mfxSyncPoint drainSync{};
                        // Release the lock for the async call; reacquire to
                        // push the surface. Pass the empty bitstream struct
                        // (DataLength==0, EOS flag set) as the drain query.
                        lk.unlock();
                        // Provision an output surface for the drain call too:
                        // the decoder needs a free surface to write each
                        // reorder-buffered frame into, and the internal pool
                        // may be exhausted by the time we drain.
                        mfxFrameSurface1* drainWork = nullptr;
                        mfxStatus gsd = runtime.getSurfaceForDecode(
                            session, &drainWork);
                        mfxFrameSurface1* drainArg = (gsd == MFX_ERR_NONE
                            && drainWork) ? drainWork : nullptr;
                        mfxStatus d = runtime.decodeFrameAsync(
                            session, &bs, drainArg, &drain, &drainSync);
                        if (drainWork && drain != drainWork) {
                            if (drainWork->FrameInterface) {
                                drainWork->FrameInterface->Release(drainWork);
                            }
                        }
                        lk.lock();
                        drainIters++;
                        if (d == MFX_ERR_NONE && drain) {
                            pending.push_back({drain, drainSync});
                            stats_.framesIn++;
                            emptyInARow = 0;
                        } else if (drain) {
                            // Returned an error but allocated a surface; let
                            // drainPendingLocked Release it.
                            pending.push_back({drain, drainSync});
                            emptyInARow = 0;
                        } else {
                            // No surface this call. The decoder can still emit
                            // on a later drain call (it returns MORE_DATA
                            // between delayed frames), so only stop after two
                            // consecutive empty calls.
                            emptyInARow++;
                        }
                        if (pending.size() >= 4) {
                            drainPendingLocked();
                        }
                    }
                    drainPendingLocked();
                    finished = true;
                    cvFrames.notify_all();
                    goto out;
                    }
                case MFX_ERR_MORE_SURFACE:
                    // The decoder consumed the input it could and has decoded
                    // frames ready, but needs another output surface to write
                    // the next one. surfOut is null on this status. We simply
                    // loop back: the next iteration re-provisions a surface via
                    // GetSurfaceForDecode and re-calls DecodeFrameAsync with the
                    // SAME bitstream (bs still holds any unconsumed bytes), so
                    // the decoder retrieves the ready frame and continues
                    // consuming. Do NOT call with a null bitstream here to
                    // "drain ready frames": bs may still hold unconsumed data,
                    // and a null bitstream signals "no more input", which made
                    // the decoder flush prematurely on chunked inputs and drop
                    // tail frames (h264 lost 4/30 when chunk 2 had not yet
                    // arrived).
                    break;
                case MFX_WRN_DEVICE_BUSY:
                    break;
                default:
                    if (eof && bs.DataLength == 0) {
                        // At EOF some codecs (VP9, MPEG2) return MFX_ERR_UNKNOWN
                        // from the final drain call rather than MFX_ERR_MORE_DATA.
                        // Treat that as end-of-stream: flush what's pending and
                        // stop, instead of poisoning the worker and dropping the
                        // tail frames that are still in the pipeline.
                        drainPendingLocked();
                        finished = true;
                        cvFrames.notify_all();
                        goto out;
                    }
                    std::cerr << "QSVDecoder: DecodeFrameAsync error: " << sts << "\n";
                    workerError = true;
                    cvFrames.notify_all();
                    goto out;
            }
#if defined(__linux__) && defined(QSV_HAS_VA)
            // VP8/VP9 per-packet feed: unblock FillInput once the worker has
            // consumed the current packet (bs.DataLength dropped to 0 — the
            // decoder ate the bytes). If the packet was NOT consumed (MORE_DATA
            // with bytes remaining, MORE_SURFACE, DEVICE_BUSY), keep FillInput
            // blocked so the same packet is re-fed next iteration. On a fatal
            // error, notify so FillInput does not deadlock on a dead packet.
            if (perPacketFeed) {
                if (bs.DataLength == 0
                        || (sts != MFX_ERR_NONE && sts != MFX_ERR_MORE_DATA
                            && sts != MFX_ERR_MORE_SURFACE
                            && sts != MFX_WRN_DEVICE_BUSY)) {
                    cvDrained.notify_one();
                }
            }
#endif
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

#if defined(__linux__) && defined(QSV_HAS_VA)
    // Per-packet-feed variant for the VP8/VP9 VA path. VA surfaces have no
    // mfxFrameSurfaceInterface (they are MemId-only, tracked via the
    // registered allocator's GetHDL), so drainPendingLocked's fi->Synchronize/
    // Map path does not apply. Instead: sync via vaAlloc->copySurfaceToHost
    // (vaSyncSurface + vaDeriveImage + vaMapBuffer), push the host frame, and
    // KEEP the VA surface alive (do not return it to the pool) so libmfx-gen's
    // P-frame reference stays resident. Surfaces are reclaimed when the pool
    // is destroyed at teardown. Caller holds mu.
    void drainPendingLockedKeepAlive() {
        for (auto& p : pending) {
            if (!p.surf) continue;
            // The surface's Data.MemId is our mfxHDLPair*; .first is the
            // VASurfaceID*. Recover the id to sync+copy via libva.
            mfxHDLPair* pair = static_cast<mfxHDLPair*>(
                p.surf->Data.MemId);
            if (!pair || !pair->first) continue;
            VASurfaceID sid = *static_cast<VASurfaceID*>(pair->first);
            int w = p.surf->Info.CropW;
            int h = p.surf->Info.CropH;
            int stride = 0;
            uint8_t* px = vaAlloc->copySurfaceToHost(sid, w, h, &stride);
            if (!px) continue;
            CodecFrame frame;
            frame.width = w;
            frame.height = h;
            frame.format = PixelFormat::NV12;
            frame.size = static_cast<size_t>(w) * h * 3 / 2;
            frame.strides[0] = static_cast<size_t>(w);
            frame.data = px;
            auto owned = std::shared_ptr<uint8_t>(
                static_cast<uint8_t*>(frame.data), std::free);
            frame.release = [owned]() { /* freed when `owned` drops last ref */ };
            stats_.framesOut++;
            stats_.localityHost++;
            stats_.bytesOut += frame.size;
            frames.push(std::move(frame));
            // Recycle the VA surface back to the pool. The decoder tracks the
            // reference-frame lifetime internally (it IncreaseReference's the
            // VASurfaceID on submit and DecreaseReference's when a frame drops
            // out of the reference window — both go through the allocator's
            // GetHDL, not our inUse flag). Our inUse bookkeeping only guards
            // against handing the same slot to two concurrent DecodeFrameAsync
            // calls, so once this frame is copied to host the slot is free to
            // reuse. This keeps the 16-surface pool from exhausting on streams
            // longer than the pool (VP9 25-frame, VP8 101-frame).
            vaAlloc->releaseSurface(p.surf);
        }
        pending.clear();
        stats_.pendingAsync = 0;
        cvFrames.notify_all();
    }
#endif

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
#if defined(__linux__) && defined(QSV_HAS_VA)
        // Destroy the VA surface pool after the session is terminated (the
        // surfaces were backed by the session's VADisplay; safe to release
        // once the decoder no longer references them). unique_ptr dtor calls
        // VaApiAllocator::destroy (vaDestroySurfaces + dlclose).
        vaAlloc.reset();
#endif
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
                  << "' (qsvdec supports h264/hevc/av1/jpeg/vp9/vp8/vc1/mpeg2)"
                  << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    // VP8/VP9 decode uses per-packet feed (one IVF packet per
    // DecodeFrameAsync) — libmfx-gen's VP8/VP9 decoders reject bulk-fed
    // multi-packet bitstreams. The VA-surface pool that unblocks P-frame
    // decode is built later in the worker after decodeInit (Linux+libva).
    impl_->perPacketFeed = (impl_->codecId == MFX_CODEC_VP9
                            || impl_->codecId == MFX_CODEC_VP8);
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
        std::unique_lock<std::mutex> lk(impl_->mu);
        // Per-packet feed (VP8/VP9): if the worker still has an unconsumed
        // packet in bs, wait for it to drain before appending the next, so the
        // worker sees exactly one packet per DecodeFrameAsync call. The worker
        // signals cvDrained once the packet is consumed (bs.DataLength==0) or
        // a fatal error is set.
        if (impl_->perPacketFeed) {
            impl_->cvDrained.wait(lk, [this] {
                return impl_->bs.DataLength == 0 || impl_->workerError
                       || impl_->finished;
            });
            if (impl_->workerError || impl_->finished) {
                return 0;
            }
        }
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
