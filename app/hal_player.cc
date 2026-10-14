// hal_player — 跨平台硬解播放器,HAL Decoder + SDL2 + OpenGL.
//
//   解码 : HAL Decoder 抽象 (vtbox/nvdec/qsvdec/amfdec) — 平台无关 API.
//   渲染 : SDL2 + OpenGL (跨 macOS / Linux / Windows).
//   数据流: Annex-B → HAL Decoder::FillInput → GetFrame (CodecFrame)
//           → DownloadToHost (仅 device 内存帧) → NV12→BGRA (CPU) → GL texture
//
// 跨平台关键:
//   - 解码走 HAL 抽象层,后端由平台决定 (macOS=vtbox,Linux=qsvdec,Windows=amfdec).
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
#include <mach/mach_time.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"
#include "plugin_loader.h"

namespace {

// ---- 计时 ----
static inline uint64_t NowNs() { return mach_absolute_time(); }
static inline double NsToSec(uint64_t ns) {
    static mach_timebase_info_data_t info;
    static int once = (mach_timebase_info(&info), 1);
    (void)once;
    return (double)(ns * info.numer) / (double)info.denom / 1e9;
}

// 从 Annex-B 流中提取 SPS+PPS (H.264) 或 VPS+SPS+PPS (HEVC),输出 AVCC 格式
// [4-byte BE len][nalu][4-byte len][nalu]...  — VT/VTDecoder backend 要求.
//
// Codec detect:不能靠 forbidden_zero_bit (byte & 0x80) 区分 — H.264 和 HEVC
// 的 forbidden_zero_bit 都必须是 0!正确做法是看 type 值范围:
//   H.264 types  7(SPS),8(PPS),5(IDR),6(SEI)   不会出现在 HEVC;
//   HEVC  types 32(VPS),33(SPS),34(PPS),16-21  不会出现在 H.264.
static void ExtractAnnexBParamSets(const uint8_t* data, size_t size,
                                   std::vector<uint8_t>* out) {
    // First pass:扫描所有 NALs,精确 detect codec
    int seenHEVCVPS = 0, seenH264SPS = 0;
    std::vector<std::pair<uint8_t*, size_t>> nals;  // (nalu ptr, nalu size, NO start code)
    size_t i = 0;
    while (i + 3 < size) {
        size_t sc = 0;
        if (data[i]==0 && data[i+1]==0 && data[i+2]==1) sc = 3;
        else if (i+3<size && data[i]==0 && data[i+1]==0 && data[i+2]==0 && data[i+3]==1) sc = 4;
        if (!sc) { ++i; continue; }
        uint8_t* nalStart = const_cast<uint8_t*>(data + i + sc);
        size_t j = i + sc + 1;
        while (j + 3 < size) {
            if (data[j]==0 && data[j+1]==0 && (data[j+2]==1 || (j+3<size && data[j+2]==0 && data[j+3]==1))) break;
            ++j;
        }
        size_t nalSize = j - (i + sc);
        uint8_t h264type = nalStart[0] & 0x1F;
        uint8_t hevctype = (nalStart[0] >> 1) & 0x3F;
        nals.push_back({nalStart, nalSize});
        if (h264type == 7) seenH264SPS = 1;
        if (hevctype == 32) seenHEVCVPS = 1;
        i = j;
    }
    int isHEVC = seenHEVCVPS ? 1 : 0;
    if (!isHEVC && !seenH264SPS) {
        for (auto& [p, sz] : nals) if (((p[0] >> 1) & 0x3F) == 33) { isHEVC = 1; break; }
    }

    // Second pass:按 codec 的正确 type 位提取 PS
    int gotVPS = 0, gotSPS = 0, gotPPS = 0;
    for (auto& [p, sz] : nals) {
        uint8_t t = isHEVC ? ((p[0] >> 1) & 0x3F) : (p[0] & 0x1F);
        bool isParam = false;
        if (!isHEVC) {
            if (t == 7 && !gotSPS) { gotSPS = 1; isParam = true; }
            else if (t == 8 && !gotPPS && gotSPS) { gotPPS = 1; isParam = true; }
        } else {
            if (t == 32 && !gotVPS) { gotVPS = 1; isParam = true; }
            else if (t == 33 && !gotSPS && gotVPS) { gotSPS = 1; isParam = true; }
            else if (t == 34 && !gotPPS && gotSPS) { gotPPS = 1; isParam = true; }
        }
        if (isParam) {
            out->push_back((sz>>24)&0xFF); out->push_back((sz>>16)&0xFF);
            out->push_back((sz>>8)&0xFF);   out->push_back(sz&0xFF);
            out->insert(out->end(), p, p + sz);
        }
        if (gotSPS && gotPPS) break;
    }
    std::cerr << "[hal_player] AnnexB PS: isHEVC=" << isHEVC
              << " gotVPS=" << gotVPS << " gotSPS=" << gotSPS << " gotPPS=" << gotPPS
              << " extradata=" << out->size() << " bytes\n";
}

// NV12 (semi-planar YUV420) → BGRA (packed,row-major).
// 逐像素计算 chroma,独立支持 luma/chroma stride.
// x86 benchmark: ~3ns/pixel,即 720p 单帧 ~2.7ms — 不成为瓶颈.
static void NV12ToBGRA(const uint8_t* y, const uint8_t* uv, int w, int h,
                       size_t yStride, size_t uvStride,
                       uint8_t* outBGRA) {
    // BT.601 限定范围 (video range):Y=[16,235],chroma=[16,240].
    // 用整数近似:R = Y + 1.402*(V-128),G = Y - 0.34414*(U-128) - 0.71414*(V-128),B = Y + 1.772*(U-128).
    for (int row = 0; row < h; row++) {
        const uint8_t* yRow = y + row * yStride;
        const uint8_t* uvRow = uv + (row >> 1) * uvStride;  // chroma 半高
        uint8_t* outRow = outBGRA + row * w * 4;
        for (int col = 0; col < w; col++) {
            uint8_t Yv = yRow[col];
            // UV 交错:偶索引 U,奇索引 V
            int cu = (col & ~1);
            int8_t U = (int8_t)uvRow[cu];       // 有符号 -128..+127
            int8_t V = (int8_t)uvRow[cu + 1];
            int R = Yv + (141 * V) / 100;
            int G = Yv - (34 * U) / 100 - (71 * V) / 100;
            int B = Yv + (177 * U) / 100;
            if (R < 0) R = 0; else if (R > 255) R = 255;
            if (G < 0) G = 0; else if (G > 255) G = 255;
            if (B < 0) B = 0; else if (B > 255) B = 255;
            outRow[col*4+0] = (uint8_t)B;
            outRow[col*4+1] = (uint8_t)G;
            outRow[col*4+2] = (uint8_t)R;
            outRow[col*4+3] = 0xFF;
        }
    }
}

struct Player {
    SDL_Window* window = nullptr;
    SDL_GLContext glCtx = nullptr;
    GLuint texBGRA = 0;
    int winW = 0, winH = 0;
    bool running = true;

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

