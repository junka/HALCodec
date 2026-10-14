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
//   AllocatePools()→ 平台 InitOnce 之前:GetImageAttributes + ICP/ISP0 NvSciBufObj 池
//   Prepare()→ 平台 InitOnce 之后:RegisterImages + RegisterNvSciSyncObj + EOF sync
//   Start()  → 首个源触发平台级 camera->Start()
//   Stop()   → 释放本源的 pool/eof/combinedAttr;最后一个源触发 camera->Stop/Deinit + 模块关闭
//
// 编译门控:仅 aarch64 + NVMEDIA_DRIVE_SDK_DIR(app/CMakeLists.txt),同时链接
// libnvsipl/libnvscibuf/libnvscisync/libnvmedia_iep_sci。

#include "camera_source_factory.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
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

// 读回 reconciled attr list 的实际内容并打印 —— RegisterImages 只在 SIPL 内部
// 校验,失败时不给任何 NvSciBuf 级线索,只能由我们把 SIPL 最终接受的
// surface 描述打出来对照。
// @attention 对 reconciled attr list 必须用 value=nullptr/size=0 先取库内部指针
// 再解引用(厂商 PrintISPOutputFormat 就是这么做的);传自己的栈缓冲会
// BadParameter 且值不写回,看上去全是 0。
// 诊断开关:设 HALCODEC_SIPL_DUMP_ATTR=1 才打印 attr list(默认关闭;
// 排查 NvSciBuf reconcile / 引擎权限问题时打开)。
bool DumpAttrsEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("HALCODEC_SIPL_DUMP_ATTR");
        return e && *e && *e != '0';
    }();
    return on;
}

