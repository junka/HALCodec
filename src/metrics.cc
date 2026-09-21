#include "metrics.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>

namespace halcodec {

namespace {

// Format a double to a fixed precision without pulling in <iomanip> (which
// would bloat the ostream formatting). Returns a small string like "29.7".
std::string formatFixed(double v, int prec) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return std::string(buf);
}

// Steady-clock seconds since epoch. Used as the StreamStats wallStartSecs so
// wallSeconds() is a simple elapsed-time subtraction independent of when the
// metrics code was loaded.
double nowSecs() {
    auto tp = std::chrono::steady_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::duration<double>>(tp.time_since_epoch());
    return secs.count();
}

// Process-wide registry of live stream stats. A std::vector<StreamStats*> is
// fine: backends register/unregister infrequently (Initialize/Finalize) and
// snapshotStreams() is called at most a few times per second. The mutex keeps
// registration and snapshot consistent across threads / .so boundaries.
std::mutex& registryMu() {
    static std::mutex m;
    return m;
}
std::vector<StreamStats*>& registry() {
    static std::vector<StreamStats*> v;
    return v;
}

// --- NVML dlopen (Layer 3, NVIDIA) ---
//
// libnvidia-ml.so.1 ships with the driver (nvidia-smi links it). We dlopen it
// lazily on first snapshotGpu() and cache the handle + the function pointers
// we need. If the dlopen fails (no NVIDIA driver, or an Intel/AMD box), every
// field stays at -1 and vendor stays "". We never link nvml.
struct Nvml {
    void* handle = nullptr;
    int (*init)(void) = nullptr;                       // nvmlInit_v2
    int (*shutdown)(void) = nullptr;                   // nvmlShutdown
    int (*getHandleByIndex)(int, void**) = nullptr;    // nvmlDeviceGetHandleByIndex
    int (*getUtilizationRates)(void*, void*) = nullptr;// nvmlDeviceGetUtilizationRates
    int (*getMemoryInfo)(void*, void*) = nullptr;      // nvmlDeviceGetMemoryInfo
    int (*getEncoderUtil)(void*, unsigned int*, unsigned int*) = nullptr;
    int (*getDecoderUtil)(void*, unsigned int*, unsigned int*) = nullptr;
    bool tried = false;  // dlopen attempted (success or fail)
    bool ok = false;     // dlopen + required symbols resolved

