#ifndef SRC_CAMERA_SOURCE_H
#define SRC_CAMERA_SOURCE_H

#include "frame.h"

namespace halcodec {

// 统一相机源抽象,将三类相机采集(SIPL / V4L2 / AVFoundation)背后的差异收敛到同一接口:
//   - SIPL(DRIVE-OS,NvSciBufObj 零拷贝):Grab 返回 locality=NvSciBufObj 的帧,
//     缓冲归 SIPL 所有,通过 frame.release 回调归还(同步 nvmedia encoder 消费后,
//     由捕获循环在 FillFrame 返回后触发 item->Release)。
//   - V4L2(Linux USB/UVC):YUYV→NV12 CPU 转换后返回 host NV12;缓冲归源内部
//     所有,下次 Grab/Stop 前有效,release 为空。
//   - AVFoundation(macOS):420YpCbCr8BiPlanarVideoRange(=NV12)host 帧;base
//     address 在 Grab 时锁定,release 回调负责解锁并归还采样缓冲(vtenc 在
//     FillFrame 内立即拷贝,故 FillFrame 返回后即可解锁)。
//
// Grab 契约:帧缓冲归 source 所有,有效期为从 Grab 返回到捕获循环调用
// frame.release(若设置)或下一次 Grab()/Stop()。调用方不得跨 Grab 保留 data 指针。
class CameraSource {
public:
    CameraSource() = default;
    virtual ~CameraSource() = default;

    // 打开设备并协商采集格式。成功后 width()/height() 可用,但尚未开始出帧。
    // 对 SIPL:完成每路 pipeline 的 SetPipelineCfg(必须先于平台 Init 全部就绪)。
    virtual bool Open() = 0;

    // 第二阶段:在全部源的 Open() 完成之后、首次 Start() 之前调用,分配并注册
    // 捕获缓冲。对 SIPL:平台 Init + RegisterImages + EOF sync(Init 须在所有
    // SetPipelineCfg 之后);对 V4L2/AVFoundation 为空实现。
    virtual bool Prepare() = 0;

    // 开始出帧。对 SIPL:首个源的 Start 触发平台级 Start(所有源已注册完图像)。
    virtual bool Start() = 0;

    // 请求停止出帧:捕获线程退出信号,由调用方在 join 线程前调用。Grab 应在
    // 一个阻塞期内探测到后尽快返回 false(如 SIPL 5s 超时、V4L2 轮询)。
    // 设备资源仍由 Stop() 释放;RequestStop 不触碰缓冲,可安全与 Grab 并发。
    virtual void RequestStop() = 0;

    // 阻塞取下一帧并填入 frame(PixelFormat::NV12)。false 表示采集错误或流结束。
    // 成功后 frame.release(若设置)由捕获循环在编码器消费完(或填充失败)后调用。
    virtual bool Grab(CodecFrame& frame) = 0;

    // 停止出帧并释放设备资源(队列线程已停止后调用)。
    virtual void Stop() = 0;

    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
};

} // namespace halcodec

#endif // SRC_CAMERA_SOURCE_H