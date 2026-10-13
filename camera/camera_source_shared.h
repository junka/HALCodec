#ifndef CAMERA_SOURCE_SHARED_H
#define CAMERA_SOURCE_SHARED_H

#include <cstdint>
#include <memory>

#include "camera_source.h"
#include "frame_channel.h"

namespace camera {

// 层 3 消费端:把"通过 FrameChannel 订阅到的流"伪装成一个普通 CameraSource。
// 于是 hal_cam 的捕获循环(Grab → FillFrame)对"本地相机"和"别的进程共享来的流"
// 完全一致——订阅进程就是一个普通 hal_cam,只是 --source shared。
//
// Host 通道:Grab 直接把共享槽像素交给编码器(该槽在 frame.release 触发前只读有效)。
// 设备通道:Grab 经 ImportSharedFrame 还原设备句柄,零拷贝交给后端编码器。
//
// 几何由生产者在通道协商时给出(ChannelConfig.width/height),故 width()/height()
// 在 Open 之前即有效——hal_cam 需要在 Start 前用它初始化编码器。
class SharedFrameSource : public halcodec::CameraSource {
public:
    SharedFrameSource(std::shared_ptr<FrameChannel> ch, uint32_t w, uint32_t h)
        : ch_(std::move(ch)), w_(w), h_(h) {}

    bool Open() override;
    bool Prepare() override { return true; }  // 订阅无需缓冲注册
    bool Start() override { return opened_; }
    void RequestStop() override;
    bool Grab(halcodec::CodecFrame& frame) override;
    void Stop() override;

    uint32_t width() const override { return w_; }
    uint32_t height() const override { return h_; }

private:
    std::shared_ptr<FrameChannel> ch_;
    uint32_t w_ = 0, h_ = 0;
    bool opened_ = false;
};

} // namespace camera

#endif // CAMERA_SOURCE_SHARED_H
