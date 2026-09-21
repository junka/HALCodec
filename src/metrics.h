#ifndef SRC_METRICS_H
#define SRC_METRICS_H

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "session.h"  // for HALCODEC_API

namespace halcodec {

// Per-stream runtime observation counters. Populated by each backend's worker
// in its existing submit/drain path (Layer 1: pure app-side counting — no
// hardware query, so every backend can supply at least this much) and
// optionally enriched with vendor session stats (Layer 2: mfxEncodeStat /
// nvEncGetEncodeStats, best-effort, -1 = unsupported).
//
// Backends register their StreamStats* into a process-wide registry at
// Initialize() and unregister at Finalize(); snapshotStreams() walks the
// registry. The registry lives in halcodec_core (the shared core lib) so a
// StreamStats registered by a dlopen'd backend .so is visible to the app.
struct StreamStats {
    // Identity (set once at Initialize).
    std::string backend;     // "qsvdec" / "qsvenc" / "nvenc" / ...
    std::string codec;       // "h264" / "hevc" / ...
    int width = 0;
    int height = 0;
    bool zeroCopy = false;

    // Layer 1 counters — updated by the backend worker under its own lock.
    // snapshotStreams() copies them out under the registry lock, so readers see
    // a consistent snapshot even though individual fields are not atomic.
    uint64_t framesIn = 0;       // frames/packets submitted (FillFrame/FillInput)
    uint64_t framesOut = 0;      // frames/packets emitted (GetFrame)
    uint64_t bytesIn = 0;        // compressed bytes fed in (decode) / raw in (encode)
    uint64_t bytesOut = 0;       // bytes emitted (encode bitstream / decode raw)
    uint64_t localityHost = 0;   // emitted frames that were host memory
    uint64_t localityDevice = 0; // emitted frames that were device memory
    int pendingAsync = 0;        // submissions in flight (encoder/decoder pipeline depth)

    // Session-configured (Layer 2-lite, from CodecParams at Initialize).
    int targetBitrateKbps = -1;  // -1 = unset
    int frameRateNum = -1;
    int frameRateDen = -1;

    // Vendor session stat (Layer 2, best-effort; -1 = unsupported / not queried).
    int avgQp = -1;
    int reportedBitrateKbps = -1;  // runtime-reported actual avg bitrate

    // Timing. wallStartSecs is set at Initialize() (steady clock, seconds since
    // epoch). wallSeconds() is the elapsed time since then; derived fps / bitrate
    // divide the counters by it.
    double wallStartSecs = 0.0;
    double wallSeconds() const;       // elapsed since wallStartSecs
    double fpsOut() const;            // framesOut / wallSeconds (0 if no time yet)
    double avgBitrateKbps() const;    // bytesOut*8/1000 / wallSeconds
};

// Whole-machine GPU utilization (Layer 3, best-effort). Sourced via dlopen
// (NVML on NVIDIA, sysfs on Intel/AMD) — never linked. Fields stay at -1 /
// empty when unsupported, so callers can always print "n/a".
struct GpuUtil {
    int gpuPercent = -1;            // -1 = unsupported
    int encPercent = -1;
    int decPercent = -1;
    int memPercent = -1;
    uint64_t memUsedBytes = 0;
    uint64_t memTotalBytes = 0;
    std::string vendor;             // "nvidia" / "intel" / "amd" / ""
    int deviceIndex = -1;           // which device ordinal was queried
};

// Snapshot all live streams across all backends. Returns one StreamStats per
// registered backend instance, copied out under the registry lock. Safe to
// call from any thread; the backends' workers keep updating their originals.
HALCODEC_API std::vector<StreamStats> snapshotStreams();

// Snapshot whole-machine GPU utilization for the given device ordinal.
// dlopens NVML (NVIDIA) or reads sysfs (Intel/AMD) best-effort; returns a
// GpuUtil with vendor="" / all -1 when nothing is available (e.g. the Intel
// iGPU on the dev box exposes no engine sysfs). Never throws.
HALCODEC_API GpuUtil snapshotGpu(int deviceIndex = 0);

// Human-readable dump of all streams + GPU util to the given stream. Used by
// the apps for periodic / final metrics printing. Unsupported fields print
// "n/a" so the layout stays stable.
HALCODEC_API void printStats(std::ostream& os);

// --- Backend registration (called by backend .so's, not by apps) ---
//
// Mirrors RegisterCudaFrameDownload's cross-.so export model: the backend
// registers a pointer to its own StreamStats (which it keeps alive for the
// lifetime of its Impl), and unregisters it at teardown. snapshotStreams()
// copies the pointed-to struct out, so the backend is free to keep mutating
// its original between snapshots.
HALCODEC_API void registerStreamStats(StreamStats* s);
HALCODEC_API void unregisterStreamStats(StreamStats* s);

} // namespace halcodec

#endif // SRC_METRICS_H
