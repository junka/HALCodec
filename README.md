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
  `<input without ext>.<format>`. For a directory, every regular file maps to
  `<name without ext>.<format>` in the same directory. `--format` is the right
  source for that extension on this side: a decoder's output is raw pixels, and
  the layout asked for is exactly what the files hold. Directory input is only
  meaningful for backend decoders that walk the path themselves (e.g. `nvjpeg`
  on Linux); `vtbox` requires a single file, since its parameter sets come from
  one Annex-B stream. `hal_enc` names its outputs by codec instead, because its
  `--format` describes what it reads -- see its section.
  **This side still has the hazard `hal_enc` just lost**: a directory walk here
  opens the first output name before decoding starts (`hal_dec.cc:418`), so
  `-f` matching the inputs' extension truncates one of them, and the name list
  is consumed one entry per *decoded frame* by the BMP and `nvjpeg` paths, which
  walks off the end when the frames outnumber the files. Neither is fixed.
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

## Build

```sh
cmake -B build && cmake --build build
```

On Linux + CUDA, the NVEnc/NVJPEG targets are enabled automatically;
`app/CMakeLists.txt` force-loads the shared layers (`--no-as-needed`) so their
static self-registration runs.