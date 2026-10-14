# HALCodec

Vendors provide their own hardware-specific encoders and decoders. This project
aims to simplify the usage of hw codec by exposing one unified C++ interface
(`halcodec`) over every vendor adapter.

---

# HALCodec（中文说明）

各硬件厂商都提供自己的硬件编解码器。本项目把它们的差异收拢到一套统一的
C++ 接口（`halcodec` 命名空间）之下，让你用同一套 API、同一套命令行工具驱动
NVIDIA / AMD / Intel / Apple / NVIDIA DRIVE 的硬件编解码，无需为每个厂商单独写代码。

> 本文件为双语：上方为英文原文，下方为等量中文说明。两份内容覆盖同一信息，
> 按需阅读其一即可。

## 架构

```
                ┌─────────────────── app/ (命令行工具) ──────────────┐
                │                                                    │
   文件/流      │  hal_dec   hal_enc   hal_transcode   codecinfo     │  相机
   ────────────►│                                              hal_cam│◄──────────
                │       (parse_cli.h, encode_config.h)             │  SIPL/V4L2/AV
                └───────────────┬──────────────────────┬────────────┘
                                 │                      │
                  统一 C++ 接口                  CameraSource
                  （halcodec 命名空间）          (camera/)
                                 │                      │
              ┌──────────────────┴──────────────────────┴──────────┐
              │              halcodec_core（共享库）                │
              │         Registry<Encoder/Decoder/Cap>              │
              │           （持有进程级单例）                        │
              └──────────────────┬──────────────────────────────────┘
                                 │  启动时 dlopen（plugin_loader）
                                 │  依赖缺失 ⇒ 跳过该后端
   ┌──────────┬──────────┬───────┴────────┬───────────┬─────────────┐
   ▼          ▼          ▼                ▼           ▼             ▼
nvenc_     nvjpeg_    amf_layers       qsv_layers  vtbox_layers  nvmedia_
layers     layers                         (libvpl)    (VideoTB)    layers
(NVDEC/    (NVJPEG)   (AMF)             (Intel)     (Apple)       (DRIVE OS
NVENC)                                                macOS)       aarch64)
   │          │          │                │           │             │
   ▼          ▼          ▼                ▼           ▼             ▼
 CUDA       CUDA      libamfrt64       libmfx-gen  CoreVideo    libnvmedia
                      (dlopen)          +libva      框架         +nvsci
```

两个设计要点：

- **dlopen 而非链接期绑定。** 各厂商后端是 `*_layers.so`/`.dylib`，通过
  `HALCODEC_CONNECT` 静态初始化器自注册。若在链接期绑进每个可执行文件，动态
  加载器会在进程启动时解析每个后端的 `NEEDED` 项（如 `libnvidia-encode.so.1`），
  使二进制在缺少这些驱动库的机器上无法运行。`plugin_loader` 改为启动时按需
  dlopen 各后端：依赖缺失的后端加载失败即被跳过（打印一条警告），其余后端照常
  工作。
- **统一 API 里刻意没有 `Device`/context 抽象。** 上下文是厂商相关的，留在各后端
  内部；统一接口只交换 `CodecFrame` 与 `CodecParams`。

## 已注册的后端

| 后端 | 解码器 | 编码器 | 能力自省 | 平台 |
|------|--------|--------|----------|------|
| NVIDIA NVENC/NVDEC | `nvdec` | `nvenc` | `nvidia` | Linux + CUDA |
| NVIDIA NVJPEG | `nvjpeg` | — | — | Linux + CUDA |
| AMD AMF | `amfdec` | `amfenc` | `amf` | Linux + AMD |
| Intel QSV | `qsvdec` | `qsvenc` | `qsv` | Linux + Intel iGPU |
| NVIDIA DRIVE NvMedia | `nvmedia` | `nvmedia` | `nvmedia` | Linux（DRIVE OS）aarch64 |
| Apple VideoToolbox | `vtbox` | `vtenc` | `vtbox` | macOS |

- **NvMedia** 适配 NVIDIA DRIVE OS（Linux aarch64），仅当 `NvMedia/include/nvmedia_6x`
  与 `lib-target/` 下的 SDK 头文件存在时构建。解码（H.264/HEVC/VP9/AV1/VP8/MPEG-2/MPEG-4）
  与编码（H.264/HEVC）数据通路已在 thor 验证，详见下方 NvMedia codec 状态表。
- **AMF** 仅当 `AMF/AMF-*/amf/public/include` 下有 SDK 头文件时构建，运行时 dlopen
  `libamfrt64.so.1`（运行机器上需有）。
- **QSV** 仅当 `QSV/libvpl-*/api` 下有 libvpl 头文件时构建，运行时需 `libvpl.so.2`
  + Intel media driver。

## 统一接口（`src/`）

所有类型都在 `halcodec` 命名空间下：

- `Encoder` / `Decoder`（`encoder.h`、`decoder.h`）—— 对称的编解码接口：
  `Initialize(CodecParams&)`、`FillFrame` / `FillInput`、`GetFrame(CodecFrame&)`、
  `Finalize`。解码器额外暴露 `PullFrames()`（内部源驱动）、`IsAsync()` 与
  `SignalInputComplete()`（用于显式喂流的场景）。
- `CodecFrame`（`frame.h`）—— 统一帧容器，携带 `data`、`size`、`width`、`height`、
  `PixelFormat`、逐 plane 的 `strides` 与 `pts`，通过 `frame.release()` 释放。
- `CodecParams`（`codec_config.h`）—— 参数化的初始化配置（设备序号、输入、帧格式、
  extradata）。
- `CapabilityProvider`（`capability.h`）—— 后端自省（设备枚举 + 解码/编码能力打印），
  供 `codecinfo` 使用。统一 API 里**刻意没有** `Device`/context 抽象：上下文是
  厂商相关的，留在各自后端内部。
- `Registry<T>`（`registry.h`）—— 共享工厂模板；适配器在静态初始化期通过
  `HALCODEC_CONNECT(Base, Name, Class)` 宏自注册。
- `CameraSource`（`camera/camera_source.h`）—— 统一相机采集抽象，把 SIPL
  （DRIVE-OS，NvSciBufObj 零拷贝）、V4L2（Linux USB/UVC）、AVFoundation（macOS）
  三类相机收拢到同一个 `Grab(CodecFrame&)` 接口下。平台实现分别在
  `camera/camera_source_{sipl,v4l2,av}.*`，按平台编译；由 `hal_cam` 应用消费。

## 命令行工具（`app/`）

各工具只依赖统一接口 + 后端注册表，因此换后端无需重编译。用 `-b/--backend`
选后端；省略时用平台默认值：

| 工具 | 默认后端 | 可选后端 |
|------|----------|----------|
| `hal_dec` | macOS: `vtbox`；Linux aarch64: `nvmedia`；其它 Linux: `nvdec` | `nvjpeg`、`amfdec`、`qsvdec` |
| `hal_enc` | 第一个注册的编码后端：macOS `vtenc`，Linux+CUDA `nvenc` | `amfenc`、`qsvenc`、`nvmedia` |
| `hal_transcode` | 由 `-b <dec>:<enc>` 指定解码+编码对；`-z` 帧留显存零拷贝 | 任意解码→编码组合 |
| `hal_cam` | 按采集源：`sipl`→`nvmedia`、`v4l2`→`qsvenc`、`av`→`vtenc` | 任意编码后端（`-B`） |
| `codecinfo` | 所有注册的能力 provider | `-b` 限定单个 |

