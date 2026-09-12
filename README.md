# HALCodec

Vendors provide their own hardware-specific encoders and decoders. This project
aims to simplify the usage of hw codec by exposing one unified C++ interface
(`halcodec`) over every vendor adapter.

## Unified interface (`src/`)

All types live in the `halcodec` namespace:

- `Device` (`device.h`) — device enumeration / context management, plus
  capability introspection.
- `Encoder` / `Decoder` (`encoder.h`, `decoder.h`) — symmetric codec
  interfaces: `Initialize(CodecParams&)`, `FillFrame` / `FillinFrame`,
  `GetFrame(CodecFrame&)`, `Finalize`.
- `CodecFrame` (`frame.h`) — unified frame container carrying `data`, `size`,
  `width`, `height`, `PixelFormat`, per-plane `strides` and `pts`. Frames are
  released through `frame.release()`.
- `CodecParams` (`codec_config.h`) — parameterized init config (device index,
  inputs, frame format, extradata).
- `Registry<T>` (`registry.h`) — shared factory template; adapters self-register
  via the `HALCODEC_CONNECT(Base, Name, Class)` macro at static-init time.

## Registered backends

| Backend     | Device    | Decoder  | Encoder  | Platform    |
|-------------|-----------|----------|----------|-------------|
| NVIDIA NVENC/NVDEC | `nvidia` | `nvdec` | `nvenc` (empty) | Linux + CUDA |
| NVIDIA NVJPEG | -        | `nvjpeg` | -        | Linux + CUDA |
| Apple VideoToolbox | -     | `vtbox`  | -        | macOS        |

NvMedia (tegra) and AMD AMF adapters are planned.

## Layout

```
src/                 unified interface headers
NVEnc/layers/        NVIDIA adapter (nvenc namespace)
NVJPEG/layers/       NVIDIA JPEG adapter (nvjpeg namespace)
VideoToolbox/layers/ Apple adapter (vtbox namespace)
app/                 example CLIs: hal_dec, hal_enc, codecinfo
```

## Build

```sh
cmake -B build && cmake --build build
```

On Linux + CUDA, the NVEnc/NVJPEG targets are enabled automatically;
`app/CMakeLists.txt` force-loads the shared layers (`--no-as-needed`) so their
static self-registration runs.