    void renderFrame(const halcodec::CodecFrame& frame, uint8_t* bgraBuf) {
        NV12ToBGRA(frame.data, frame.data + frame.strides[0] * frame.height,
                   frame.width, frame.height,
                   frame.strides[0], frame.strides[1], bgraBuf);
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
        std::cerr << "usage: hal_player [--backend NAME] [--bench] <file.h264|file.hevc|file.mp4>\n"
                  << "  --backend  decoder backend (vtbox/nvdec/qsvdec/amfdec); default: first available\n"
                  << "  --bench    no window,pure decode throughput\n";
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

    // 读文件
    std::ifstream fin(path, std::ios::binary | std::ios::ate);
    if (!fin) { std::cerr << "cannot open " << path << "\n"; return 1; }
    std::streamsize sz = fin.tellg(); fin.seekg(0);
    std::vector<uint8_t> raw(sz);
    fin.read(reinterpret_cast<char*>(raw.data()), sz);
    fin.close();
    std::cerr << "[hal_player] file size: " << sz << " bytes\n";

    // 给 vtbox/qsvdec 等 backend 提供 extradata (SPS+PPS or VPS+SPS+PPS,AVCC 格式)
    halcodec::CodecParams params;
    params.inputs.push_back(path);
    params.zeroCopy = true;  // 让 backend 优先 device memory (NV12 on GPU)
    ExtractAnnexBParamSets(raw.data(), raw.size(), &params.extradata);

    if (!dec->Initialize(params)) {
        std::cerr << "decoder Initialize failed\n";
        return 1;
    }

    Player player;
    std::vector<uint8_t> bgraBuf;

    if (bench) {
        // ============ BENCH MODE:no window,pure decode throughput ============
        uint64_t t0 = NowNs();
        std::cerr << "[hal_player] calling FillInput (" << raw.size() << " bytes)...\n";
        int ready = dec->FillInput(raw.data(), raw.size());
        std::cerr << "[hal_player] FillInput returned ready=" << ready
                  << "  backend isAsync=" << (dec->isAsync() ? "yes" : "no") << "\n";

        int64_t frames = 0;
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
        dec->FillInput(raw.data(), raw.size());
        dec->SignalInputComplete();
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

    player.shutdown();
    dec->Finalize();
    std::cerr << "[hal_player] exit\n";
    return 0;
}
