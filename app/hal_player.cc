// hal_player — 跨平台硬解播放器,HAL Decoder + SDL2 + OpenGL.
//
//   解码 : HAL Decoder 抽象 (vtbox/nvdec/qsvdec) — 平台无关 API.
//   渲染 : SDL2 + OpenGL (跨 macOS / Linux / Windows).
//   数据流: Annex-B → HAL Decoder::FillInput → GetFrame (CodecFrame)
//           → DownloadToHost (仅 device 内存帧) → NV12→BGRA (CPU) → GL texture
//
// 跨平台关键:
//   - 解码走 HAL 抽象层,后端由平台决定 (macOS=vtbox,Linux=qsvdec,Windows=nvdec).
//   - 渲染用 SDL2 + OpenGL,所有平台统一.
//   - NV12→BGRA 转换在 bench 模式下跳过 (纯吞吐不渲染).
//
// 编译:需要 SDL2 + OpenGL.framework (macOS)/libGL.so (Linux)/opengl32.lib (Windows).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "annexb.h"
#include "codec_config.h"
#include "codec_names.h"
#include "decoder.h"
#include "frame.h"
#include "output_paths.h"
#if defined(__linux__) || defined(__APPLE__)
#include "shm_ring.h"  // live input over a shm ring (shm:NAME)
#endif
#include "plugin_loader.h"
#include "yuv_to_bgra.h"

namespace {

// ---- 计时 ----
// macOS: mach_absolute_time 是纳秒 tick(mach_timebase_info 给出换算比).
// Linux/Windows: 统一用 POSIX CLOCK_MONOTONIC(纳秒) — NsToSec 即平凡换算.
#if defined(__APPLE__)
static inline uint64_t NowNs() { return mach_absolute_time(); }
static inline double NsToSec(uint64_t ns) {
    static mach_timebase_info_data_t info;
    static int once = (mach_timebase_info(&info), 1);
    (void)once;
    return (double)(ns * info.numer) / (double)info.denom / 1e9;
}
#else
static inline uint64_t NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline double NsToSec(uint64_t ns) { return (double)ns / 1e9; }
#endif

// 从 Annex-B 流中提取 SPS+PPS (H.264) 或 VPS+SPS+PPS (HEVC),输出 AVCC 格式
// [4-byte BE len][nalu][4-byte len][nalu]...  — VT/VTDecoder backend 要求.
//
// 扫描与 codec 判定都在 src/annexb.h:不能靠 forbidden_zero_bit (byte & 0x80)
// 区分 — H.264 和 HEVC 的 forbidden_zero_bit 都必须是 0!正确做法是看 type 值
// 范围: H.264 types 7(SPS),8(PPS),5(IDR),6(SEI) 不会出现在 HEVC;HEVC types
// 32(VPS),33(SPS),34(PPS),16-21 不会出现在 H.264.
//
// 返回检测到的 codec: 1=HEVC, 0=H.264, -1=未能判定.
// 本文件与 hal_dec 的差别只在策略:播放器要自己定 codec,所以必须
// 同时抓到 SPS 和 PPS 才认(否则交给下面的扩展名兜底).
static int ExtractAnnexBParamSets(const uint8_t* data, size_t size,
                                  std::vector<uint8_t>* out) {
    std::vector<halcodec::AnnexBNal> nals = halcodec::SplitAnnexB(data, size);
    halcodec::CodecHint codec = halcodec::DetectCodec(nals);
    unsigned mask = halcodec::ParamSetMask(nals, codec);
    if (codec != halcodec::CodecHint::kUnknown) {
        halcodec::AppendParamSetsAvcc(nals, codec, out);
    }
    int isHEVC = codec == halcodec::CodecHint::kHEVC ? 1 : 0;
    std::cerr << "[hal_player] AnnexB PS: isHEVC=" << isHEVC
              << " gotVPS=" << ((mask & halcodec::kVpsBit) ? 1 : 0)
              << " gotSPS=" << ((mask & halcodec::kSpsBit) ? 1 : 0)
              << " gotPPS=" << ((mask & halcodec::kPpsBit) ? 1 : 0)
              << " extradata=" << out->size() << " bytes\n";
    // 没抓到关键 PS → 交给 caller 的扩展名兜底
    if ((mask & (halcodec::kSpsBit | halcodec::kPpsBit)) !=
        (halcodec::kSpsBit | halcodec::kPpsBit)) return -1;
    return isHEVC;
}

// YUV → BGRA 转换移到 app/yuv_to_bgra.h:播放器消费的格式不止 NV12(见那里的
// 说明与 dispatch 表),转换按 frame.format 分派。原来的单格式版本只认半平面
// 8-bit 4:2:0,UV 基址在 renderFrame 里按 strides[0]*height 算 —— 平面格式
// (I420/YUV444P)在那一步就已经读错了。

struct Player {
    SDL_Window* window = nullptr;
    SDL_GLContext glCtx = nullptr;
    GLuint texBGRA = 0;
    int winW = 0, winH = 0;
    bool running = true;
    bool warnedFormat_ = false;

