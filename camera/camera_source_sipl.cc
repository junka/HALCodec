// camera_source_sipl — DRIVE-OS(Thor/Orin, aarch64)SIPL 相机源实现。
//
// 从 hal_camera 泛化而来:每路 SIPL pipeline 对应一个 SIPLCameraSource,共享
// 平台级 INvSIPLCamera / NvSci 模块生命周期(SIPLPlatform,多相机共用一个
// platform 实例)。Grab 零拷贝返回 locality=NvSciBufObj 的 CodecFrame,缓冲归
// SIPL 所有,通过 frame.release 回调归还(item->Release),由捕获循环在同步
// nvmedia encoder 的 FillFrame 返回后触发。
//
// 生命周期顺序(约束沿袭 self-contained 的 SIPL API):
//   Open()   → SetPipelineCfg + 解析分辨率(必须全部 Open 完才能 Init)
//   Prepare()→ 平台 InitOnce 之后:BuildCombinedAttr + pool 分配 + RegisterImages + EOF sync
//   Start()  → 首个源触发平台级 camera->Start()
//   Stop()   → 释放本源的 pool/eof/combinedAttr;最后一个源触发 camera->Stop/Deinit + 模块关闭
//
// 编译门控:仅 aarch64 + NVMEDIA_DRIVE_SDK_DIR(app/CMakeLists.txt),同时链接
// libnvsipl/libnvscibuf/libnvscisync/libnvmedia_iep_sci。

