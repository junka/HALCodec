// hal_cam — 统一相机采集→编码应用(归并 hal_camera / hal_camera_v4l2)。
//
// 通过 --source 选择采集源(SIPL / V4L2 / AVFoundation),三源共享同一套
// "每源捕获线程 + 共享输出队列 + main 写文件" 结构与 Encoder 泛型接口:
//
//   CaptureUnit(源 + encoder + 线程):Grab → 填 pts → FillFrame → (release)
//   → 同步 encoder 则 DrainPackets;结束后 FillFrame(eos) + SignalInputComplete
//   + DrainPackets。
//
// 数据通路:
//   - sipl:  DRIVE-OS ISP0 NvSciBufObj → nvmediaenc 零拷贝(必须 -B nvmedia)
//   - v4l2:  Linux USB/UVC(YUYV→NV12 CPU 转换)→ host NV12 → qsvenc 等
//   - av:    macOS AVFoundation(420YpCbCr8BiPlanarVideoRange=NV12)→ host NV12
//              → vtenc 等(默认 vtenc)
//
// Usage:
//   hal_cam --source av -B vtenc -c h264 -o out.h264 --bitrate 4000 --fps 30
//           --duration-frames 300
//   hal_cam --source v4l2 --device /dev/video0 --input-format yuyv -c h264 ...
//   hal_cam --source sipl --num-cameras 2 --sensor-index 0 -B nvmedia -o out.h264 ...
// 多相机输出:out_cam0.h264, out_cam1.h264, ...(由 -o 派生)。
//
// 退出:SIGINT/SIGTERM 优雅停止;--duration-frames N 编码满后自动退出。

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>

#include "camera_source.h"
#include "camera_source_factory.h"
#include "encoder.h"
#include "encode_config.h"
#include "parse_cli.h"
#include "plugin_loader.h"

#include "codec_config.h"
#include "frame.h"

namespace {

struct EncodedPacket {
    uint32_t camId = 0;
    std::vector<uint8_t> bytes;
};

// 每路相机 = 一个 CameraSource + 一个 Encoder + 一个捕获线程。编码输出经共享
// outQ 交给 main 线程写文件。
struct CaptureUnit {
    uint32_t camId = 0;
    int durationFrames = 0;
    std::unique_ptr<halcodec::CameraSource> src;
    std::unique_ptr<halcodec::Encoder> enc;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> threadAlive{true};   // 线程自然退出后由 main 检测
    std::atomic<uint64_t> framesEncoded{0};
    std::string error;

    // 共享输出队列(所有相机共用一个进程级队列,main 线程统一写文件)。
    std::mutex* outMu = nullptr;
    std::condition_variable* outCv = nullptr;
    std::queue<EncodedPacket>* outQ = nullptr;
};

// 把当前可取的编码包排入共享队列,返回包数。
int DrainPackets(CaptureUnit& c) {
    int n = 0;
    halcodec::CodecFrame out;
    while (c.enc->GetFrame(out)) {
        halcodec::DownloadToHost(out);
        EncodedPacket pkt;
        pkt.camId = c.camId;
        if (out.data && out.size > 0) {
            pkt.bytes.assign(out.data, out.data + out.size);
        }
        if (out.release) out.release();
        {
            std::lock_guard<std::mutex> lk(*c.outMu);
            c.outQ->push(std::move(pkt));
        }
        c.outCv->notify_one();
        ++n;
    }
    return n;
}

void CaptureThread(CaptureUnit& c) {
    int64_t pts = 0;
    while (c.running.load()) {
        if (c.durationFrames > 0 &&
            c.framesEncoded.load() >= static_cast<uint64_t>(c.durationFrames)) {
            break;
        }
        halcodec::CodecFrame frame;
        if (!c.src->Grab(frame)) {
            // 采集错误或 RequestStop 后退出;Stop 前不置 error。
            if (c.running.load()) c.error = "Grab failed";
            break;
        }
        frame.pts = pts++;
        // 同步 encoder 的 FillFrame 返回即消费完毕;异步 encoder(vtenc/qsvenc)
        // 在 FillFrame 内立即拷贝入内部缓冲,故填帧后调用 release 归还帧缓冲。
        if (!c.enc->FillFrame(frame)) {
            std::cerr << "hal_cam: cam " << c.camId << " FillFrame failed at frame "
                      << c.framesEncoded.load() << "\n";
            c.error = "FillFrame failed";
            if (frame.release) frame.release();
            break;
        }
        if (frame.release) frame.release();
        c.framesEncoded.fetch_add(1);
        if (!c.enc->isAsync()) DrainPackets(c);
    }
    // 冲刷尾部包:EOS + 全部收尾。
    halcodec::CodecFrame eos;
    eos.width = c.src->width();
    eos.height = c.src->height();
    eos.format = halcodec::PixelFormat::NV12;
    c.enc->FillFrame(eos);
    c.enc->SignalInputComplete();
    DrainPackets(c);
    c.threadAlive.store(false);
}

std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop.store(true); }

} // namespace

