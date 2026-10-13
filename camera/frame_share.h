#ifndef CAMERA_FRAME_SHARE_H
#define CAMERA_FRAME_SHARE_H

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "frame.h"

namespace halcodec {

// 层 1:BufferSharer —— 把一个设备帧的显存句柄序列化成可跨进程重建的描述符,
// 以及反向的重建。与 src/frame.cc 的 DownloadToHost hook 模型平行:frame_share
// 本身不链接任何厂商库,由产出设备的 backend(NvSciBuf 等)在 static-init 时
// 注册一对 export/import 函数,按 FrameLocality 分发。
//
// 职责边界:
//   - frame_share 只做"句柄 <-> 字节"的纯变换,不管理缓冲生命周期、不做像素拷贝
//     (Host 帧的像素传输由 FrameChannel 负责)。
//   - 缓冲的所有权/归还(引用计数、return-fence)由 FrameRouter + FrameChannel
//     建模,见 camera/frame_router.h。
//
// 一个 export descriptor 只能 import 一次(NvSciBuf 契约),故 router 对每个消费者
// 各调用一次 ExportSharedFrame;广播复用同一条 buffer 时,消费者各自持一份 descriptor。

// 可跨进程传输的帧描述符。厂商中立:payload 承载产出平台需要的一切字节
// (NvSciBufObjIpcExportDescriptor 等);Host 帧的像素不在 payload 里,由
// FrameChannel 的共享内存槽承载。
struct SharedFrameDesc {
    FrameLocality locality = FrameLocality::Host;
    PixelFormat format = PixelFormat::Unknown;
    int width = 0;
    int height = 0;
    std::array<size_t, 4> strides{};
    int64_t pts = 0;
    uint64_t sequence = 0;        // producer 分配,单调递增
    std::vector<uint8_t> payload; // 设备句柄字节(NvSciBuf/CUDA/VPL)
};

// 导出:产出帧 -> 可传输描述符(填充 payload)。返回 false 表示该 locality 未注册
// 或导出失败。
using FrameExportFn = std::function<bool(const CodecFrame&, SharedFrameDesc&)>;
// 导入:描述符 -> 本进程可用的设备帧(重建 device 句柄,locality 保持设备类)。
using FrameImportFn = std::function<bool(const SharedFrameDesc&, CodecFrame&)>;

// Backend 在 static-init 时安装某一 locality 的导出/导入路径。导出到
// halcodec_core(default visibility),使编进 hal_cam 的 camera/*.cc 能注册。
void RegisterFrameShare(FrameLocality locality, FrameExportFn e, FrameImportFn i);

// 按 frame.locality 分发。未注册的 locality 返回 false。
bool ExportSharedFrame(const CodecFrame& frame, SharedFrameDesc& desc);
bool ImportSharedFrame(const SharedFrameDesc& desc, CodecFrame& frame);

// 是否已为某个 locality 注册(用于工厂挑通道/判定能力)。
bool FrameShareRegistered(FrameLocality locality);

} // namespace halcodec

#endif // CAMERA_FRAME_SHARE_H
