// frame_share_sipl.cc — 层 1 的设备句柄 backend:SIPL 的 NvSciBufObj 跨进程零拷贝。
//
// 与 SDK 的 rawstream 样例同构(见 NvMedia/samples/nvsci/rawstream):在一条已建立
// 的 NvSciIpc 端点上交换 NvSciBuf 的 attr list(一次)与对象导出描述符(每个对象
// 一次),之后每帧只走一个对象下标。像素一字节不过 IPC —— 消费者拿到的
// NvSciBufObj 与 SIPL 注册进 ISP 的是同一块显存。
//
// 为什么每对象一次而不是每帧一次:NvSciBufObjIpcExportDescriptor 的契约是"只能被
// import 一次"(nvscibuf.h:5413)。池子里的对象会被反复复用,若每帧都新导出一份
// 描述符,消费端就得每帧 import 一个新对象再释放,句柄往返没有收益;而按
// rawstream 的做法——每个对象导出一次、消费端缓存下标——每帧的 IPC 负载降到几个
// 字节。故这里让层 1 自己维护 obj→index 表,首见某对象时带上完整描述符,其后只带
// 下标。
//
// 反向方向前提(nvscibuf.h:5304 NvSciBufObjIpcExport):对象必须"在参与其 attr
// list reconcile 的 unreconciled attr list 被导出方向的反方向上"导出。消费端在
// 握手时用 NvSciBufAttrListIpcExportUnreconciled 把它的读 attrs 送到 producer
// (方向 C→P),producer 随后把对象导出回 C(方向 P→C,即反向),满足该前提。
// producer 侧的 reconciled attr list 直接取自对象本身(NvSciBufObjGetAttrList),
// 不做猜测式重建 —— 这批对象是 SIPL 已 reconcile 并注册进 ISP 的。
//
// 生命周期:消费端缓存自己 import 出来的对象,通道关闭时统一释放;帧的 release 是
// 空操作,因为对象不随帧生灭。producer 侧永远在"复制出来的句柄"上导出(见
// ExportSharedFrame 的 objCopy),消费端的释放不会动到 SIPL 的池子。
//
// 平台:仅 DRIVE-OS(aarch64/NvMedia SDK),与非该平台不编译的 nvsciipc_channel.cc
// 编在同一个可执行文件里(共享 NvSciBufModule 与端点状态)。

#include "frame_share.h"

#include "nvscibuf.h"
#include "nvscierror.h"
#include "nvsciipc.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace halcodec {

