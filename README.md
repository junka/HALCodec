# HALCodec

Vendors provide their own hardware-specific encoders and decoders. This project
aims to simplify the usage of hw codec by exposing one unified C++ interface
(`halcodec`) over every vendor adapter.

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

## Registered backends

| Backend     | Decoder  | Encoder  | Caps provider | Platform    |
|-------------|----------|----------|---------------|-------------|
| NVIDIA NVENC/NVDEC | `nvdec` | `nvenc` (empty) | `nvidia` | Linux + CUDA |
| NVIDIA NVJPEG | `nvjpeg` | -        | -             | Linux + CUDA |
| AMD AMF     | `amfdec` | `amfenc` (empty) | `amf`    | Linux + AMD |
| Intel QSV   | `qsvdec` | `qsvenc` (empty) | `qsv`    | Linux + Intel iGPU |
| Apple VideoToolbox | `vtbox`  | -        | `vtbox`       | macOS        |

NvMedia (tegra) is planned. The AMF adapter is built only when the AMF SDK
headers are present under `AMF/AMF-*/amf/public/include`; the AMF runtime
(`libamfrt64.so.1`) is dlopen'd at runtime and is required on the machine
where the binary runs. Same for QSV: built only when libvpl headers exist
under `QSV/libvpl-*/api`, with `libvpl.so.2` + an Intel media driver expected
at runtime.

## Layout

```
src/                 unified interface headers
NVEnc/layers/        NVIDIA adapter (nvenc namespace)
NVJPEG/layers/       NVIDIA JPEG adapter (nvjpeg namespace)
VideoToolbox/layers/ Apple adapter (vtbox namespace)
AMF/layers/          AMD adapter (amd namespace), Linux-only
QSV/layers/          Intel QSV/libvpl adapter (qsv namespace), Linux-only
app/                 example CLIs
```

## Command-line tools (`app/`)

All three tools exercise only the unified interface plus the backend registry,
so they work against any registered backend without recompilation. Pick a
backend with `-b/--backend`; when omitted, the platform default is used:

| Tool      | Default backend         | Alternative backends |
|-----------|-------------------------|----------------------|
| `hal_dec` | macOS: `vtbox`, Linux: `nvdec` | `nvjpeg`, `amfdec`, `qsvdec` |
| `hal_enc` | `nvenc`                 | `amfenc`, `qsvenc` |
| `codecinfo` | all registered capability providers | `-b` restricts to one |

Common options (from `app/parse_cli.h`):

| Option | Meaning |
|--------|---------|
| `-h, --help` | show the full option list and exit |
| `-i, --input <file\|dir>` | input path |
| `-o, --output <file>` | output path (defaults to `<input without ext>.<format>`) |
| `-b, --backend <name>` | backend name, e.g. `vtbox` / `nvdec` / `nvjpeg` / `nvenc` |
| `--gpu <idx>` | device ordinal (default `0`) |
| `-f, --format <ext>` | output extension (default `yuv`) |

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
  `<input without ext>.<format>`. For a directory, every regular file maps to
  `<name without ext>.<format>` in the same directory. Directory input is only
  meaningful for backend decoders that walk the path themselves (e.g. `nvjpeg`
  on Linux); `vtbox` requires a single file, since its parameter sets come from
  one Annex-B stream.
- **Decode loop**: synchronous backends are driven by `PullFrames()` (feeds
  input, then each returned frame is written via `GetFrame()` and released with
  `frame.release()`). Async backends (`vtbox`, `isAsync() == true`) are fed
  chunk-by-chunk with `FillInput()` and drained until `GetFrame()` returns
  false (blocking-until-EOF). The `nvjpeg` backend switches to a new output
  file per frame.
- **Current status**: `vtbox` decodes asynchronously via its internal queue
  — `FillInput()` accepts Annex-B access units, frames arrive from the
  decompression callback and `GetFrame()` blocks until the stream ends.

### hal_enc — encode

```sh
cd build
./app/hal_enc -i raw_320x240.yuv -f yuv -o out.h264     # single raw stream
./app/hal_enc -i images/ -b nvjpegenc -f rgb            # one frame per file
```

Purpose: the encoder-side counterpart of `hal_dec`, demonstrating the
`Encoder` factory and the explicit feed model. The app owns input reading:
- single file inputs are sliced into fixed-size frames (bytes per frame are
  derived from `-f` and the `WxH` resolution), each delivered as one
  `CodecFrame` via `FillFrame()`; BMP input is read as a single image;
- directory inputs feed one file per `FillFrame()` call;
- an empty marker frame ends the stream, letting the encoder flush trailing
  packets, drained with `GetFrame()` until it returns false.

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

## Build

```sh
cmake -B build && cmake --build build
```

On Linux + CUDA, the NVEnc/NVJPEG targets are enabled automatically;
`app/CMakeLists.txt` force-loads the shared layers (`--no-as-needed`) so their
static self-registration runs.