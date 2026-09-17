#!/usr/bin/env bash
# regression_nvenc.sh — async NVDEC/NVENC regression on a remote NVIDIA host.
#
# Compiles the NVEnc backends locally (this machine has the SDKs but no GPU),
# deploys the binaries + their full transitive .so dependency tree to a remote
# host that has an NVIDIA driver (and libnvidia-encode/libnvcuvid/libcuda) but
# no toolchain, then runs three async-path regressions there:
#
#   1. async NVDEC decode  (cars_320x240.h264 -> NV12, 30 frames)
#   2. async NVENC encode  (NV12 -> H.264, 30 frames)  + NVDEC round-trip,
#      asserting Y/UV PSNR >= threshold on every frame (catches chroma-layout
#      regressions like feeding NV12 bytes as I420).
#   3. multi-stream async NVDEC via hal_session (3 streams, fan-in), asserting
#      every stream's output is byte-identical to the single-stream decode.
#
# Usage:
#   scripts/regression_nvenc.sh [remote]
#       remote   SSH host with an NVIDIA GPU (default: ada)
#
# Exits non-zero if any assertion fails. Designed for CI / manual regression.
set -euo pipefail

REMOTE="${1:-ada}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build"
ASSET_H264="$REPO/QSV/libvpl-2.17.0/examples/content/cars_320x240.h264"
ASSET_NV12="$REPO/QSV/libvpl-2.17.0/examples/content/cars_320x240.nv12"
REMOTE_DIR="halcodec_nv"
# Minimum per-plane PSNR (dB) for the encode round-trip. p1/VBR@10M on
# cars_320x240 comfortably clears ~45 dB on both planes; this leaves margin.
PSNR_FLOOR=45

log() { printf '\n\033[1;34m=== %s ===\033[0m\n' "$*"; }
fail() { printf '\033[1;31mFAIL: %s\033[0m\n' "$*" >&2; exit 1; }

[ -f "$ASSET_H264" ] || fail "missing asset $ASSET_H264"
[ -f "$ASSET_NV12" ] || fail "missing asset $ASSET_NV12"

# ---------------------------------------------------------------------------
# 1. Build locally.
# ---------------------------------------------------------------------------
log "build (local)"
cmake --S "$REPO" --B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 || true
cmake --build "$BUILD" --target hal_dec hal_enc hal_session nvenc_layers -j

# POST_BUILD copy only fires on exe relink; refresh the app-dir copy explicitly
# so the deployed .so matches the freshly built one.
cp "$BUILD/NVEnc/layers/libnvenc_layers.so" "$BUILD/app/libnvenc_layers.so"

# ---------------------------------------------------------------------------
# 2. Stage binaries + core lib + boost + full transitive .so dependency tree
#    (the remote has no ffmpeg/boost/vpl/nvjpeg runtime libs) + test assets.
# ---------------------------------------------------------------------------
log "stage dependency tree"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp "$BUILD/app/hal_dec" "$BUILD/app/hal_enc" "$BUILD/app/hal_session" \
   "$BUILD/app/libnvenc_layers.so" "$BUILD/app/libnvjpeg_layers.so" \
   "$BUILD/app/libamf_layers.so" "$BUILD/app/libqsv_layers.so" \
   "$BUILD/libhalcodec_core.so" "$STAGE/"
cp /lib/x86_64-linux-gnu/libboost_program_options.so.1.83.0 "$STAGE/" 2>/dev/null || \
    ldd "$BUILD/app/hal_dec" | grep -oE '/[^ ]*libboost_program_options[^ ]*' | head -1 | xargs -I{} cp -L {} "$STAGE/"
cp "$ASSET_H264" "$ASSET_NV12" "$STAGE/"

# Recursively resolve every .so the binaries/layer-libs need; copy real paths.
ldd "$BUILD/app/hal_dec" "$BUILD/app/hal_enc" "$BUILD/app/hal_session" \
    "$BUILD/app/libnvenc_layers.so" "$BUILD/app/libnvjpeg_layers.so" \
    "$BUILD/app/libamf_layers.so" "$BUILD/app/libqsv_layers.so" 2>&1 \
  | grep -oE '/[^ ]+\.so\.[0-9.]+' | sort -u \
  | grep -ivE 'libcuda|libnvidia-encode|libnvcuvid|libstdc|libgcc_s|libunwind|libpthread|libc\.so|libm\.so|libz\.so|libdl|librt|libidn|libcrypt|libresolv|libanl|libmvec' \
  | while read -r p; do [ -f "$p" ] && cp -L "$p" "$STAGE/"; done