namespace {

// --- NvSciBufModule:进程级一次(NvSciBufModuleOpen/Close 无引用计数) ------------
std::mutex& ModuleMu() {
    static std::mutex mu;
    return mu;
}
NvSciBufModule& Module() {
    static NvSciBufModule mod = nullptr;
    return mod;
}
int& ModuleRefs() {
    static int refs = 0;
    return refs;
}

NvSciBufModule ModuleAcquire() {
    std::lock_guard<std::mutex> lk(ModuleMu());
    if (ModuleRefs() == 0) {
        if (NvSciBufModuleOpen(&Module()) != NvSciError_Success) {
            std::cerr << "frame_share_sipl: NvSciBufModuleOpen failed\n";
            Module() = nullptr;
            return nullptr;
        }
    }
    ++ModuleRefs();
    return Module();
}

void ModuleRelease() {
    std::lock_guard<std::mutex> lk(ModuleMu());
    if (ModuleRefs() > 0 && --ModuleRefs() == 0) {
        NvSciBufModuleClose(Module());
        Module() = nullptr;
    }
}

// --- 一条传输(一个端点)的层 1 状态 ---------------------------------------------
//
// 键 = SharedFrameDesc::transport(通道交出来的 NvSciIpcEndpoint)。producer 与
// consumer 在各自进程里各持一份,以端点为键 —— 多通道 = 多端点 = 多份状态。
struct Transport {
    bool producer = false;
    bool handshaked = false;
    NvSciBufAttrList reconciled = nullptr;  // consumer 侧:import 回来的 reconciled
    // producer 侧:对象 -> 下标;consumer 侧:下标 -> 已 import 的对象。
    std::unordered_map<void*, uint32_t> objToIndex;
    std::vector<void*> objects;
};

std::mutex& TransportsMu() {
    static std::mutex mu;
    return mu;
}
std::unordered_map<uint64_t, Transport>& Transports() {
    static std::unordered_map<uint64_t, Transport> t;
    return t;
}

// payload 编码:1 字节 mode + 1 字节保留 + 4 字节下标(+ mode==1 时跟 256 字节
// 导出描述符)。producer 与 consumer 共用一个文件,格式在此一处定义。
//
//   kModeCached = 0:消费者按下标取已缓存的对象,无附加数据。
//   kModeFresh  = 1:首次见到该对象,后面跟 NvSciBufObjIpcExportDescriptor。
constexpr size_t kPayloadHeader = 8;
constexpr uint8_t kModeCached = 0;
constexpr uint8_t kModeFresh = 1;

void PutHeader(std::vector<uint8_t>& out, uint8_t mode, uint32_t index) {
    out.resize(kPayloadHeader);
    out[0] = mode;
    out[1] = 0;
    std::memcpy(out.data() + 4, &index, sizeof(index));
}

bool GetHeader(const std::vector<uint8_t>& in, uint8_t& mode, uint32_t& index) {
    if (in.size() < kPayloadHeader) return false;
    mode = in[0];
    std::memcpy(&index, in.data() + 4, sizeof(index));
    return true;
}

// 消费端的 unreconciled 读 attrs:描述"我要读一个 NV12 图像",交给 producer 去
// 对账。字段与 camera_source_sipl.cc 的 FillNV12SurfaceAttrs 对齐(少写方独有的
// base-align/padding),权限收窄到 Readonly。
bool FillConsumerReadAttrs(NvSciBufAttrList list, int width, int height) {
    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);
    NvSciBufType bufType = NvSciBufType_Image;
    NvSciBufAttrValAccessPerm access = NvSciBufAccessPerm_Readonly;
    NvSciBufAttrValImageLayoutType layout = NvSciBufImage_BlockLinearType;
    uint32_t planeCount = 2;
    NvSciBufAttrValColorFmt colorFormat[2] = {NvSciColor_Y8, NvSciColor_V8U8};
    NvSciBufAttrValColorStd colorStd[2] = {NvSciColorStd_REC601_ER,
                                           NvSciColorStd_REC601_ER};
    uint32_t planeWidth[2] = {w, w >> 1};
    uint32_t planeHeight[2] = {h, h >> 1};
    bool vprFlag = false;
    NvSciBufAttrValImageScanType scanType = NvSciBufScan_ProgressiveType;
    NvSciBufAttrKeyValuePair attrs[] = {
        {NvSciBufGeneralAttrKey_RequiredPerm, &access, sizeof(access)},
        {NvSciBufGeneralAttrKey_Types, &bufType, sizeof(bufType)},
        {NvSciBufImageAttrKey_Layout, &layout, sizeof(layout)},
        {NvSciBufImageAttrKey_PlaneCount, &planeCount, sizeof(planeCount)},
        {NvSciBufImageAttrKey_PlaneColorFormat, &colorFormat,
         planeCount * sizeof(NvSciBufAttrValColorFmt)},
        {NvSciBufImageAttrKey_PlaneColorStd, &colorStd,
         planeCount * sizeof(NvSciBufAttrValColorStd)},
        {NvSciBufImageAttrKey_PlaneWidth, &planeWidth,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneHeight, &planeHeight,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_VprFlag, &vprFlag, sizeof(vprFlag)},
        {NvSciBufImageAttrKey_ScanType, &scanType, sizeof(scanType)},
    };
    return NvSciBufAttrListSetAttrs(list, attrs, sizeof(attrs) / sizeof(attrs[0]))
           == NvSciError_Success;
}

// --- 定长/变长 blob 的收发:先发 size_t,再发体(rawstream 的两段协议) ----------
bool SendBlob(NvSciIpcEndpoint ep, const void* buf, size_t len) {
    size_t off = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&len);
    while (off < sizeof(len)) {
        uint32_t wrote = 0;
        NvSciError err = NvSciIpcWriteSafe(ep, p + off,
                                           static_cast<uint32_t>(sizeof(len) - off),
                                           &wrote);
        if (err != NvSciError_Success) return false;
        off += wrote;
    }
    off = 0;
    p = static_cast<const uint8_t*>(buf);
    while (off < len) {
        uint32_t wrote = 0;
        NvSciError err = NvSciIpcWriteSafe(ep, p + off,
                                           static_cast<uint32_t>(len - off), &wrote);
        if (err != NvSciError_Success) return false;
        off += wrote;
    }
    return true;
}