#include "camera_source_factory.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
// of the given dimensions (从 hal_camera 平移)。BlockLinear, 256-byte base
// align, REC601_ER, progressive。
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
// IEP (encoder engine, reader) + NV12 surface dims(从 hal_camera 平移)。返回
// nullptr 时调用方不再持有 siplAttr 之外的内存。
NvSciBufAttrList BuildCombinedAttrList(NvSciBufModule mod,
                                       uint32_t sensorIndex,
                                       INvSIPLCamera* camera,
                                       uint32_t w, uint32_t h) {
    NvSciBufAttrList siplAttr = nullptr, iepAttr = nullptr,
                     reconciled = nullptr, conflict = nullptr;
    if (NvSciBufAttrListCreate(mod, &siplAttr) != NvSciError_Success || !siplAttr) {
        return nullptr;
    }
    if (camera->GetImageAttributes(
            sensorIndex,
            INvSIPLClient::ConsumerDesc::OutputType::ISP0,
            siplAttr) != NVSIPL_STATUS_OK) {
        std::cerr << "hal_cam: SIPL GetImageAttributes failed for sensor "
                  << sensorIndex << "\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    if (NvMediaIEPFillNvSciBufAttrList(NVMEDIA_ENCODER_INSTANCE_0, siplAttr)
            != NVMEDIA_STATUS_OK) {
        std::cerr << "hal_cam: NvMediaIEPFillNvSciBufAttrList failed\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    if (!FillNV12SurfaceAttrs(siplAttr, w, h)) {
        std::cerr << "hal_cam: FillNV12SurfaceAttrs failed\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    NvSciBufAttrList arr[1] = {siplAttr};
    if (NvSciBufAttrListReconcile(arr, 1, &reconciled, &conflict)
            != NvSciError_Success || !reconciled) {
        std::cerr << "hal_cam: SIPL+IEP attr reconcile failed for sensor "
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

// 从平台配置解析 sensor 的分辨率(从 hal_camera 平移)。
bool ResolveSensorResolution(const PlatformCfg& cfg, uint32_t sensorIndex,
                             uint32_t& w, uint32_t& h) {
    w = 1920; h = 1080; // fallback if the config doesn't expose it
    if (cfg.numDeviceBlocks == 0) return false;
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

// 平台级共享状态:一个 INvSIPLCamera 实例 + NvSci 模块 + Init/Start/Stop 只执行
// 一次的协调。由全部 SIPLCameraSource 通过 shared_ptr 共享;最后一个源 Stop 时
// 完成 camera->Stop/Deinit 与模块关闭。
class SIPLPlatform {
public:
    bool Init(const CommandLineParser& cli) {
        query_ = INvSIPLQuery::GetInstance();
        if (!query_) {
            std::cerr << "hal_cam: INvSIPLQuery::GetInstance failed\n";
            return false;
        }
        SIPLStatus sts = cli.getSiplDb().empty()
            ? query_->ParseDatabase()
            : query_->ParseJsonFile(cli.getSiplDb());
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: ParseDatabase/JsonFile failed: " << sts << "\n";
            return false;
        }
        PlatformCfg platCfg;
        if (!cli.getSiplPlatform().empty()) {
            if (query_->GetPlatformCfg(cli.getSiplPlatform(), platCfg)
                    != NVSIPL_STATUS_OK) {
                std::cerr << "hal_cam: GetPlatformCfg("
                          << cli.getSiplPlatform() << ") failed\n";
                return false;
            }
        } else {
            auto cfgList = query_->GetPlatformCfgList();
            if (cfgList.empty()) {
                std::cerr << "hal_cam: no SIPL platform configs available\n";
                return false;
            }
            platCfg = *cfgList.front();
        }
        config_ = platCfg;
        std::cout << "hal_cam: platform=" << platCfg.platform
                  << " numDeviceBlocks=" << platCfg.numDeviceBlocks << "\n";

        camera_ = INvSIPLCamera::GetInstance();
        if (!camera_) {
            std::cerr << "hal_cam: INvSIPLCamera::GetInstance failed\n";
            return false;
        }
        sts = camera_->SetPlatformCfg(&platCfg);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: SetPlatformCfg failed: " << sts << "\n";
            return false;
        }
        // NvSci 模块与 CPU wait context 全平台共享。
        if (NvSciBufModuleOpen(&bufModule_) != NvSciError_Success ||
            NvSciSyncModuleOpen(&syncModule_) != NvSciError_Success ||
            NvSciSyncCpuWaitContextAlloc(syncModule_, &cpuWaitCtx_)
                != NvSciError_Success) {
            std::cerr << "hal_cam: NvSci module open failed\n";
            return false;
        }
        return true;
    }

    // 必须在所有源的 SetPipelineCfg(Open)完成之后调用;只执行一次。
    bool InitOnce() {
        if (inited_) return true;
        if (camera_->Init() != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: SIPL Init failed\n";
            return false;
        }
        inited_ = true;
        return true;
    }

    // 首个源 Start 时触发,只执行一次。
    bool StartStreaming() {
        if (!started_) {
            if (camera_->Start() != NVSIPL_STATUS_OK) {
                std::cerr << "hal_cam: SIPL Start failed\n";
                return false;
            }
            started_ = true;
        }
        return true;
    }

    INvSIPLCamera* camera() const { return camera_.get(); }
    NvSciBufModule bufModule() const { return bufModule_; }
    NvSciSyncModule syncModule() const { return syncModule_; }
    NvSciSyncCpuWaitContext cpuWaitCtx() const { return cpuWaitCtx_; }
    const PlatformCfg& config() const { return config_; }

    // 一个源停止;最后一个源负责平台级 teardown。
    void SourceStarted() { ++activeSources_; }
    void SourceStopped() {
        if (activeSources_ > 0) --activeSources_;
        if (activeSources_ != 0) return;
        if (started_) {
            camera_->Stop();
            started_ = false;
        }
        camera_->Deinit();
        inited_ = false;
        if (cpuWaitCtx_) NvSciSyncCpuWaitContextFree(cpuWaitCtx_);
        if (syncModule_) NvSciSyncModuleClose(syncModule_);
        if (bufModule_) NvSciBufModuleClose(bufModule_);
        cpuWaitCtx_ = nullptr;
        syncModule_ = nullptr;
        bufModule_ = nullptr;
    }

private:
    // GetInstance() 返回 unique_ptr(DRIVE-OS C++ API 习惯),平台级单实例
    // 随最后一个源释放。
    std::unique_ptr<INvSIPLQuery> query_;
    std::unique_ptr<INvSIPLCamera> camera_;
    NvSciBufModule bufModule_ = nullptr;
    NvSciSyncModule syncModule_ = nullptr;
    NvSciSyncCpuWaitContext cpuWaitCtx_ = nullptr;
    PlatformCfg config_{};
    std::atomic<int> activeSources_{0};
    std::atomic<bool> inited_{false};
    std::atomic<bool> started_{false};
};

// 单路 SIPL pipeline 源:拥有一路 completion queue、一个 NvSciBuf 池、一个 EOF
// sync,共享平台实例。Grab 零拷贝输出 NvSciBufObj 帧。
class SIPLCameraSource : public halcodec::CameraSource {
public:
    SIPLCameraSource(std::shared_ptr<SIPLPlatform> platform, uint32_t sensorIndex)
        : platform_(std::move(platform)), sensorIndex_(sensorIndex) {}

    bool Open() override {
        // SetPipelineCfg 必须在平台 Init 之前全部就绪。
        NvSIPLPipelineConfiguration pipeCfg{};
        pipeCfg.isp0OutputRequested = true;
        NvSIPLPipelineQueues queues{};
        SIPLStatus sts = platform_->camera()->SetPipelineCfg(
            sensorIndex_, pipeCfg, queues);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: SetPipelineCfg(sensor "
                      << sensorIndex_ << ") failed: " << sts << "\n";
            return false;
        }
        completionQ_ = queues.isp0CompletionQueue;
        if (!completionQ_) {
            std::cerr << "hal_cam: no isp0CompletionQueue for sensor "
                      << sensorIndex_ << "\n";
            return false;
        }
        ResolveSensorResolution(platform_->config(), sensorIndex_, w_, h_);
        return true;
    }

    // 平台 Init 之后:构造 cross-engine attr、分配池、RegisterImages、EOF sync。
    bool Prepare() override {
        INvSIPLCamera* camera = platform_->camera();
        combinedAttr_ = BuildCombinedAttrList(
            platform_->bufModule(), sensorIndex_, camera, w_, h_);
        if (!combinedAttr_) {
            std::cerr << "hal_cam: sensor " << sensorIndex_
                      << " combined attr list build failed\n";
            return false;
        }
        constexpr uint32_t kPool = 4;
        pool_.reserve(kPool);
        for (uint32_t i = 0; i < kPool; ++i) {
            NvSciBufObj obj = nullptr;
            if (NvSciBufObjAlloc(combinedAttr_, &obj) != NvSciError_Success) {
                std::cerr << "hal_cam: sensor " << sensorIndex_
                          << " NvSciBufObjAlloc " << i << " failed\n";
                return false;
            }
            pool_.push_back(obj);
        }
        SIPLStatus sts = camera->RegisterImages(
            sensorIndex_, INvSIPLClient::ConsumerDesc::OutputType::ISP0, pool_);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: sensor " << sensorIndex_
                      << " RegisterImages failed: " << sts << "\n";
            return false;
        }
        // ISP 输出需要 auto-control plugin(AEC/AWB);示例插件缺失时 ISP 仍可
        // 出帧但曝光不校正,提示后继续(与 hal_camera 行为一致)。

        // EOF sync obj(SIPL 在 ISP0 帧完成时 signal EOF)(从 hal_camera 平移)。
        NvSciSyncAttrList syncAttr = nullptr, sReconciled = nullptr, sConflict = nullptr;
        NvSciSyncAttrListCreate(platform_->syncModule(), &syncAttr);
        bool cpuSignaler = true;
        NvSciSyncAttrKeyValuePair kv[] = {
            {NvSciSyncAttrKey_NeedCpuAccess, &cpuSignaler, sizeof(cpuSignaler)},
        };
        NvSciSyncAttrListSetAttrs(syncAttr, kv, 1);
        NvSciSyncAttrListReconcile(&syncAttr, 1, &sReconciled, &sConflict);
        NvSciSyncObjAlloc(sReconciled, &eofSyncObj_);
        NvSciSyncAttrListFree(syncAttr);
        NvSciSyncAttrListFree(sReconciled);
        NvSciSyncAttrListFree(sConflict);
        if (eofSyncObj_) {
            camera->RegisterNvSciSyncObj(
                sensorIndex_, INvSIPLClient::ConsumerDesc::OutputType::ISP0,
                NVSIPL_EOFSYNCOBJ, eofSyncObj_);
        }
        return true;
    }

    bool Start() override {
        if (!platform_->StartStreaming()) return false;
        platform_->SourceStarted();
        requestedStop_.store(false);
        return true;
    }

    bool Grab(halcodec::CodecFrame& frame) override {
        constexpr int64_t kGetTimeoutUs = 5 * 1000 * 1000; // 5 s per frame
        while (!requestedStop_.load()) {
            INvSIPLClient::INvSIPLBuffer* item = nullptr;
            SIPLStatus sts = completionQ_->Get(item, kGetTimeoutUs);
            if (sts != NVSIPL_STATUS_OK || !item) {
                // 超时:重试(RequestStop 后由循环条件退出)。
                std::cerr << "hal_cam: sensor " << sensorIndex_
                          << " queue Get timeout (" << sts << ")\n";
                continue;
            }
            auto* nvm = dynamic_cast<INvSIPLClient::INvSIPLNvMBuffer*>(item);
            NvSciBufObj siplBuf = nvm ? nvm->GetNvSciBufImage() : nullptr;
            if (!siplBuf) {
                item->Release();
                continue;
            }
            // 零拷贝:NvSciBufObj 交给 encoder;同步 nvmedia 的 FillFrame 返回
            // 即消费完毕,由捕获循环调用 release 归还 SIPL(NvMediaEncoder 是
            // 同步的,见 src/encoder.h isAsync()==false)。
            INvSIPLClient::INvSIPLBuffer* keep = item;
            frame.locality = halcodec::FrameLocality::NvSciBufObj;
            frame.device.nvSciBufObj = siplBuf;
            frame.width = static_cast<int>(w_);
            frame.height = static_cast<int>(h_);
            frame.format = halcodec::PixelFormat::NV12;
            frame.release = [keep]() { keep->Release(); };
            return true;
        }
        return false;
    }

    void Stop() override {
        for (auto& obj : pool_) { if (obj) NvSciBufObjFree(obj); }
        pool_.clear();
        if (eofSyncObj_) NvSciSyncObjFree(eofSyncObj_);
        eofSyncObj_ = nullptr;
        if (combinedAttr_) NvSciBufAttrListFree(combinedAttr_);
        combinedAttr_ = nullptr;
        platform_->SourceStopped();
    }

    void RequestStop() override { requestedStop_.store(true); }

    uint32_t width()  const override { return w_; }
    uint32_t height() const override { return h_; }

private:
    std::shared_ptr<SIPLPlatform> platform_;
    uint32_t sensorIndex_ = 0;
    uint32_t w_ = 0;
    uint32_t h_ = 0;
    std::atomic<bool> requestedStop_{false};
    INvSIPLFrameCompletionQueue* completionQ_ = nullptr;
    NvSciBufAttrList combinedAttr_ = nullptr;
    std::vector<NvSciBufObj> pool_;
    NvSciSyncObj eofSyncObj_ = nullptr;
};

} // namespace

