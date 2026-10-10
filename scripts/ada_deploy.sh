#!/usr/bin/env bash
# Build the nvenc/nvdec backend + the decode/transcode apps and push them to ada.
#
# The app/ copies of the backend .so are produced by a POST_BUILD step, which
# only runs when its app target actually relinks. Building just `nvenc_layers`
# leaves build/app/libnvenc_layers.so stale, and deploying that makes a code
# change look like it had no effect. Copy from the canonical build output
# instead of trusting the staged copy.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build"
remote=${ADA_DIR:-halcodec_nv}

cmake --build "$build" -j"$(nproc)" --target \
    nvenc_layers nvjpeg_layers hal_dec hal_transcode hal_session hal_enc codecinfo

cp "$build/NVEnc/layers/libnvenc_layers.so"   "$build/app/libnvenc_layers.so"
cp "$build/NVJpeg/layers/libnvjpeg_layers.so" "$build/app/libnvjpeg_layers.so" 2>/dev/null || true

scp -q "$build/app/libnvenc_layers.so" \
       "$build/app/libnvjpeg_layers.so" \
       "$build/libhalcodec_core.so" \
       "$build/app/hal_dec" "$build/app/hal_enc" \
       "$build/app/hal_transcode" "$build/app/hal_session" \
       "$build/app/codecinfo" \
       "ada:~/$remote/"
echo "deployed to ada:~/$remote/"
