// camera_source_shared.cc — 层 3 消费端:FrameChannel -> CameraSource 适配。
// 见 camera/camera_source_shared.h。平台无关,编进 hal_cam。

#include "camera_source_shared.h"

#include <iostream>
#include <memory>
#include <utility>

#include "camera_source_factory.h"

namespace camera {

bool SharedFrameSource::Open() {
    if (!ch_) return false;
    if (!ch_->Open()) {
        std::cerr << "shared_source: channel open failed\n";
        return false;
    }
    opened_ = true;
    return true;
}

void SharedFrameSource::RequestStop() {
    // 关闭通道以唤醒阻塞中的 Subscribe。幂等。
    if (ch_) ch_->Close();
}

bool SharedFrameSource::Grab(halcodec::CodecFrame& frame) {
    std::unique_ptr<ReceivedFrame> recv = ch_->Subscribe();
    if (!recv) return false;  // 通道关闭/超时

    const halcodec::SharedFrameDesc& d = recv->desc();
    frame = halcodec::CodecFrame{};
    frame.width = d.width;
    frame.height = d.height;
    frame.format = d.format;
    frame.strides = d.strides;
    frame.pts = d.pts;

    if (d.locality == halcodec::FrameLocality::Host) {
        frame.locality = halcodec::FrameLocality::Host;
        frame.data = recv->hostData();
        frame.size = recv->hostSize();
    } else {
        // 设备帧:经已注册的 import 还原设备句柄(零拷贝)。
        if (!halcodec::ImportSharedFrame(d, frame)) {
            std::cerr << "shared_source: ImportSharedFrame failed\n";
            return false;
        }
    }

    // 归还:上层消费完(同步编码器 FillFrame 返回后)触发;共享的 ReceivedFrame
    // 由 release 持有,析构时归还槽(引用计数减一)或释放设备句柄。
    auto shared = std::shared_ptr<ReceivedFrame>(std::move(recv));
    frame.release = [shared]() { /* 引用归零即触发 ~ReceivedFrame */ };
    return true;
}

void SharedFrameSource::Stop() {
    if (ch_) ch_->Close();
    opened_ = false;
}

// --- 层 3 消费端工厂(见 camera_source_factory.h) ---------------------------------
//
// 按 --channel-kind/--channel-name 建一条订阅通道,包成 SharedFrameSource。
// 几何来自 --width/--height(生产端与消费端在这一版靠 CLI 约定;nvscistream 版
// 会在 Open 时把几何写进描述符,不再需要)。每次调用建一条通道 → 多路订阅 = 多个
// -i/多个实例(见 hal_cam 的 --source shared 路由)。
bool CreateShared(const CommandLineParser& cli,
                  std::vector<std::unique_ptr<halcodec::CameraSource>>& out) {
    const std::string kind = cli.getChannelKind();
    if (kind.empty()) {
        std::cerr << "shared_source: --channel-kind is required\n";
        return false;
    }
    if (!FrameChannelFactoryRegistered(kind)) {
        std::cerr << "shared_source: no FrameChannel backend registered for kind '"
                  << kind << "'\n";
        return false;
    }
    const std::string name = cli.getChannelName();
    if (name.empty()) {
        std::cerr << "shared_source: --channel-name is required\n";
        return false;
    }
    const int w = cli.getWidth() > 0 ? cli.getWidth() : cli.getV4l2Width();
    const int h = cli.getHeight() > 0 ? cli.getHeight() : cli.getV4l2Height();

    ChannelConfig cfg;
    cfg.name = name;
    cfg.producer = false;
    cfg.locality = halcodec::FrameLocality::Host;
    cfg.width = w;
    cfg.height = h;
    cfg.numSlots = cli.getChannelSlots();

    std::shared_ptr<FrameChannel> ch = CreateFrameChannel(kind, cfg);
    if (!ch) {
        std::cerr << "shared_source: CreateFrameChannel('" << kind
                  << "', '" << name << "') failed\n";
        return false;
    }
    auto src = std::make_unique<SharedFrameSource>(ch, static_cast<uint32_t>(w),
                                                   static_cast<uint32_t>(h));
    if (!src->Open()) return false;
    out.push_back(std::move(src));
    return true;
}

} // namespace camera
