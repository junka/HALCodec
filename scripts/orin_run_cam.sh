#!/usr/bin/env bash
# Run hal_cam's SIPL path on the DRIVE-OS board with a library path that does
# NOT shadow the system nvsci libs.
#
# Why this exists (established on the board, 2026-05):
#   * The SDK lib dir (~/halcodec-nv/sdk_libs) must NOT be ahead of the system
#     libs: its libnvscibuf.so.1 (md5 460f5048490d3d0c) differs from the system
#     one (9bb4998e00313d49), and SIPL's ICP RegisterImages returns ERROR under
#     the SDK copy. libnvscisync / libnvscicommon are identical either way.
#   * But the nvmedia backend NEEDs libnvmedia_ijpd_sci.so, which exists ONLY in
#     sdk_libs, and that ijpd resolves NvVideoJPEGDecoderSetAttributes from the
#     SDK libnvvideo.so (the system libnvvideo.so lacks that symbol).
# So we stage exactly those two into a private dir and put only that dir (plus
# the deploy dir for our own core lib + boost) on LD_LIBRARY_PATH. Everything
# else — libnvscibuf, libnvscisync, libnvrm_*, libnvos — resolves from /usr/lib
# and /lib as the SIPL stack expects.
#
# Usage (on the board, or via `ssh orin ...`):
#   bash ~/halcodec-nv/orin_run_cam.sh [extra hal_cam args...]
# Env overrides:
#   SIPL_PLATFORM  (default GWM_MULTIPLE_CAMERA_THORU_GROUPB)
#   SIPL_MASK      (default "0x0111 0x1111 0x1111 0x0")
#   SIPL_VERBOSE   (default 0)
#   NUM_CAMERAS    (default 1)
#   CAM_TIMEOUT    (default 90)
#   SDK_LIBS       (default $HOME/halcodec-nv/sdk_libs)
set -u

cd "$HOME/halcodec-nv" || exit 1
SDK=${SDK_LIBS:-$HOME/halcodec-nv/sdk_libs}
MIN=/tmp/pin_min
mkdir -p "$MIN"
for lib in libnvmedia_ijpd_sci.so libnvvideo.so; do
    cp -f "$SDK/$lib" "$MIN/" 2>/dev/null
    for s in "$SDK/$lib".*; do
        [ -e "$s" ] && cp -f "$s" "$MIN/"
    done
done

# Unbuffered stdout: a crash otherwise loses the last prints, and SIPL's own
# verbosity goes to stderr where a block-buffered pipe would swallow it.
LD_LIBRARY_PATH=".:$MIN" exec stdbuf -o0 -e0 \
    timeout "${CAM_TIMEOUT:-90}" ./hal_cam \
    --source sipl \
    --sipl-platform "${SIPL_PLATFORM:-GWM_MULTIPLE_CAMERA_THORU_GROUPB}" \
    --sipl-mask "${SIPL_MASK:-0x0111 0x1111 0x1111 0x0}" \
    --sipl-verbose "${SIPL_VERBOSE:-0}" \
    --num-cameras "${NUM_CAMERAS:-1}" \
    -b nvmedia "$@"