bool RecvBlob(NvSciIpcEndpoint ep, std::vector<uint8_t>& out) {
    size_t len = 0;
    size_t off = 0;
    uint8_t* p = reinterpret_cast<uint8_t*>(&len);
    while (off < sizeof(len)) {
        uint32_t got = 0;
        NvSciError err = NvSciIpcReadSafe(ep, p + off,
                                          static_cast<uint32_t>(sizeof(len) - off),
                                          &got);
        if (err != NvSciError_Success) return false;
        off += got;
    }
    out.resize(len);
    off = 0;
    while (off < len) {
        uint32_t got = 0;
        NvSciError err = NvSciIpcReadSafe(ep, out.data() + off,
                                          static_cast<uint32_t>(len - off), &got);
        if (err != NvSciError_Success) return false;
        off += got;
    }
    return true;
}

// producer 侧一次性握手:收消费者的读 attrs -> 对象自己的 reconciled list 导出给
// 消费者。对象尚未出现时无法取 reconciled,故握手推迟到第一帧(见 Export)。
bool ProducerHandshake(Transport& t, NvSciIpcEndpoint ep, NvSciBufObj obj) {
    std::vector<uint8_t> consumerDesc;
    if (!RecvBlob(ep, consumerDesc)) {
        std::cerr << "frame_share_sipl: recv consumer attrs failed\n";
        return false;
    }
    NvSciBufAttrList consumerAttrs = nullptr;
    NvSciBufModule mod = ModuleAcquire();
    if (!mod) return false;
    NvSciError err = NvSciBufAttrListIpcImportUnreconciled(
        mod, ep, consumerDesc.data(), consumerDesc.size(), &consumerAttrs);
    if (err != NvSciError_Success) {
        std::cerr << "frame_share_sipl: import consumer attrs failed (0x"
                  << std::hex << static_cast<uint32_t>(err) << std::dec << ")\n";
        ModuleRelease();
        return false;
    }
    // 消费者 attrs 只用于满足"反向导出"前提,import 完即可释放。
    NvSciBufAttrListFree(consumerAttrs);

    // 对象自身已 reconcile 的 attr list 是权威的:这批 buffer 由 SIPL+ISP 注册。
    NvSciBufAttrList reconciled = nullptr;
    err = NvSciBufObjGetAttrList(obj, &reconciled);
    if (err != NvSciError_Success) {
        std::cerr << "frame_share_sipl: NvSciBufObjGetAttrList failed (0x"
                  << std::hex << static_cast<uint32_t>(err) << std::dec << ")\n";
        ModuleRelease();
        return false;
    }
    void* descBuf = nullptr;
    size_t descLen = 0;
    err = NvSciBufAttrListIpcExportReconciled(reconciled, ep, &descBuf, &descLen);
    NvSciBufAttrListFree(reconciled);
    if (err != NvSciError_Success) {
        std::cerr << "frame_share_sipl: export reconciled attrs failed (0x"
                  << std::hex << static_cast<uint32_t>(err) << std::dec
                  << ") — 若为 NotPermitted,需按 rawstream 补上 producer 侧"
                     " unreconciled write attrs 的双向导出\n";
        ModuleRelease();
        return false;
    }
    bool ok = SendBlob(ep, descBuf, descLen);
    NvSciBufAttrListFreeDesc(descBuf);
    if (!ok) {
        std::cerr << "frame_share_sipl: send reconciled attrs failed\n";
        ModuleRelease();
        return false;
    }
    t.handshaked = true;
    return true;
}