namespace camera {

bool CreateSIPL(const CommandLineParser& cli,
                std::vector<std::unique_ptr<halcodec::CameraSource>>& out) {
    auto platform = std::make_shared<SIPLPlatform>();
    if (!platform->Init(cli)) return false;

    int numCameras = cli.getNumCameras();
    if (numCameras < 1) numCameras = 1;
    int baseSensor = cli.getSensorIndex();

    // 阶段 1:全部 Open(SetPipelineCfg)完成,才能 Init。
    std::vector<std::unique_ptr<SIPLCameraSource>> sources;
    sources.reserve(static_cast<size_t>(numCameras));
    for (int i = 0; i < numCameras; ++i) {
        auto s = std::make_unique<SIPLCameraSource>(
            platform, static_cast<uint32_t>(baseSensor + i));
        if (!s->Open()) return false;
        std::cout << "hal_cam: cam " << i << " sensor " << (baseSensor + i)
                  << " resolution " << s->width() << "x" << s->height() << "\n";
        sources.push_back(std::move(s));
    }
    // 阶段 2:平台 Init,然后逐个 Prepare(RegisterImages 等)。
    if (!platform->InitOnce()) return false;
    for (auto& s : sources) {
        if (!s->Prepare()) return false;
    }
    for (auto& s : sources) {
        out.push_back(std::move(s));
    }
    return true;
}

} // namespace camera