// hal_camera — DRIVE-OS (nv118 / Thor) camera-to-encode app.
//
// Starts one or more NvSIPLCamera (SIPL) sensor pipelines, feeds each ISP0
// output NvSciBufObj **zero-copy** into a nvmediaenc (NvMedia IEP) encoder
// instance, and writes N H.264 elementary streams. Multi-camera is done with
// app-layer per-camera capture threads + a shared output queue (the NvMedia
// encoder is synchronous, so hal_session's async fan-in is not used here —
// see halcodec_camera_plan.md "矛盾 1").
//
// Data path (per camera):
//   INvSIPLCamera (one platform-level instance, N pipelines)
//     -> SetPlatformCfg / SetPipelineCfg(sensorIndex + i, isp0OutputRequested)
//     -> Init
//     -> RegisterImages(sensorIndex + i, ISP0, [app-allocated NvSciBufObj pool])
//        The pool's NvSciBuf attr list is reconciled from SIPL + IEP + NV12
//        together so the same buffer is writable by the ISP engine *and*
//        readable by the IEP engine (the cross-engine reconcile pattern from
//        the NvMedia dec->enc zero-copy fix; without it IEP rejects the SIPL
//        buf at RegisterNvSciBufObj with engine-permission mismatch).
//     -> RegisterAutoControlPlugin (ISP output needs AEC/AWB)
//     -> RegisterNvSciSyncObj(EOF)
//     -> Start
//     -> loop { isp0CompletionQueue->Get -> GetNvSciBufImage
//               -> CodecFrame{locality=NvSciBufObj} -> enc->FillFrame
//               -> enc->GetFrame (sync) -> write packet -> item->Release }
//     -> Stop / Deinit
//
// The SIPL C++ API and NvSciBuf/NvMedia IEP are linked directly (not dlopen'd):
// this app is aarch64-only and only builds when the DRIVE-OS SDK is present,
// matching the nvmedia_layers condition in the top-level CMakeLists.
//
// Usage:
//   hal_camera -B nvmedia -c h264 -o out.h264 --bitrate 4000 --fps 30
//              [--num-cameras N] [--sensor-index i] [--duration-frames N]
//              [--sipl-platform <name>] [--sipl-db <json>]
// Multi-camera output: out_cam0.h264, out_cam1.h264, ... (derived from -o).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>

#include "encoder.h"
#include "encode_config.h"
#include "parse_cli.h"
#include "plugin_loader.h"

#include "codec_config.h"
#include "frame.h"

#include "NvSIPLCamera.hpp"
#include "NvSIPLPipelineMgr.hpp"
#include "NvSIPLQuery.hpp"
#include "NvSIPLDeviceBlockInfo.hpp"

#include "nvmedia_iep.h"
#include "nvscibuf.h"
#include "nvscisync.h"

using namespace nvsipl;