int main(int argc, char* argv[]) {
    ::signal(SIGINT, OnSignal);
    ::signal(SIGTERM, OnSignal);

    CommandLineParser cli;
    cli.parse(argc, argv);

    halcodec::LoadBackends();

    // --- 源选择:按平台编译进哪些源;默认取优先级最高的(sipl > v4l2 > av) ---
    struct SourceEntry {
        const char* name;
        const char* defaultEncoder;
        bool (*create)(const CommandLineParser&,
                       std::vector<std::unique_ptr<halcodec::CameraSource>>&);
    };
    const SourceEntry available[] = {
#if defined(HAL_CAM_HAVE_SIPL)
        {"sipl", "nvmedia", camera::CreateSIPL},
#endif
#if defined(HAL_CAM_HAVE_V4L2)
        {"v4l2", "qsvenc", camera::CreateV4L2},
#endif
#if defined(HAL_CAM_HAVE_AV)
        {"av", "vtenc", camera::CreateAVF},
#endif
    };
    const int numAvailable =
        static_cast<int>(sizeof(available) / sizeof(available[0]));
    if (numAvailable == 0) {
        std::cerr << "hal_cam: no camera source compiled in for this platform\n";
        return 1;
    }

    std::string source = cli.getSource();
    const SourceEntry* entry = nullptr;
    if (source.empty()) {
        entry = &available[0];  // 平台原生默认源
    } else {
        for (int i = 0; i < numAvailable; ++i) {
            if (source == available[i].name) {
                entry = &available[i];
                break;
            }
        }
        if (!entry) {
            std::cerr << "hal_cam: unknown or unsupported --source '"
                      << source << "' (this build supports:";
            for (int i = 0; i < numAvailable; ++i) {
                std::cerr << " " << available[i].name;
            }
            std::cerr << ")\n";
            return 1;
        }
    }
    source = entry->name;

    // --- Encoder 后端:默认按源,`-B qsv` 别名兼容 qsvenc ---
    std::string encoderBackend = cli.getEncoderBackend();
    if (encoderBackend.empty()) encoderBackend = entry->defaultEncoder;
    if (encoderBackend == "qsv") encoderBackend = "qsvenc";
    if (source == "sipl" && encoderBackend != "nvmedia") {
        // 零拷贝 NvSciBuf 帧只能被 nvmedia(基于 NvMedia IEP)消费。
        std::cerr << "hal_cam: --source sipl requires -B nvmedia (got '"
                  << encoderBackend << "')\n";
        return 1;
    }

    // --- 构造相机源(工厂内完成 Open + Prepare) ---
    std::vector<std::unique_ptr<halcodec::CameraSource>> sources;
    if (!entry->create(cli, sources) || sources.empty()) {
        std::cerr << "hal_cam: camera source '" << source << "' setup failed\n";
        return 1;
    }

    // --- 每源一个 CaptureUnit:Open 后的尺寸初始化 encoder ---
    std::vector<std::unique_ptr<CaptureUnit>> units;
    units.reserve(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        auto u = std::make_unique<CaptureUnit>();
        u->camId = static_cast<uint32_t>(i);
        u->durationFrames = cli.getDurationFrames();
        u->src = std::move(sources[i]);

        auto enc = halcodec::Encoder::Create(encoderBackend);
        if (!enc) {
            std::cerr << "hal_cam: Encoder::Create(" << encoderBackend
                      << ") failed for cam " << i << "\n";
            return 1;
        }
        halcodec::CodecParams params;
        params.codec = cli.getCodec();
        params.deviceIndex = cli.getGpuIndex();
        params.width = u->src->width();
        params.height = u->src->height();
        params.inputFormat = halcodec::PixelFormat::NV12;
        if (source == "sipl") params.zeroCopy = true;
        if (!cli.getEncodeConfigFile().empty()) {
            LoadEncodeConfig(cli.getEncodeConfigFile(), params.encode);
        }
        if (!cli.getRateControl().empty()) params.encode.rateControl = cli.getRateControl();
        if (cli.getBitrateKbps() >= 0)    params.encode.bitrateKbps = cli.getBitrateKbps();
        if (cli.getMaxBitrateKbps() >= 0) params.encode.maxBitrateKbps = cli.getMaxBitrateKbps();
        if (cli.getQp() >= 0)             params.encode.qp = cli.getQp();
        if (cli.getGopLength() >= 0)      params.encode.gopLength = cli.getGopLength();
        if (cli.getNumBFrames() >= 0)     params.encode.numBFrames = cli.getNumBFrames();
        if (cli.getFps() > 0)             params.encode.frameRateNum = cli.getFps();
        if (!cli.getProfile().empty())    params.encode.profile = cli.getProfile();
        if (!cli.getLevel().empty())      params.encode.level = cli.getLevel();
        if (cli.getLowDelay())            params.encode.lowDelay = true;
        if (!enc->Initialize(params)) {
            std::cerr << "hal_cam: encoder Initialize failed for cam "
                      << i << " (" << encoderBackend << ")\n";
            return 1;
        }
        u->enc = std::move(enc);
        units.push_back(std::move(u));
    }

    // --- 输出文件(多相机派生 _camN) ---
    std::vector<std::ofstream> outs(units.size());
    auto deriveName = [&](int i) -> std::string {
        const std::string& o = cli.getOutputFile();
        if (units.size() == 1) return o.empty() ? "out.h264" : o;
        std::string base = o.empty() ? "out.h264" : o;
        size_t dot = base.find_last_of('.');
        if (dot == std::string::npos) {
            return base + "_cam" + std::to_string(i);
        }
        return base.substr(0, dot) + "_cam" + std::to_string(i) + base.substr(dot);
    };
    for (size_t i = 0; i < units.size(); ++i) {
        outs[i].open(deriveName(static_cast<int>(i)),
                     std::ios::out | std::ios::binary);
        if (!outs[i]) {
            std::cerr << "hal_cam: cannot open output for cam " << i << "\n";
            return 1;
        }
    }

    // --- 共享输出队列 ---
    std::mutex outMu;
    std::condition_variable outCv;
    std::queue<EncodedPacket> outQ;
    for (auto& u : units) {
        u->outMu = &outMu;
        u->outCv = &outCv;
        u->outQ = &outQ;
    }

    // --- Start:全部源启动后开线程(SIPL 首个 Start 触发平台级 Start) ---
    for (auto& u : units) {
        if (!u->src->Start()) {
            std::cerr << "hal_cam: cam " << u->camId << " Start failed\n";
            return 1;
        }
    }
    std::cout << "hal_cam: '" << source << "' streaming, " << units.size()
              << " camera(s) → " << encoderBackend << "\n";
    for (auto& u : units) {
        u->running.store(true);
        u->thread = std::thread(CaptureThread, std::ref(*u));
    }

    // --- main:排水写文件,直到 g_stop 或全部线程自然结束 ---
    uint64_t totalPackets = 0;
    auto allThreadsFinished = [&]() {
        for (auto& u : units) {
            if (u->threadAlive.load()) return false;
        }
        return true;
    };
    while (!g_stop.load()) {
        std::unique_lock<std::mutex> lk(outMu);
        outCv.wait_for(lk, std::chrono::milliseconds(200),
                       [&] { return !outQ.empty(); });
        while (!outQ.empty()) {
            EncodedPacket pkt = std::move(outQ.front());
            outQ.pop();
            lk.unlock();
            if (pkt.camId < outs.size() && !pkt.bytes.empty()) {
                outs[pkt.camId].write(
                    reinterpret_cast<const char*>(pkt.bytes.data()),
                    pkt.bytes.size());
                ++totalPackets;
            }
            lk.lock();
        }
        if (allThreadsFinished() && outQ.empty()) break;
    }

    // --- Teardown:停线程 → encoder Finalize → 源 Stop(释放设备) ---
    for (auto& u : units) {
        u->running.store(false);
        u->src->RequestStop();
    }
    for (auto& u : units) {
        if (u->thread.joinable()) u->thread.join();
    }
    // 最终排水(残余包)。
    {
        std::lock_guard<std::mutex> lk(outMu);
        while (!outQ.empty()) {
            EncodedPacket pkt = std::move(outQ.front());
            outQ.pop();
            if (pkt.camId < outs.size() && !pkt.bytes.empty()) {
                outs[pkt.camId].write(
                    reinterpret_cast<const char*>(pkt.bytes.data()),
                    pkt.bytes.size());
                ++totalPackets;
            }
        }
    }
    for (auto& u : units) {
        if (u->enc) u->enc->Finalize();
    }
    for (auto& u : units) {
        if (u->src) u->src->Stop();
    }

    // --- 汇总 ---
    for (size_t i = 0; i < units.size(); ++i) {
        uint64_t n = units[i]->framesEncoded.load();
        std::cout << "hal_cam: cam " << i << " encoded " << n << " frames";
        if (!units[i]->error.empty()) {
            std::cout << " (" << units[i]->error << ")";
        }
        std::cout << "\n";
    }
    std::cout << "hal_cam: total packets written: " << totalPackets << "\n";
    return 0;
}