常用选项（来自 `app/parse_cli.h`）：`-i/--input`、`-o/--output`、`-b/--backend`、
`--gpu <idx>`、`-c/--codec`、`-f/--format`、`-z/--zero-copy`（解码帧留在显存）。
跨后端转码用 `hal_transcode -b <dec>:<enc>`。

## QSV codec 状态

在 Arrow Lake-P iGPU（Intel Core Ultra 7 255H，iHD driver，`libmfx-gen` 24.4.4）
上实测，`hal_dec`/`hal_enc -b qsvdec`/`qsvenc`：

| codec | 解码 | 编码 |
|-------|------|------|
| h264  | 30/30 ✅ | 30/30 ✅ |
| hevc  | 15/15 ✅ | 15/15 ✅ |
| av1   | 13/13 ✅ | 13/13 ✅ |
| jpeg  | 1/1 ✅ | 30/30 ✅ |
| vp8   | 13/13 ✅ | — |
| vp9   | 25/25 ✅ | 未实现 |
| vc1   | 已 wire ✅ | — |
| mpeg2 | 14/15（hw）| 未实现 |

解码 round-trip（`hal_enc`→`hal_dec` 同 codec）满帧；零拷贝转码
（`hal_transcode -z qsvdec:qsvenc`）30/30、0 host 帧。VP8/VP9 解码输出与 ffmpeg
`vp8_qsv`/`vp9_qsv` 字节一致。VP8/VP9 解码需通过 `dlopen libva.so.2` 注册外部帧
分配器（**无 libva 链接依赖**，头文件仅在 CMake 找到 `va/va.h` 时引入）。

## NvMedia codec 状态

在 NVIDIA DRIVE OS 7.0.3.0，T234/tegra264，aarch64上实测，
`hal_dec`/`hal_enc -b nvmedia`，经 `tools/cmake/driveos-aarch64.cmake` 交叉编译、
部署到 `~/halcodec-nv/`。板子出厂的 `/usr/lib` 里是**精简版** `libnvmedia`；
`NvMedia/lib-target/` 里的完整版 SDK 库（同 DRIVE-OS 版本）可解锁其余 codec。

| codec | 解码 | 编码 |
|-------|------|------|
| h264  | 30/30 ✅（已验证）| 30/30 ✅（已验证）|
| hevc  | 30/30 ✅（已验证）| 30/30 ✅（已验证）|
| av1   | 30/30 ✅（已验证，IVF profile 0）| 库接受，未 wire |
| vp9   | 30/30 ✅（已验证，IVF）| 不支持（hw/lib）|
| vp8   | 30/30 ✅（已验证，IVF）| — |
| mpeg2 | 30/30 ✅（已验证，elementary）| — |
| mpeg4 | 30/30 ✅（已验证，elementary）| — |
| vc1   | 2677/2677 ✅（已验证，RCV）| — |
| jpeg  | 30/30 ✅（已验证，IJPD）| 30/30 ✅（已验证，IJPE）|

- **已验证**（h264/hevc/vp9/av1/vp8/mpeg2/mpeg4/vc1/jpeg 解码，h264/hevc/jpeg 编码）：数据通路完整 wire
  并实测。HEVC 编码可经 NvMedia HEVC 解码 round-trip（30 帧 NV12），4Mbps CBR 下 PSNR
  ~51.8 dB。H.264 的 IDE→IEP 零拷贝路径已验证。VP9/AV1/VP8 解码需 IVF 输入（框架解容器后
  按帧喂 NvMediaParser 并置 `bCompletePicture=1`——这些 codec 无 start code，NvMediaParser
  无法从裸流自切分帧）；AV1 解码还需在 `UpdateReferenceFrames` 里把 `RefPics[8]` 的 pool
  槽指针翻译成 NvSciBufObj。MPEG-2/MPEG-4 elementary 流按 raw chunk 喂（VOS/sequence
  start code），其 `forward/backward_reference` 同样做 pool 槽→NvSciBufObj 翻译。VC1
  Simple/Main 无 start code 且无带内 sequence header，NvMediaParser 无法自检序列——需 RCV
  容器：框架在 `Initialize` 解析 RCV 文件头、把 sequence header 经
  `pExternalSeqInfo` 带外喂给 `NvMediaParserCreate`，再按 4 字节长度前缀的 per-frame 记录
  逐帧喂 `bCompletePicture=1`；`UpdateReferenceFrames` 把 VC1 的
  `forward/backward_reference` + `range_mapped` pool 槽翻译成 NvSciBufObj（前两者插
  pre-fence，range_mapped 不插——对齐 videodemo.c）。2677 帧 NV12 输出，luma 与 NVIDIA
  `nvm_ide_sci` 示例逐字节一致（RCV 由 WMV3/ASF 源转换而来：`tools/wmv3_to_rcv.py`）。
  JPEG 走专用 IJPE/IJPD 图像引擎（IDE 不收 MJPEG），PitchLinear YUV420；编码每帧一张独立 JPEG
  （30 帧 → 30 个有效 JPEG，ffmpeg 解码 320×240 yuvj420p，luma PSNR ~31 dB @q75），解码支持
  单张 .jpg 与多帧 MJPEG 流（SOI/EOI 分帧），IJPE→IJPD round-trip luma PSNR ~46 dB。
- **库接受，未 wire** ⚠️：完整版 SDK 库在 `NvMediaParserCreate` / `NvMediaIEPCreate`
  处接受该 codec（精简版系统库会以 "Codec Not supported" 拒绝），但框架未 wire。
  AV1 编码未 wire（缺 init/config/pic-params 三元组）。VC1 Advanced（start code 00 00 01 0D）
  走 elementary 流路径，未 wire（无编码器/样片；SP/MP 已覆盖）。可用
  `NvMedia/probes/`（codec_probe、feed_probe、iep_probe、ijpe_probe、ijpd_probe、vc1_probe）复现
  库版本差异与 RCV/外部 SeqInfo 路径。VP9 编码在两套库
  里都在 `NvMediaIEPCreate` 失败——是这块板子真实的硬件/库限制。
- **部署注意**：框架部署默认加载**系统** `/usr/lib` 库，因此只能用 h264/hevc。要用
  库接受的 codec，需把 `NvMedia/lib-target/` 的完整 nv* 集合随部署带上、并放
  `LD_LIBRARY_PATH` 最前（它 NEEDED `libnvrm_*`、`libnvparser`、`libnvvideo`、
  `libnvos`、`libnvsocsys` 等目标板系统库，全套传递依赖必须一起带上）。

## Apple M5 实测吞吐

在 Apple M5 Max（macOS 26.3，`vtbox` 后端，Release 构建）上实测。Release 树相比
Debug 树有 1.8x–3.9x 的差距（`-O0` 下 `hal_enc` 整文件输入 buffer 与解码侧逐帧拷贝
的销毁占可观 wall time），所以下表均取 Release 树。方法：`hal_dec`/`hal_enc` 端到端
（exec 到 exit），解码取 5 趟最优、编码取 3 趟最优；`-o /dev/null` 与 `-o <file>` 均
测（两者是不同的数，见下）。

**解码 1080p**（fps）：