// consumer 侧一次性握手:把自己的读 attrs 送出去 -> 收回 reconciled list。
bool ConsumerHandshake(Transport& t, NvSciIpcEndpoint ep, const SharedFrameDesc& d) {
    NvSciBufModule mod = ModuleAcquire();
    if (!mod) return false;
    NvSciBufAttrList readAttrs = nullptr;
    if (NvSciBufAttrListCreate(mod, &readAttrs) != NvSciError_Success ||
        !readAttrs) {
        std::cerr << "frame_share_sipl: NvSciBufAttrListCreate failed\n";
        ModuleRelease();
        return false;
    }
    int w = d.width > 0 ? d.width : 1920;
    int h = d.height > 0 ? d.height : 1080;
    if (!FillConsumerReadAttrs(readAttrs, w, h)) {
        std::cerr << "frame_share_sipl: FillConsumerReadAttrs failed\n";
        NvSciBufAttrListFree(readAttrs);
        ModuleRelease();
        return false;
    }
    void* descBuf = nullptr;
    size_t descLen = 0;
    NvSciError err = NvSciBufAttrListIpcExportUnreconciled(
        &readAttrs, 1, ep, &descBuf, &descLen);
    NvSciBufAttrListFree(readAttrs);
    if (err != NvSciError_Success) {
        std::cerr << "frame_share_sipl: export read attrs failed (0x"
                  << std::hex << static_cast<uint32_t>(err) << std::dec << ")\n";
        ModuleRelease();
        return false;
    }
    bool ok = SendBlob(ep, descBuf, descLen);
    NvSciBufAttrListFreeDesc(descBuf);
    if (!ok) {
        std::cerr << "frame_share_sipl: send read attrs failed\n";
        ModuleRelease();
        return false;
    }

    std::vector<uint8_t> combinedDesc;
    if (!RecvBlob(ep, combinedDesc)) {
        std::cerr << "frame_share_sipl: recv reconciled attrs failed\n";
        ModuleRelease();
        return false;
    }
    // 消费端不额外附加 unreconciled 输入(NULL/0 合法,见 nvscibuf.h:5795)。
    err = NvSciBufAttrListIpcImportReconciled(
        mod, ep, combinedDesc.data(), combinedDesc.size(), nullptr, 0,
        &t.reconciled);
    if (err != NvSciError_Success) {
        std::cerr << "frame_share_sipl: import reconciled attrs failed (0x"
                  << std::hex << static_cast<uint32_t>(err) << std::dec << ")\n";
        ModuleRelease();
        return false;
    }
    t.handshaked = true;
    return true;
}

// --- 层 1 的两端 ---------------------------------------------------------------

bool ExportSiplFrame(const CodecFrame& frame, SharedFrameDesc& desc) {
    const NvSciIpcEndpoint ep = static_cast<NvSciIpcEndpoint>(desc.transport);
    if (ep == 0) {
        std::cerr << "frame_share_sipl: no transport on desc (endpoint 0)\n";
        return false;
    }
    if (frame.locality != FrameLocality::NvSciBufObj ||
        frame.device.nvSciBufObj == nullptr) {
        return false;
    }
    NvSciBufObj obj = static_cast<NvSciBufObj>(frame.device.nvSciBufObj);

    std::lock_guard<std::mutex> lk(TransportsMu());
    Transport& t = Transports()[desc.transport];
    t.producer = true;
    if (!t.handshaked && !ProducerHandshake(t, ep, obj)) return false;

    uint32_t index = 0;
    auto it = t.objToIndex.find(obj);
    if (it != t.objToIndex.end()) {
        index = it->second;
        PutHeader(desc.payload, kModeCached, index);
        desc.locality = FrameLocality::NvSciBufObj;
        return true;
    }

    // 首见该对象:分配下标并把导出描述符随本帧带过去(定长 256 字节)。
    index = static_cast<uint32_t>(t.objects.size());
    NvSciBufObjIpcExportDescriptor objDesc;
    NvSciError err = NvSciBufObjIpcExport(obj, NvSciBufAccessPerm_ReadWrite, ep,
                                         &objDesc);
    if (err != NvSciError_Success) {
        std::cerr << "frame_share_sipl: NvSciBufObjIpcExport failed (0x"
                  << std::hex << static_cast<uint32_t>(err) << std::dec << ")\n";
        return false;
    }
    t.objects.push_back(obj);
    t.objToIndex[obj] = index;
    PutHeader(desc.payload, kModeFresh, index);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&objDesc);
    desc.payload.insert(desc.payload.end(), p, p + sizeof(objDesc));
    desc.locality = FrameLocality::NvSciBufObj;
    return true;
}

