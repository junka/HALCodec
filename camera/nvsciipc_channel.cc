// nvsciipc_channel.cc — DRIVE-OS 跨进程通道,层 2 的一个 backend(SIPL 设备帧)。
//
// 用途:SIPL 相机把 ISP0 的 NvSciBufObj 交给多个消费者进程零拷贝处理。这条通道
// 自身只搬"控制面":句柄(层 1 的 export descriptor)与元数据;像素一字节不搬,
// 消费者拿到的就是 SIPL 注册进 ISP 的同一块 buffer。
//
// 为什么不是 NvSciStream:NvSciStream 的 pool 自持 buffer,要把 SIPL RegisterImages
// 的那批 buffer 接进去只能 device→device 拷一份,零拷贝就没了。所以这里走
// NvSciIpc + NvSciBufObjIpcExport 这条路(与 SDK 的 rawstream 样例同构):
//   - 通道在 Open() 时打开自己的 NvSciIpc 端点(名字 = channel name,须在
//     /etc/nvsciipc.cfg 里成对声明);
//   - 层 1 的 frame_share_sipl 在这个端点上交换 NvSciBufAttrList(一次)与
//     NvSciBufObjIpcExportDescriptor(每个对象一次);
//   - 通道只负责把 desc.payload 送到对端,并把对端送来的 payload 装进 ReceivedFrame。
//
// 端点与多通道:NvSciIpc 端点名在进程内只能开一次(重开返回 Busy),而名字是
// 对称的一对(producer 开 _0、consumer 开 _1 是同一条路径的两个端)。因此本通道
// 把 ChannelConfig::name 里的 "k" 解析成端点下标 pair:偶数 = producer 侧名,
// 奇数 = consumer 侧名。多消费者 = 多对端点名(见 --channel-name cam0/cam1/...),
// 与"每消费者一条通道"的设计一致,不做端点内复用。
//
// 首帧协商:层 1 在第一条帧上做一次 attr list 交换 —— 消费端把读 attrs 送到
// producer,producer 把对象自身 reconciled 的 attr list 送回(反向规则见
// nvscibuf.h:NvSciBufObjIpcExport 的 NotPermitted 前提)。之后每帧只带一个对象
// 下标,通道对此完全透明,只搬 desc.payload 的字节。
//
// 平台:仅 DRIVE-OS(aarch64/NvMedia SDK)。非该平台不编译本文件,kind "nvscipc"
// 不存在,工厂会明确报错。

#include "frame_channel.h"

#include "nvscibuf.h"
#include "nvsciipc.h"

#include <cerrno>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