| feed | `/dev/null` | to file | ffmpeg |
|------|------------:|--------:|-------:|
| H.264 High | 695 | 600 | 2346 |
| HEVC Main 8 | 883 | 751 | 644 |
| HEVC Main 10 | 716 | 566 | 460 |
| AV1 Main 8 | 822 | 704 | 1022 |
| ProRes 422 | 938 | 591 | 1956 |

**解码 4K**（fps）：H.264 High 228（to file 192），HEVC Main 8 305（249），
HEVC Main 10 225（167）。

**编码 1080p**（fps）：H.264 High 215，HEVC Main 8 200，HEVC Main 10 180，
ProRes 422 438，JPEG q90 393 img/s。4K 80 帧：H.264 High 87，HEVC Main 8 86，
HEVC Main 10 72。

关键观察：把每帧解码结果落盘只付出"写"的代价（边际 13–19 GB/s，视频 codec 掉
13–23%，ProRes 掉约三分之一）；ProRes 各档只差输出大小（该 session 拒绝码率控制）。


## hal_player — 跨平台硬解播放器 (HAL Decoder + SDL2 + OpenGL)

`app/hal_player.cc` 是跨平台 C++17 硬解播放器,走完整硬解 + GPU 帧渲染通路。

- **解码**: `halcodec::Decoder` 抽象工厂,后端由平台自动选择 — macOS = vtbox (VideoToolbox),Linux = qsvdec / amfdec,Windows = amfdec / nvdec
- **渲染**: SDL2 + legacy OpenGL (fixed-function pipeline),macOS / Linux / Windows 统一
- **色彩**: NV12 → BGRA CPU 转换 (BT.601 整数近似,~3ns/pixel)

**数据流**:

```
Annex-B .h264/.hevc 文件
    └─ ExtractAnnexBParamSets → AVCC [4-byte len][nalu]...
        └─ Decoder::Initialize + FillInput
            └─ 平台硬解 (vtbox / nvdec / qsvdec / amfdec)
                └─ CodecFrame (CVPixelBuffer / CUDA / VADeviceMem)
                    └─ DownloadToHost (仅 device 帧)
                        └─ NV12 → BGRA
                            └─ GL texture upload → SDL2 窗口
```

**用法**:

```sh
# bench 模式 (纯解码吞吐,无窗口)
./app/hal_player --bench --backend vtbox input.h264
./app/hal_player --bench --backend vtbox input.hevc

# window 模式 (SDL2 + GL 渲染)
./app/hal_player --backend vtbox input.h264
./app/hal_player input.hevc   # 自动选第一个可用 backend
```

`--bench` 跳过渲染,只测硬解吞吐。输出 PERF 块:

```
================ PERF ================
  backend        : vtbox
  decoded frames : 60
  total wall time: 0.348 s
  throughput     : 172.4 fps
======================================
```

**构建逻辑** (`app/CMakeLists.txt`):

| SDL2 | 平台 | 产物 |
|------|------|------|
| ✅ | 全部 | `hal_player.cc` (跨平台 C++ + SDL2 + GL) |
| ❌ | macOS | `hal_player.mm` (Apple 专有 ObjC++ fallback) |
| ❌ | Linux/Windows | 跳过 hal_player target |

### hal_player 性能

macOS (Intel UHD 630 iGPU,Release 构建) 实测,测试素材 720p 短序列:

| Codec | 后端 | 帧数 | Wall | 吞吐 |
|-------|------|------|------|------|
| H.264 720p | vtbox | 60 | 0.35s | **172 fps** |
| HEVC  720p | vtbox | 60 | 0.55s | **109 fps** |

解读:HEVC 硬解比 H.264 慢 ~36%,符合 Intel Gen10 iGPU 规格;窗口模式额外开销在 NV12→BGRA CPU 转换 + GL texture upload,对 30fps 实时播放绰绰有余。
## 构建

```sh
cmake -B build && cmake --build build
```

Linux + CUDA 下 NVEnc/NVJPEG 目标自动启用；macOS 下 VideoToolbox 自动启用；QSV/AMF
按各自 SDK 头文件存在与否启用；NvMedia 仅在 aarch64 且 `NvMedia/include/nvmedia_6x`
存在时启用。交叉编译到 DRIVE OS：

```sh
cmake -B build-drive -DCMAKE_TOOLCHAIN_FILE=tools/cmake/driveos-aarch64.cmake \
      -DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT=/tmp/boost-aarch64/sysroot/usr \
      -DBoost_NO_SYSTEM_PATHS=ON
cmake --build build-drive -j
```

详细构建说明见下方英文版 `## Build`。

---

## Unified interface (`src/`)

All types live in the `halcodec` namespace:

- `Encoder` / `Decoder` (`encoder.h`, `decoder.h`) — symmetric codec
  interfaces: `Initialize(CodecParams&)`, `FillFrame` / `FillInput`,
  `GetFrame(CodecFrame&)`, `Finalize`. Decoders additionally expose
  `PullFrames()` (internal-source drive), `IsAsync()` and
  `SignalInputComplete()` for explicitly fed streams.
- `CodecFrame` (`frame.h`) — unified frame container carrying `data`, `size`,
  `width`, `height`, `PixelFormat`, per-plane `strides` and `pts`. Frames are
  released through `frame.release()`.
- `CodecParams` (`codec_config.h`) — parameterized init config (device index,
  inputs, frame format, extradata).
- `CapabilityProvider` (`capability.h`) — backend self-introspection (device
  enumeration + decoder/encoder capability printout), consumed by `codecinfo`.
  There is deliberately no `Device`/context abstraction in the unified API:
  context handling is vendor-specific and stays inside each backend.
- `Registry<T>` (`registry.h`) — shared factory template; adapters self-register
  via the `HALCODEC_CONNECT(Base, Name, Class)` macro at static-init time.
- `CameraSource` (`camera/camera_source.h`) — unified camera capture abstraction
  that folds SIPL (DRIVE-OS, NvSciBufObj zero-copy), V4L2 (Linux USB/UVC) and
  AVFoundation (macOS) behind one `Grab(CodecFrame&)` interface. Platform
  implementations live in `camera/camera_source_{sipl,v4l2,av}.*` and are
  compiled in per platform; consumed by the `hal_cam` app.

## Registered backends

| Backend     | Decoder  | Encoder  | Caps provider | Platform    |
|-------------|----------|----------|---------------|-------------|
| NVIDIA NVENC/NVDEC | `nvdec` | `nvenc` (empty) | `nvidia` | Linux + CUDA |
| NVIDIA NVJPEG | `nvjpeg` | -        | -             | Linux + CUDA |
| AMD AMF     | `amfdec` | `amfenc` (empty) | `amf`    | Linux + AMD |
| Intel QSV   | `qsvdec` | `qsvenc` (empty) | `qsv`    | Linux + Intel iGPU |
| NVIDIA DRIVE NvMedia | `nvmedia` | `nvmedia` (empty) | `nvmedia` | Linux (DRIVE OS) aarch64 |
| Apple VideoToolbox | `vtbox`  | `vtenc`  | `vtbox`       | macOS        |

The NvMedia adapter targets NVIDIA DRIVE OS (Linux aarch64) and is built only
when the SDK headers are present under `NvMedia/include/nvmedia_6x` plus
`lib-target/`. Decode (H.264/HEVC/VP9/AV1/VP8/MPEG-2/MPEG-4) and encode
(H.264/HEVC) data paths are wired and verified end to end on T234; see the
NvMedia codec status table below. The AMF adapter is built only when the AMF SDK
headers are present under `AMF/AMF-*/amf/public/include`; the AMF runtime
(`libamfrt64.so.1`) is dlopen'd at runtime and is required on the machine
where the binary runs. Same for QSV: built only when libvpl headers exist
under `QSV/libvpl-*/api`, with `libvpl.so.2` + an Intel media driver expected
at runtime.