    ~Player() { shutdown(); }

    bool initWindow(int w, int h) {
        winW = w; winH = h;
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
            return false;
        }
        window = SDL_CreateWindow("hal_player (HAL Decoder + SDL2)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
            SDL_Quit(); return false;
        }
        glCtx = SDL_GL_CreateContext(window);
        if (!glCtx) {
            std::cerr << "SDL_GL_CreateContext: " << SDL_GetError() << "\n";
            SDL_DestroyWindow(window); window = nullptr; SDL_Quit(); return false;
        }
        // GL:上传 NV12→BGRA texture
        glGenTextures(1, &texBGRA);
        glBindTexture(GL_TEXTURE_2D, texBGRA);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        SDL_GL_SetSwapInterval(1);  // vsync
        std::cerr << "[hal_player] SDL2 window " << w << "x" << h
                  << " GL context OK\n";
        return true;
    }

    // 上传一帧。格式不支持就报一次(不每帧刷屏),不画——宁可黑屏也不要拿
    // 读错的像素冒充画面.
    void renderFrame(const halcodec::CodecFrame& frame, uint8_t* bgraBuf) {
        if (!halplayer::ConvertFrame(frame, bgraBuf)) {
            if (!warnedFormat_) {
                std::cerr << "[hal_player] cannot render format "
                          << halplayer::FormatName(frame.format)
                          << " (" << static_cast<int>(frame.format)
                          << ") — no conversion for it; frames shown as black\n";
                warnedFormat_ = true;
            }
            return;
        }
        glBindTexture(GL_TEXTURE_2D, texBGRA);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width, frame.height,
                     0, GL_BGRA, GL_UNSIGNED_BYTE, bgraBuf);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(-1, -1);
            glTexCoord2f(1, 1); glVertex2f( 1, -1);
            glTexCoord2f(1, 0); glVertex2f( 1,  1);
            glTexCoord2f(0, 0); glVertex2f(-1,  1);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        SDL_GL_SwapWindow(window);
    }

    bool pumpEvents() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return false;
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q))
                return false;
        }
        return true;
    }

    void shutdown() {
        if (texBGRA) { glDeleteTextures(1, &texBGRA); texBGRA = 0; }
        if (glCtx) { SDL_GL_DeleteContext(glCtx); glCtx = nullptr; }
        if (window) { SDL_DestroyWindow(window); window = nullptr; }
        SDL_Quit();
    }
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: hal_player [--backend NAME] [--bench] <file.h264|file.hevc|shm:NAME>\n"
                  << "  --backend  decoder backend (vtbox/nvdec/qsvdec); default: first available\n"
                  << "  --bench    no window,pure decode throughput\n"
                  << "  shm:NAME   live stream from a producer process (Linux/macOS;\n"
                  << "             app/hal_push.cc) -- the player hosts the shm ring +\n"
                  << "             wakeup channels, waits for the producer, bootstraps on\n"
                  << "             the stream's parameter sets, plays (macOS: name <= 21 chars)\n";
        return 1;
    }

    std::string path, backend;
    bool bench = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--bench") bench = true;
        else if (a == "--backend" && i + 1 < argc) backend = argv[++i];
        else path = a;
    }
    if (path.empty()) { std::cerr << "no input file\n"; return 1; }

    std::cerr << "[hal_player] input: " << path
              << "  backend=" << (backend.empty() ? "<first available>" : backend)
              << "  bench=" << (bench ? "yes" : "no") << "\n";

    // dlopen 所有 backend .so/.dylib
    halcodec::LoadBackends();

    // 选 backend
    if (backend.empty()) {
        auto names = halcodec::Registry<halcodec::Decoder>::Names();
        if (names.empty()) {
            std::cerr << "No decoder backend loaded.Check HALCODEC_BACKEND_DIR or link halcodec_stage_backends.\n";
            return 1;
        }
        backend = names.front();
    }
    auto dec = halcodec::Decoder::Create(backend);
    if (!dec) { std::cerr << "Create decoder '" << backend << "' failed\n"; return 1; }
    std::cerr << "[hal_player] backend: " << dec->getName() << "\n";

    // 输入:文件整读,或 shm:NAME 走共享内存活流(参数集从流头引导)。
    halcodec::CodecParams params;
    params.zeroCopy = true;  // 让 backend 优先 device memory (NV12 on GPU)
    std::vector<uint8_t> raw;
    bool live = false;
