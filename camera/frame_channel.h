#ifndef CAMERA_FRAME_CHANNEL_H
#define CAMERA_FRAME_CHANNEL_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "frame_share.h"

namespace camera {

// 层 2:FrameChannel —— 一帧数据从 producer 流向单个 consumer 的通道。
// 平台无关接口;三种 backend:
//   LoopbackChannel     进程内(单进程多消费者):共享堆槽 + 引用计数。用于框架
//                       自检与不跨进程的多路读取。
//   UnixFdChannel       Linux:POSIX 共享内存环 + AF_UNIX 传 SCM_RIGHTS fd。
//   NvSciStreamChannel  DRIVE-OS(SIPL):NvSciStream Multicast + IpcSrc/IpcDst,
//                       设备帧零拷贝(buffer 与 fence 由 NvSciStream 管理)。
//
// 每消费者一条通道:多消费者 = FrameRouter 持有 N 条通道(见 frame_router.h),
// 而不是单通道内部扇出。这样 backend 只需处理点对点,扇出策略集中在 router。
//
// 像素归属:通道负责像素的搬运与生命周期,SharedFrameDesc 只带元数据。
//   - Host 通道:producer 用 AcquireHostSlot() 拿可写槽填像素,consumer 经
//     ReceivedFrame::hostData() 直接读该槽(零拷贝);槽在消费者释放 ReceivedFrame
//     前不被覆写(引用计数)。
//   - 设备通道:producer 先 ExportSharedFrame() 填 desc.payload,consumer 侧
//     ReceivedFrame::desc() 经 ImportSharedFrame() 还原成设备句柄。

// consumer 取到的一帧:持有像素槽/设备句柄,析构即归还(归还 = 引用计数减一)。
class ReceivedFrame {
public:
    virtual ~ReceivedFrame() = default;
    virtual const halcodec::SharedFrameDesc& desc() const = 0;
    // Host 通道:指向共享槽中已填充的像素;设备通道:nullptr。
    virtual uint8_t* hostData() { return nullptr; }
    virtual size_t hostSize() { return 0; }
};

class FrameChannel {
public:
    virtual ~FrameChannel() = default;

    // 建立连接。producer 侧创建/绑定端点;consumer 侧连接。false = 失败(已打印)。
    virtual bool Open() = 0;

    // producer, Host 通道:取一块可写槽填充本帧像素(容量 >= bytes)。返回 nullptr
    // 表示当前无空闲槽(慢消费者积压)——router 按策略丢弃本帧。
    // 设备通道与 consumer 侧返回 nullptr。
    virtual uint8_t* AcquireHostSlot(size_t bytes) { (void)bytes; return nullptr; }

    // producer:提交本帧(desc 描述上一步填的槽或设备句柄)。非阻塞。
    virtual bool Publish(const halcodec::SharedFrameDesc& desc) = 0;

    // consumer:阻塞取下一帧(可带超时;关闭时返回 nullptr)。
    virtual std::unique_ptr<ReceivedFrame> Subscribe() = 0;

    // 关闭并释放端点(幂等)。
    virtual void Close() = 0;

    // 本通道承载的帧 locality(Host / NvSciBufObj 等)。
    virtual halcodec::FrameLocality locality() const = 0;

    // 通道标识(端点名),用于日志与协商。
    virtual const std::string& name() const = 0;
};

// 构造一条通道所需的全部参数。producer/consumer 用同名共享一条路径。
struct ChannelConfig {
    std::string name;                                        // 端点名(唯一)
    bool producer = true;                                    // 角色
    halcodec::FrameLocality locality = halcodec::FrameLocality::Host;
    int width = 0, height = 0;                               // Host 槽需要几何
    halcodec::PixelFormat format = halcodec::PixelFormat::NV12;
    int numSlots = 4;                                        // Host 槽数量
};

// 各 backend 注册自己的构造器(kind: "loopback" / "unixfd" / "nvscistream")。
// 工厂按 kind 选择,便于 --channel-kind 切换。
using FrameChannelFactoryFn =
    std::function<std::unique_ptr<FrameChannel>(const ChannelConfig&)>;
void RegisterFrameChannelFactory(const std::string& kind, FrameChannelFactoryFn);
std::unique_ptr<FrameChannel> CreateFrameChannel(const std::string& kind,
                                                 const ChannelConfig& cfg);
bool FrameChannelFactoryRegistered(const std::string& kind);

} // namespace camera

#endif // CAMERA_FRAME_CHANNEL_H
