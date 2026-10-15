# HALCodec（中文说明）

> 英文原文见 [README.md](README.md)。本文件只是中文版；两份分文件保存，改动时
> `git diff` 会直接指出哪一份漏改。

各硬件厂商都提供自己的硬件编解码器。本项目把它们的差异收拢到一套统一的
C++ 接口（`halcodec` 命名空间）之下，让你用同一套 API、同一套命令行工具驱动
NVIDIA / AMD / Intel / Apple / NVIDIA DRIVE 的硬件编解码，无需为每个厂商单独写代码。

## 架构

```
                ┌─────────────────── app/ (命令行工具) ──────────────┐
                │                                                    │
   文件/流      │  hal_dec   hal_enc   hal_transcode                 │  相机
   ────────────►│                                              hal_cam│◄──────────
                │  codecinfo     (parse_cli.h, encode_overrides.h)   │  SIPL/V4L2/AV
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
| NVIDIA NVJPEG | `nvjpeg` | `nvjpegenc` | `nvjpeg` | Linux + CUDA |
| AMD AMF | —（数据通路 TODO）| — | `amf` | Linux + AMD |
| Intel QSV | `qsvdec` | `qsvenc` | `qsv` | Linux + Intel iGPU |
| NVIDIA DRIVE NvMedia | `nvmedia` | `nvmedia` | `nvmedia` | Linux（DRIVE OS）aarch64 |
| Apple VideoToolbox | `vtbox` | `vtenc` | `vtbox` | macOS |

- **NvMedia** 适配 NVIDIA DRIVE OS（Linux aarch64），仅当 `NvMedia/include/nvmedia_6x`
  与 `lib-target/` 下的 SDK 头文件存在时构建。解码（H.264/HEVC/VP9/AV1/VP8/MPEG-2/MPEG-4）
  与编码（H.264/HEVC）数据通路已在 thor 验证，详见下方 NvMedia codec 状态表。
- **AMF** 仅当 `AMF/AMF-*/amf/public/include` 下有 SDK 头文件时构建，运行时 dlopen
  `libamfrt64.so.1`（运行机器上需有）。AMF 解码/编码类参与编译但**刻意不注册**：
  数据通路（`FillInput`/`GetFrame`）尚未实现，注册空壳会让 `hal_*` 静默选中它作为
  默认后端、输出 0 帧。`amf` 能力 provider 仅在运行时存在于本机时注册（无
  `libamfrt64.so.1` 或非 AMD 机器上根本不会进入注册表），在 AMD 机器上 `codecinfo`
  照常展示 AMD 能力；数据通路落地后取消 `amfdecoder.cc`/`amfencoder.cc` 里
  `HALCODEC_CONNECT` 的注释即可。
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
| `hal_dec` | macOS：`vtbox`；其它平台：第一个注册的解码后端 | `nvjpeg`、`qsvdec`；`-i` 可重复以并发解码 N 路流 |
| `hal_enc` | 第一个注册的编码后端：macOS `vtenc`，Linux+CUDA `nvenc` | `qsvenc`、`nvmedia`、`nvjpegenc`；`-i` 可重复以并发编码 N 路流 |
| `hal_transcode` | 由 `-b <dec> -B <enc>` 指定解码+编码对；`-z` 帧留显存零拷贝 | 任意解码→编码组合；`-i` 可重复以并发转码 N 路流（仅 host 帧，`-z` 只限单路） |
| `hal_cam` | 按采集源：`sipl`→`nvmedia`、`v4l2`→`qsvenc`、`av`→`vtenc` | 任意编码后端（`-B`） |
| `codecinfo` | 所有注册的能力 provider | `-b` 限定单个 |

Linux 上没有硬编码的解码默认后端：`hal_dec` 在运行时取第一个注册的
解码后端；NvMedia **刻意不做**这个默认，需要时用 `-b nvmedia` 显式指定。

常用选项（来自 `app/parse_cli.h`）：`-i/--input`、`-o/--output`、`-b/--backend`、
`--gpu <idx>`、`-c/--codec`、`-f/--format`、`-z/--zero-copy`（解码帧留在显存）。
跨后端转码用 `hal_transcode -b <dec> -B <enc>`。

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

- **解码**: `halcodec::Decoder` 抽象工厂,后端由 `--backend` 指定;省略时取第一个可用后端(无平台硬编码)
- **渲染**: SDL2 + legacy OpenGL (fixed-function pipeline),macOS / Linux / Windows 统一
- **色彩**: NV12 → BGRA CPU 转换 (BT.601 整数近似,~3ns/pixel)

**数据流**:

```
Annex-B .h264/.hevc 文件
    └─ ExtractAnnexBParamSets → AVCC [4-byte len][nalu]...
        └─ Decoder::Initialize + FillInput
            └─ 平台硬解 (vtbox / nvdec / qsvdec)
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
存在时启用。各后端编成 `*_layers.so`（`.dylib`），由 `halcodec_stage_backends`
目标（`copy_if_different`）拷到可执行文件旁，而非链接进去；`plugin_loader` 启动时
从可执行文件所在目录 dlopen 它们，缺某个后端的驱动只会跳过该后端（见[架构](#架构)）。
交叉编译到 DRIVE OS：

```sh
cmake -B build-drive -DCMAKE_TOOLCHAIN_FILE=tools/cmake/driveos-aarch64.cmake \
      -DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT=/tmp/boost-aarch64/sysroot/usr \
      -DBoost_NO_SYSTEM_PATHS=ON
cmake --build build-drive -j
```

英文版构建说明见 [README.md](README.md) 的 `## Build` 一节。