namespace camera {

namespace {

// 通道控制消息:定长头 + 变长 payload。NvSciIpc 本身是字节流,没有消息边界,
// 故这里用 "头 + 体" 两段写、两段读的方式自己划帧。头里的 magic/magic^1 做
// 双向校验,防止把对端残留字节当消息解析。
constexpr uint32_t kMagic = 0x48504d43U;  // "HPMC"
constexpr uint32_t kMagicOk = kMagic ^ 0xffffffffU;

constexpr uint8_t kMsgFrame = 1;  // producer → consumer:一帧
constexpr uint8_t kMsgClose = 2;  // producer → consumer:流结束

#pragma pack(push, 1)
struct MsgHeader {
    uint32_t magic;
    uint32_t tail;       // 定长头自校验
    uint32_t payloadLen; // 后面跟的字节数
    uint8_t type;        // kMsgFrame / kMsgClose
    uint8_t flags;       // bit0: payload 是设备句柄导出描述符
    int64_t pts;
    uint64_t sequence;
    int32_t width;
    int32_t height;
};
#pragma pack(pop)

bool HeaderValid(const MsgHeader& h) {
    return h.magic == kMagic && h.tail == kMagicOk;
}

MsgHeader MakeHeader(uint8_t type, uint8_t flags, size_t payloadLen,
                     const halcodec::SharedFrameDesc* desc) {
    MsgHeader h{};
    h.magic = kMagic;
    h.tail = kMagicOk;
    h.payloadLen = static_cast<uint32_t>(payloadLen);
    h.type = type;
    h.flags = flags;
    if (desc) {
        h.pts = desc->pts;
        h.sequence = desc->sequence;
        h.width = desc->width;
        h.height = desc->height;
    }
    return h;
}

// 一次 IPCSend 的启动开销明显大于收尾,小的握手包(如 attr list 描述符)会很多。
// NvSciIpcGetEventSafe 在事务未满时会自动提交,故这里不做缓冲,一枚消息一次写。

// NvSciIpcInit/NvSciIpcDeinit 是进程级的一对:库本身没有引用计数,且 Deinit 要求
// "所有端点已关闭"。一个进程里可以有多条通道(多消费者 = 多对端点),故这里自己
// 计数,只在第一个通道 Open 时 Init、最后一个通道 Close 时 Deinit。
std::mutex& IpcModuleMu() {
    static std::mutex mu;
    return mu;
}
int& IpcModuleRefs() {
    static int refs = 0;
    return refs;
}

bool IpcModuleAcquire() {
    std::lock_guard<std::mutex> lk(IpcModuleMu());
    if (IpcModuleRefs() == 0) {
        NvSciError err = NvSciIpcInit();
        if (err != NvSciError_Success) {
            std::cerr << "nvsciipc: NvSciIpcInit failed (0x" << std::hex
                      << static_cast<uint32_t>(err) << std::dec << ")\n";
            return false;
        }
    }
    ++IpcModuleRefs();
    return true;
}

void IpcModuleRelease() {
    std::lock_guard<std::mutex> lk(IpcModuleMu());
    if (IpcModuleRefs() > 0 && --IpcModuleRefs() == 0) {
        NvSciIpcDeinit();
    }
}
}  // namespace

// consumer 侧收到的一帧:持有从 IpcRead 读回的描述符字节;像素/设备句柄的还原
// 由层 1(ImportSharedFrame)按 desc.payload 完成。
class NvSciIpcReceived : public ReceivedFrame {
public:
    NvSciIpcReceived(halcodec::SharedFrameDesc desc) : desc_(std::move(desc)) {}
    const halcodec::SharedFrameDesc& desc() const override { return desc_; }

private:
    halcodec::SharedFrameDesc desc_;
};

class NvSciIpcChannel : public FrameChannel {
public:
    NvSciIpcChannel(const ChannelConfig& cfg)
        : name_(cfg.name),
          producer_(cfg.producer),
          width_(cfg.width),
          height_(cfg.height),
          format_(cfg.format) {}

    ~NvSciIpcChannel() override { Close(); }

    bool Open() override {
        if (open_) return true;

        // NvSciIpcInit 是进程级一次性的(见 IpcModuleAcquire):多通道时只初始化
        // 一次,Deinit 推迟到最后一个通道关闭。
        if (!IpcModuleAcquire()) return false;
        inited_ = true;

        // 端点名:producer 用 ChannelConfig::name 原样,consumer 用同名 ——
        // nvsciipc.cfg 里成对声明的那两个名字就是"同一条路径的两端",两侧各自
        // 开自己那一个,不需要在这里推导奇偶。名字不对(没在 cfg 里)时
        // OpenEndpoint 返回 NvSciError_NoSuchEntry;被别人占用返回 Busy。
        NvSciError err = NvSciIpcOpenEndpoint(name_.c_str(), &ep_);
        if (err != NvSciError_Success) {
            std::cerr << "nvsciipc: NvSciIpcOpenEndpoint('" << name_
                      << "') failed (0x" << std::hex
                      << static_cast<uint32_t>(err) << std::dec << ")";
            if (err == NvSciError_NoSuchEntry) {
                std::cerr << " — endpoint not declared in /etc/nvsciipc.cfg";
            } else if (err == NvSciError_Busy) {
                std::cerr << " — endpoint already open in this process";
            }
            std::cerr << "\n";
            Close();
            return false;
        }

        NvSciIpcEndpointInfo info{};
        err = NvSciIpcGetEndpointInfo(ep_, &info);
        if (err != NvSciError_Success) {
            std::cerr << "nvsciipc: NvSciIpcGetEndpointInfo failed (0x"
                      << std::hex << static_cast<uint32_t>(err) << std::dec
                      << ")\n";
            Close();
            return false;
        }
        maxFrameBytes_ = static_cast<size_t>(info.frame_size);

        err = NvSciIpcGetLinuxEventFd(ep_, &eventFd_);
        if (err != NvSciError_Success) {
            std::cerr << "nvsciipc: NvSciIpcGetLinuxEventFd failed (0x"
                      << std::hex << static_cast<uint32_t>(err) << std::dec
                      << ")\n";
            Close();
            return false;
        }
        // 事件 fd 是非阻塞的,配合 select 使用。
        int fl = ::fcntl(eventFd_, F_GETFL, 0);
        if (fl >= 0) ::fcntl(eventFd_, F_SETFL, fl | O_NONBLOCK);

        err = NvSciIpcResetEndpointSafe(ep_);
        if (err != NvSciError_Success) {
            std::cerr << "nvsciipc: NvSciIpcResetEndpointSafe failed (0x"
                      << std::hex << static_cast<uint32_t>(err) << std::dec
                      << ")\n";
            Close();
            return false;
        }

        open_ = true;
        return true;
    }