#if defined(__linux__) || defined(__APPLE__)
    std::thread feeder;                                   // live: feeds FillInput
    std::unique_ptr<halcodec::ShmRingHost> ring;          // live: the transport
    if (path.rfind("shm:", 0) == 0) {
        live = true;
        const std::string name = path.substr(4);
        constexpr uint64_t kRingBytes = 4ull << 20;  // 4 MiB ≈ 2 s of 4K HEVC
        ring = std::make_unique<halcodec::ShmRingHost>();
        std::string err;
        if (!ring->create(name, kRingBytes, &err) ||
            !ring->acceptProducer(&err)) {
            std::cerr << "[hal_player] shm ring: " << err << "\n";
            return 1;
        }
        if (!dec->isAsync()) {
            std::cerr << "shm live input needs an async decoder backend "
                         "(vtbox/nvdec/qsvdec)\n";
            return 1;
        }
        std::cerr << "[hal_player] waiting for producer on shm:" << name
                  << " ...\n";
        // Accumulate the stream head until the parameter sets are in: the
        // decoder needs extradata before Initialize, and an Annex-B stream
        // leads with them. Cap the bootstrap so a param-set-less stream
        // errors out instead of buffering forever.
        std::vector<uint8_t> chunk(256 * 1024);
        constexpr size_t kBootstrapCap = 16 << 20;
        while (true) {
            int64_t n = ring->read(chunk.data(), chunk.size());
            if (n <= 0) {
                std::cerr << "stream ended before H.264/H.265 parameter sets "
                             "arrived\n";
                return 1;
            }
            raw.insert(raw.end(), chunk.begin(), chunk.begin() + n);
            if (ExtractAnnexBParamSets(raw.data(), raw.size(),
                                       &params.extradata) >= 0)
                break;
            if (raw.size() > kBootstrapCap) {
                std::cerr << "no parameter sets in the first " << kBootstrapCap
                          << " bytes\n";
                return 1;
            }
        }
        std::cerr << "[hal_player] producer connected, bootstrapped from "
                  << raw.size() << " bytes\n";
    } else
#endif
    {
        std::ifstream fin(path, std::ios::binary | std::ios::ate);
        if (!fin) { std::cerr << "cannot open " << path << "\n"; return 1; }
        std::streamsize sz = fin.tellg(); fin.seekg(0);
        raw.resize(static_cast<size_t>(sz));
        fin.read(reinterpret_cast<char*>(raw.data()), sz);
        fin.close();
        std::cerr << "[hal_player] file size: " << sz << " bytes\n";
        params.inputs.push_back(path);
    }

    // 给 vtbox/qsvdec 等 backend 提供 extradata (SPS+PPS or VPS+SPS+PPS,AVCC 格式)
    int det = ExtractAnnexBParamSets(raw.data(), raw.size(), &params.extradata);

    // codec:extradata 探测优先(以流内容为准),探测不出来才退回扩展名.
    // 少了这一步,params.codec 默认为空,qsvdec 会以 "unsupported codec ''" 拒绝.
    if (det >= 0) {
        params.codec = det ? "hevc" : "h264";
    } else {
        // Shared extension table; "h264" stays the fallback when the extension
        // says nothing (the same default the inline chain used).
        params.codec = halcodec::CodecFromExtension(ExtOf(path));
        if (params.codec.empty()) params.codec = "h264";
    }
    std::cerr << "[hal_player] codec: " << params.codec
              << " (detected=" << det << ")\n";

    if (!dec->Initialize(params)) {
        std::cerr << "decoder Initialize failed\n";
        return 1;
    }

    // Live mode: a feeder thread streams the ring into FillInput so the
    // decode/render loop below never waits on input while a frame may be
    // pending (a blocking GetFrame with un-fed bytes in the ring would
    // deadlock against a full ring). Async backends run FillInput and
    // GetFrame from different threads safely -- the Session fan-in relies
    // on the same contract.