void DumpBufAttrList(const char* tag, NvSciBufAttrList list) {
    if (!list) { std::cerr << "  [" << tag << "] (null attr list)\n"; return; }
    constexpr size_t kMax = 12;
    NvSciBufAttrKey keyIds[kMax] = {
        NvSciBufImageAttrKey_PlaneCount,
        NvSciBufImageAttrKey_Layout,
        NvSciBufImageAttrKey_SurfType,
        NvSciBufImageAttrKey_SurfBPC,
        NvSciBufImageAttrKey_SurfMemLayout,
        NvSciBufImageAttrKey_PlaneColorFormat,
        NvSciBufImageAttrKey_PlaneColorStd,
        NvSciBufImageAttrKey_PlaneWidth,
        NvSciBufImageAttrKey_PlaneHeight,
        NvSciBufImageAttrKey_PlanePitch,
    };
    constexpr size_t kn = 10;
    NvSciBufAttrKeyValuePair kv[kMax] = {};
    for (size_t i = 0; i < kn; ++i) {
        kv[i].key = keyIds[i];
        kv[i].value = nullptr;
        kv[i].len = 0;
    }
    NvSciError err = NvSciBufAttrListGetAttrs(list, kv, kn);
    std::cout << "  [" << tag << "] getAttrs=" << static_cast<int>(err);
    for (size_t i = 0; i < kn; ++i) {
        if (!kv[i].value || kv[i].len == 0) continue;
        // value 指向库内部存储,不归调用方释放。数组 key 只打印首元素 + 总字节数。
        const uint8_t* p = static_cast<const uint8_t*>(kv[i].value);
        uint32_t v = 0;
        for (size_t b = 0; b < kv[i].len && b < 4; ++b) {
            v |= static_cast<uint32_t>(p[b]) << (8 * b);
        }
        std::cout << " " << kv[i].key << "=" << v
                  << "(n" << kv[i].len << ")";
    }
    std::cout << "\n";
}

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
// includeIep=true 时额外把 NvMediaIEP(编码器)的引擎属性揉进同一个 attr list,
// 让 reconcile 出来的 buffer 同时能被 ISP 写、被 IEP 读(真零拷贝)。此板系统
// NvMedia 是缩减构建,NvMediaIEPFillNvSciBufAttrList 返回 ERROR(7),这时保持
// false:与厂商 nvsipl_camera 一致,只声明 SIPL 侧属性(它压根不碰 IEP)。
NvSciBufAttrList BuildCombinedAttrList(NvSciBufModule mod,
                                       uint32_t sensorIndex,
                                       INvSIPLCamera* camera,
                                       INvSIPLClient::ConsumerDesc::OutputType outType,
                                       uint32_t w, uint32_t h,
                                       bool includeIep = false) {
    NvSciBufAttrList siplAttr = nullptr,
                     reconciled = nullptr, conflict = nullptr;
    if (NvSciBufAttrListCreate(mod, &siplAttr) != NvSciError_Success || !siplAttr) {
        return nullptr;
    }
    // 必须在 GetImageAttributes 之前把 Types/RequiredPerm 填好 —— SIPL 只填"引擎
    // 兼容性"那几个 key(PeerHwEngine/PlatformName/面格式),不会替你声明这是一块
    // Image buffer,也不会给权限。缺了它们,reconcile 出来的 attr 不足以让 SIPL 后续
    // 用它注册(ICP RegisterImages 返回 ERROR)。厂商 CaptureFillAttributesAllocateSciBuf
    // 对 ICP 也是这么做的(Types=Image + RequiredPerm=ReadWrite,再 GetImageAttributes)。
    // @attention 只填这两个通用 key;ICP 的"面类型"必须留给 SIPL 决定,不能在
    // GetImageAttributes 之前预填(预填会被判为不支持的 surface type)。
    {
        NvSciBufType bufType = NvSciBufType_Image;
        NvSciBufAttrValAccessPerm accessPerm = NvSciBufAccessPerm_ReadWrite;
        NvSciBufAttrKeyValuePair baseKv[] = {
            {NvSciBufGeneralAttrKey_Types, &bufType, sizeof(bufType)},
            {NvSciBufGeneralAttrKey_RequiredPerm, &accessPerm, sizeof(accessPerm)},
        };
        // ICP 只填 RequiredPerm,不填 Types —— 与厂商 NvSci 路径
        // CNvSIPLMasterNvSci::RegisterSource 一致(unreconciled list 只有
        // RequiredPerm=ReadWrite,GetImageAttributes 自己会把 Types=Image 加进去)。
        // 预填 Types 后 reconcile 出来的 ICP attr 格式看着对(PlaneCount=1/
        // PitchLinear/Bayer),但 SIPL 的 ICP 注册校验仍返回 ERROR。ISP0 保持
        // Types+RequiredPerm(厂商非 NvSci 路径就这么建,且已能过)。
        const uint32_t baseKvCount =
            (outType == INvSIPLClient::ConsumerDesc::OutputType::ICP)
                ? 1u
                : static_cast<uint32_t>(sizeof(baseKv) / sizeof(baseKv[0]));
        if (NvSciBufAttrListSetAttrs(siplAttr, baseKv, baseKvCount)
                != NvSciError_Success) {
            std::cerr << "hal_cam: NvSciBufAttrListSetAttrs(Types/Perm) failed\n";
            NvSciBufAttrListFree(siplAttr);
            return nullptr;
        }
    }
    if (camera->GetImageAttributes(
            sensorIndex,
            outType,
            siplAttr) != NVSIPL_STATUS_OK) {
        std::cerr << "hal_cam: SIPL GetImageAttributes failed for sensor "
                  << sensorIndex << " (outType "
                  << static_cast<int>(outType) << ")\n";
        NvSciBufAttrListFree(siplAttr);
        return nullptr;
    }
    // 诊断:SIPL 自己往 list 里塞了什么(引擎兼容性 key)。这是 reconcile 的输入,
    // 与 reconcile 后的结果对比能看出哪一步丢了信息。
    if (DumpAttrsEnabled()) {
        DumpBufAttrList(outType == INvSIPLClient::ConsumerDesc::OutputType::ICP
                            ? "ICP attr(unrecon)" : "ISP0 attr(unrecon)", siplAttr);
    }
    // NvMediaIEPFillNvSciBufAttrList 的 @pre 是 NvMediaIEPGetVersion() —— 不先
    // 调它,Fill 会返回 INVALID_STATE。这里只取版本号,触发 IEP 子系统的初始化。
    // 仅对 ISP0(NV12 编码器输入)做 IEP reconcile;ICP 是 raw bayer,不喂编码器。
    const bool isIsp0 = (outType == INvSIPLClient::ConsumerDesc::OutputType::ISP0);
    bool iepReady = false;
    if (isIsp0 && includeIep) {
        NvMediaVersion iepVer{};
        if (NvMediaIEPGetVersion(&iepVer) != NVMEDIA_STATUS_OK) {
            std::cerr << "hal_cam: NvMediaIEPGetVersion failed"
                      << " (zero-copy SIPL->IEP disabled, will need device copy)\n";
        } else {
            NvMediaStatus iepSt = NvMediaIEPFillNvSciBufAttrList(
                NVMEDIA_ENCODER_INSTANCE_0, siplAttr);
            if (iepSt != NVMEDIA_STATUS_OK) {
                // orin 系统 NvMedia 是缩减构建,MSENC 引擎属性可能不可用 -> ERROR(7)。
                // 不致命:退化为纯 SIPL attr pool(SIPL 仍能出帧,但下游 nvmediaenc
                // 不能零拷贝读,需要 device->device 拷贝)。
                std::cerr << "hal_cam: NvMediaIEPFillNvSciBufAttrList failed: "
                          << static_cast<int>(iepSt)
                          << " (zero-copy SIPL->IEP disabled, will need device copy)\n";
            } else {
                iepReady = true;
            }
        }
    }
    (void)iepReady;  // 保留:零拷贝是否可用,后续编码器侧用它决定拷贝路径
    // IEP Fill 失败(系统缩减构建)时退化为纯 SIPL attr:GetImageAttributes 已经
    // 把 ISP0 输出的格式/维度/引擎权限全填好了,不再覆盖 FillNV12SurfaceAttrs
    // (那会与 SIPL 的 BlockLinear/色彩标准冲突 -> SetAttrs 失败)。只有 IEP
    // 成功时才补 NV12 维度(因为那是为了让 IEP 也能读)。
    if (iepReady) {
        if (!FillNV12SurfaceAttrs(siplAttr, w, h)) {
            std::cerr << "hal_cam: FillNV12SurfaceAttrs failed\n";
            NvSciBufAttrListFree(siplAttr);
            return nullptr;
        }
    }
    NvSciBufAttrList arr[1] = {siplAttr};
    if (NvSciBufAttrListReconcile(arr, 1, &reconciled, &conflict)
            != NvSciError_Success || !reconciled) {
        std::cerr << "hal_cam: SIPL attr reconcile failed for sensor "
                  << sensorIndex << "\n";
        NvSciBufAttrListFree(siplAttr);
        NvSciBufAttrListFree(conflict);
        return nullptr;
    }
    NvSciBufAttrListFree(siplAttr);
    NvSciBufAttrListFree(conflict);
    return reconciled;
}

