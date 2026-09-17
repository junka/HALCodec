#include <cuda.h>
#include <vector>
#include <algorithm>
#include <string>
#include <regex>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "nvencoder.h"

#include "frame.h"
#include "registry.h"

namespace halcodec {
namespace nvenc {

// NVENC encoding pipeline. Raw frames are delivered by the caller via
// FillFrame(const CodecFrame&); an internal worker thread copies each onto
// the device and submits it to NVENC, then queues the resulting elementary-
// stream packets for GetFrame() to drain. This keeps the async contract
// (isAsync() == true): FillFrame returns as soon as the frame is queued,
// GetFrame blocks until a packet is ready or the stream ends.

namespace {

// Deep-copies a raw input frame's bytes into a self-owned CodecFrame so the
// worker can read them after FillFrame returns (the caller's buffer may be
// reused or freed before the worker consumes it). A zero-size frame is the
// EOS marker, passed through as-is (data stays null).
CodecFrame DeepCopyInput(const CodecFrame& in) {
    CodecFrame c;
    c.width = in.width;
    c.height = in.height;
    c.format = in.format;
    c.size = in.size;
    c.pts = in.pts;
    if (in.size > 0) {
        c.data = static_cast<uint8_t*>(std::malloc(in.size));
        if (c.data) {
            std::memcpy(c.data, in.data, in.size);
            uint8_t* owned = c.data;
            c.release = [owned]() { std::free(owned); };
        }
    }
    return c;
}

// Copies an encoded access unit out of an NvEncOutputFrame into a malloc'd
// CodecFrame owned by the caller (release = std::free).
CodecFrame TakePacket(const NvEncOutputFrame& pkt, int w, int h) {
    CodecFrame f;
    f.size = pkt.frame.size();
    f.data = static_cast<uint8_t*>(std::malloc(f.size ? f.size : 1));
    if (f.data && f.size) {
        std::memcpy(f.data, pkt.frame.data(), f.size);
    }
    uint8_t* owned = f.data;
    f.release = [owned]() { std::free(owned); };
    f.width = w;
    f.height = h;
    f.format = PixelFormat::Unknown; // encoded elementary stream
    return f;
}

} // namespace

// PIMPL: worker thread pulls queued input frames, submits them to NVENC, and
// pushes encoded packets onto a queue for GetFrame.
class NVEncoder::AsyncPipe {
public:
    AsyncPipe(NvEncoderCuda* enc, CUcontext ctx)
        : encoder_(enc), cudaCtx_(ctx) {}

    ~AsyncPipe() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            eof_ = true;
            finished_ = true;
            cvInput_.notify_all();
            cvPackets_.notify_all();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        while (!inFrames_.empty()) {
            if (inFrames_.front().release) inFrames_.front().release();
            inFrames_.pop();
        }
        while (!packets_.empty()) {
            if (packets_.front().release) packets_.front().release();
            packets_.pop();
        }
    }

    void start() {
        worker_ = std::thread([this] { run(); });
    }

    void pushFrame(CodecFrame f) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            inFrames_.push(std::move(f));
        }
        cvInput_.notify_one();
    }

    void signalEOF() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            eof_ = true;
        }
        cvInput_.notify_one();
    }

    bool getFrame(CodecFrame& out) {
        std::unique_lock<std::mutex> lk(mu_);
        cvPackets_.wait(lk, [this] {
            return finished_ || workerError_ || !packets_.empty();
        });
        if (workerError_ && packets_.empty()) return false;
        if (packets_.empty()) return false;
        out = std::move(packets_.front());
        packets_.pop();
        return true;
    }

