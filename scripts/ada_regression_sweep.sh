#!/bin/bash
# Full NVIDIA regression sweep on ada after the primary-context switch.
#
# The context change touched every nvenc-family backend (nvdec, nvenc, nvjpeg),
# and the failure mode it can introduce is shape-dependent: a stale thread
# context shows up as CUDA_ERROR_INVALID_CONTEXT the first time a session
# creates a stream, which for a tiny 320x240 clip can hide behind retries.
# Run the larger multi-stream and JPEG cases too, not just the fast one.
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH=$PWD

pass=0
fail=0
check() {  # $1 = label; rest = command
    local label=$1; shift
    if "$@" >/tmp/reg.log 2>&1; then
        echo "PASS  $label"
        pass=$((pass + 1))
    else
        echo "FAIL  $label"
        tail -5 /tmp/reg.log | sed 's/^/        /'
        fail=$((fail + 1))
    fi
}

check "nvdec decode h264 (demuxer)" \
    ./hal_dec -b nvdec --codec h264 -i cars_320x240.h264 -o /tmp/r1.yuv
check "nvdec decode h264 (feed mode)" \
    ./hal_transcode -b nvdec --codec h264 -i cars_320x240.h264 -o /tmp/r2.h264
check "nvdec->nvenc transcode host" \
    ./hal_transcode -b nvdec --codec h264 -i cars_320x240.h264 -o /tmp/r3.h264
check "nvdec->nvenc transcode zero-copy" \
    ./hal_transcode -b nvdec --codec h264 -i cars_320x240.h264 -o /tmp/r4.h264 -z
check "nvenc encode from yuv" \
    ./hal_enc -b nvenc --codec h264 -i cars_320x240.nv12 -o /tmp/r5.h264
check "nvjpeg decode" \
    ./hal_dec -b nvjpeg -i test_cars_320x240.jpg -o /tmp/r6.yuv -f yuv
check "nvjpegenc encode" \
    ./hal_enc -b nvjpegenc -i test_cars_320x240.bmp -o /tmp/r7.jpg -f bmp
check "codecinfo" ./codecinfo
check "multi-stream session" \
    ./hal_session -b nvdec --codec h264 \
        -i cars_320x240.h264 -i cars_320x240.h264 -i cars_320x240.h264 \
        -o /tmp/ms.yuv

echo "-----"
echo "$pass passed, $fail failed"

# Decoder capability table must not be empty: an empty table is the signature
# of the cuvidGetDecoderCaps-without-a-current-context bug.
echo "nvdec capability rows:"
rows=$(./codecinfo -b nvidia 2>/dev/null |
    sed -n '/decoder capabilities/,/encoder capabilities/p' |
    grep -cE '^(H264|HEVC|MPEG1|MPEG2|MPEG4|VC1|VP8|VP9|AV1|JPEG) ')
echo "$rows"
[ "$rows" -ge 10 ] || { echo "FAIL  decoder capability table empty/truncated"; fail=$((fail + 1)); }
exit $fail