## Layout

```
src/                 unified interface headers (Encoder/Decoder/CodecFrame/...)
camera/              camera source abstraction (SIPL/V4L2/AVFoundation → CameraSource)
NVEnc/layers/        NVIDIA adapter (nvenc namespace)
NVJPEG/layers/       NVIDIA JPEG adapter (nvjpeg namespace)
VideoToolbox/layers/ Apple adapter (vtbox namespace)
AMF/layers/          AMD adapter (amd namespace), Linux-only
QSV/layers/          Intel QSV/libvpl adapter (qsv namespace), Linux-only
NvMedia/layers/      NVIDIA DRIVE OS adapter (nvmedia namespace), Linux-aarch64-only
app/                 example CLIs (hal_dec/hal_enc/hal_transcode/hal_cam/codecinfo)
tools/               helper scripts (SDK update/import)
```

## Architecture

```
                ┌─────────────────── app/ (CLIs) ───────────────────┐
                │                                                    │
   file/stream  │  hal_dec   hal_enc   hal_transcode   codecinfo     │  camera
   ────────────►│                                              hal_cam│◄──────────
                │       (parse_cli.h, encode_config.h)             │  SIPL/V4L2/AV
                └───────────────┬──────────────────────┬────────────┘
                                 │                      │
                  unified C++ interface           CameraSource
                   (halcodec namespace)          (camera/)
                                 │                      │
              ┌──────────────────┴──────────────────────┴──────────┐
              │              halcodec_core (SHARED)                 │
              │         Registry<Encoder/Decoder/Cap>               │
              │              (owns process-wide singletons)         │
              └──────────────────┬──────────────────────────────────┘
                                 │  dlopen at startup (plugin_loader)
                                 │  missing deps ⇒ backend skipped
   ┌──────────┬──────────┬───────┴────────┬───────────┬─────────────┐
   ▼          ▼          ▼                ▼           ▼             ▼
nvenc_     nvjpeg_    amf_layers       qsv_layers  vtbox_layers  nvmedia_
layers     layers                         (libvpl)    (VideoTB)    layers
(NVDEC/    (NVJPEG)   (AMF)             (Intel)     (Apple)       (DRIVE OS
NVENC)                                                macOS)       aarch64)
   │          │          │                │           │             │
   ▼          ▼          ▼                ▼           ▼             ▼
 CUDA       CUDA      libamfrt64       libmfx-gen  CoreVideo    libnvmedia
                      (dlopen)          +libva      frameworks   +nvsci
```

Two design choices to note:

- **dlopen, not link-time binding.** Vendor backends are `*_layers.so`/`.dylib`
  that self-register via `HALCODEC_CONNECT` static initializers. Linking them
  into every executable would force the dynamic loader to resolve each backend's
  `NEEDED` entries (e.g. `libnvidia-encode.so.1`) at process start, making the
  binary unusable on machines lacking those driver libraries. `plugin_loader`
  dlopens each backend lazily at startup instead; a backend whose deps are
  missing simply fails to load and is skipped with a warning, while the rest
  keep working.
- **No `Device`/context abstraction in the unified API.** Context handling is
  vendor-specific, so it stays inside each backend. The unified interface only
  trades `CodecFrame`s and `CodecParams`.

## SDK updates (`tools/update_sdks.sh`)

The vendored SDKs (NVIDIA Video Codec SDK, AMD AMF, Intel oneVPL) live under
`NVEnc/`, `AMF/` and `QSV/`. `tools/update_sdks.sh` downloads and installs
them, preferring GitHub releases:

1. the corresponding repo's latest-release **asset** (matched per-SDK);
2. if no asset matches, the release tag's **source tarball** as a fallback;
3. NVIDIA SDK has no public download source — download it from the NVIDIA
   site, then import the local zip with `--import`.