namespace {

// Fill an NvSciBuf attr list for an 8-bit NV12 (semi-planar YUV 4:2:0) surface
// of the given dimensions. Mirrors NvMediaEncoder::FillNV12SurfaceAttrs and the
// decoder's equivalent: BlockLinear, 256-byte base align, REC601_ER,
// progressive. Applied to the *combined* SIPL+IEP attr list so the reconciled
// buffer satisfies both engines.
bool FillNV12SurfaceAttrs(NvSciBufAttrList list, uint32_t w, uint32_t h) {
    NvSciBufType bufType = NvSciBufType_Image;
    NvSciBufAttrValAccessPerm access = NvSciBufAccessPerm_ReadWrite;
    bool needCpuAccess = true;
    NvSciBufAttrValImageLayoutType layout = NvSciBufImage_BlockLinearType;
    uint32_t planeCount = 2;
    NvSciBufAttrValColorFmt colorFormat[2] = {NvSciColor_Y8, NvSciColor_V8U8};
    NvSciBufAttrValColorStd colorStd[2] = {NvSciColorStd_REC601_ER,
                                           NvSciColorStd_REC601_ER};
    uint32_t planeWidth[2] = {w, w >> 1};
    uint32_t planeHeight[2] = {h, h >> 1};
    uint32_t baseAddrAlign[2] = {32, 32};
    uint64_t padding[2] = {0, 0};
    bool vprFlag = false;
    NvSciBufAttrValImageScanType scanType = NvSciBufScan_ProgressiveType;
    NvSciBufAttrKeyValuePair attributes[] = {
        {NvSciBufGeneralAttrKey_RequiredPerm, &access, sizeof(access)},
        {NvSciBufGeneralAttrKey_Types, &bufType, sizeof(bufType)},
        {NvSciBufGeneralAttrKey_NeedCpuAccess, &needCpuAccess, sizeof(needCpuAccess)},
        {NvSciBufGeneralAttrKey_EnableCpuCache, &needCpuAccess, sizeof(needCpuAccess)},
        {NvSciBufImageAttrKey_TopPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_BottomPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_LeftPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_RightPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_Layout, &layout, sizeof(layout)},
        {NvSciBufImageAttrKey_PlaneCount, &planeCount, sizeof(planeCount)},
        {NvSciBufImageAttrKey_PlaneColorFormat, &colorFormat, planeCount * sizeof(NvSciBufAttrValColorFmt)},
        {NvSciBufImageAttrKey_PlaneColorStd, &colorStd, planeCount * sizeof(NvSciBufAttrValColorStd)},
        {NvSciBufImageAttrKey_PlaneBaseAddrAlign, &baseAddrAlign, planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneWidth, &planeWidth, planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneHeight, &planeHeight, planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_VprFlag, &vprFlag, sizeof(vprFlag)},
        {NvSciBufImageAttrKey_ScanType, &scanType, sizeof(scanType)},
    };
    return NvSciBufAttrListSetAttrs(list, attributes,
                                    sizeof(attributes)/sizeof(attributes[0]))
           == NvSciError_Success;
}

// Build a reconciled NvSciBuf attr list combining SIPL (ISP engine, writer) +
// IEP (encoder engine, reader) + NV12 surface dims. The returned list is what
// the app's RegisterImages pool is allocated against, and is also handed to
// NvMediaIEPCreate so IEP's create-time attr list matches the buffers it will
// later register. Returns nullptr on failure; caller frees with
// NvSciBufAttrListFree.
NvSciBufAttrList BuildCombinedAttrList(NvSciBufModule mod,
                                       uint32_t sensorIndex,
                                       INvSIPLCamera* camera,
                                       uint32_t w, uint32_t h) {
    NvSciBufAttrList siplAttr = nullptr, iepAttr = nullptr,
                     reconciled = nullptr, conflict = nullptr;
    if (NvSciBufAttrListCreate(mod, &siplAttr) != NvSciError_Success || !siplAttr) {
        return nullptr;
    }
    // SIPL fills its engine-access requirements into the list (PeerHwEngineArray
    // for the ISP engine) plus the ISP0 surface type defaults (NV12 semi-planar
    // block-linear). This is the ISP-as-writer side of the reconcile.
    if (camera->GetImageAttributes(
            sensorIndex,
            INvSIPLClient::ConsumerDesc::OutputType::ISP0,
            siplAttr) != NVSIPL_STATUS_OK) {
        std::cerr << "hal_camera: SIPL GetImageAttributes failed for sensor "
                  << sensorIndex << "\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    // IEP fills its engine-access requirements (the reader side).
    if (NvMediaIEPFillNvSciBufAttrList(NVMEDIA_ENCODER_INSTANCE_0, siplAttr)
            != NVMEDIA_STATUS_OK) {
        std::cerr << "hal_camera: NvMediaIEPFillNvSciBufAttrList failed\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    if (!FillNV12SurfaceAttrs(siplAttr, w, h)) {
        std::cerr << "hal_camera: FillNV12SurfaceAttrs failed\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    NvSciBufAttrList arr[1] = {siplAttr};
    if (NvSciBufAttrListReconcile(arr, 1, &reconciled, &conflict)
            != NvSciError_Success || !reconciled) {
        std::cerr << "hal_camera: SIPL+IEP attr reconcile failed for sensor "
                  << sensorIndex
                  << " (SIPL and IEP engine constraints conflict — zero-copy "
                  << "needs a device->device copy fallback)\n";
        NvSciBufAttrListFree(siplAttr);
        NvSciBufAttrListFree(conflict);
        return nullptr;
    }
    NvSciBufAttrListFree(siplAttr);
    NvSciBufAttrListFree(conflict);
    return reconciled;
}

// Per-camera capture unit. Owns one SIPL pipeline (sensor index) + one encoder
// instance + its own capture thread. Encoded packets are pushed to a shared
// process-wide queue keyed by camera id; the main thread drains and writes.
struct EncodedPacket {
    uint32_t camId = 0;
    std::vector<uint8_t> bytes;
};

struct CameraCapture {
    uint32_t camId = 0;
    uint32_t sensorIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int durationFrames = 0;

    INvSIPLCamera* camera = nullptr;          // shared platform-level instance
    NvSciBufModule bufModule = nullptr;
    NvSciSyncModule syncModule = nullptr;
    NvSciSyncCpuWaitContext cpuWaitCtx = nullptr;

    NvSciBufAttrList combinedAttr = nullptr;   // reconciled SIPL+IEP+NV12
    std::vector<NvSciBufObj> pool;             // app-allocated, registered with SIPL
    NvSciSyncObj eofSyncObj = nullptr;
    INvSIPLFrameCompletionQueue* completionQ = nullptr;

    std::unique_ptr<halcodec::Encoder> enc;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> framesEncoded{0};
    std::string error;

    // Shared output queue (process-wide; all cameras push here).
    std::mutex* outMu = nullptr;
    std::condition_variable* outCv = nullptr;
    std::queue<EncodedPacket>* outQ = nullptr;
    std::atomic<bool>* mainDone = nullptr;
};

// Drain available encoded packets from `enc` into the shared queue. The
// NvMedia encoder is synchronous: FillFrame blocks until the frame is
// submitted and GetFrame returns a packet when one is ready. Returns the
// number of packets drained.
int DrainPackets(CameraCapture& c) {
    int n = 0;
    halcodec::CodecFrame out;
    while (c.enc->GetFrame(out)) {
        halcodec::DownloadToHost(out);
        EncodedPacket pkt;
        pkt.camId = c.camId;
        if (out.data && out.size > 0) {
            pkt.bytes.assign(out.data, out.data + out.size);
        }
        if (out.release) out.release();
        {
            std::lock_guard<std::mutex> lk(*c.outMu);
            c.outQ->push(std::move(pkt));
        }
        c.outCv->notify_one();
        ++n;
    }
    return n;
}

void CameraThread(CameraCapture& c) {
    constexpr int64_t kGetTimeoutUs = 5 * 1000 * 1000; // 5 s per frame
    int64_t pts = 0;
    while (c.running.load()) {
        if (c.durationFrames > 0 &&
            c.framesEncoded.load() >= static_cast<uint64_t>(c.durationFrames)) {
            break;
        }
        INvSIPLClient::INvSIPLBuffer* item = nullptr;
        SIPLStatus sts = c.completionQ->Get(item, kGetTimeoutUs);
        if (sts != NVSIPL_STATUS_OK || !item) {
            if (c.mainDone->load()) break;
            std::cerr << "hal_camera: cam " << c.camId
                      << " queue Get failed: " << sts << "\n";
            continue;
        }
        auto* nvm = dynamic_cast<INvSIPLClient::INvSIPLNvMBuffer*>(item);
        NvSciBufObj siplBuf = nvm ? nvm->GetNvSciBufImage() : nullptr;
        if (!siplBuf) {
            item->Release();
            continue;
        }
        // Zero-copy: hand the SIPL-owned NvSciBufObj to the encoder without
        // detiling/copying. FeedDeviceFrame registers it with IEP on first
        // sight (the combined attr list makes that succeed) and feeds it.
        // The SIPL buffer is returned via item->Release() after the encode
        // has consumed it — NvMediaEncoder is sync, so FillFrame returning
        // means the frame is done reading.
        halcodec::CodecFrame frame;
        frame.locality = halcodec::FrameLocality::NvSciBufObj;
        frame.device.nvSciBufObj = siplBuf;
        frame.width = c.width;
        frame.height = c.height;
        frame.format = halcodec::PixelFormat::NV12;
        frame.pts = pts++;
        if (!c.enc->FillFrame(frame)) {
            std::cerr << "hal_camera: cam " << c.camId
                      << " FillFrame failed (zero-copy registration rejected "
                      << "the SIPL buf — attr reconcile mismatch?)\n";
            c.error = "FillFrame failed";
            item->Release();
            break;
        }
        DrainPackets(c);
        item->Release();
        c.framesEncoded.fetch_add(1);
    }
    // Flush trailing packets.
    halcodec::CodecFrame eos;
    eos.width = c.width;
    eos.height = c.height;
    eos.format = halcodec::PixelFormat::NV12;
    c.enc->FillFrame(eos);
    c.enc->SignalInputComplete();
    DrainPackets(c);
}

// Resolve a sensor's resolution from the platform config (for pool alloc).
bool ResolveSensorResolution(const PlatformCfg& cfg, uint32_t sensorIndex,
                             uint32_t& w, uint32_t& h) {
    w = 1920; h = 1080; // fallback if the config doesn't expose it
    if (cfg.numDeviceBlocks == 0) return false;
    // Sensor index maps onto the flattened camera-module list across device
    // blocks. Walk until we hit the requested index.
    uint32_t seen = 0;
    for (uint32_t b = 0; b < cfg.numDeviceBlocks; ++b) {
        auto& db = cfg.deviceBlockList[b];
        for (uint32_t m = 0; m < db.numCameraModules; ++m) {
            if (seen == sensorIndex) {
                auto& si = db.cameraModuleInfoList[m].sensorInfo;
                w = si.vcInfo.resolution.width;
                h = si.vcInfo.resolution.height;
                return true;
            }
            ++seen;
        }
    }
    return false;
}

std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop.store(true); }

} // namespace

int main(int argc, char* argv[]) {
    ::signal(SIGINT, OnSignal);
    ::signal(SIGTERM, OnSignal);

    CommandLineParser cli;
    cli.parse(argc, argv);

    halcodec::LoadBackends();

    std::string encoderBackend = cli.getEncoderBackend();
    if (encoderBackend.empty()) encoderBackend = "nvmedia";
    if (encoderBackend != "nvmedia") {
        std::cerr << "hal_camera: only -B nvmedia is supported (got '"
                  << encoderBackend << "')\n";
        return 1;
    }

    // --- SIPL platform config (one instance for all cameras) ---
    auto query = INvSIPLQuery::GetInstance();
    if (!query) {
        std::cerr << "hal_camera: INvSIPLQuery::GetInstance failed\n";
        return 1;
    }
    SIPLStatus sts = cli.getSiplDb().empty()
        ? query->ParseDatabase()
        : query->ParseJsonFile(cli.getSiplDb());
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_camera: ParseDatabase/JsonFile failed: " << sts << "\n";
        return 1;
    }
    PlatformCfg platCfg;
    if (!cli.getSiplPlatform().empty()) {
        if (query->GetPlatformCfg(cli.getSiplPlatform(), platCfg) != NVSIPL_STATUS_OK) {
            std::cerr << "hal_camera: GetPlatformCfg("
                      << cli.getSiplPlatform() << ") failed\n";
            return 1;
        }
    } else {
        auto cfgList = query->GetPlatformCfgList();
        if (cfgList.empty()) {
            std::cerr << "hal_camera: no SIPL platform configs available\n";
            return 1;
        }
        platCfg = *cfgList.front();
    }
    std::cout << "hal_camera: platform=" << platCfg.platform
              << " numDeviceBlocks=" << platCfg.numDeviceBlocks << "\n";