#if defined(__linux__) || defined(__APPLE__)
    if (live) {
        feeder = std::thread([&dec, &raw, &ring] {
            constexpr size_t kFeedChunk = 256 * 1024;
            size_t off = 0;
            while (off < raw.size()) {  // bootstrap bytes arrived with the param sets
                size_t n = std::min(kFeedChunk, raw.size() - off);
                dec->FillInput(raw.data() + off, n);
                off += n;
            }
            std::vector<uint8_t> chunk(kFeedChunk);
            while (true) {
                int64_t n = ring->read(chunk.data(), chunk.size());
                if (n <= 0) break;
                dec->FillInput(chunk.data(), static_cast<int>(n));
            }
            dec->SignalInputComplete();
        });
    }
#endif

    Player player;
    std::vector<uint8_t> bgraBuf;

    if (bench) {
        // ============ BENCH MODE:no window,pure decode throughput ============
        uint64_t t0 = NowNs();
        int64_t frames = 0;
        if (live) {
            // The feeder streams the ring into FillInput; drain decoded
            // frames until the stream ends.
            halcodec::CodecFrame f;
            while (dec->GetFrame(f)) {
                frames++;
                if (f.release) f.release();
            }
        } else {
        std::cerr << "[hal_player] calling FillInput (" << raw.size() << " bytes)...\n";
        int ready = dec->FillInput(raw.data(), raw.size());
        std::cerr << "[hal_player] FillInput returned ready=" << ready
                  << "  backend isAsync=" << (dec->isAsync() ? "yes" : "no") << "\n";

        if (ready >= 0) {
            dec->SignalInputComplete();  // EOF → GetFrame 不会永远等更多 input
            halcodec::CodecFrame f;
            while (dec->GetFrame(f)) {
                frames++;
                if (f.release) f.release();
            }
        } else {
            halcodec::CodecFrame f;
            int n;
            do {
                n = dec->PullFrames();
                while (dec->GetFrame(f)) { frames++; if (f.release) f.release(); }
            } while (n > 0);
        }
        }
        uint64_t t1 = NowNs();
        double wall = NsToSec(t1 - t0);
        std::cerr << "\n================ PERF ================\n";
        std::cerr << "  backend        : " << dec->getName() << "\n";
        std::cerr << "  decoded frames : " << frames << "\n";
        std::cerr << "  total wall time: " << std::fixed << std::setprecision(3) << wall << " s\n";
        if (wall > 0)
            std::cerr << "  throughput     : " << std::fixed << std::setprecision(1)
                      << frames / wall << " fps\n";
        std::cerr << "======================================\n\n";
    } else {
        // ============ WINDOW MODE:decode + NV12→BGRA + SDL2/GL render ============
        if (!live) {  // live: the feeder thread feeds FillInput
            dec->FillInput(raw.data(), raw.size());
            dec->SignalInputComplete();
        }
        halcodec::CodecFrame f;
        while (player.running && dec->GetFrame(f)) {
            if (!f.data) halcodec::DownloadToHost(f);
            if (!f.data) {
                std::cerr << "DownloadToHost failed (locality="
                          << static_cast<int>(f.locality) << ")\n";
                if (f.release) f.release();
                continue;
            }
            // 第一帧才知道尺寸 → 延迟建窗口
            static int initialized = 0;
            if (!initialized) {
                if (!player.initWindow(f.width, f.height)) return 1;
                bgraBuf.resize(f.width * f.height * 4);
                initialized = 1;
            }
            static int64_t rendered = 0;
            rendered++;
            player.renderFrame(f, bgraBuf.data());
            if (rendered == 1 || rendered % 30 == 0)
                std::cerr << "[hal_player] frame " << rendered
                          << " (" << f.width << "x" << f.height
                          << " fmt=" << static_cast<int>(f.format)
                          << " loc=" << static_cast<int>(f.locality) << ")\n";
            if (f.release) f.release();
            if (!player.pumpEvents()) break;
        }
    }

#if defined(__linux__) || defined(__APPLE__)
    if (live) {
        // Release the feeder if the window closed while the producer still
        // streams; after a natural end-of-stream this is a no-op.
        ring->abort();
        feeder.join();
    }
#endif
    player.shutdown();
    dec->Finalize();
    std::cerr << "[hal_player] exit\n";
    return 0;
}