// sensor 索引编码(与 ConfigureAllPipelines 一致):sensorIndex = blockIndex *
// MAX_CAMERAMODULES_PER_BLOCK + moduleIndex,空 module 槽位不存在。sensor 3
// (block0 module3)即此类 —— 按物理槽位定位,不能按"出现的第几个 module"稠密计数。
bool FindModule(const PlatformCfg& cfg, uint32_t sensorIndex,
                const CameraModuleInfo*& out) {
    const uint32_t b = sensorIndex / MAX_CAMERAMODULES_PER_BLOCK;
    const uint32_t m = sensorIndex % MAX_CAMERAMODULES_PER_BLOCK;
    if (b >= cfg.numDeviceBlocks) return false;
    const DeviceBlockInfo& db = cfg.deviceBlockList[b];
    if (m >= db.numCameraModules) return false;
    out = &db.cameraModuleInfoList[m];
    return true;
}

// 从平台配置解析 sensor 的 module 名(CameraModuleInfo::name,例如
// "GWM_IMX728_F030_24BIT_RGGB")。ISP0 auto-control 插件要按这个名字找 NITO
// 标定文件。
bool ResolveSensorModuleName(const PlatformCfg& cfg, uint32_t sensorIndex,
                             std::string& name) {
    const CameraModuleInfo* cm = nullptr;
    if (!FindModule(cfg, sensorIndex, cm)) return false;
    name = cm->name;
    return !name.empty();
}

// 按厂商 CUtils.cpp LoadNITOFile 的规则找标定文件:
//   <dir><moduleName>.nito → <dir><小写 moduleName>.nito → <dir>default.nito
// dir 取 HALCODEC_SIPL_NITO_DIR 环境变量,默认 /opt/nvidia/nvmedia/nit
// (板子上 GWM_* / OX08D10_* 的 .nito 都只在这里;/usr/share/camera 只有
// 其它模组名和 template.nito)。文件缺失不是致命错误:插件仍可注册,只是 ISP
// 不做 AEC/AWB 标定校正 —— 与厂商"找不到就 LOG_ERR 返回 BAD_ARGUMENT"不同,
// 这里降级为警告以便无标定也能抓帧。
bool LoadNitoBlob(const std::string& moduleName, std::vector<uint8_t>& blob) {
    const char* envDir = std::getenv("HALCODEC_SIPL_NITO_DIR");
    std::string dir = (envDir && *envDir) ? envDir : "/opt/nvidia/nvmedia/nit/";
    if (!dir.empty() && dir.back() != '/') dir.push_back('/');

    std::string lower = moduleName;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const std::string cands[3] = {
        dir + moduleName + ".nito", dir + lower + ".nito", dir + "default.nito",
    };
    for (const std::string& path : cands) {
        FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) continue;
        std::fseek(fp, 0, SEEK_END);
        long fsize = std::ftell(fp);
        std::rewind(fp);
        if (fsize <= 0) { std::fclose(fp); continue; }
        blob.resize(static_cast<size_t>(fsize));
        size_t got = std::fread(blob.data(), 1, blob.size(), fp);
        std::fclose(fp);
        if (got != blob.size()) { blob.clear(); continue; }
        std::cout << "hal_cam: NITO '" << path << "' loaded ("
                  << blob.size() << " bytes) for module '" << moduleName << "'\n";
        return true;
    }
    std::cerr << "hal_cam: no NITO file for module '" << moduleName
              << "' under '" << dir
              << "' (ISP auto-control will run uncorrected)\n";
    return false;
}

// 为一个 sensor 建 EOF sync object:SIPL 在 ISP0 帧完成时 signal 它。
//   取 SIPL signaler attr → 与 CPU waiter reconcile → NvSciSyncObjAlloc
// 这里只"建对象",登记要等 RegisterImages 之后(RegisterNvSciSyncObj 的 @pre)。
// CPU 侧必须两个 key 一起给(NeedCpuAccess + RequiredPerm=WaitOnly):只写
// NeedCpuAccess 时与 SIPL 的 signaler attr(SignalOnly)reconcile 会以
// NotPermitted(65792 = 0x10100)失败 —— 厂商 CNvSIPLMasterNvSci::
// SetupNvSciSync2DSignalerWaiter 也是两个一起给。我们是 consumer,只 wait。
bool AllocEofSyncObj(INvSIPLCamera* camera, NvSciSyncModule mod,
                     uint32_t sensorIndex, NvSciSyncObj& out) {
    NvSciSyncAttrList siplSyncAttr = nullptr, cpuSyncAttr = nullptr,
                      sReconciled = nullptr, sConflict = nullptr;
    auto cleanup = [&]() {
        if (siplSyncAttr) NvSciSyncAttrListFree(siplSyncAttr);
        if (cpuSyncAttr) NvSciSyncAttrListFree(cpuSyncAttr);
        if (sReconciled) NvSciSyncAttrListFree(sReconciled);
        if (sConflict) NvSciSyncAttrListFree(sConflict);
    };
    NvSciSyncAttrListCreate(mod, &siplSyncAttr);
    NvSciSyncAttrListCreate(mod, &cpuSyncAttr);
    if (!siplSyncAttr || !cpuSyncAttr) {
        cleanup();
        return false;
    }
    bool cpuWaiter = true;
    NvSciSyncAccessPerm cpuPerm = NvSciSyncAccessPerm_WaitOnly;
    NvSciSyncAttrKeyValuePair cpuKv[] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuWaiter, sizeof(cpuWaiter)},
        {NvSciSyncAttrKey_RequiredPerm, &cpuPerm, sizeof(cpuPerm)},
    };
    NvSciSyncAttrListSetAttrs(cpuSyncAttr, cpuKv, 2);
    SIPLStatus fillSts = camera->FillNvSciSyncAttrList(
        sensorIndex, INvSIPLClient::ConsumerDesc::OutputType::ISP0,
        siplSyncAttr, NvSiplNvSciSyncClientType::SIPL_SIGNALER);
    if (fillSts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " FillNvSciSyncAttrList failed: " << StatusName(fillSts) << "\n";
        cleanup();
        return false;
    }
    NvSciSyncAttrList arr[2] = {siplSyncAttr, cpuSyncAttr};
    if (NvSciSyncAttrListReconcile(arr, 2, &sReconciled, &sConflict)
            != NvSciError_Success || !sReconciled) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " NvSciSyncAttrListReconcile failed\n";
        cleanup();
        return false;
    }
    if (NvSciSyncObjAlloc(sReconciled, &out) != NvSciError_Success || !out) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " NvSciSyncObjAlloc failed\n";
        cleanup();
        return false;
    }
    cleanup();
    return true;
}