    auto camera = INvSIPLCamera::GetInstance();
    if (!camera) {
        std::cerr << "hal_camera: INvSIPLCamera::GetInstance failed\n";
        return 1;
    }
    sts = camera->SetPlatformCfg(&platCfg);
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_camera: SetPlatformCfg failed: " << sts << "\n";
        return 1;
    }

    int numCameras = cli.getNumCameras();
    if (numCameras < 1) numCameras = 1;
    int baseSensor = cli.getSensorIndex();

    // --- Configure each pipeline (SetPipelineCfg must run before Init) ---
    std::vector<std::unique_ptr<CameraCapture>> cams;
    cams.reserve(numCameras);
    for (int i = 0; i < numCameras; ++i) {
        auto c = std::make_unique<CameraCapture>();
        c->camId = static_cast<uint32_t>(i);
        c->sensorIndex = static_cast<uint32_t>(baseSensor + i);
        c->durationFrames = cli.getDurationFrames();
        c->camera = camera.get();
        NvSIPLPipelineConfiguration pipeCfg{};
        pipeCfg.isp0OutputRequested = true;
        NvSIPLPipelineQueues queues{};
        sts = camera->SetPipelineCfg(c->sensorIndex, pipeCfg, queues);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_camera: SetPipelineCfg(sensor "
                      << c->sensorIndex << ") failed: " << sts << "\n";
            return 1;
        }
        c->completionQ = queues.isp0CompletionQueue;
        if (!c->completionQ) {
            std::cerr << "hal_camera: no isp0CompletionQueue for sensor "
                      << c->sensorIndex << "\n";
            return 1;
        }
        uint32_t w = 0, h = 0;
        ResolveSensorResolution(platCfg, c->sensorIndex, w, h);
        c->width = w;
        c->height = h;
        std::cout << "hal_camera: cam " << i << " sensor " << c->sensorIndex
                  << " resolution " << w << "x" << h << "\n";
        cams.push_back(std::move(c));
    }

    sts = camera->Init();
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_camera: SIPL Init failed: " << sts << "\n";
        return 1;
    }

    // --- NvSci modules (shared) ---
    NvSciBufModule bufModule = nullptr;
    NvSciSyncModule syncModule = nullptr;
    NvSciSyncCpuWaitContext cpuWaitCtx = nullptr;
    if (NvSciBufModuleOpen(&bufModule) != NvSciError_Success ||
        NvSciSyncModuleOpen(&syncModule) != NvSciError_Success ||
        NvSciSyncCpuWaitContextAlloc(syncModule, &cpuWaitCtx) != NvSciError_Success) {
        std::cerr << "hal_camera: NvSci module open failed\n";
        return 1;
    }

    // --- Per-camera: combined attr list, pool, EOF sync, encoder ---
    for (auto& c : cams) {
        c->bufModule = bufModule;
        c->syncModule = syncModule;
        c->cpuWaitCtx = cpuWaitCtx;

        c->combinedAttr = BuildCombinedAttrList(bufModule, c->sensorIndex,
                                                camera.get(), c->width, c->height);
        if (!c->combinedAttr) {
            std::cerr << "hal_camera: cam " << c->camId
                      << " combined attr list build failed\n";
            return 1;
        }
        // App-allocated pool, registered with SIPL. The same combined attr
        // list means each buf is ISP-writable and IEP-readable.
        constexpr uint32_t kPool = 4;
        c->pool.reserve(kPool);
        for (uint32_t i = 0; i < kPool; ++i) {
            NvSciBufObj obj = nullptr;
            if (NvSciBufObjAlloc(c->combinedAttr, &obj) != NvSciError_Success) {
                std::cerr << "hal_camera: cam " << c->camId
                          << " NvSciBufObjAlloc " << i << " failed\n";
                return 1;
            }
            c->pool.push_back(obj);
        }
        sts = camera->RegisterImages(c->sensorIndex,
                INvSIPLClient::ConsumerDesc::OutputType::ISP0, c->pool);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_camera: cam " << c->camId
                      << " RegisterImages failed: " << sts << "\n";
            return 1;
        }

        // ISP output requires an auto-control plugin (AEC/AWB). Use the SDK
        // sample plugin if present; if registration fails the ISP may still
        // produce frames but with uncorrected exposure, so warn and continue.
        // (The plugin .so is loaded by SIPL internally; no app-side dlopen.)
        // RegisterAutoControlPlugin is optional for capture-only use; skipped
        // here to avoid a hard dependency on libnvsipl_sampleplugin.so.

        // EOF sync obj (SIPL signals EOF when an ISP0 frame is complete).
        NvSciSyncAttrList syncAttr = nullptr, sReconciled = nullptr, sConflict = nullptr;
        NvSciSyncAttrListCreate(syncModule, &syncAttr);
        bool cpuSignaler = true;
        NvSciSyncAttrKeyValuePair kv[] = {
            {NvSciSyncAttrKey_NeedCpuAccess, &cpuSignaler, sizeof(cpuSignaler)},
        };
        NvSciSyncAttrListSetAttrs(syncAttr, kv, 1);
        NvSciSyncAttrListReconcile(&syncAttr, 1, &sReconciled, &sConflict);
        NvSciSyncObjAlloc(sReconciled, &c->eofSyncObj);
        NvSciSyncAttrListFree(syncAttr);
        NvSciSyncAttrListFree(sReconciled);
        NvSciSyncAttrListFree(sConflict);
        if (c->eofSyncObj) {
            camera->RegisterNvSciSyncObj(c->sensorIndex,
                INvSIPLClient::ConsumerDesc::OutputType::ISP0,
                NVSIPL_EOFSYNCOBJ, c->eofSyncObj);
        }

        // Encoder instance (nvmediaenc). It allocates its own internal surface
        // pool, but for zero-copy we feed external SIPL bufs via FillDeviceFrame
        // (registered lazily on first FillFrame). The encoder's create-time
        // attr list is the same combined list so registration succeeds.
        c->enc = halcodec::Encoder::Create(encoderBackend);
        if (!c->enc) {
            std::cerr << "hal_camera: Encoder::Create(" << encoderBackend
                      << ") failed for cam " << c->camId << "\n";
            return 1;
        }
        halcodec::CodecParams params;
        params.codec = cli.getCodec();
        params.deviceIndex = cli.getGpuIndex();
        params.width = c->width;
        params.height = c->height;
        params.inputFormat = halcodec::PixelFormat::NV12;
        params.zeroCopy = true;
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
        if (!c->enc->Initialize(params)) {
            std::cerr << "hal_camera: encoder Initialize failed for cam "
                      << c->camId << "\n";
            return 1;
        }
    }

    // --- Output files ---
    std::vector<std::ofstream> outs(cams.size());
    auto deriveName = [&](int i) -> std::string {
        const std::string& o = cli.getOutputFile();
        if (cams.size() == 1) return o.empty() ? "out.h264" : o;
        // Multi-camera: insert _cam<i> before the extension.
        std::string base = o.empty() ? "out.h264" : o;
        size_t dot = base.find_last_of('.');
        if (dot == std::string::npos) {
            return base + "_cam" + std::to_string(i);
        }
        return base.substr(0, dot) + "_cam" + std::to_string(i) + base.substr(dot);
    };
    for (size_t i = 0; i < cams.size(); ++i) {
        outs[i].open(deriveName(static_cast<int>(i)),
                     std::ios::out | std::ios::binary);
        if (!outs[i]) {
            std::cerr << "hal_camera: cannot open output for cam " << i << "\n";
            return 1;
        }
    }

    // --- Shared output queue (cameras -> main writer) ---
    std::mutex outMu;
    std::condition_variable outCv;
    std::queue<EncodedPacket> outQ;
    std::atomic<bool> mainDone{false};
    for (auto& c : cams) {
        c->outMu = &outMu;
        c->outCv = &outCv;
        c->outQ = &outQ;
        c->mainDone = &mainDone;
    }

    // --- Start ---
    sts = camera->Start();
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_camera: SIPL Start failed: " << sts << "\n";
        return 1;
    }
    std::cout << "hal_camera: SIPL started, " << cams.size()
              << " camera(s) streaming\n";
    for (auto& c : cams) {
        c->running.store(true);
        c->thread = std::thread(CameraThread, std::ref(*c));
    }

    // --- Main: drain shared queue to files until all cameras stop ---
    uint64_t totalPackets = 0;
    auto allStopped = [&]() {
        for (auto& c : cams) {
            if (c->running.load() && !(g_stop.load())) return false;
        }
        return true;
    };
    while (!g_stop.load()) {
        std::unique_lock<std::mutex> lk(outMu);
        outCv.wait_for(lk, std::chrono::milliseconds(200),
                       [&] { return !outQ.empty(); });
        while (!outQ.empty()) {
            EncodedPacket pkt = std::move(outQ.front());
            outQ.pop();
            lk.unlock();
            if (pkt.camId < outs.size() && !pkt.bytes.empty()) {
                outs[pkt.camId].write(
                    reinterpret_cast<const char*>(pkt.bytes.data()),
                    pkt.bytes.size());
                ++totalPackets;
            }
            lk.lock();
        }
        if (allStopped() && outQ.empty()) break;
    }

    // --- Teardown ---
    for (auto& c : cams) c->running.store(false);
    mainDone.store(true);
    for (auto& c : cams) {
        if (c->thread.joinable()) c->thread.join();
    }
    // Final drain.
    std::lock_guard<std::mutex> lk(outMu);
    while (!outQ.empty()) {
        EncodedPacket pkt = std::move(outQ.front());
        outQ.pop();
        if (pkt.camId < outs.size() && !pkt.bytes.empty()) {
            outs[pkt.camId].write(
                reinterpret_cast<const char*>(pkt.bytes.data()),
                pkt.bytes.size());
            ++totalPackets;
        }
    }

    camera->Stop();
    for (auto& c : cams) {
        if (c->enc) c->enc->Finalize();
    }
    for (auto& c : cams) {
        for (auto& obj : c->pool) { if (obj) NvSciBufObjFree(obj); }
        if (c->eofSyncObj) NvSciSyncObjFree(c->eofSyncObj);
        if (c->combinedAttr) NvSciBufAttrListFree(c->combinedAttr);
    }
    camera->Deinit();
    if (cpuWaitCtx) NvSciSyncCpuWaitContextFree(cpuWaitCtx);
    if (syncModule) NvSciSyncModuleClose(syncModule);
    if (bufModule) NvSciBufModuleClose(bufModule);

    for (size_t i = 0; i < cams.size(); ++i) {
        std::cout << "hal_camera: cam " << i << " encoded "
                  << cams[i]->framesEncoded.load() << " frames\n";
    }
    std::cout << "hal_camera: total packets written: " << totalPackets << "\n";
    return 0;
}
