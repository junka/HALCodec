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
#include <sstream>
#include <string>
#include <vector>

#include "NvSIPLCamera.hpp"
#include "NvSIPLPipelineMgr.hpp"
#include "NvSIPLQuery.hpp"
#include "NvSIPLDeviceBlockInfo.hpp"
#include "NvSIPLQueryTrace.hpp"
#include "NvSIPLTrace.hpp"
#include "NvSIPLVersion.hpp"

#include "nvmedia_iep.h"
#include "nvscibuf.h"
#include "nvscisync.h"

using namespace nvsipl;

// SIPLStatus is an enum with underlying type uint8_t: `os << sts` does not pick
// the enum overload — it converts to unsigned char and prints the raw byte. An
// error code prints as a stray control byte and NVSIPL_STATUS_OK prints as NUL,
// so failures are invisible on the terminal. Always print through this.
static const char* StatusName(SIPLStatus s) {
    switch (s) {
        case NVSIPL_STATUS_OK: return "OK";
        case NVSIPL_STATUS_BAD_ARGUMENT: return "BAD_ARGUMENT";
        case NVSIPL_STATUS_NOT_SUPPORTED: return "NOT_SUPPORTED";
        case NVSIPL_STATUS_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case NVSIPL_STATUS_RESOURCE_ERROR: return "RESOURCE_ERROR";
        case NVSIPL_STATUS_TIMED_OUT: return "TIMED_OUT";
        case NVSIPL_STATUS_INVALID_STATE: return "INVALID_STATE";
        case NVSIPL_STATUS_EOF: return "EOF";
        case NVSIPL_STATUS_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case NVSIPL_STATUS_FAULT_STATE: return "FAULT_STATE";
        case NVSIPL_STATUS_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

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
    // NvMediaIEPFillNvSciBufAttrList 的 @pre 是 NvMediaIEPGetVersion() —— 不先
    // 调它,Fill 会返回 INVALID_STATE。这里只取版本号,触发 IEP 子系统的初始化。
    NvMediaVersion iepVer{};
    if (NvMediaIEPGetVersion(&iepVer) != NVMEDIA_STATUS_OK) {
        std::cerr << "hal_cam: NvMediaIEPGetVersion failed\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    NvMediaStatus iepSt = NvMediaIEPFillNvSciBufAttrList(
        NVMEDIA_ENCODER_INSTANCE_0, siplAttr);
    if (iepSt != NVMEDIA_STATUS_OK) {
        std::cerr << "hal_cam: NvMediaIEPFillNvSciBufAttrList failed: "
                  << static_cast<int>(iepSt) << "\n";
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
        // 版本与 trace 是两件事,但都要在 ParseDatabase/SetPlatformCfg 之前。
        // trace 是唯一能让 SIPL 自己解释"为什么这个 platform cfg 被拒"的开关:
        // 厂商 nvsipl_camera 的 7 个 SIPL import 里就有这两个 trace 单例,而我们
        // 此前一个都没碰过。级别取自 --sipl-verbose,默认 0(不开)。
        NvSIPLVersion ver{};
        NvSIPLGetVersion(ver);
        std::cout << "hal_cam: NvSIPL version " << ver.uMajor << "."
                  << ver.uMinor << "." << ver.uPatch << "\n";

        queryTrace_ = INvSIPLQueryTrace::GetInstance();
        sipTrace_ = INvSIPLTrace::GetInstance();
        const int verbose = cli.getSiplVerbose();
        if (verbose > 0) {
            if (queryTrace_) {
                const auto lvl = verbose >= 6
                    ? INvSIPLQueryTrace::LevelAll
                    : static_cast<INvSIPLQueryTrace::TraceLevel>(verbose);
                queryTrace_->SetLevel(lvl);
            }
            if (sipTrace_) {
                const auto lvl = verbose >= 4
                    ? INvSIPLTrace::LevelDebug
                    : static_cast<INvSIPLTrace::TraceLevel>(verbose);
                sipTrace_->SetLevel(lvl);
            }
            std::cout << "hal_cam: SIPL trace level " << verbose << "\n";
        }

        query_ = INvSIPLQuery::GetInstance();
        if (!query_) {
            std::cerr << "hal_cam: INvSIPLQuery::GetInstance failed\n";
            return false;
        }
        SIPLStatus sts = cli.getSiplDb().empty()
            ? query_->ParseDatabase()
            : query_->ParseJsonFile(cli.getSiplDb());
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: ParseDatabase/JsonFile failed: "
                  << StatusName(sts) << "\n";
            return false;
        }
        // 取平台配置有两条路:GetPlatformCfg(name, &out) 按"名字"拷贝,
        // GetPlatformCfgList() 返回 lib 自持的 const PlatformCfg* 列表。实测在这块
        // 板子上两者给出的 numDeviceBlocks 不一致 —— by-name 返回 3(已按平台默认
        // mask 裁剪过),list 返回 4(未裁剪的完整拓扑)。而 ApplyMask 的契约是
        // "mask 个数必须等于 deserializer 数",厂商 nvsipl_camera 传 4 个 mask、
        // 走 list 路径拿到 4 块,ApplyMask 把第 4 块(mask=0)裁掉,得到 3 块。
        // 我们此前走 by-name 拿到 3 块再传 4 个 mask,precondition 不满足,ApplyMask
        // 行为未定义,SetPlatformCfg 随后以 BAD_ARGUMENT 拒绝。所以这里一律走 list
        // 路径,按 platformConfig 名字匹配,与厂商样例对齐。
        PlatformCfg platCfg;
        const std::string& want = cli.getSiplPlatform();
        auto cfgList = query_->GetPlatformCfgList();
        const PlatformCfg* found = nullptr;
        for (auto* pc : cfgList) {
            if (!pc) continue;
            if (!want.empty() && pc->platformConfig == want) { found = pc; break; }
            if (!want.empty() && pc->platform == want)       { found = pc; break; }
        }
        if (!found) {
            if (want.empty()) {
                if (cfgList.empty()) {
                    std::cerr << "hal_cam: no SIPL platform configs available\n";
                    return false;
                }
                found = cfgList.front();
            } else {
                // 兜底:列表里没匹配到名字,退回 by-name(某些 SDK 版本只认这条路径)。
                if (query_->GetPlatformCfg(want, platCfg) != NVSIPL_STATUS_OK) {
                    std::cerr << "hal_cam: GetPlatformCfg(" << want << ") failed\n";
                    return false;
                }
                found = &platCfg;
            }
        }
        if (found != &platCfg) {
            platCfg = *found;  // 仅供下方诊断 dump 读取;SetPlatformCfg 用 found
        }
        // 关键:厂商 nvsipl_camera 把"lib 自持的 PlatformCfg*"一路传到
        // SetPlatformCfg,ApplyMask 也在同一个对象上就地修改。我们此前拷贝到栈上
        // 的 platCfg 再传给 SetPlatformCfg,会被 SIPL 判定为"陌生结构体"而走另一条
        // 校验路径(重新 FindFirstLibPath/LoadDriversLibraries,并把 camera-module
        // 驱动当 deser 驱动加载),最终 BAD_ARGUMENT。所以这里 ApplyMask 与
        // SetPlatformCfg 都作用于 lib 自持对象(const_cast 去掉 const,与厂商行为一致)。
        PlatformCfg* cfgForSet = const_cast<PlatformCfg*>(found);
        // 平台配置里的 deserializer link mask 描述的是"该平台能挂什么",不是"这台
        // 车挂了什么"。板子上只有部分 link 有串行器时,未裁剪的平台会被
        // SetPlatformCfg 以 BAD_ARGUMENT 拒绝 —— 必须先 ApplyMask 收敛到实际
        // 在位的 link(与 sensor_camera.yaml 的 platform_mask 同义)。
        // mask 的分隔符:厂商 CLI(-m)用空格,我们的历史写法用逗号 —— 两种都收,
        // 免得把"格式写错"误认成"平台不支持"。掩码个数必须等于平台配置里的
        // deserializer 数(见 INvSIPLQuery::ApplyMask 的 @param vMasks)。
        std::vector<uint32_t> masks;
        if (!cli.getSiplMask().empty()) {
            std::string tok;
            std::istringstream is(cli.getSiplMask());
            while (is >> tok) {
                std::string::size_type pos = 0;
                while (pos <= tok.size()) {
                    auto comma = tok.find(',', pos);
                    std::string field = tok.substr(
                        pos, comma == std::string::npos ? std::string::npos
                                                        : comma - pos);
                    pos = comma == std::string::npos ? tok.size() + 1 : comma + 1;
                    if (field.empty()) continue;
                    masks.push_back(
                        static_cast<uint32_t>(std::stoul(field, nullptr, 0)));
                }
            }
            if (masks.empty() ||
                query_->ApplyMask(*cfgForSet, masks) != NVSIPL_STATUS_OK) {
                std::cerr << "hal_cam: SIPL ApplyMask('" << cli.getSiplMask()
                          << "') failed\n";
                return false;
            }
            std::cout << "hal_cam: applied link masks to " << masks.size()
                      << " deserializer(s)\n";
        }
        config_ = *cfgForSet;
        // 同时打印 mask 值 + GetPlatformCfgList 里同名配置的 numDeviceBlocks,
        // 用来判断"是 GetPlatformCfg(name,out) 的拷贝把尾部块写坏了,还是
        // 平台配置本身在这个库里就是 4 模组"。厂商 nvsipl_camera 走的是 list
        // 路径(它持有 lib 分配的 const PlatformCfg*),我们走的是 by-name 拷贝
        // 路径 —— 两者若不一致,根因就在这次拷贝。
        std::cout << "hal_cam: masks=";
        for (size_t i = 0; i < masks.size(); ++i) {
            std::cout << (i ? " " : "") << "0x" << std::hex << masks[i] << std::dec;
        }
        std::cout << " (n=" << masks.size() << ")\n";
        std::cout << "hal_cam: [cfg] platform='" << cfgForSet->platform
                  << "' platformConfig='" << cfgForSet->platformConfig
                  << "' numDeviceBlocks=" << cfgForSet->numDeviceBlocks
                  << " sizeof(PlatformCfg)=" << sizeof(PlatformCfg) << "\n";
        {
            auto cfgList = query_->GetPlatformCfgList();
            for (auto* pc : cfgList) {
                if (pc && pc->platformConfig == platCfg.platformConfig) {
                    std::cout << "hal_cam: [list]    platformConfig='"
                              << pc->platformConfig << "' numDeviceBlocks="
                              << pc->numDeviceBlocks << "\n";
                    for (uint32_t b = 0; b < pc->numDeviceBlocks; ++b) {
                        std::cout << "hal_cam: [list]    block " << b
                                  << " numCameraModules="
                                  << pc->deviceBlockList[b].numCameraModules
                                  << "\n";
                    }
                    break;
                }
            }
        }
        // 逐块列出 SIPL 真正拿到的拓扑。SetPlatformCfg 返回 BAD_ARGUMENT 时,
        // 唯一能自查的就是"我们递进去的结构体里到底写了什么"—— 设备块编号、I2C
        // 总线、每块的模组数、每个模组的 sensor 名与分辨率。sizeof(PlatformCfg)
        // 同时暴露 NV_IS_SAFETY 的 ABI(6 块 vs 4 块)对不对得上板子上的库。
        for (uint32_t b = 0; b < cfgForSet->numDeviceBlocks; ++b) {
            const DeviceBlockInfo& db = cfgForSet->deviceBlockList[b];
            std::cout << "hal_cam:   block " << b
                      << " csiPort=" << static_cast<int>(db.csiPort)
                      << " phyMode=" << static_cast<int>(db.phyMode)
                      << " i2cBus=" << db.i2cDevice
                      << " deser='" << db.deserInfo.name << "'"
                      << " deserI2cAddr=0x" << std::hex
                      << static_cast<int>(db.deserInfo.i2cAddress) << std::dec
                      << " numCameraModules=" << db.numCameraModules << "\n";
            for (uint32_t m = 0; m < db.numCameraModules; ++m) {
                const CameraModuleInfo& cm = db.cameraModuleInfoList[m];
                const SensorInfo& si = cm.sensorInfo;
                std::cout << "hal_cam:     module " << m
                          << " name='" << cm.name << "'"
                          << " linkIndex=" << cm.linkIndex
                          << " sensor='" << si.name << "'"
                          << " i2cAddr=0x" << std::hex
                          << static_cast<int>(si.i2cAddress) << std::dec
                          << " resolution=" << si.vcInfo.resolution.width << "x"
                          << si.vcInfo.resolution.height << "\n";
            }
        }

        camera_ = INvSIPLCamera::GetInstance();
        if (!camera_) {
            std::cerr << "hal_cam: INvSIPLCamera::GetInstance failed\n";
            return false;
        }
        // 用两参数重载 SetPlatformCfg(cfg, queues):厂商 nvsipl_camera 走的就是
        // 这条路径(它要拿 devblk 事件通知队列)。单参数重载在这块板子上会触发
        // CDevBlkDriverLibs::FindFirstLibPath + LoadDriversLibraries,并把
        // camera-module 库错当 deser 驱动加载,最终 BAD_ARGUMENT。queues 里的
        // 通知指针全部留 nullptr(我们暂时不需要 devblk 事件)。
        NvSIPLDeviceBlockQueues dbQueues{};
        sts = camera_->SetPlatformCfg(cfgForSet, dbQueues);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: SetPlatformCfg failed: " << StatusName(sts)
                  << "\n";
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

    // 必须在 Init 之前为平台配置里所有被 mask 选中的 sensor 都调一次
    // SetPipelineCfg —— 实测若只配置部分 sensor,camera_->Init() 会返回
    // INVALID_STATE(它按"已配置的 pipeline 全集"来初始化硬件探测/ISP 流水线,
    // 缺 sensor 会让状态机停在半就绪)。与厂商 nvsipl_camera 的行为一致:它在
    // Init 前对全部 sensor 都建了 consumer。这里只配置、不开流;真正取流的源
    // 由 numCameras 决定。
    std::vector<INvSIPLFrameCompletionQueue*> allCompletionQs_;
    // sensor 索引按 blockIndex * MAX_CAMERAMODULES_PER_BLOCK + moduleIndex 编码,
    // 空 module 槽位跳过(实测厂商 nvsipl_camera 的 sensor 序列是 0,1,2,4,5..11
    // —— block 0 只有 3 个 module,所以 sensor 3 不存在,SetPipelineCfg(3) 会
    // BAD_ARGUMENT)。allCompletionQs_ 按"真实 module 出现顺序"存,下标是稠密的;
    // completionQ() 用稠密下标取回。
    std::vector<uint32_t> allSensorIndices_;
    bool ConfigureAllPipelines() {
        allCompletionQs_.clear();
        allSensorIndices_.clear();
        const PlatformCfg& cfg = config_;
        uint32_t sensorIdx = 0;
        for (uint32_t b = 0; b < cfg.numDeviceBlocks; ++b) {
            for (uint32_t m = 0; m < cfg.deviceBlockList[b].numCameraModules; ++m) {
                NvSIPLPipelineConfiguration pipeCfg{};
                pipeCfg.isp0OutputRequested = true;
                NvSIPLPipelineQueues queues{};
                SIPLStatus sts = camera_->SetPipelineCfg(
                    sensorIdx, pipeCfg, queues);
                if (sts != NVSIPL_STATUS_OK) {
                    std::cerr << "hal_cam: SetPipelineCfg(sensor "
                              << sensorIdx << ") failed: " << StatusName(sts)
                              << "\n";
                    return false;
                }
                allSensorIndices_.push_back(sensorIdx);
                allCompletionQs_.push_back(queues.isp0CompletionQueue);
                ++sensorIdx;
            }
            // 跳过本 block 内未用的 module 槽位(MAX_CAMERAMODULES_PER_BLOCK=4):
            // sensor 索引按物理槽位编号,不是稠密编号。
            sensorIdx = (b + 1) * MAX_CAMERAMODULES_PER_BLOCK;
        }
        std::cout << "hal_cam: configured " << allCompletionQs_.size()
                  << " sensor pipeline(s)\n";
        return true;
    }

    INvSIPLFrameCompletionQueue* completionQ(uint32_t denseIdx) const {
        return denseIdx < allCompletionQs_.size()
            ? allCompletionQs_[denseIdx] : nullptr;
    }

    // 必须在所有源的 SetPipelineCfg(Open)完成之后调用;只执行一次。
    bool InitOnce() {
        if (inited_) return true;
        auto sts = camera_->Init();
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: SIPL Init failed: " << StatusName(sts) << "\n";
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
    // Trace 单例由库持有(GetInstance 返回裸指针,不是 unique_ptr):这里只存
    // 借用指针,用于 --sipl-verbose 时在 Init 前打开日志级别。
    INvSIPLQueryTrace* queryTrace_ = nullptr;
    INvSIPLTrace* sipTrace_ = nullptr;
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
        // SetPipelineCfg 已在平台 ConfigureAllPipelines() 里对全部 sensor 统一
        // 配置完毕(见其注释)。这里只取回本 sensor 的 isp0 完成队列。
        completionQ_ = platform_->completionQ(sensorIndex_);
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
                      << " RegisterImages failed: " << StatusName(sts) << "\n";
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
                          << " queue Get timeout (" << StatusName(sts) << ")\n";
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

    // 阶段 1:为平台里所有被 mask 选中的 sensor 配置 pipeline(必须全部配完
    // 才能 Init)。只取前 numCameras 个作为对外输出源。
    if (!platform->ConfigureAllPipelines()) return false;

    // 阶段 1b:构造请求的源(仅 Open = 取回 completion queue + 解析分辨率)。
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