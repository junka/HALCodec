#ifndef LAYERS_VTENCODER_H_
#define LAYERS_VTENCODER_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <VideoToolbox/VideoToolbox.h>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

namespace halcodec {
namespace vtbox {

// VideoToolbox 异步编码器(vtenc)。与 VTDecoder 对称:压缩回调驱动,输出
// 进队列,GetFrame() 阻塞等待;isAsync() = true。
class VTEncoder : public Encoder {
public:
    VTEncoder() = default;
    ~VTEncoder() override;

    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& in) override;
    bool SignalInputComplete() override;
    bool isAsync() const override { return true; }
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;

    // JPEG 是图像编码器:每个 sample 都是一条完整 JFIF 码流,首尾相接写进
    // 同一个文件就读不出来了,所以一帧对应一个输出文件。
    bool oneOutputFilePerFrame() const override {
        return codecType_ == kCMVideoCodecType_JPEG;
    }

    std::string getName() const override { return "vtenc"; }

private:
    VTCompressionSessionRef session_ = nullptr;

    // 已编码输出队列,由压缩回调填充、GetFrame() 消费。mtx_/cv_ 保护。
    std::deque<CodecFrame> outQueue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool eof_ = false;

    int64_t frameIndex_ = 0;   // 已提交帧数,用作 PTS 来源
    int fpsN_ = 30, fpsD_ = 1; // 帧率,默认 30fps
    int width_ = 0, height_ = 0;
    CMVideoCodecType codecType_ = kCMVideoCodecType_H264;
    PixelFormat inputFormat_ = PixelFormat::I420;

    // 压缩回调:把 CMSampleBuffer 转成 Annex-B 后推入输出队列。
    static void CompressionCallback(void* refcon,
                                    void* sourceFrameRefCon,
                                    OSStatus status,
                                    VTEncodeInfoFlags infoFlags,
                                    CMSampleBufferRef sampleBuffer);

    // 把 length-prefixed 的 AVCC 访问单元(关键帧含参数集)转成 Annex-B
    // 元素流;JPEG 则原样透出一条 JFIF 码流。
    void emitSample(CMSampleBufferRef sampleBuffer, bool isKeyframe);

    // 把一条已编码的码流拷成独立缓冲推入输出队列,PTS 取自 sample。
    void queueEncoded(const std::vector<uint8_t>& bytes,
                      CMSampleBufferRef sampleBuffer);

    // 把宿主内存 I420/NV12/P010 帧逐行拷入池中像素缓冲(lib 按行对齐)。
    bool copyFrameToPixelBuffer(const CodecFrame& in,
                                CVPixelBufferRef pixelBuffer);
};

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_VTENCODER_H_