    void load() {
        if (tried) return;
        tried = true;
        // RTLD_LAZY: symbols we don't use (most of NVML) never resolve.
        handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!handle) {
            handle = dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (!handle) return;
        // Resolve each NVML export by name. Field names are our own; the dlsym
        // strings must match the NVML symbol names exactly.
        init = reinterpret_cast<int(*)(void)>(dlsym(handle, "nvmlInit_v2"));
        shutdown = reinterpret_cast<int(*)(void)>(dlsym(handle, "nvmlShutdown"));
        getHandleByIndex =
            reinterpret_cast<int(*)(int, void**)>(dlsym(handle, "nvmlDeviceGetHandleByIndex"));
        getUtilizationRates =
            reinterpret_cast<int(*)(void*, void*)>(dlsym(handle, "nvmlDeviceGetUtilizationRates"));
        getMemoryInfo =
            reinterpret_cast<int(*)(void*, void*)>(dlsym(handle, "nvmlDeviceGetMemoryInfo"));
        getEncoderUtil =
            reinterpret_cast<int(*)(void*, unsigned int*, unsigned int*)>(
                dlsym(handle, "nvmlDeviceGetEncoderUtilization"));
        getDecoderUtil =
            reinterpret_cast<int(*)(void*, unsigned int*, unsigned int*)>(
                dlsym(handle, "nvmlDeviceGetDecoderUtilization"));
        if (!init || !getHandleByIndex || !getUtilizationRates || !getMemoryInfo) {
            return;
        }
        if (init() != 0) {  // nvmlInit_v2
            return;
        }
        ok = true;
    }
};

Nvml& nvml() {
    static Nvml n;
    return n;
}

// nvmlMemory_t: we only touch the first three fields (total/free/used), which
// have been stable across NVML versions. Reading into a minimal struct avoids
// pulling in nvml.h (and its version-sensitive layout).
struct NvmlMemLite {
    unsigned long long total = 0;
    unsigned long long free = 0;
    unsigned long long used = 0;
};
struct NvmlUtilLite {
    unsigned int gpu = 0;
    unsigned int memory = 0;
};

// Read an integer from a sysfs path. Returns -1 on any error (file missing,
// non-numeric). Used for the Intel/AMD best-effort paths.
int readSysfsInt(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    int n = std::fscanf(f, "%d", &v);
    std::fclose(f);
    return n == 1 ? v : -1;
}

// Best-effort Intel iGPU utilization via sysfs. The i915 driver exposes
// gpu_busy_percent on some kernels; rps_cur_freq_mhz + rc6_residency_ms are a
// frequency/energy proxy on others. On the dev box neither is present, so this
// returns a GpuUtil with vendor="intel" and the fields it could read (often
// all -1). The point is to leave the hook in place for target machines that do
// expose sysfs.
GpuUtil snapshotIntel(int deviceIndex) {
    GpuUtil g;
    g.vendor = "intel";
    g.deviceIndex = deviceIndex;
    // gpu_busy_percent exists on AMD too; on Intel the equivalent is under
    // gt/gtN/. Try the simple path first.
    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/class/drm/card%d/device/gpu_busy_percent", deviceIndex);
    int busy = readSysfsInt(path);
    if (busy >= 0) g.gpuPercent = busy;
    // i915 per-engine freq / RC6 (energy proxy, not true util).
    std::snprintf(path, sizeof(path),
                  "/sys/class/drm/card%d/gt/gt0/rps_cur_freq_mhz", deviceIndex);
    int freq = readSysfsInt(path);
    (void)freq;  // exposed as a separate field only if we add one; keep for now
    return g;
}

// Best-effort AMD GPU utilization via sysfs (amdgpu exposes gpu_busy_percent
// and mem info under /sys/class/drm/cardN/device/).
GpuUtil snapshotAmd(int deviceIndex) {
    GpuUtil g;
    g.vendor = "amd";
    g.deviceIndex = deviceIndex;
    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/class/drm/card%d/device/gpu_busy_percent", deviceIndex);
    int busy = readSysfsInt(path);
    if (busy >= 0) g.gpuPercent = busy;
    std::snprintf(path, sizeof(path),
                  "/sys/class/drm/card%d/device/mem_info_percent", deviceIndex);
    int mem = readSysfsInt(path);
    if (mem >= 0) g.memPercent = mem;
    return g;
}

} // namespace

double StreamStats::wallSeconds() const {
    return nowSecs() - wallStartSecs;
}

double StreamStats::fpsOut() const {
    double s = wallSeconds();
    if (s <= 0.0) return 0.0;
    return static_cast<double>(framesOut) / s;
}

double StreamStats::avgBitrateKbps() const {
    double s = wallSeconds();
    if (s <= 0.0) return 0.0;
    // bytes * 8 / 1000 = kbits; / seconds = kbps.
    return (static_cast<double>(bytesOut) * 8.0 / 1000.0) / s;
}

HALCODEC_API void registerStreamStats(StreamStats* s) {
    if (!s) return;
    std::lock_guard<std::mutex> lk(registryMu());
    registry().push_back(s);
}

HALCODEC_API void unregisterStreamStats(StreamStats* s) {
    if (!s) return;
    std::lock_guard<std::mutex> lk(registryMu());
    auto& v = registry();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == s) {
            v[i] = v.back();
            v.pop_back();
            return;
        }
    }
}

HALCODEC_API std::vector<StreamStats> snapshotStreams() {
    std::lock_guard<std::mutex> lk(registryMu());
    std::vector<StreamStats> out;
    out.reserve(registry().size());
    for (StreamStats* s : registry()) {
        if (s) out.push_back(*s);  // copy the live struct out
    }
    return out;
}