    // 设备通道:producer 侧不需要 host 槽;像素由 SIPL 的 buffer 直接经层 1 导出。
    uint8_t* AcquireHostSlot(size_t) override { return nullptr; }

    bool Publish(const halcodec::SharedFrameDesc& desc) override {
        if (!producer_ || !open_) return false;
        const uint8_t flags = 0;  // 预留:payload 类型标记
        const MsgHeader h = MakeHeader(kMsgFrame, flags, desc.payload.size(),
                                       &desc);
        // 头 + 体是一枚逻辑消息,头尾两段写撑爆事务大小时后段会静默截断,
        // 故这里先自行拦下(设备句柄描述符 256B,远超正常帧大小的配置才是问题)。
        if (maxFrameBytes_ != 0 &&
            sizeof(h) + desc.payload.size() > maxFrameBytes_) {
            std::cerr << "nvsciipc: frame exceeds endpoint transaction size ("
                      << sizeof(h) + desc.payload.size() << " > "
                      << maxFrameBytes_ << "); check /etc/nvsciipc.cfg\n";
            return false;
        }
        if (!WriteAll(&h, sizeof(h))) return false;
        if (!desc.payload.empty() &&
            !WriteAll(desc.payload.data(), desc.payload.size())) {
            return false;
        }
        return true;
    }

    std::unique_ptr<ReceivedFrame> Subscribe() override {
        if (producer_ || !open_) return nullptr;
        while (true) {
            MsgHeader h{};
            // 阻塞等一条完整头(Close 时由 stop_ 打断 select)。
            if (!ReadAll(&h, sizeof(h))) return nullptr;
            if (!HeaderValid(h)) {
                std::cerr << "nvsciipc: bad header from '" << name_ << "'\n";
                return nullptr;
            }
            if (h.type == kMsgClose) return nullptr;

            halcodec::SharedFrameDesc desc;
            desc.locality = halcodec::FrameLocality::NvSciBufObj;
            desc.format = format_;
            desc.width = h.width;
            desc.height = h.height;
            desc.pts = h.pts;
            desc.sequence = h.sequence;
            // desc.transport 由消费者侧(层 1 入口)填本通道的端点句柄,通道自身
            // 不写它,避免 channel 层知道层 1 的表示。
            if (h.payloadLen > 0) {
                desc.payload.resize(h.payloadLen);
                if (!ReadAll(desc.payload.data(), h.payloadLen)) return nullptr;
            }
            return std::make_unique<NvSciIpcReceived>(std::move(desc));
        }
    }

    void Close() override {
        stop_ = true;  // 打断可能正在 select 的读线程
        if (ep_ != 0) {
            // 端点还活着时先让层 1 释放它为这条传输缓存的对象/attr list(端点关闭
            // 后再清账会引用已失效的句柄)。未注册该 locality 时是空操作。
            halcodec::CloseSharedTransport(halcodec::FrameLocality::NvSciBufObj,
                                           static_cast<uint64_t>(ep_));
            // clear=false:清 buffer 的语义在 producer/consumer 上不同,这里只
            // 关闭本端;重置流的责任在重连时的 ResetEndpointSafe。
            NvSciIpcCloseEndpointSafe(ep_, false);
            ep_ = 0;
        }
        if (inited_) {
            IpcModuleRelease();
            inited_ = false;
        }
        eventFd_ = -1;
        open_ = false;
    }