// 为一个 sensor 取 attributes 并分配 pool(厂商 CaptureFillAttributesAllocateSciBuf
// 的等价物)。@pre 是 SetPipelineCfg 之后、Init 之前(GetImageAttributes 的 @pre
// 只要求 SetPipelineCfg),并且必须在 RegisterImages 之前完成。
bool AllocSensorPools(NvSciBufModule mod, INvSIPLCamera* camera,
                      uint32_t sensorIndex, uint32_t w, uint32_t h, bool wantIsp,
                      NvSciBufAttrList& icpAttr, std::vector<NvSciBufObj>& icpPool,
                      NvSciBufAttrList& isp0Attr, std::vector<NvSciBufObj>& isp0Pool) {
    // ICP pool 取 6(厂商 NUM_CAPTURE_BUFFERS_PER_POOL,合法区间 [3,40]);
    // ISP0 pool 取 4(厂商 NUM_ISP_BUFFERS_PER_POOL)。
    constexpr uint32_t kIspPool = 4;
    constexpr uint32_t kIcpPool = 6;
    icpAttr = BuildCombinedAttrList(
        mod, sensorIndex, camera,
        INvSIPLClient::ConsumerDesc::OutputType::ICP, w, h);
    if (!icpAttr) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " ICP attr list build failed\n";
        return false;
    }
    icpPool.reserve(kIcpPool);
    for (uint32_t i = 0; i < kIcpPool; ++i) {
        NvSciBufObj obj = nullptr;
        if (NvSciBufObjAlloc(icpAttr, &obj) != NvSciError_Success) {
            std::cerr << "hal_cam: sensor " << sensorIndex
                      << " ICP NvSciBufObjAlloc " << i << " failed\n";
            return false;
        }
        icpPool.push_back(obj);
    }
    if (!wantIsp) {
        // YUV 直出模组没有 ISP 路径:不建 ISP0 池(也不登记),只登记 ICP。
        return true;
    }
    isp0Attr = BuildCombinedAttrList(
        mod, sensorIndex, camera,
        INvSIPLClient::ConsumerDesc::OutputType::ISP0, w, h);
    if (!isp0Attr) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " ISP0 attr list build failed\n";
        return false;
    }
    isp0Pool.reserve(kIspPool);
    for (uint32_t i = 0; i < kIspPool; ++i) {
        NvSciBufObj obj = nullptr;
        if (NvSciBufObjAlloc(isp0Attr, &obj) != NvSciError_Success) {
            std::cerr << "hal_cam: sensor " << sensorIndex
                      << " ISP0 NvSciBufObjAlloc " << i << " failed\n";
            return false;
        }
        isp0Pool.push_back(obj);
    }
    return true;
}

// 把一个 sensor 的 pool/attr/sync 登记给 SIPL。顺序严格照厂商:
//   1. RegisterImages(ICP)        —— @pre:ICP 必须先于 ISP 登记
//   2. RegisterImages(ISP0)
//   3. RegisterNvSciSyncObj(ISP0, NVSIPL_EOFSYNCOBJ) —— @pre 是 1、2 已完成
//   4. RegisterAutoControlPlugin  —— "必须为每个启用了 ISP 输出的 pipeline 调用"
//      (NvSIPLCamera.hpp:281),@pre 是 RegisterImages 之后、Start 之前
// 缺任何一步,只要该 pipeline 开了 ISP 输出,Start() 就会返回 INVALID_STATE。
bool RegisterPipeline(INvSIPLCamera* camera, const PlatformCfg& cfg,
                      uint32_t sensorIndex, bool registerIcp,
                      NvSciBufAttrList icpAttr, std::vector<NvSciBufObj>& icpPool,
                      NvSciBufAttrList isp0Attr, std::vector<NvSciBufObj>& isp0Pool,
                      NvSciSyncObj eofSyncObj, bool hasIsp) {
    (void)isp0Attr;
    if (registerIcp && !icpPool.empty()) {
        SIPLStatus sts = camera->RegisterImages(
            sensorIndex, INvSIPLClient::ConsumerDesc::OutputType::ICP, icpPool);
        if (sts != NVSIPL_STATUS_OK) {
            std::cerr << "hal_cam: sensor " << sensorIndex
                      << " ICP RegisterImages failed: " << StatusName(sts) << "\n";
            return false;
        }
    }
    if (!hasIsp) {
        // ICP-only pipeline:厂商对这类 pipeline 也只登记 ICP,不做 ISP0
        // RegisterImages/SyncObj/AutoControl(见 /tmp/vfull.log)。
        return true;
    }
    SIPLStatus sts = camera->RegisterImages(
        sensorIndex, INvSIPLClient::ConsumerDesc::OutputType::ISP0, isp0Pool);
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " ISP0 RegisterImages failed: " << StatusName(sts) << "\n";
        return false;
    }
    sts = camera->RegisterNvSciSyncObj(
        sensorIndex, INvSIPLClient::ConsumerDesc::OutputType::ISP0,
        NVSIPL_EOFSYNCOBJ, eofSyncObj);
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " RegisterNvSciSyncObj failed: " << StatusName(sts) << "\n";
        return false;
    }
    std::string moduleName;
    std::vector<uint8_t> blob;
    if (ResolveSensorModuleName(cfg, sensorIndex, moduleName)) {
        LoadNitoBlob(moduleName, blob);
    } else {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " no module name in platform cfg; NITO blob empty\n";
    }
    sts = camera->RegisterAutoControlPlugin(sensorIndex, NV_PLUGIN, nullptr, blob);
    if (sts != NVSIPL_STATUS_OK) {
        std::cerr << "hal_cam: sensor " << sensorIndex
                  << " RegisterAutoControlPlugin failed: " << StatusName(sts)
                  << "\n";
        return false;
    }
    return true;
}

