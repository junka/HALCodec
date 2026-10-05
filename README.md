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
| NVIDIA DRIVE NvMedia | `nvmedia` | `nvmedia` (empty) | `nvmedia` | Linux (DRIVE OS) aarch64 |
| Apple VideoToolbox | `vtbox`  | `vtenc`  | `vtbox`       | macOS        |

The NvMedia adapter targets NVIDIA DRIVE OS (Linux aarch64) and is built only
when the SDK headers are present under `NvMedia/include/nvmedia_6x` plus
`lib-target/`. Its data path is currently an initialization stub (the IDE/IEP
engines are created, decode/encode frames are not yet wired). The AMF adapter
is built only when the AMF SDK
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
NvMedia/layers/      NVIDIA DRIVE OS adapter (nvmedia namespace), Linux-aarch64-only
app/                 example CLIs
tools/               helper scripts (SDK update/import)
```

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
tools/update_sdks.sh --prune amf     # also drop .gitignore'd big dirs (AMF Thirdparty)
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
  excludes (`AMF/AMF-*/Thirdparty`, CI metadata under `.github/`).

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

## Command-line tools (`app/`)

All three tools exercise only the unified interface plus the backend registry,
so they work against any registered backend without recompilation. Pick a
backend with `-b/--backend`; when omitted, the platform default is used:

| Tool      | Default backend         | Alternative backends |
|-----------|-------------------------|----------------------|
| `hal_dec` | macOS: `vtbox`; Linux aarch64: `nvmedia`; other Linux: `nvdec` | `nvjpeg`, `amfdec`, `qsvdec` |
| `hal_enc` | the first registered encoder backend: `vtenc` on macOS, `nvenc` on Linux + CUDA | `amfenc`, `qsvenc`, `nvmedia` |
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

## Measured throughput on Apple M5

Numbers come from an Apple M5 Max running macOS 26.3, `vtbox` backend, built
from a Release tree:

```sh
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j
```

The build type matters more than anything else here. The same code, the same
material, the Debug tree next to the Release one:

| 1080p, whole process | Debug | Release |
| --- | ---: | ---: |
| H.264 encode | 56.5 fps | 218.7 fps |
| ProRes 422 decode | 181.7 fps | 759.5 fps |
| H.264 decode | 578.9 fps | 787.3 fps |

At `-O0` the element-wise destruction of `hal_enc`'s whole-file input buffer,
and of every frame copy on the decode side, is a genuine share of the wall
time, so a Debug-tree rate measures the compiler flags as much as the codec.
Every number below is from the Release tree.

Method: `hal_dec` / `hal_enc` run end to end (exec to exit), decoded or encoded
frames discarded with `-o /dev/null`, best of three passes, and the lowest
pass recorded below in whole fps. Material is ffmpeg-generated at
`ultrafast` / `preset 8`, 200 frames at 25 fps for 1080p and 80 frames for 4K.
Reported frame counts are checked against the input length, so a short run
shows up as a failure rather than a rate. Run-to-run spread is about 12% for
the video codecs and up to 20% on the ProRes tiers, so the last digit is noise.

### Decode

| feed | 1080p | 4K | software decode, 1080p |
| --- | ---: | ---: | ---: |
| H.264 High | 766 fps | 246 fps | 3283 fps |
| HEVC Main 8 | 820 | 281 | 2601 |
| HEVC Main 10 | 484 | 124 | 1205 |
| HEVC Main 4 2:2 10 | 378 | — | 1017 |
| HEVC Monochrome 8 | 1014 | — | 2957 |
| HEVC Monochrome 10 | 895 | — | 1549 |
| AV1 Main 8 | 814 | — | 1400 |
| AV1 Main 10 | 484 | — | 1132 |
| ProRes 422 | 765 | 244 | 1489 |
| ProRes 4444 | 465 | — | 619 |

A `—` is a case the matrix did not cover, not a failure. The last column is
ffmpeg with all cores and no VideoToolbox, measured by the same whole-process
method; it clears the hardware because it never has to hand a frame back to a
caller, so read it as a floor to beat, not a like-for-like rate.

### Encode

| encode | input | 1080p | 4K |
| --- | --- | ---: | ---: |
| H.264 High | NV12 | 210 fps | 90 fps |
| HEVC Main 8 | NV12 | 198 | 91 |
| HEVC Main 10 | P010 | 186 | 77 |
| HEVC Main 4 2:2 10 | P210 | 170 | — |
| HEVC Monochrome 8 | NV12 | 199 | — |
| HEVC Monochrome 10 | P010 | 182 | — |
| ProRes Proxy | P210 | 475 | — |
| ProRes LT | P210 | 356 | — |
| ProRes 422 | P210 | 475 | — |
| ProRes 422 HQ | P210 | 474 | — |
| ProRes 4444 | P210 | 309 | — |
| ProRes 4444 XQ | P210 | 262 | — |
| JPEG, q90 | NV12 | 137 img/s | — |

The video profiles all land within about 20% of one rate whatever the profile
or bit depth, which is what an XPC-bound session looks like:
`VTCompressionSessionEncodeFrame` blocks in
`xpc_connection_send_message_with_reply_sync`, so the wait is the codec
engine's own pace rather than the work asked of it. ProRes runs ahead of it
and its tiers differ only by output size because that session rejects rate
control outright: `AverageBitRate`, `Quality`, `MaxKeyFrameInterval` and
`DataRateLimits` each answer `kVTPropertyNotSupportedErr`.

### What actually moves these numbers

- **Keeping the decoded frames.** Writing the raw output halves the rate:
  1080p H.264 to `/dev/null` 766 fps, to a 622 MB file 368 fps; 4K HEVC Main 10
  to `/dev/null` 124 fps, to a 1.99 GB file 52 fps. In a pipeline that stores
  frames the decoder is not the limit.
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
  that difference is a small fraction of either run, so treat it as ±20%. A
  multi-picture run would spread the 58 ms, but the feeds here carry one image
  per sample, so no measured case does. JPEG encode is the multi-picture
  counterexample: 730 ms for a directory of 100 1080p q90 frames, 7.3
  ms/picture, every output verified as decodable 1920x1080 mjpeg. ProRes RAW is
  a 2-picture 4112x2176 sample (81.6 ms), so its ~10 ms/picture is indicative
  only.

### No hardware path on this machine

- **AV1 encode**: `VTCopyVideoEncoderList` has no AV1 entry on M5, so AV1 is
  decode-only.
- **ProRes RAW encode**: decode-only.
- ffmpeg's `-hwaccel videotoolbox` rates are deliberately left out. It falls
  back to the software decoder without saying so on profiles it does not hand
  over, and the results show it: Monochrome 8 "hardware" at 2823 fps is its own
  CPU rate, and Main 10 at 805 fps beats Main 8 at 508 fps. A column like that
  is not a hardware measurement.

## Build

```sh
cmake -B build && cmake --build build
```

On Linux + CUDA, the NVEnc/NVJPEG targets are enabled automatically;
`app/CMakeLists.txt` force-loads the shared layers (`--no-as-needed`) so their
static self-registration runs.