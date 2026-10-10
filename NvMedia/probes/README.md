# NvMedia codec-support probes

Standalone aarch64 programs that test which codecs the *deployed runtime
library* accepts — independent of the halcodec framework. They exist to tell
a lib-version limit (reduced build) from a hardware/SKU limit.

## Why

nv118 (T234) ships a **reduced** `libnvmedia_ide_parser` / `libnvmedia_iep_sci`
in `/usr/lib` that rejects most codecs at create time ("Codec Not supported").
The matching DRIVE-OS 7.0.3.0 SDK `lib-target`
(`/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target`) is the **full**
build and accepts them. These probes make that difference measurable on the
board by swapping `LD_LIBRARY_PATH`.

- `codec_probe.cc` — calls `NvMediaParserCreate` for every `NvMediaVideoCodec`
  (decode side). No NvSciBuf needed.
- `iep_probe.cc` — builds a reconciled NV12 `NvSciBufAttrList` and calls
  `NvMediaIEPCreate` for H264/HEVC/VP9/AV1 (encode side).

## Build (cross-compile on the dev box)

```sh
SDK=/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux
aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
    -I$SDK/include -I$SDK/include/nvmedia_6x \
    codec_probe.cc -o codec_probe \
    -Wl,-rpath-link,$SDK/lib-target -L$SDK/lib-target \
    -lnvmedia_ide_parser -lnvparser -lnvrm_surface -lnvos -lnvsocsys \
    -lnvscibuf -lnvscisync -lnvvideo -ldl

aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
    -I$SDK/include -I$SDK/include/nvmedia_6x \
    iep_probe.cc -o iep_probe \
    -Wl,-rpath-link,$SDK/lib-target -L$SDK/lib-target \
    -lnvmedia_iep_sci -lnvvideo -lnvscibuf -lnvscisync \
    -lnvrm_surface -lnvos -lnvsocsys -ldl
```

## Run on nv118

```sh
scp codec_probe iep_probe nv118:/tmp/

# Against the system /usr/lib (reduced build):
/tmp/codec_probe
/tmp/iep_probe

# Against the full SDK lib-target (deploy first — tar preserves symlinks;
# `scp -r *.so*` corrupts them):
ssh nv118 'mkdir -p ~/sdk_libs'
tar czf - -C $SDK/lib-target libnvmedia_*.so* libnvparser*.so* \
    libnvrm_*.so* libnvos.so libnvsocsys.so libnvsci*.so* libnvvideo*.so* \
    libnvtegrahv.so | ssh nv118 'tar xzf - -C ~/sdk_libs'
ssh nv118 'LD_LIBRARY_PATH=$HOME/sdk_libs /tmp/codec_probe'
ssh nv118 'LD_LIBRARY_PATH=$HOME/sdk_libs /tmp/iep_probe'
```

## Expected (audited 2026-10-10)

| probe | system libs | SDK libs |
|-------|-------------|----------|
| codec_probe (decode) | h264 + hevc only | all 11 codecs OK |
| iep_probe (encode)   | h264 + hevc only | h264 + hevc + **av1**; vp9 fails in both |

The framework deploy (`~/halcodec-nv`) currently loads the **system** libs at
runtime, so hal_dec/hal_enc still see the reduced matrix until the SDK
lib-target is shipped into the deploy dir and put first on `LD_LIBRARY_PATH`.
