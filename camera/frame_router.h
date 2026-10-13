#ifndef CAMERA_FRAME_ROUTER_H
#define CAMERA_FRAME_ROUTER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "camera_source.h"
#include "frame_channel.h"

namespace camera {

// 层 3:FrameRouter —— 平台无关的扇出策略。持有一路 CameraSource(采集),
// 在其自己的捕获线程里 Grab 一帧后:
//   1) 交给本地 sink(进程内消费者,如同进程编码器),同步消费;
//   2) 对每条 FrameChannel 发布一份帧描述符(进程/域外消费者);
//   3) 触发采集帧的 release。
// 消费者数量、快慢、是否跨进程,都不影响采集循环——慢消费者按通道内策略丢帧。
class FrameRouter {
public:
    // 本地 sink:收到 host 像素(设备帧会被 DownloadToHost 后再交;调用方只读,
    // 返回后 router 即释放采集帧)。camId 用于多路区分。
    using LocalSink = std::function<void(uint32_t camId, const halcodec::CodecFrame&)>;

    explicit FrameRouter(std::unique_ptr<halcodec::CameraSource> capture)
        : capture_(std::move(capture)) {}
    ~FrameRouter() { Stop(); }

    // 添加一条对外通道(不进 Open,由 router 在 Start 前统一 Open)。
    void AddChannel(std::shared_ptr<FrameChannel> ch);
    // 添加一个本地 sink。
    void AddLocalSink(LocalSink sink);

    void setCamId(uint32_t id) { camId_ = id; }

    // 打开全部通道并启动捕获线程,返回是否至少一帧可采。设备资源由 CameraSource
    // 的 Open/Prepare 在构造前完成(工厂内),这里只驱动 Start。
    bool Start();

    // 请求停止(捕获线程退出信号),join 线程,再析构底层源。幂等。
    void Stop();

    uint64_t framesCaptured() const { return frames_.load(); }
    uint32_t width() const { return capture_ ? capture_->width() : 0; }
    uint32_t height() const { return capture_ ? capture_->height() : 0; }
    halcodec::CameraSource* source() { return capture_.get(); }

private:
    void Loop();

    std::unique_ptr<halcodec::CameraSource> capture_;
    std::vector<std::shared_ptr<FrameChannel>> channels_;
    std::vector<LocalSink> localSinks_;
    uint32_t camId_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_{0};
    std::atomic<uint64_t> sequence_{0};
};

} // namespace camera

#endif // CAMERA_FRAME_ROUTER_H