private:
    NvEncoderCuda* encoder_;
    CUcontext cudaCtx_ = nullptr;

    std::mutex mu_;
    std::condition_variable cvInput_;
    std::condition_variable cvPackets_;
    std::queue<CodecFrame> inFrames_;
    std::queue<CodecFrame> packets_;
    bool eof_ = false;
    bool finished_ = false;
    bool workerError_ = false;
    std::thread worker_;

    void run() {
        if (cudaCtx_) {
            cuCtxSetCurrent(cudaCtx_);
        }
        std::vector<NvEncOutputFrame> vPacket;
        while (true) {
            CodecFrame input;
            bool eos = false;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cvInput_.wait(lk, [this] {
                    return eof_ || workerError_ || !inFrames_.empty();
                });
                if (workerError_) return;
                if (!inFrames_.empty()) {
                    input = std::move(inFrames_.front());
                    inFrames_.pop();
                } else if (eof_) {
                    // Drain delayed frames via EndEncode, then finish.
                    eos = true;
                } else {
                    continue;
                }
            }

            if (eos) {
                vPacket.clear();
                encoder_->EndEncode(vPacket);
                std::lock_guard<std::mutex> lk(mu_);
                int w = encoder_->GetEncodeWidth();
                int h = encoder_->GetEncodeHeight();
                for (const auto& p : vPacket) {
                    packets_.push(TakePacket(p, w, h));
                }
                finished_ = true;
                cvPackets_.notify_all();
                return;
            }

            // EOS marker frame (size == 0) takes the drain path too.
            if (input.size == 0) {
                if (input.release) input.release();
                vPacket.clear();
                encoder_->EndEncode(vPacket);
                std::lock_guard<std::mutex> lk(mu_);
                int w = encoder_->GetEncodeWidth();
                int h = encoder_->GetEncodeHeight();
                for (const auto& p : vPacket) {
                    packets_.push(TakePacket(p, w, h));
                }
                finished_ = true;
                cvPackets_.notify_all();
                return;
            }

            const NvEncInputFrame* encoderInputFrame = encoder_->GetNextInputFrame();
            NvEncoderCuda::CopyToDeviceFrame(cudaCtx_,
                input.data, 0,
                (CUdeviceptr)encoderInputFrame->inputPtr,
                (int)encoderInputFrame->pitch,
                encoder_->GetEncodeWidth(),
                encoder_->GetEncodeHeight(),
                CU_MEMORYTYPE_HOST,
                encoderInputFrame->bufferFormat,
                encoderInputFrame->chromaOffsets,
                encoderInputFrame->numChromaPlanes);
            vPacket.clear();
            encoder_->EncodeFrame(vPacket);

            if (input.release) input.release();

            if (!vPacket.empty()) {
                std::lock_guard<std::mutex> lk(mu_);
                int w = encoder_->GetEncodeWidth();
                int h = encoder_->GetEncodeHeight();
                for (const auto& p : vPacket) {
                    packets_.push(TakePacket(p, w, h));
                }
                cvPackets_.notify_all();
            }
        }
    }
};