// sensor 是否走 ISP(RAW Bayer 输入)还是仅 ICP(YUV 直出)。
// 厂商 nvsipl_camera 在同一平台配置下只对 RAW 模组(IMX728)开 ISP0 输出并注册
// auto-control;YUV422 模组(ISX031)只请求/登记 ICP —— 因为 ISP 的输入是 RAW,
// YUV 模组没有 ISP 路径,对它们开 ISP0 会在 RegisterAutoControlPlugin 阶段返回
// BAD_ARGUMENT(且这些模组在板上没有 .nito 标定文件)。
bool SensorNeedsIsp(const PlatformCfg& cfg, uint32_t sensorIndex, bool& isRaw) {
    isRaw = true; // 保守:解析不到就当作 RAW(保持旧行为)
    const CameraModuleInfo* cm = nullptr;
    if (!FindModule(cfg, sensorIndex, cm)) return true;
    switch (cm->sensorInfo.vcInfo.inputFormat) {
    case NVSIPL_CAP_INPUT_FORMAT_TYPE_YUV422:
    case NVSIPL_CAP_INPUT_FORMAT_TYPE_YUV422_10:
    case NVSIPL_CAP_INPUT_FORMAT_TYPE_YUV444:
    case NVSIPL_CAP_INPUT_FORMAT_TYPE_RGB888:
        isRaw = false;
        return false;
    default:
        isRaw = true;
        return true;
    }
}