HALCODEC_API GpuUtil snapshotGpu(int deviceIndex) {
    // Try NVIDIA first (dlopen). If NVML initializes and the device handle is
    // obtainable, report NVIDIA utilization. Otherwise fall back to sysfs
    // (Intel/AMD). The dev box has no i915 engine sysfs, so Intel returns -1.
    Nvml& n = nvml();
    n.load();
    if (n.ok) {
        void* dev = nullptr;
        if (n.getHandleByIndex(deviceIndex, &dev) == 0 && dev) {
            GpuUtil g;
            g.vendor = "nvidia";
            g.deviceIndex = deviceIndex;
            NvmlUtilLite util{};
            if (n.getUtilizationRates(dev, &util) == 0) {
                g.gpuPercent = static_cast<int>(util.gpu);
                g.memPercent = static_cast<int>(util.memory);
            }
            NvmlMemLite mem{};
            if (n.getMemoryInfo(dev, &mem) == 0) {
                g.memUsedBytes = mem.used;
                g.memTotalBytes = mem.total;
                if (g.memPercent < 0 && mem.total > 0) {
                    g.memPercent = static_cast<int>(
                        (static_cast<unsigned long long>(mem.used) * 100) / mem.total);
                }
            }
            unsigned int encUtil = 0, encSampling = 0;
            if (n.getEncoderUtil && n.getEncoderUtil(dev, &encUtil, &encSampling) == 0) {
                g.encPercent = static_cast<int>(encUtil);
            }
            unsigned int decUtil = 0, decSampling = 0;
            if (n.getDecoderUtil && n.getDecoderUtil(dev, &decUtil, &decSampling) == 0) {
                g.decPercent = static_cast<int>(decUtil);
            }
            return g;
        }
    }
    // No NVIDIA device at this index. Try sysfs paths: amdgpu exposes
    // gpu_busy_percent under device/, i915 under gt/. Probe both.
    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/class/drm/card%d/device/gpu_busy_percent", deviceIndex);
    if (readSysfsInt(path) >= 0) {
        // amdgpu also has mem_info_percent; i915 does not. Distinguish by the
        // presence of an amd-specific sysfs entry.
        char amdpath[256];
        std::snprintf(amdpath, sizeof(amdpath),
                      "/sys/class/drm/card%d/device/mem_info_percent", deviceIndex);
        if (readSysfsInt(amdpath) >= 0) {
            return snapshotAmd(deviceIndex);
        }
        return snapshotIntel(deviceIndex);
    }
    // Nothing found.
    GpuUtil g;
    g.deviceIndex = deviceIndex;
    return g;
}

HALCODEC_API void printStats(std::ostream& os) {
    auto streams = snapshotStreams();
    if (streams.empty()) {
        os << "[metrics] no live streams\n";
    } else {
        for (const auto& s : streams) {
            os << "[stream] " << s.backend << " " << s.codec
               << " " << s.width << "x" << s.height
               << (s.zeroCopy ? " zc" : "")
               << " | in:" << s.framesIn << " out:" << s.framesOut
               << " host:" << s.localityHost << " dev:" << s.localityDevice
               << " pend:" << s.pendingAsync
               << " | fps:" << (s.framesOut ? formatFixed(s.fpsOut(), 1) : std::string("n/a"));
            if (s.bytesOut > 0) {
                os << " br:" << formatFixed(s.avgBitrateKbps(), 0) << "kbps";
            } else {
                os << " br:n/a";
            }
            if (s.targetBitrateKbps >= 0) {
                os << " tgt:" << s.targetBitrateKbps << "kbps";
            }
            if (s.avgQp >= 0)        os << " qp:" << s.avgQp;
            if (s.reportedBitrateKbps >= 0) os << " rbr:" << s.reportedBitrateKbps;
            os << "\n";
        }
    }
    // GPU util across the first few device ordinals (most setups have 1 GPU).
    for (int d = 0; d < 1; ++d) {
        GpuUtil g = snapshotGpu(d);
        if (g.vendor.empty()) {
            os << "[gpu " << d << "] unsupported\n";
            continue;
        }
        os << "[gpu " << d << "] " << g.vendor
           << " gpu:" << (g.gpuPercent >= 0 ? std::to_string(g.gpuPercent) : std::string("n/a")) << "%"
           << " enc:" << (g.encPercent >= 0 ? std::to_string(g.encPercent) : std::string("n/a")) << "%"
           << " dec:" << (g.decPercent >= 0 ? std::to_string(g.decPercent) : std::string("n/a")) << "%"
           << " mem:" << (g.memPercent >= 0 ? std::to_string(g.memPercent) : std::string("n/a")) << "%";
        if (g.memTotalBytes > 0) {
            os << " (" << (g.memUsedBytes >> 20) << "/" << (g.memTotalBytes >> 20) << "MiB)";
        }
        os << "\n";
    }
}

} // namespace halcodec