bool NVEncoder::Initialize(const CodecParams& params) {
    if (!cudaCtx_.create(params.deviceIndex)) {
        std::cerr << "NVEncoder: failed to create CUDA context for device "
                  << params.deviceIndex << std::endl;
        return false;
    }
    auto cudaCtx = cudaCtx_.get();
    int width = params.width;
    int height = params.height;
    if (width <= 0 || height <= 0) {
        std::string input = params.inputs.empty() ? "" : params.inputs[0];
        std::regex pattern(R"((\d+)[xX](\d+))");
        std::smatch match;
        if (std::regex_search(input, match, pattern)) {
            width = std::stoi(match[1].str());
            height = std::stoi(match[2].str());
        } else {
            std::cerr << "Failed to match resolution in: " << input << std::endl;
            return false;
        }
    }
    std::string format = "nv12";
    switch (params.inputFormat) {
        case PixelFormat::I420: format = "iyuv"; break;
        case PixelFormat::NV12: format = "nv12"; break;
        case PixelFormat::YUV444P: format = "yuv444"; break;
        default: break;
    }
    auto eFormat = [](std::string format) {
        std::vector<std::string> bufferFormatStr = {
            "iyuv", "nv12", "yv12", "yuv444", "p010", "yuv444p16", "bgra", "bgra10", "ayuv", "abgr", "abgr10",
#if NVENCAPI_MAJOR_VERSION > 12
            "nv16", "p210"
#endif
        };
        NV_ENC_BUFFER_FORMAT inFormat[] = {
            NV_ENC_BUFFER_FORMAT_IYUV,
            NV_ENC_BUFFER_FORMAT_NV12,
            NV_ENC_BUFFER_FORMAT_YV12,
            NV_ENC_BUFFER_FORMAT_YUV444,
            NV_ENC_BUFFER_FORMAT_YUV420_10BIT,
            NV_ENC_BUFFER_FORMAT_YUV444_10BIT,
            NV_ENC_BUFFER_FORMAT_ARGB,
            NV_ENC_BUFFER_FORMAT_ARGB10,
            NV_ENC_BUFFER_FORMAT_AYUV,
            NV_ENC_BUFFER_FORMAT_ABGR,
            NV_ENC_BUFFER_FORMAT_ABGR10,
#if NVENCAPI_MAJOR_VERSION > 12
            NV_ENC_BUFFER_FORMAT_NV16,
            NV_ENC_BUFFER_FORMAT_P210,
#endif
        };
        auto it = std::find(bufferFormatStr.begin(), bufferFormatStr.end(), format);
        if (it != bufferFormatStr.end()) {
            return inFormat[it - bufferFormatStr.begin()];
        }
        return NV_ENC_BUFFER_FORMAT_UNDEFINED;
    }(format);

#if NVENCAPI_MAJOR_VERSION > 12
    encoder_ = std::make_unique<NvEncoderCuda>(cudaCtx, width, height, eFormat, 3, false, false, false);
#else
    encoder_ = std::make_unique<NvEncoderCuda>(cudaCtx, width, height, eFormat);
#endif

    NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
    NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };

    initializeParams.encodeConfig = &encodeConfig;
    NvEncoderInitParam encodeCLIOptions("-codec h264 -preset p1 -tuninginfo hq -fps 1 -rc vbr -bitrate 10M");
    encoder_->CreateDefaultEncoderParams(&initializeParams, encodeCLIOptions.GetEncodeGUID(), encodeCLIOptions.GetPresetGUID(), encodeCLIOptions.GetTuningInfo());
    encodeCLIOptions.SetInitParams(&initializeParams, eFormat);

    encoder_->CreateEncoder(&initializeParams);
    pipe_ = std::make_unique<AsyncPipe>(encoder_.get(), cudaCtx);
    pipe_->start();
    std::cout << "NVEncoder: async session up, codec=h264 " << width << "x"
              << height << std::endl;
    return true;
}

void NVEncoder::Finalize() {
    pipe_.reset();  // joins the worker thread first
    if (encoder_) {
        encoder_->DestroyEncoder();
    }
}

NVEncoder::~NVEncoder() {
    Finalize();
}

std::string NVEncoder::getName() const {
    return "nvenc";
}

bool NVEncoder::FillFrame(const CodecFrame& in) {
    if (!pipe_) {
        return false;
    }
    // Deep-copy the input bytes: the worker reads them later, after the
    // caller's buffer may have been reused or freed.
    pipe_->pushFrame(DeepCopyInput(in));
    return true;
}

bool NVEncoder::SignalInputComplete() {
    if (!pipe_) {
        return false;
    }
    pipe_->signalEOF();
    return true;
}

bool NVEncoder::GetFrame(CodecFrame& out) {
    if (!pipe_) {
        return false;
    }
    return pipe_->getFrame(out);
}

HALCODEC_CONNECT(Encoder, nvenc, NVEncoder);

} // namespace nvenc
} // namespace halcodec