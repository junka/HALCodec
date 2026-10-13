// frame_router.cc — 层 3 扇出策略(见 camera/frame_router.h)。
//
// 一路采集 → 本地 sink(进程内,如写文件)+ N 条 FrameChannel(进程/域外)。
// 平台无关:像素的零拷贝与否由通道决定——设备帧(Host 之外的 locality)对每条通道
// 调 ExportSharedFrame 拿设备句柄描述符;Host 帧走通道的共享槽,由 router 把像素
// 拷进槽(槽本身零拷贝给消费者)。
//
// 生命周期:采集帧在每个本地 sink 调用后、每次通道发布前被"复制或导出";全部分发
// 完毕后 router 触发 frame.release 归还采集缓冲(采集帧不再被任何下游持有)。

#include "frame_router.h"

#include <cstring>
#include <iostream>
#include <utility>

namespace camera {

void FrameRouter::AddChannel(std::shared_ptr<FrameChannel> ch) {
    if (ch) channels_.push_back(std::move(ch));
}

void FrameRouter::AddLocalSink(LocalSink sink) {
    if (sink) localSinks_.push_back(std::move(sink));
}

bool FrameRouter::Start() {
    if (!capture_) return false;
    for (auto& ch : channels_) {
        if (!ch->Open()) {
            std::cerr << "frame_router: channel '" << ch->name()
                      << "' open failed\n";
            return false;
        }
    }
    if (!capture_->Start()) return false;
    running_.store(true);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void FrameRouter::Stop() {
    if (!running_.exchange(false)) {
        // 尚未 Start 或已 Stop:仍要释放源设备资源(构造时已 Open/Prepare)。
        if (capture_) capture_->Stop();
        return;
    }
    if (capture_) capture_->RequestStop();
    if (thread_.joinable()) thread_.join();
    for (auto& ch : channels_) ch->Close();
    if (capture_) capture_->Stop();
}

void FrameRouter::Loop() {
    while (running_.load()) {
        halcodec::CodecFrame frame;
        if (!capture_->Grab(frame)) {
            if (running_.load()) std::cerr << "frame_router: Grab failed\n";
            break;
        }
        const uint64_t seq = sequence_.fetch_add(1);

        // --- 本地 sink:保证 host 像素(设备帧按需下载)。---
        for (auto& sink : localSinks_) {
            halcodec::CodecFrame view = frame;  // 浅拷贝;release 仅在下方触发一次
            view.release = nullptr;             // sink 不得释放采集缓冲
            if (view.locality != halcodec::FrameLocality::Host) {
                if (!halcodec::DownloadToHost(view)) {
                    // 该平台无下载路径(如未加载 nvmedia backend):跳过本 sink。
                }
            }
            sink(camId_, view);
        }

        // --- 通道:Host 帧走共享槽,设备帧走 ExportSharedFrame。---
        for (auto& ch : channels_) {
            halcodec::SharedFrameDesc desc;
            desc.locality = frame.locality;
            desc.format = frame.format;
            desc.width = frame.width;
            desc.height = frame.height;
            desc.strides = frame.strides;
            desc.pts = frame.pts;
            desc.sequence = seq;

            if (frame.locality == halcodec::FrameLocality::Host) {
                if (!frame.data) continue;
                const size_t bytes = frame.size ? frame.size
                    : static_cast<size_t>(frame.width) * frame.height * 3 / 2;
                uint8_t* slot = ch->AcquireHostSlot(bytes);
                if (!slot) continue;  // 消费者积压:丢本帧(通道内策略)
                // Host 帧按 tightly-packed NV12 拷进槽(不同步处理 stride 差异;
                // 设备帧的 stride 信息随 desc 携带)。
                std::memcpy(slot, frame.data, bytes);
                desc.locality = halcodec::FrameLocality::Host;
                ch->Publish(desc);
            } else {
                if (!halcodec::ExportSharedFrame(frame, desc)) continue;
                ch->Publish(desc);
            }
        }

        ++frames_;
        if (frame.release) frame.release();
    }
}

} // namespace camera