# ---------------------------------------------------------------------------
# 3. Deploy to remote (clean slate each run).
# ---------------------------------------------------------------------------
log "deploy to $REMOTE:$REMOTE_DIR"
ssh "$REMOTE" "rm -rf ~/$REMOTE_DIR && mkdir -p ~/$REMOTE_DIR"
rsync -a "$STAGE/" "$REMOTE:$REMOTE_DIR/"

run() { ssh "$REMOTE" "cd ~/$REMOTE_DIR && export LD_LIBRARY_PATH=\$PWD && $*"; }

# ---------------------------------------------------------------------------
# 4. Regression 1: async NVDEC decode.
# ---------------------------------------------------------------------------
log "regression 1: async NVDEC decode"
run ./hal_dec -b nvdec -i cars_320x240.h264 -o dec.yuv -f nv12 2>&1 | tail -3
run 'bash -c "[ -s dec.yuv ]"' || fail "dec.yuv empty"
DEC_BYTES=$(run 'stat -c %s dec.yuv')
[ "$DEC_BYTES" = "3456000" ] || fail "decode size $DEC_BYTES != 3456000 (30 * 320*240*1.5)"

# ---------------------------------------------------------------------------
# 5. Regression 2: async NVENC encode (NV12) + round-trip + PSNR.
# ---------------------------------------------------------------------------
log "regression 2: async NVENC NV12 encode + round-trip"
run ./hal_enc -b nvenc -i cars_320x240.nv12 -o enc.h264 -f nv12 2>&1 | tail -2
run 'bash -c "[ -s enc.h264 ]"' || fail "enc.h264 empty"
run ./hal_dec -b nvdec -i enc.h264 -o rt.yuv -f nv12 2>&1 | tail -2
RT_BYTES=$(run 'stat -c %s rt.yuv')
[ "$RT_BYTES" = "3456000" ] || fail "round-trip size $RT_BYTES != 3456000"

# Per-frame Y + UV PSNR vs the original NV12. Asserts >= PSNR_FLOOR on both
# planes of every frame; also asserts the frame counts match (no dropped/dup).
run python3 - <<PYEOF
import math, sys
w,h=320,240; ysz=w*h; cysz=w*h//2; fsz=ysz+cysz; floor=$PSNR_FLOOR
a=open("cars_320x240.nv12","rb").read(); b=open("rt.yuv","rb").read()
na,nb=len(a)//fsz,len(b)//fsz
if na!=nb or na!=30:
    print(f"frame count mismatch: orig={na} rt={nb} (expected 30)"); sys.exit(1)
def psnr(sa,sb,n):
    mse=sum((x-y)**2 for x,y in zip(sa,sb))/n
    return 99.0 if mse==0 else 10*math.log10(255*255/mse)
worst_y=worst_c=99.0
for fr in range(na):
    o=fr*fsz
    yp=psnr(a[o:o+ysz],b[o:o+ysz],ysz)
    cp=psnr(a[o+ysz:o+fsz],b[o+ysz:o+fsz],cysz)
    worst_y=min(worst_y,yp); worst_c=min(worst_c,cp)
    if yp<floor or cp<floor:
        print(f"frame {fr}: Y={yp:.2f} UV={cp:.2f} below floor {floor}"); sys.exit(1)
print(f"round-trip OK: 30 frames, worst Y={worst_y:.2f} dB, worst UV={worst_c:.2f} dB (floor {floor})")
PYEOF
[ $? -eq 0 ] || fail "round-trip PSNR below floor or frame-count mismatch"

# ---------------------------------------------------------------------------
# 6. Regression 3: multi-stream async NVDEC (3 streams, fan-in).
# ---------------------------------------------------------------------------
log "regression 3: multi-stream async NVDEC (3 streams)"
run ./hal_session -b nvdec \
    -i cars_320x240.h264 -i cars_320x240.h264 -i cars_320x240.h264 \
    -o sess -f nv12 2>&1 | tail -3
for s in 0 1 2; do
    SZ=$(run "stat -c %s sess.$s.nv12")
    [ "$SZ" = "3456000" ] || fail "stream $s size $SZ != 3456000"
    run "cmp -s dec.yuv sess.$s.nv12" || fail "stream $s not byte-identical to single-stream decode"
done

log "ALL REGRESSIONS PASSED"
