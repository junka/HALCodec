#!/usr/bin/env bash
# Cross-build the NvMedia backend + apps and push them to the DRIVE-OS board.
#
# Same idea as ada_deploy.sh (the aarch64 twin): build the targets through the
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
# `remote` is the ssh alias; `destdir` is the directory on the board. The target
# board is reached through the `orin` alias (10.49.18.118) and the tree lives in
# ~/halcodec-nv, so both defaults line up; override either independently.
remote=${ORIN_DIR:-orin}
destdir=${ORIN_DEST_DIR:-halcodec-nv}

# hal_cam is the camera app (SIPL / V4L2 / AV per platform) and it carries the
# camera/ layers 1-3 plus the nvsciipc channel. hal_camera no longer exists (it
# was merged into hal_cam), so naming it here only fails the build.
cmake --build "$build" -j4 --target \
    nvmedia_layers hal_dec hal_transcode hal_enc hal_cam codecinfo

cp "$build/NvMedia/layers/libnvmedia_layers.so" "$build/app/libnvmedia_layers.so"

# The cross tree has no libboost_program_options.so (the toolchain finds it from
# the SDK sysroot at link time); the board's deploy dir already carries the
# matching 1.83.0 copy, resolved via LD_LIBRARY_PATH=$PWD.
files=(
    "$build/app/libnvmedia_layers.so"
    "$build/libhalcodec_core.so"
    "$build/app/hal_dec"
    "$build/app/hal_transcode"
    "$build/app/hal_enc"
    "$build/app/hal_cam"
    "$build/app/codecinfo"
)
scp -q "${files[@]}" "$remote:$destdir/"
echo "deployed to $remote:$destdir/"