// 从平台配置解析 sensor 的分辨率(从 hal_camera 平移)。
bool ResolveSensorResolution(const PlatformCfg& cfg, uint32_t sensorIndex,
                             uint32_t& w, uint32_t& h) {
    w = 1920; h = 1080; // fallback if the config doesn't expose it
    const CameraModuleInfo* cm = nullptr;
    if (!FindModule(cfg, sensorIndex, cm)) return false;
    const SensorInfo& si = cm->sensorInfo;
    w = si.vcInfo.resolution.width;
    h = si.vcInfo.resolution.height;
    return true;
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

        // 记录"对外要消费哪些 sensor":ConfigureAllPipelines() 只对这几个开 ISP0
        // 输出。SetPipelineCfg 仍要对全部 module 调(见其注释),但 ISP 输出一旦
        // 开启,SIPL 就要求 Start() 前把该 pipeline 的 RegisterImages +
        // RegisterNvSciSyncObj + RegisterAutoControlPlugin 都做完 —— 只开不注册会
        // 让 Start() 返回 INVALID_STATE(全 mask 下 11 开 1 注册即此情形)。
        consumeBase_ = static_cast<uint32_t>(cli.getSensorIndex() < 0
                                             ? 0 : cli.getSensorIndex());
        int nc = cli.getNumCameras();
        consumeCount_ = static_cast<uint32_t>(nc < 1 ? 1 : nc);

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
                          << si.vcInfo.resolution.height
                          << " inputFormat=" << static_cast<int>(si.vcInfo.inputFormat)
                          << " cfa=" << static_cast<int>(si.vcInfo.cfa) << "\n";
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
    // sensorIndex → 该 pipeline 是否走 ISP(ConfigureAllPipelines 里按 inputFormat
    // 判定;AllocateAllPools/PrepareAllPipelines 按它决定要不要建/登记 ISP0 池)。
    std::map<uint32_t, bool> sensorHasIsp_;
    // 注意区分两件事:
    //  (a) 是否向 SIPL **请求** ICP(capture)输出 → pipelineCfg.captureOutputRequested;
    //  (b) 是否**登记** ICP buffer 池 → source.Prepare 里的 RegisterImages(ICP)。
    // 厂商 nvsipl_camera 默认 bDisableRaw=true → (a)=false(日志首行
    // "Raw output: disabled"),但 RegisterBuffers 里 registerWithSipl[0] 写死 true
    // → (b) 照样登记 6 块 ICP buffer(ICP buffer 是 ISP 的输入,必须登记)。
    // 这块板子没有 /dev/nvhost-vi*:只要 (a)=true,ICP RegisterImages 就报 ERROR;
    // 与厂商一致地把 (a) 关掉后即可通过。
    bool captureRequested_ = false;   // (a) 恒为 false
    bool registerIcp_ = true;         // (b) 恒为 true
    bool captureEnabled() const { return registerIcp_; }
    // 对外消费的 sensor 区间(稠密序号 [consumeBase_, consumeBase_+consumeCount_)),
    // 来自 --sensor-index / --num-cameras;在 Init() 里从 CLI 读入。
    uint32_t consumeBase_ = 0;
    uint32_t consumeCount_ = 1;
    bool ConfigureAllPipelines() {
        allCompletionQs_.clear();
        allSensorIndices_.clear();
        const PlatformCfg& cfg = config_;
        uint32_t sensorIdx = 0;
        for (uint32_t b = 0; b < cfg.numDeviceBlocks; ++b) {
            for (uint32_t m = 0; m < cfg.deviceBlockList[b].numCameraModules; ++m) {
                NvSIPLPipelineConfiguration pipeCfg{};
                // 按输入格式决定这条 pipeline 走 ISP 还是只走 ICP —— 与厂商
                // nvsipl_camera 在同一平台配置下的行为一致(见 /tmp/vfull.log):
                // 只有 RAW 模组(IMX728)请求 ISP0 输出并注册 auto-control;YUV422
                // 模组(ISX031)只请求 ICP。ISP 的输入是 RAW Bayer,YUV 模组没有
                // ISP 路径;对它们开 ISP0 会让 SetPipelineCfg 的后续登记链在
                // RegisterAutoControlPlugin 处返回 BAD_ARGUMENT。
                bool isRaw = true;
                SensorNeedsIsp(cfg, sensorIdx, isRaw);
                const bool hasIsp = isRaw;
                // (a) ICP(capture)输出请求:RAW pipeline 沿用全局策略(默认不请求,
                // ICP 只作为 ISP 输入登记);YUV pipeline 只能靠 ICP 出数据,必须请求。
                pipeCfg.captureOutputRequested = hasIsp ? captureRequested_ : true;
                // isp0 只对有 ISP 路径的 pipeline 开。SetPipelineCfg 对"既无 capture
                // 又无 ISP 输出"的 pipeline 返回 BAD_ARGUMENT,所以两条路必须二选一
                // (非此即彼,不存在两者都为 false 的 pipeline)。
                pipeCfg.isp0OutputRequested = hasIsp;
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
                sensorHasIsp_[sensorIdx] = hasIsp;
                // ISP pipeline 取 ISP0 队列;ICP-only pipeline 的帧在 capture 队列上
                // (它请求的是 captureOutputRequested)。哪条非空与请求一致。
                allCompletionQs_.push_back(hasIsp ? queues.isp0CompletionQueue
                                                  : queues.captureCompletionQueue);
                ++sensorIdx;
            }
            // 跳过本 block 内未用的 module 槽位(MAX_CAMERAMODULES_PER_BLOCK=4):
            // sensor 索引按物理槽位编号,不是稠密编号。
            sensorIdx = (b + 1) * MAX_CAMERAMODULES_PER_BLOCK;
        }
        uint32_t nIsp = 0;
        for (auto& kv : sensorHasIsp_) { if (kv.second) ++nIsp; }
        std::cout << "hal_cam: configured " << allCompletionQs_.size()
                  << " sensor pipeline(s), " << nIsp << " with ISP0, "
                  << (allCompletionQs_.size() - nIsp) << " ICP-only" << std::endl;
        return true;
    }

    INvSIPLFrameCompletionQueue* completionQ(uint32_t denseIdx) const {
        return denseIdx < allCompletionQs_.size()
            ? allCompletionQs_[denseIdx] : nullptr;
    }

    // 单个 pipeline(一个 sensor)的登记状态与它自己的 buffer 池。之所以平台级
    // 持有而不是 source 级:全 mask 下 Start() 要求**每一条** ISP 输出启用的
    // pipeline 都注册完,而这些 sensor 并没有对应的 SIPLCameraSource。
    struct PipelineRes {
        uint32_t sensorIndex = 0;
        // 该 pipeline 是否有 ISP 路径(RAW Bayer 输入)。false 表示 YUV 直出模组,
        // 只请求/登记 ICP;对它做 ISP0 登记链会在 RegisterAutoControlPlugin 返回
        // BAD_ARGUMENT(板上也没有这种模组的 .nito)。见 SensorNeedsIsp()。
        bool hasIsp = true;
        uint32_t w = 0;
        uint32_t h = 0;
        NvSciBufAttrList icpAttr = nullptr;
        std::vector<NvSciBufObj> icpPool;
        NvSciBufAttrList isp0Attr = nullptr;
        std::vector<NvSciBufObj> isp0Pool;
        NvSciSyncObj eofSyncObj = nullptr;
        bool registered = false;

        PipelineRes() = default;
        // 只可移动:析构负责释放 NvSci 句柄,若让编译器隐式生成拷贝构造,
        // vector 扩容时"拷贝 + 析构原对象"会把副本仍在引用的句柄释放掉
        // (实测表现为第二次 push_back 扩容时 abort)。
        PipelineRes(const PipelineRes&) = delete;
        PipelineRes& operator=(const PipelineRes&) = delete;
        PipelineRes(PipelineRes&& o) noexcept { MoveFrom(std::move(o)); }
        PipelineRes& operator=(PipelineRes&& o) noexcept {
            if (this != &o) { Release(); MoveFrom(std::move(o)); }
            return *this;
        }
        ~PipelineRes() { Release(); }

    private:
        void MoveFrom(PipelineRes&& o) {
            sensorIndex = o.sensorIndex;
            hasIsp = o.hasIsp;
            w = o.w; h = o.h;
            icpAttr = o.icpAttr; icpPool = std::move(o.icpPool);
            isp0Attr = o.isp0Attr; isp0Pool = std::move(o.isp0Pool);
            eofSyncObj = o.eofSyncObj;
            registered = o.registered;
            o.icpAttr = nullptr; o.isp0Attr = nullptr; o.eofSyncObj = nullptr;
            o.icpPool.clear(); o.isp0Pool.clear();
            o.registered = false;
        }
        void Release() {
            for (auto o : icpPool) { if (o) NvSciBufObjFree(o); }
            for (auto o : isp0Pool) { if (o) NvSciBufObjFree(o); }
            if (eofSyncObj) NvSciSyncObjFree(eofSyncObj);
            if (icpAttr) NvSciBufAttrListFree(icpAttr);
            if (isp0Attr) NvSciBufAttrListFree(isp0Attr);
            icpPool.clear();
            isp0Pool.clear();
            icpAttr = nullptr; isp0Attr = nullptr; eofSyncObj = nullptr;
        }
    };
    std::vector<PipelineRes> allPipelines_;

    PipelineRes* Find(uint32_t sensorIndex) {
        for (auto& r : allPipelines_) {
            if (r.sensorIndex == sensorIndex) return &r;
        }
        return nullptr;
    }
    const PipelineRes* Find(uint32_t sensorIndex) const {
        for (const auto& r : allPipelines_) {
            if (r.sensorIndex == sensorIndex) return &r;
        }
        return nullptr;
    }

    // 必须在 Init **之前** 调用,因为 GetImageAttributes 的 @pre 是"SetPipelineCfg
    // 之后、RegisterImages 之前",且厂商 trace 里全部 GetImageAttributes 都出现在
    // "Init: Entering" 之前。对平台里**每一个**被 mask 选中的 sensor 都建池。
    bool AllocateAllPools() {
        for (uint32_t sensorIdx : allSensorIndices_) {
            uint32_t w = 0, h = 0;
            ResolveSensorResolution(config_, sensorIdx, w, h);
            PipelineRes res;
            res.sensorIndex = sensorIdx;
            auto it = sensorHasIsp_.find(sensorIdx);
            res.hasIsp = (it == sensorHasIsp_.end()) ? true : it->second;
            if (!AllocSensorPools(bufModule_, camera_.get(), sensorIdx, w, h,
                                  res.hasIsp,
                                  res.icpAttr, res.icpPool,
                                  res.isp0Attr, res.isp0Pool)) {
                return false;
            }
            res.w = w;
            res.h = h;
            allPipelines_.push_back(std::move(res));
        }
        return true;
    }

    // 厂商 nvsipl_camera 对"每个 ISP 输出启用的 pipeline"都做
    //   RegisterImages(ICP) → RegisterImages(ISP0) → RegisterNvSciSyncObj →
    //   RegisterAutoControlPlugin
    // Start() 的 @pre 明写这四件事都要在 Start 之前完成(NvSIPLCamera.hpp:625),
    // 而 SetPipelineCfg 又不允许把某个 pipeline 的输出全关掉(既无 capture 又无
    // ISP 输出 → BAD_ARGUMENT)。所以全 mask 下只有两条路:要么把**全部** 11 条
    // pipeline 都注册完(这里),要么用 ApplyMask 把平台收窄到真正要的 sensor。
    // 我们两条都支持:这里负责前者。
    bool PrepareAllPipelines() {
        for (auto& res : allPipelines_) {
            if (res.hasIsp) {
                if (!AllocEofSyncObj(camera_.get(), syncModule_, res.sensorIndex,
                                     res.eofSyncObj)) {
                    return false;
                }
            }
            if (!RegisterPipeline(camera_.get(), config_, res.sensorIndex,
                                  registerIcp_, res.icpAttr, res.icpPool,
                                  res.isp0Attr, res.isp0Pool, res.eofSyncObj,
                                  res.hasIsp)) {
                return false;
            }
            res.registered = true;
        }
        std::cout << "hal_cam: registered " << allPipelines_.size()
                  << " pipeline(s) with SIPL" << std::endl;
        return true;
    }

    // 取某个 sensor 的 pool —— 由 SIPLCameraSource::Prepare 复用,避免为消费的
    // sensor 重复分配一套 buffer。
    NvSciBufAttrList icpAttrOf(uint32_t sensorIndex) const {
        const PipelineRes* r = Find(sensorIndex);
        return r ? r->icpAttr : nullptr;
    }
    std::vector<NvSciBufObj>* icpPoolOf(uint32_t sensorIndex) {
        PipelineRes* r = Find(sensorIndex);
        return r ? &r->icpPool : nullptr;
    }
    NvSciBufAttrList isp0AttrOf(uint32_t sensorIndex) const {
        const PipelineRes* r = Find(sensorIndex);
        return r ? r->isp0Attr : nullptr;
    }
    std::vector<NvSciBufObj>* isp0PoolOf(uint32_t sensorIndex) {
        PipelineRes* r = Find(sensorIndex);
        return r ? &r->isp0Pool : nullptr;
    }
    NvSciSyncObj eofSyncObjOf(uint32_t sensorIndex) const {
        const PipelineRes* r = Find(sensorIndex);
        return r ? r->eofSyncObj : nullptr;
    }
    bool prepared(uint32_t sensorIndex) const {
        const PipelineRes* r = Find(sensorIndex);
        return r && r->registered;
    }
    // 该 pipeline 是否走 ISP(见 PipelineRes::hasIsp)。YUV 直出模组为 false,
    // 它的输出帧在 ICP 队列上、格式是 YUV422 而不是 ISP 的 NV12。
    bool hasIspOf(uint32_t sensorIndex) const {
        const PipelineRes* r = Find(sensorIndex);
        return r ? r->hasIsp : true;
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
            SIPLStatus sts = camera_->Start();
            if (sts != NVSIPL_STATUS_OK) {
                std::cerr << "hal_cam: SIPL Start failed: " << StatusName(sts) << "\n";
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
        // 配置完毕(见其注释)。这里只取回本 sensor 的 isp0 完成队列与平台为其
        // 建好的 buffer 池(池由平台统一分配 —— 因为 Start() 要求**每一条**启用了
        // ISP 输出的 pipeline 都注册,不只是对外消费的那几条)。
        completionQ_ = platform_->completionQ(sensorIndex_);
        if (!completionQ_) {
            std::cerr << "hal_cam: no completion queue for sensor "
                      << sensorIndex_ << "\n";
            return false;
        }
        grabQ_ = completionQ_;
        hasIsp_ = platform_->hasIspOf(sensorIndex_);
        if (hasIsp_) {
            isp0Pool_ = platform_->isp0PoolOf(sensorIndex_);
            if (!isp0Pool_) {
                std::cerr << "hal_cam: no ISP0 pool for sensor " << sensorIndex_
                          << "\n";
                return false;
            }
        } else {
            // YUV 直出模组:帧走 ICP 队列(见 Grab),没有 ISP0 池。
            icpPool_ = platform_->icpPoolOf(sensorIndex_);
            if (!icpPool_) {
                std::cerr << "hal_cam: no ICP pool for sensor " << sensorIndex_
                          << "\n";
                return false;
            }
        }
        ResolveSensorResolution(platform_->config(), sensorIndex_, w_, h_);
        return true;
    }

    // 阶段 B —— 平台 Init 之后:本 sensor 的登记工作已由
    // SIPLPlatform::PrepareAllPipelines() 统一完成(全 mask 下必须对每条 ISP
    // 输出启用的 pipeline 都做,见其注释),这里只做前置检查 + 回读。
    bool Prepare() override {
        if (!platform_->prepared(sensorIndex_)) {
            std::cerr << "hal_cam: sensor " << sensorIndex_
                      << " pipeline not registered by platform\n";
            return false;
        }
        // 回读 ISP0 池的 attr,便于与 reconcile 结果对照(诊断用)。
        if (DumpAttrsEnabled()) {
            NvSciBufAttrList attr = platform_->isp0AttrOf(sensorIndex_);
            if (attr) DumpBufAttrList("ISP0 attr", attr);
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
        if (!grabQ_) {
            std::cerr << "hal_cam: sensor " << sensorIndex_
                      << " no completion queue to grab from\n";
            return false;
        }
        while (!requestedStop_.load()) {
            INvSIPLClient::INvSIPLBuffer* item = nullptr;
            SIPLStatus sts = grabQ_->Get(item, kGetTimeoutUs);
            if (sts != NVSIPL_STATUS_OK || !item) {
                // 超时:重试(RequestStop 后由循环条件退出)。首个和每 30 次各打一行,
                // 否则 5s×N 会把日志刷满。
                if (timeouts_++ == 0 || timeouts_ % 30 == 0) {
                    std::cerr << "hal_cam: sensor " << sensorIndex_
                              << " queue Get timeout (" << StatusName(sts)
                              << ", " << timeouts_ << " so far)\n";
                }
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
            // YUV422 直出的 ICP 帧不是 NV12;ISP 帧才是平面 NV12。
            frame.format = hasIsp_ ? halcodec::PixelFormat::NV12
                                   : halcodec::PixelFormat::NV16;
            frame.release = [keep]() { keep->Release(); };
            return true;
        }
        return false;
    }

    void Stop() override {
        // buffer 池 / sync obj / attr list 全部由 SIPLPlatform 按 pipeline 持有,
        // 随平台对象释放(必须在 camera_->Deinit() 之后,故不在这里 Free)。
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
    uint64_t timeouts_ = 0;
    INvSIPLFrameCompletionQueue* completionQ_ = nullptr;
    // 取帧用的完成队列:ISP 模组取 ISP0 队列,YUV 直出模组取 ICP 队列。
    INvSIPLFrameCompletionQueue* grabQ_ = nullptr;
    bool hasIsp_ = true;
    // 本 sensor 的 buffer 池 —— 由平台持有,这里只借用(Start 前平台已登记)。
    std::vector<NvSciBufObj>* isp0Pool_ = nullptr;
    std::vector<NvSciBufObj>* icpPool_ = nullptr;
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

    // 阶段 1b:Init 之前取 image attributes 并分配 NvSciBufObj 池(厂商顺序:
    // GetImageAttributes 全部发生在 Init: Entering 之前)。对**每一条**被 mask
    // 选中的 pipeline 都建池 —— Start() 要求每条 ISP 输出启用的 pipeline 都注册。
    if (!platform->AllocateAllPools()) return false;

    // 阶段 1c:构造请求的源(Open = 取回 completion queue + 借用平台建的 ISP0 池)。
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
    // 阶段 2:平台 Init,然后把**全部** pipeline 登记给 SIPL(RegisterImages /
    // RegisterNvSciSyncObj / RegisterAutoControlPlugin)。只登记对外消费的那几条
    // 会让 Start() 返回 INVALID_STATE。
    if (!platform->InitOnce()) return false;
    if (!platform->PrepareAllPipelines()) return false;
    for (auto& s : sources) {
        if (!s->Prepare()) return false;
    }
    for (auto& s : sources) {
        out.push_back(std::move(s));
    }
    return true;
}

} // namespace camera