    halcodec::FrameLocality locality() const override {
        return halcodec::FrameLocality::NvSciBufObj;
    }
    const std::string& name() const override { return name_; }
    uint64_t transportHandle() const override { return ep_; }

private:
    bool WriteAll(const void* buf, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(buf);
        size_t off = 0;
        while (off < size) {
            uint32_t want = static_cast<uint32_t>(size - off);
            uint32_t wrote = 0;
            NvSciError err = NvSciIpcWriteSafe(ep_, p + off, want, &wrote);
            if (err != NvSciError_Success) {
                std::cerr << "nvsciipc: write failed (0x" << std::hex
                          << static_cast<uint32_t>(err) << std::dec << ")\n";
                return false;
            }
            off += wrote;
            if (wrote == 0) {
                // 写侧缓冲满:等 WRITE 事件(可能等不到对端,给个超时兜底)。
                if (!WaitEvent(NV_SCI_IPC_EVENT_WRITE, 1000)) return false;
            }
        }
        return true;
    }

    bool ReadAll(void* buf, size_t size) {
        uint8_t* p = static_cast<uint8_t*>(buf);
        size_t off = 0;
        while (off < size) {
            uint32_t want = static_cast<uint32_t>(size - off);
            uint32_t got = 0;
            NvSciError err = NvSciIpcReadSafe(ep_, p + off, want, &got);
            if (err == NvSciError_NoData) {
                if (!WaitEvent(NV_SCI_IPC_EVENT_READ, 200)) {
                    if (stop_) return false;
                    continue;  // 超时:重试(Close 会置 stop_)
                }
                continue;
            }
            if (err != NvSciError_Success) {
                std::cerr << "nvsciipc: read failed (0x" << std::hex
                          << static_cast<uint32_t>(err) << std::dec << ")\n";
                return false;
            }
            off += got;
        }
        return true;
    }

    // 在事件 fd 上 select,事件到后用 GetEventSafe 取回并清位。timeoutMs<=0 表示
    // 无限等(仍会被 Close() 的 stop_ + 200ms 轮询打断,见 ReadAll)。
    bool WaitEvent(uint32_t want, int timeoutMs) {
        const int fd = eventFd_;
        if (fd < 0) return false;
        for (;;) {
            if (stop_) return false;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            timeval tv{};
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            int pr = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            // 即使 select 超时也试着取一次事件:事件可能在 fd 可读与清位之间
            // 被 GetEventSafe 顺手清掉,漏等一次会造成假死。
            uint32_t events = 0;
            NvSciError err = NvSciIpcGetEventSafe(ep_, &events);
            if (err == NvSciError_Success && (events & want)) return true;
            if (err != NvSciError_Success && err != NvSciError_NoData) {
                return false;
            }
            if (pr == 0) return false;  // 真超时
        }
    }

    std::string name_;
    bool producer_ = true;
    int width_ = 0, height_ = 0;
    halcodec::PixelFormat format_ = halcodec::PixelFormat::NV12;

    NvSciIpcEndpoint ep_ = 0;
    int eventFd_ = -1;
    size_t maxFrameBytes_ = 0;
    bool inited_ = false;
    bool open_ = false;
    std::atomic<bool> stop_{false};
};

void RegisterNvSciIpcChannel() {
    RegisterFrameChannelFactory(
        "nvscipc", [](const ChannelConfig& cfg) -> std::unique_ptr<FrameChannel> {
            return std::make_unique<NvSciIpcChannel>(cfg);
        });
}

namespace {
struct NvSciIpcRegistrar {
    NvSciIpcRegistrar() { RegisterNvSciIpcChannel(); }
};
NvSciIpcRegistrar g_nvsciipcRegistrar;
}  // namespace

}  // namespace camera
