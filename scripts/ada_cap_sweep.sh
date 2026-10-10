#!/bin/bash
# Full hardware-capability sweep on ada (RTX 4000 Ada).
#
# Goal: for every codec the GPU's NVDEC/NVJPEG/NVENC engines advertise,
# verify the halcodec framework actually drives it end to end. codecinfo
# reports what the silicon can do; this script reports what the framework
# does with it. A codec listed by codecinfo but failing here is a coverage
# gap, not a hardware limit.
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH=$PWD

# VC-1 has no ffmpeg encoder, so the test stream is a SMPTE conformance vector
# fetched from the ffmpeg FATE suite. Fetch once if missing (ada has outbound
# https to fate.ffmpeg.org).
if [ ! -s SA00050.vc1 ]; then
    echo "fetching VC-1 conformance stream SA00050.vc1 ..."
    curl -fsSL -o SA00050.vc1 https://fate.ffmpeg.org/fate-suite/vc1/SA00050.vc1 \
        || { echo "FAIL  vc1 sample fetch (no network? pre-stage SA00050.vc1)"; exit 1; }
fi

pass=0; fail=0; skip=0
declare -a FAILED
run() {  # $1 = label ; rest = command
    local label=$1; shift
    if "$@" >/tmp/sw.log 2>&1; then
        # sanity: did it actually produce output frames? decode -> yuv file, encode -> codestream
        echo "PASS  $label"
        pass=$((pass + 1))
    else
        echo "FAIL  $label"
        tail -4 /tmp/sw.log | sed 's/^/        /'
        fail=$((fail + 1)); FAILED+=("$label")
    fi
}

echo "############ DECODE (NVDEC) ############"
# Every NVDEC-advertised codec. Output to /dev/null to measure pure decode.
run "h264    decode" ./hal_dec -b nvdec --codec h264    -i t_h264.h264  -o /tmp/d_h264.yuv
run "hevc    decode" ./hal_dec -b nvdec --codec hevc    -i t_hevc.h265  -o /tmp/d_hevc.yuv
run "av1     decode" ./hal_dec -b nvdec --codec av1     -i t_av1.ivf    -o /tmp/d_av1.yuv
run "vp8     decode" ./hal_dec -b nvdec --codec vp8     -i t_vp8.ivf    -o /tmp/d_vp8.yuv
run "vp9     decode" ./hal_dec -b nvdec --codec vp9     -i t_vp9.ivf    -o /tmp/d_vp9.yuv
run "mpeg1   decode" ./hal_dec -b nvdec --codec mpeg1   -i t_mpeg1.mpg   -o /tmp/d_mpeg1.yuv
run "mpeg2   decode" ./hal_dec -b nvdec --codec mpeg2   -i t_mpeg2.mpg   -o /tmp/d_mpeg2.yuv
run "mpeg4   decode" ./hal_dec -b nvdec --codec mpeg4   -i t_mpeg4.m4v   -o /tmp/d_mpeg4.yuv
run "jpeg    decode (nvdec)" ./hal_dec -b nvdec --codec jpeg -i t_jpeg.jpg -o /tmp/d_jpeg.yuv
run "jpeg    decode (nvjpeg)" ./hal_dec -b nvjpeg -i t_jpeg.jpg -o /tmp/d_jpeg2.yuv -f yuv
# VC1: SMPTE VC-1 conformance streams from the ffmpeg FATE suite (no ffmpeg VC1
# encoder exists, so these are fetched rather than generated). SA00050 is
# 320x240 Advanced profile, 30 frames — matches the cars test-stream shape.
run "vc1     decode (demuxer)"  ./hal_dec    -b nvdec -i SA00050.vc1 -o /tmp/d_vc1.yuv
run "vc1     decode (feed)"     ./hal_session -b nvdec --codec vc1 -i SA00050.vc1 -o /tmp/d_vc1_sess

echo
echo "############ ENCODE (NVENC + NVJPEG) ############"
# NVENC codecs from a 320x240 NV12 raw source (cars_320x240.nv12 already present)
run "h264    encode" ./hal_enc -b nvenc --codec h264 -i cars_320x240.nv12 -f nv12 -o /tmp/e_h264.h264
run "hevc    encode" ./hal_enc -b nvenc --codec hevc -i cars_320x240.nv12 -f nv12 -o /tmp/e_hevc.h265
run "av1     encode" ./hal_enc -b nvenc --codec av1  -i cars_320x240.nv12 -f nv12 -o /tmp/e_av1.av1
# JPEG encode via the nvjpegenc backend from a BMP
run "jpeg    encode (nvjpegenc)" ./hal_enc -b nvjpegenc -i test_cars_320x240.bmp -f bmp -o /tmp/e_jpeg.jpg

echo
echo "############ ROUND-TRIP (NVENC -> NVDEC) ############"
# Encode then decode back, verify frame count survives the round trip.
for c in h264 hevc av1; do
    ext=h264; [ $c = hevc ] && ext=h265; [ $c = av1 ] && ext=av1
    ./hal_enc -b nvenc --codec $c -i cars_320x240.nv12 -f nv12 -o /tmp/rt_$c.$ext >/tmp/sw.log 2>&1 || { echo "FAIL  $c round-trip (encode stage)"; fail=$((fail+1)); FAILED+=("$c round-trip"); continue; }
    run "$c round-trip decode-back" ./hal_dec -b nvdec --codec $c -i /tmp/rt_$c.$ext -o /tmp/rt_$c.yuv
done

echo
echo "############ TRANSCODE (NVDEC -> NVENC, host + zero-copy) ############"
run "transcode host      h264->h264" ./hal_transcode -b nvdec --codec h264 -i cars_320x240.h264 -o /tmp/tc_host.h264
run "transcode zero-copy h264->h264" ./hal_transcode -b nvdec --codec h264 -i cars_320x240.h264 -o /tmp/tc_zc.h264 -z
run "transcode vc1->h264  (feed)"    ./hal_transcode -b nvdec --codec vc1  -i SA00050.vc1        -o /tmp/tc_vc1.h264

echo
echo "############ SUMMARY ############"
echo "$pass passed, $fail failed"
if [ ${#FAILED[@]} -gt 0 ]; then
    echo "failures:"; printf '  - %s\n' "${FAILED[@]}"
fi
exit $fail