bool ImportSiplFrame(const SharedFrameDesc& desc, CodecFrame& frame) {
    const NvSciIpcEndpoint ep = static_cast<NvSciIpcEndpoint>(desc.transport);
    if (ep == 0) {
        std::cerr << "frame_share_sipl: no transport on desc (endpoint 0)\n";
        return false;
    }
    uint8_t mode = 0;
    uint32_t index = 0;
    if (!GetHeader(desc.payload, mode, index)) {
        std::cerr << "frame_share_sipl: malformed payload\n";
        return false;
    }

    std::lock_guard<std::mutex> lk(TransportsMu());
    Transport& t = Transports()[desc.transport];
    if (!t.handshaked && !ConsumerHandshake(t, ep, desc)) return false;

    if (mode == kModeCached) {
        if (index >= t.objects.size()) {
            std::cerr << "frame_share_sipl: cached index " << index
                      << " out of range\n";
            return false;
        }
    } else if (mode == kModeFresh) {
        if (desc.payload.size() < kPayloadHeader + sizeof(NvSciBufObjIpcExportDescriptor)) {
            std::cerr << "frame_share_sipl: truncated fresh descriptor\n";
            return false;
        }
        NvSciBufObjIpcExportDescriptor objDesc;
        std::memcpy(&objDesc, desc.payload.data() + kPayloadHeader, sizeof(objDesc));
        NvSciBufObj obj = nullptr;
        NvSciError err = NvSciBufObjIpcImport(ep, &objDesc, t.reconciled,
                                              NvSciBufAccessPerm_Readonly,
                                              1000, &obj);
        if (err != NvSciError_Success) {
            std::cerr << "frame_share_sipl: NvSciBufObjIpcImport failed (0x"
                      << std::hex << static_cast<uint32_t>(err) << std::dec
                      << ")\n";
            return false;
        }
        if (index > t.objects.size()) {
            std::cerr << "frame_share_sipl: fresh index " << index
                      << " out of order\n";
            NvSciBufObjFree(obj);
            return false;
        }
        if (index == t.objects.size()) t.objects.push_back(obj);
        else t.objects[index] = obj;
    } else {
        std::cerr << "frame_share_sipl: unknown payload mode\n";
        return false;
    }

    frame.locality = FrameLocality::NvSciBufObj;
    frame.device.nvSciBufObj = t.objects[index];
    // 对象由本 backend 缓存、随端点状态存活到通道关闭;帧本身不持有它,故 release
    // 为空(与"每帧新建再释放"的方案相反,见文件头)。
    frame.release = []() {};
    return true;
}

// 通道关闭时调用(端点即将失效):释放本进程为该传输缓存的对象与 reconciled
// attr list。必须在 NvSciIpcCloseEndpointSafe 之前,否则 import/reconcile 态的
// 清账可能引用已关闭的端点。
void CloseSiplTransport(uint64_t transport) {
    Transport t;
    {
        std::lock_guard<std::mutex> lk(TransportsMu());
        auto it = Transports().find(transport);
        if (it == Transports().end()) return;
        t = std::move(it->second);
        Transports().erase(it);
    }
    for (void* obj : t.objects) {
        if (obj) NvSciBufObjFree(static_cast<NvSciBufObj>(obj));
    }
    if (t.reconciled) NvSciBufAttrListFree(t.reconciled);
    if (t.handshaked) ModuleRelease();
}

void RegisterSiplFrameShare() {
    RegisterFrameShare(FrameLocality::NvSciBufObj, ExportSiplFrame, ImportSiplFrame);
    // 传输态(缓存的对象 + reconciled attr list)以传输句柄为键存活到通道关闭。
    RegisterFrameTransportClose(FrameLocality::NvSciBufObj, CloseSiplTransport);
}

struct SiplFrameShareRegistrar {
    SiplFrameShareRegistrar() { RegisterSiplFrameShare(); }
};
SiplFrameShareRegistrar g_siplFrameShareRegistrar;

}  // namespace

}  // namespace halcodec