Supported SDKs: `nvdec` (NVIDIA, manual), `amf` (AMD), `qsv` (Intel oneVPL
dispatcher headers), `vplgpu` (Intel oneVPL GPU Runtime — the `libmfx-gen`
implementation, source only; see [Building the QSV runtime](#building-the-qsv-runtime) below).

```sh
tools/update_sdks.sh                 # update all SDKs (skip already up-to-date)
tools/update_sdks.sh amf qsv         # update only the listed SDKs
tools/update_sdks.sh -v 2.16.0 qsv   # pin a specific release tag (default: latest)
tools/update_sdks.sh --check         # compare local vs upstream, download nothing
tools/update_sdks.sh --force amf     # re-download even if already present
tools/update_sdks.sh --prune amf     # install AMF without the ~1.2 GB Thirdparty/ + .github/
tools/update_sdks.sh --prune-old     # remove older version dirs of that SDK
tools/update_sdks.sh --import video_codec_sdk_13.x.x.zip nvdec  # offline import
```

Notes:

- The `latest` release is resolved via the GitHub API first; when the API is
  rate-limited or offline it falls back to parsing the `/releases/latest` page
  or `git ls-remote --tags`, so an anonymous checkout still works.
- **GitHub proxy:** `curl` does not honor git's `url.*.insteadOf` rewrite, so
  the script reads that config (or the `GITHUB_PROXY` env var) and applies the
  prefix to both `github.com` download URLs and `api.github.com` API calls.
  This lets `update_sdks.sh` work behind a mirror like `gh-proxy.org` without
  any extra flags.
- Install a new version by simply updating to a newer release, or by
  `-v <ver>` / `--import` for NVIDIA. Old directories can be swept with
  `--prune-old`.
- `--prune` additionally removes the large dirs that `.gitignore` also
  excludes (`AMF/AMF-*/Thirdparty`, CI metadata under `.github/`). The AMF
  tarball's `Thirdparty/` alone is ~1.2 GB of prebuilt binaries for AMF's own
  samples — nothing in this project reads it (the build only needs the headers
  under `AMF/AMF-*/amf/public/include`), so fetch or update AMF with `--prune`,
  or delete the directory afterwards to reclaim the disk.

### Building the QSV runtime

`qsv` (the oneVPL *dispatcher* headers under `QSV/libvpl-*/api`) lets the
adapter compile and `dlopen("libvpl.so.2")` at runtime, but the dispatcher
alone cannot create a session — it needs an **implementation** library
(`libmfx-gen.so.1.2`, the oneVPL GPU Runtime). On a machine with an Intel iGPU
but no packaged runtime, fetch and build it from source:

```sh
# 1. fetch the source (uses gh-proxy automatically, as above)
tools/update_sdks.sh vplgpu            # -> QSV/vpl-gpu-rt-<ver>/

# 2. install build dependencies (one-time; needs sudo)
sudo apt-get install -y libva-dev libdrm-dev cmake build-essential

# 3. build + install the runtime (produces libmfx-gen.so.1.2)
cd QSV/vpl-gpu-rt-<ver>
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
sudo make install
sudo ldconfig
```

After `libmfx-gen.so.1.2` is on the loader path, `MFXCreateSession` succeeds
and the `qsvdec`/`qsvenc` data paths become exercisable. The build also needs
the Intel Media Driver (iHD VA-API) and gmmlib at runtime; on Ubuntu these
ship as `intel-media-va-driver` and `libigdgmm12`.

### QSV codec status

Measured on an Arrow Lake-P iGPU (Intel Core Ultra 7 255H, iHD driver,
`libmfx-gen` 24.4.4), `hal_dec`/`hal_enc -b qsvdec`/`qsvenc`, ffmpeg-generated
streams unless noted:

| codec  | decode      | encode      |
|--------|-------------|-------------|
| h264   | 30/30 ✅     | 30/30 ✅     |
| hevc   | 15/15 ✅     | 15/15 ✅     |
| av1    | 13/13 ✅     | 13/13 ✅     |
| jpeg   | 1/1 ✅       | 30/30 ✅     |
| vp8    | 13/13 ✅     | —           |
| vp9    | 25/25 ✅     | not impl.   |
| vc1    | wired ✅     | —           |
| mpeg2  | 14/15 (hw)   | not impl.   |

Decode round-trips (`hal_enc`→`hal_dec`, same codec) are full-count and the
zero-copy transcode path (`hal_transcode -z qsvdec:qsvenc`) passes 30/30 with
0 host frames. VP8/VP9 decode output is byte-identical to ffmpeg's
`vp8_qsv`/`vp9_qsv`.

VP8/VP9 decode through `libmfx-gen` (not the legacy `libmfxhw64` path) needs an
external frame allocator so libmfx-gen selects its legacy UMC surface path
instead of the VPL FrameInterface path. `halcodec` registers one by `dlopen`ing
`libva.so.2` at runtime — there is **no link dependency on libva**; the headers
are pulled in only when CMake finds `va/va.h` (`QSV_HAS_VA`), and the whole
allocator is compiled out on non-Linux or libva-less builds. VP9 4:4:4
(Profile 1) is not supported by the current NV12 (YUV 4:2:0) VA surface pool;
it would need a YUV 4:4:4 pool and matching FourCC.

### NvMedia codec status

Measured on NVIDIA DRIVE OS 7.0.3.0, T234/tegra264, aarch64,
`hal_dec`/`hal_enc -b nvmedia`, cross-compiled via
`tools/cmake/driveos-aarch64.cmake` and deployed to `~/halcodec-nv/`. The
board ships a **reduced** `libnvmedia` in `/usr/lib`; the full-build SDK
lib-target (same DRIVE-OS version, in `NvMedia/lib-target/`) unlocks the rest.

| codec  | decode                | encode                |
|--------|-----------------------|-----------------------|
| h264   | 30/30 ✅ (verified)   | 30/30 ✅ (verified)   |
| hevc   | 30/30 ✅ (verified)   | 30/30 ✅ (verified)   |
| av1    | 30/30 ✅ (verified, IVF profile 0) | lib-accepted, not wired |
| vp9    | 30/30 ✅ (verified, IVF) | not supported (hw/lib)|
| vp8    | 30/30 ✅ (verified, IVF) | —                   |
| mpeg2  | 30/30 ✅ (verified, elementary) | —             |
| mpeg4  | 30/30 ✅ (verified, elementary) | —             |
| vc1    | 2677/2677 ✅ (verified, RCV) | —              |
| jpeg   | 30/30 ✅ (verified, IJPD) | 30/30 ✅ (verified, IJPE) |

- **Verified** (h264/hevc/vp9/av1/vp8/mpeg2/mpeg4/vc1/jpeg decode, h264/hevc/jpeg
  encode): full data paths wired and measured. HEVC encode round-trips through
  NvMedia HEVC decode (30 NV12 frames); PSNR ~51.8 dB vs the original at
  4 Mbps CBR. Zero-copy IDE→IEP path verified for H.264. VP9/AV1/VP8 decode
  take IVF input (the framework demuxes the container and feeds each frame to
  NvMediaParser with `bCompletePicture=1` — those codecs carry no start codes,
  so NvMediaParser cannot self-split a raw frame stream); AV1 decode
  additionally needed the `RefPics[8]` pool-slot → NvSciBufObj fixup in
  `UpdateReferenceFrames`. MPEG-2/MPEG-4 elementary streams feed as raw chunks
  (VOS/sequence start codes); their `forward/backward_reference` surfaces get
  the same pool-slot → NvSciBufObj fixup. VC1 Simple/Main has no start codes
  and no in-band sequence header, so NvMediaParser cannot self-detect the
  sequence — it needs an RCV container: `Initialize` parses the RCV file header
  and hands the sequence header to `NvMediaParserCreate` out-of-band via
  `pExternalSeqInfo`, then `FeedFileChunk` walks the length-prefixed per-frame
  records feeding each as a `bCompletePicture=1` packet;
  `UpdateReferenceFrames` translates VC1's `forward/backward_reference` +
  `range_mapped` pool-slot pointers to NvSciBufObj (pre-fence on the motion
  refs, none on range_mapped — mirrors videodemo.c). 2677 NV12 frames out,
  luma byte-identical to NVIDIA's `nvm_ide_sci` sample (RCV built from a
  WMV3/ASF source via `tools/wmv3_to_rcv.py`). JPEG goes through the dedicated
  IJPE/IJPD image engines (IDE rejects MJPEG), PitchLinear YUV420; encode
  emits one independent JPEG per frame (30 frames → 30 valid JPEGs, ffmpeg
  decodes 320×240 yuvj420p, luma PSNR ~31 dB @q75), decode handles both a
  single .jpg and a multi-frame MJPEG stream (SOI/EOI framing); IJPE→IJPD
  round-trip luma PSNR ~46 dB.
- **lib-accepted, not wired** ⚠️: the full-build SDK lib accepts the codec at
  `NvMediaParserCreate` / `NvMediaIEPCreate` (the reduced system lib rejects
  it with "Codec Not supported"), but the framework does not wire it.
  AV1 encode is not wired (no init/config/pic-params triple). VC1 Advanced
  (start code 00 00 01 0D) would use the elementary-stream path; not wired (no
  encoder/sample; SP/MP via RCV is covered). Use `NvMedia/probes/` (codec_probe,
  feed_probe, iep_probe, ijpe_probe, ijpd_probe, vc1_probe) to reproduce the
  lib-version difference and the RCV/external-SeqInfo path. VP9 encode fails
  at `NvMediaIEPCreate` in both libs — a real hardware/lib limit on this board.
- **Deploy note**: the framework deploy loads the **system** `/usr/lib` libs
  by default, so it sees only h264/hevc. To use the lib-accepted codecs,
  ship the full `NvMedia/lib-target/` nv* set into the deploy dir and put it
  first on `LD_LIBRARY_PATH` (it NEEDEDs `libnvrm_*`, `libnvparser`,
  `libnvvideo`, `libnvos`, `libnvsocsys` from the target system, so the full
  transitive set must travel together).

## Command-line tools (`app/`)

The CLIs exercise only the unified interface plus the backend registry, so they
work against any registered backend without recompilation. Pick a backend with
`-b/--backend`; when omitted, the platform default is used:

| Tool      | Default backend         | Alternative backends |
|-----------|-------------------------|----------------------|
| `hal_dec` | macOS: `vtbox`; Linux aarch64: `nvmedia`; other Linux: `nvdec` | `nvjpeg`, `amfdec`, `qsvdec` |
| `hal_enc` | the first registered encoder backend: `vtenc` on macOS, `nvenc` on Linux + CUDA | `amfenc`, `qsvenc`, `nvmedia` |
| `hal_transcode` | decode+encode pair from `-b <dec>:<enc>`; `-z` keeps frames in device memory | any decode→encode combo |
| `hal_cam` | per source: `sipl`→`nvmedia`, `v4l2`→`qsvenc`, `av`→`vtenc` | any encoder backend (`-B`) |
| `codecinfo` | all registered capability providers | `-b` restricts to one |

Common options (from `app/parse_cli.h`):

| Option | Meaning |
|--------|---------|
| `-h, --help` | show the full option list and exit |
| `-i, --input <file\|dir>` | input path |
| `-o, --output <file\|dir>` | output path; with a directory input `hal_enc` takes it as the output directory. Defaults to `<input without ext>` plus the extension the app writes: `.format` for `hal_dec`, the codec's for `hal_enc` |
| `-b, --backend <name>` | backend name, e.g. `vtbox` / `nvdec` / `nvjpeg` / `nvenc` |
| `--gpu <idx>` | device ordinal (default `0`) |
| `-f, --format <ext>` | raw pixel layout of the frames (default `yuv`); also the output extension for `hal_dec`, whose output is raw. `hal_enc` names its outputs by `--codec` instead |
| `-c, --codec <name>` | stream codec (default `h264`; the vtbox/vtenc backend also answers for `hevc`, `av1`, `jpeg`, `prores`) |

Exit codes — `hal_dec`: `0` success, `2` missing/invalid input, `255` decoder
create/initialize failure; `hal_enc`: `0` success, `1` backend
create/initialize failure.

### hal_dec — decode

```sh
cd build
./app/hal_dec -i /tmp/t.h264                     # default backend
./app/hal_dec -i /tmp/t.h264 -b vtbox -o out.yuv # explicit backend + output
./app/hal_dec -i frames/ -b nvjpeg               # batch JPEG decode (Linux)
```

Notes:

- **Parameter sets**: the input is scanned as an Annex-B H.264 stream, SPS/PPS
  are extracted and injected into `CodecParams::extradata` automatically.
  This is required by backends that build a format description from parameter
  sets (`vtbox`); other backends ignore it.
- **Output naming**: for a single file, output is `-o` or
  `<input without ext>.<ext>`, where `<ext>` is `bmp` when `--format` asks for a
  BMP layout (`y`, `bgr`, `rgb`) and the `--format` string otherwise -- the name
  says what the bytes are. For a directory, every regular file maps to
  `<name without ext>.<ext>`; `-o` then names the output *directory* (created if
  absent) instead of a file, the way it does for `hal_enc`. `--format` is the
  right source for that extension on this side: a decoder's output is raw
  pixels, and the layout asked for is exactly what the files hold. Directory
  input is only meaningful for backend decoders that walk the path themselves
  (e.g. `nvjpeg` on Linux); `vtbox` requires a single file, since its parameter
  sets come from one Annex-B stream. `hal_enc` names its outputs by codec
  instead, because its `--format` describes what it reads -- see its section.
  The name list holds one entry per input *file*, and a stream can carry more
  pictures than that, so names past the end number off the last one
  (`out.bmp`, `out_2.bmp`, ...).
  Two hazards used to live here and are now guards: the output stream is opened
  at the first frame that needs it, never before the input has been read, and a
  name that resolves to one of the inputs -- by device and inode, so a symlink
  counts -- is refused with exit `-1` rather than truncated. `-f h264` on an
  `.h264` input, and `-o` repeating `-i`, both land on that guard.
- **`--format` is a request, not a promise**: it reaches the decoders through
  `CodecParams::outputFormat`, and exactly one backend reads it (`nvjpeg`, for
  `rgb`/`bgr`/`y`). `vtbox` answers in the layout the stream carries, so
  `-f bgr` on a 4:2:0 stream gets a refusal naming the pixels it was handed
  (`delivered NV12`) instead of a BMP writer reading three planes out of a
  frame that holds one and a half. `-f rgbi`/`-f bgri` are refused outright: the
  HAL format table has no name for interleaved 24-bit, so no backend can
  deliver it.
- **Decode loop**: synchronous backends are driven by `PullFrames()` (feeds
  input, then each returned frame is written via `GetFrame()` and released with
  `frame.release()`). Async backends (`vtbox`, `isAsync() == true`) are fed
  chunk-by-chunk with `FillInput()` and drained until `GetFrame()` returns
  false (blocking-until-EOF). The `nvjpeg` backend switches to a new output
  file per frame.
- **Current status**: `vtbox` decodes asynchronously via its internal queue
  — `FillInput()` accepts Annex-B access units, frames arrive from the
  decompression callback and `GetFrame()` blocks until the stream ends.
  For the Annex-B H.264/HEVC feeds the layer parses picture order out of the
  parameter sets and slices, so frames leave `GetFrame()` in **presentation
  order**, B-frames included; the reorder depth it parks frames at comes from
  the stream's own POC arithmetic (`vtdecoder.cc`, `FlushReorder`). Feeds that
  carry no picture order — an AV1 OBU stream, one image per sample — are
  emitted as they are submitted. `vtdec.ffmpeg_parity` and
  `vtdec.av1_ivf_parity` compare the emitted order against ffmpeg byte for
  byte, so a regression here fails a test rather than a reader's eye.

### hal_enc — encode

```sh
cd build
./app/hal_enc -i raw_320x240.yuv -f yuv -o out.h264        # single raw stream
./app/hal_enc -i frames/ -b vtenc --codec jpeg -f nv12     # frames/*.jpg
./app/hal_enc -i frames/ -b vtenc --codec jpeg -f nv12 -o encoded/
```

Purpose: the encoder-side counterpart of `hal_dec`, demonstrating the
`Encoder` factory and the explicit feed model. The app owns input reading:
- single file inputs are sliced into fixed-size frames (bytes per frame are
  derived from `-f` and the `WxH` resolution), each delivered as one
  `CodecFrame` via `FillFrame()`; BMP input is read as a single image;
- directory inputs feed one file per `FillFrame()` call;
- an empty marker frame ends the stream, letting the encoder flush trailing
  packets, drained with `GetFrame()` until it returns false.

- **Output naming**: the default extension comes from `--codec`, not from
  `--format`. `--format` describes the raw layout `hal_enc` reads, and reusing it
  as the output extension handed every codestream the path of the frame it was
  made from (`-f nv12` over a directory of `.nv12` files). Pictures keep being
  named after their own input: `c0.nv12` with `--codec jpeg` writes `c0.jpg`,
  with `--codec hevc` `c0.h265`. The mapping is h264 → `.h264`, hevc/h265 →
  `.h265`, av1 → `.av1`, jpeg/mjpeg → `.jpg`, prores → `.prores`, anything else
  → `.<codec>`. With a directory input `-o` names the **output directory**, which
  is created if missing; with a single file it names the output file. So inputs
  are never written, and an input only ends up replaced when `-o` points a
  codestream at it or the inputs already carry the codec's own extension, which
  `hal_enc` says on stderr before writing anything. The scan takes every regular
  file in the directory, so re-running over one it has already written into
  encodes those codestreams as frames -- use `-o` or clear the outputs.
  `tests/vtenc_jpeg_naming.cmake` holds all of that down. A directory is one
  stream for a video coder, so `--codec h264/hevc/...` over a directory writes
  the first of those names and appends every picture to it; only an image coder
  (`oneOutputFilePerFrame()`, e.g. JPEG) fills the whole list. (It used to be worse
  than the collision: the first output was opened before any input was read, so
  the first-listed input was truncated to 0 bytes, 3 files yielded 2 pictures,
  every later picture landed one name off, and the last output was left empty.)

Encoders consume frame data from the `CodecFrame` parameter (`nvenc` uploads
it to the device, `nvjpegenc` uploads it as one image) instead of opening the
input file themselves.

### codecinfo — inspect backends

```sh
cd build
./app/codecinfo              # enumerate every registered capability provider
./app/codecinfo -b vtbox     # inspect a single provider
```

Prints each backend's devices plus its decoder/encoder capabilities.
VideoToolbox has no decoder enumeration API, so decode support is probed per
codec via `VTIsHardwareDecodeSupported` (e.g. `H264: hw decode supported`);
encoders are listed via `VTCopyVideoEncoderList`
(e.g. `codec=avc1 Apple H.264 (HW)`). Reports `no capability providers
registered` when nothing is available.

## Measured throughput on Apple M5

Numbers come from an Apple M5 Max running macOS 26.3, `vtbox` backend, built
from a Release tree:

```sh
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j
```

The build type matters more than anything else here. The same code, the same
material, the Debug tree next to the Release one, one pass:

| 1080p, whole process | Debug | Release | ratio |
| --- | ---: | ---: | ---: |
| H.264 decode | 395 fps | 708 fps | 1.79x |
| ProRes 422 decode | 288 | 950 | 3.30x |
| H.264 encode | 56 | 214 | 3.85x |

The Release column here agrees with the tables below to within 2%. At `-O0` the
element-wise destruction of `hal_enc`'s whole-file input buffer, and of every
frame copy on the decode side, is a genuine share of the wall time, so a
Debug-tree rate measures the compiler flags as much as the codec. Every number
below is from the Release tree.

Method: `hal_dec` / `hal_enc` run end to end (exec to exit), best of five passes
for decode and three for encode, and the lowest pass recorded below in whole fps.
`-o /dev/null` and `-o <file>` are both measured, because they are different
numbers (see below). Frame counts come out of the program's own output and are
checked against what `ffprobe` says the stream holds, so a short run shows up as a
failure rather than a rate -- the 4K HEVC Main 10 stream really carries 75
frames, not 80. Material is ffmpeg-generated at `ultrafast` / `preset 8`, 200
frames at 25 fps for 1080p and 80 for 4K. Run-to-run spread is about 12% for the
video codecs and up to 20% on the ProRes tiers, and absolute rates move by that
much between material sets, so read a row against the other rows of the same
table rather than against a table from another day.

The software column is `ffmpeg -hide_banner -loglevel error -y -i <stream> -f
null -`, all cores, no VideoToolbox, same whole-process method and the same
machine state. It is not like-for-like -- ffmpeg never hands a frame back to a
caller through HAL's frame model -- but on the 10-bit HEVC profiles it is now
below the hardware path, so it is no longer simply a floor to beat.

### Decode, 1080p

| feed | `/dev/null` | to file | ffmpeg |
| --- | ---: | ---: | ---: |
| H.264 High | 695 fps | 600 fps | 2346 fps |
| HEVC Main 8 | 883 | 751 | 644 |
| HEVC Main 10 | 716 | 566 | 460 |
| HEVC Main 4 2:2 10 | 594 | 457 | 400 |
| HEVC Monochrome 8 | 1030 | 892 | 925 |
| HEVC Monochrome 10 | 935 | 749 | 674 |
| AV1 Main 8 | 822 | 704 | 1022 |
| AV1 Main 10 | 680 | 540 | 987 |
| ProRes 422 | 938 | 591 | 1956 |
| ProRes 4444 | 629 | 398 | 1334 |

"to file" writes every decoded frame to disk: 396 MB to 2.37 GB per run depending
on the pixel format, at a marginal 13-19 GB/s. The two ProRes software rows are
ffmpeg's own reference ProRes decoder, so they price the inner format rather than
a competitive decoder. ProRes 4K is the one combination this matrix has no
material for; every other cell was measured.

### Decode, 4K

| feed | frames | `/dev/null` | to file | ffmpeg |
| --- | ---: | ---: | ---: | ---: |
| H.264 High | 80 | 228 fps | 192 fps | 622 fps |
| HEVC Main 8 | 80 | 305 | 249 | 313 |
| HEVC Main 10 | 75 | 225 | 167 | 217 |


### Encode

| encode | input | 1080p | output per run |
| --- | --- | ---: | ---: |
| H.264 High | NV12 | 215 fps | 8.3 MB |
| HEVC Main 8 | NV12 | 200 | 5.8 MB |
| HEVC Main 10 | P010 | 180 | 5.7 MB |
| HEVC Main 4 2:2 10 | P210 | 165 | 5.8 MB |
| HEVC Monochrome 8 | NV12 | 195 | 5.4 MB |
| HEVC Monochrome 10 | P010 | 175 | 5.5 MB |
| ProRes Proxy | P210 | 433 | 20.7 MB |
| ProRes LT | P210 | 429 | 46.6 MB |
| ProRes 422 | P210 | 438 | 59.3 MB |
| ProRes 422 HQ | P210 | 432 | 79.1 MB |
| ProRes 4444 | P210 | 289 | 118.4 MB |
| ProRes 4444 XQ | P210 | 286 | 123.1 MB |
| JPEG, q90 | NV12 | 393 img/s | 15.8 MB / 100 pictures |

4K, 80 frames: H.264 High 87 fps, HEVC Main 8 86, HEVC Main 10 72.

The video profiles all land within about 20% of one rate whatever the profile
or bit depth, which is what an XPC-bound session looks like:
`VTCompressionSessionEncodeFrame` blocks in
`xpc_connection_send_message_with_reply_sync`, so the wait is the codec
engine's own pace rather than the work asked of it. ProRes runs ahead of it
and its tiers differ only by output size because that session rejects rate
control outright: `AverageBitRate`, `Quality`, `MaxKeyFrameInterval` and
`DataRateLimits` each answer `kVTPropertyNotSupportedErr`.

### What actually moves these numbers

- **Keeping the decoded frames costs the write, and only the write.** Storing
  every frame is now a marginal 13-19 GB/s, which takes 13-23% off the video
  rates (1080p H.264 695 fps to `/dev/null` against 600 to a 622 MB file, Main
  10 716 against 566, 4K Main 10 225 against 167 to 1.78 GB) and about a third
  off ProRes, whose frames are 8.3-12.4 MB each (938 against 591, 629 against
  398). It used to halve every row in the table. What changed is the output
  `std::ofstream`: libc++ hands a `filebuf` 4096 bytes, so a 3 MB frame went out
  through that hole a thousand bytes at a time. A `pubsetbuf` of 4 MiB in front
  of `open()` takes 622 MB of 3 MB writes from 462.9 ms (1.34 GB/s) to 46.3 ms
  (13.4 GB/s), and the curve is flat from 1 MiB upward. The SSD was never the
  difference -- 3 MB is 2.3 ms of user-time copying against 0.2 ms in the kernel.
- **The 10-bit output plane is unpacked in NEON, not a scalar loop.**
  VideoToolbox packs three 10-bit samples into a little-endian word and pads the
  row to 64 bytes, so a 10-bit frame cannot be copied out; the row has to be
  decoded. Vectorized with `vst3q_u16` it costs 0.245 ms per 1080p P010 frame
  against 0.722 ms for the scalar form (2.94x; 4K 2.848 to 0.989), and in the
  decoder that is worth +36 to +45% on exactly the four rows whose output is
  packed 10-bit -- Main 10 517 to 716 fps, Main 422 10 411 to 594, AV1 Main 10
  499 to 680, 4K Main 10 158 to 225 -- while every 8-bit and ProRes control row
  moves by less than 2%. The two agree to within the noise: 0.477 ms taken off
  the unpack is 0.537 ms off the wall. The other vectorization that looked
  available, the `'sv44'` 4:4:4 chroma split, measured slower (0.64x, 0.87x) than
  what `-O3` already does with the scalar loop, so it stayed scalar.
- **One `filebuf` cannot be closed and reopened.** On this libc++ a stream that
  has been `close()`d and `open()`ed again loses the bulk `xsputn` path for good
  and falls into a per-character loop -- user time, not syscalls (98.7 ms against
  5.0 ms, 156 G instructions for 315 MB) -- and `pubsetbuf` does not bring it
  back. `hal_enc`'s per-picture JPEG mode writes one file per picture, so it hit
  this on every picture: the same 1080p q90 frames take 254 ms with a fresh
  stream per picture and 532 ms with one reused, 2.56 against 5.38 ms/picture.
  (That pair was measured while the output-naming collision `hal_enc`'s directory
  mode has since lost was still eating a picture, so both arms wrote the same 99;
  the fixed directory path now yields all 100 in 254 ms, 2.54 ms/picture.) On the
  single-stream encode
  rows the same fix is worth
  6-12% where the run writes 47-123 MB (ProRes LT 403 to 429, 422 397 to 438,
  HQ 388 to 432, 4444 260 to 289, XQ 255 to 286 fps), 2.6% for ProRes Proxy at
  20.7 MB, and nothing outside the spread for H.264 and HEVC at 5-8 MB -- which
  is the profile this fix is supposed to have, one file per run.
- **Session properties, not the CLI flags.** On a replica of this layer's
  session (1080p, 8 Mbps, real NV12 content, 200 frames): default 3.72
  ms/frame, `AllowFrameReordering=false` 1.96 ms/frame (510 fps),
  `RealTime=true` 5.54 ms/frame (180 fps). `--lowdelay` sets both, and the two
  effects cancel: 211.4 to 210.6 fps through `hal_enc`. `--bframes` is not
  mapped to VideoToolbox at all (211.4 to 212.1, inside the noise).
- **JPEG and ProRes RAW cost per picture, not per stream.** A `hal_dec`
  process spends ~58 ms before it touches a pixel — `hal_dec -h` alone is
  2.4 ms, so that is VideoToolbox session set-up, not process start. Decoding
  one 1080p JPEG takes 59.4 ms and one 4K JPEG 65.6 ms, so the picture itself
  is ~1.6-1.9 ms at 1080p and ~7.8 ms at 4K once the fixed cost is subtracted;
  that difference is a small fraction of either run, so treat it as ±20%. No
  measured *decode* case spreads that 58 ms, because those feeds carry one image
  per sample; the encode side does (the JPEG row above is 100 pictures in one
  process). ProRes RAW is
  a 2-picture 4112x2176 sample (81.6 ms), so its ~10 ms/picture is indicative
  only.

One caveat about how the pairs above were measured, since it changes what a
number means: an A/B of the decoder layer has to stage the dylib once rather than
swap it per run -- replacing a Mach-O in the program's directory costs the next
process load ~166 ms (219 ms against 386 ms for the same run), which is AMFI
assessing a new inode, and a per-run swap depresses every absolute by about a
third. Only paired arms from one session are comparable; the tables above carry
the fixed tree.

### No hardware path on this machine

- **AV1 encode**: `VTCopyVideoEncoderList` has no AV1 entry on M5, so AV1 is
  decode-only.
- **ProRes RAW encode**: decode-only.
- ffmpeg's `-hwaccel videotoolbox` rates are deliberately left out. It falls
  back to the software decoder without saying so on profiles it does not hand
  over, and the results show it: Monochrome 8 "hardware" at 2823 fps is its own
  CPU rate, and Main 10 at 805 fps beats Main 8 at 508 fps. A column like that
  is not a hardware measurement.


## hal_player — cross-platform hardware-decoded player (HAL Decoder + SDL2 + OpenGL)

`app/hal_player.cc` is a cross-platform C++17 hard-decode player that exercises
the full decode + GPU-frame render path end to end.

- **Decode**: `halcodec::Decoder` abstraction factory — backend picked by
  platform: macOS = vtbox (VideoToolbox), Linux = qsvdec / amfdec,
  Windows = amfdec / nvdec.
- **Render**: SDL2 + legacy OpenGL (fixed-function pipeline), uniform across
  macOS / Linux / Windows.
- **Color**: NV12 → BGRA CPU conversion (BT.601 integer approximation, ~3 ns/pixel).

**Data path**:

```
Annex-B .h264 / .hevc file
    └─ ExtractAnnexBParamSets → AVCC [4-byte len][nalu]...
        └─ Decoder::Initialize + FillInput
            └─ platform hardware decoder (vtbox / nvdec / qsvdec / amfdec)
                └─ CodecFrame (CVPixelBuffer / CUDA / VADeviceMem)
                    └─ DownloadToHost (device frames only)
                        └─ NV12 → BGRA
                            └─ GL texture upload → SDL2 window
```

**Usage**:

```sh
# bench mode (pure decode throughput, no window)
./app/hal_player --bench --backend vtbox input.h264
./app/hal_player --bench --backend vtbox input.hevc

# window mode (SDL2 + GL rendering)
./app/hal_player --backend vtbox input.h264
./app/hal_player input.hevc   # auto-pick first available backend
```

`--bench` skips rendering so you measure hardware decode alone. PERF block:

```
================ PERF ================
  backend        : vtbox
  decoded frames : 60
  total wall time: 0.348 s
  throughput     : 172.4 fps
======================================
```

**Build logic** (`app/CMakeLists.txt`):

| SDL2 present | Platform | Target |
|-------------|----------|--------|
| Yes | all | `hal_player.cc` (cross-platform C++ + SDL2 + GL) |
| No  | macOS | `hal_player.mm` (Apple-only ObjC++ fallback) |
| No  | Linux/Windows | hal_player target skipped |

### hal_player throughput

Measured on macOS (Intel UHD 630 iGPU, Release build), short 720p test clip:

| Codec | Backend | Frames | Wall | Throughput |
|-------|---------|-------:|-----:|-----------:|
| H.264 720p | vtbox | 60 | 0.35 s | **172 fps** |
| HEVC  720p | vtbox | 60 | 0.55 s | **109 fps** |

Observations: HEVC decode is ~36% slower than H.264, which matches the Intel
Gen10 iGPU spec. Window mode adds NV12→BGRA CPU conversion + GL texture
upload; well within 30 fps real-time playback capacity.
## Build

```sh
cmake -B build && cmake --build build
```

On Linux + CUDA, the NVEnc/NVJPEG targets are enabled automatically;
`app/CMakeLists.txt` force-loads the shared layers (`--no-as-needed`) so their
static self-registration runs.