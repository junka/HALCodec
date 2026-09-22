#!/usr/bin/env bash
# Cross-build the NvMedia backend + apps and push them to the DRIVE-OS board.
#
# Same idea as b3_deploy.sh (the aarch64 twin): build the targets through the
# cross toolchain file, copy the canonical backend .so into build-drive/app/ so
# the POST_BUILD staging step cannot leave a stale copy behind, then scp. The
# board has no rsync, so scp only.
#
# The board resolves libnvscibuf.so.1 / libnvscisync.so.1 from /usr/lib at
# runtime; run with LD_LIBRARY_PATH=$PWD so the freshly deployed core lib and
# project boost win over any system copy.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build-drive"
remote=${NV118_DIR:-halcodec-nv}

cmake --build "$build" -j4 --target \
    nvmedia_layers hal_dec hal_transcode hal_enc hal_camera codecinfo

cp "$build/NvMedia/layers/libnvmedia_layers.so" "$build/app/libnvmedia_layers.so"

scp -q "$build/app/libnvmedia_layers.so" \
       "$build/libhalcodec_core.so" \
       "$build/app/hal_dec" \
       "$build/app/hal_transcode" \
       "$build/app/hal_enc" \
       "$build/app/hal_camera" \
       "$build/app/codecinfo" \
       "nv118:~/$remote/"
echo "deployed to nv118:~/$remote/"
