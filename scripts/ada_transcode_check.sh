#!/bin/bash
# Stage 3 verification on ada: NVDEC -> NVENC transcode, host path vs zero-copy
# device path. Both runs must produce the same frame count, and each output
# must decode back close to the reference decode of the source.
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH=$PWD

REF=cars_320x240.nv12      # reference decode of the source, from earlier checks
NFRAMES=30
FRAMESZ=$((320 * 240 * 3 / 2))

run() {
    echo "=== hal_transcode $*"
    ./hal_transcode "$@" >/tmp/rt.log 2>&1
    echo "exit=$?"
    grep -iE 'frames|error|zero-copy|Fail' /tmp/rt.log | head -10
}

psnr() {  # $1 = candidate yuv, $2 = reference yuv
    python3 - "$1" "$2" <<'PY'
import sys, math
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
n = min(len(a), len(b))
if n == 0:
    print("  empty input"); sys.exit(1)
se = 0
for x, y in zip(a[:n], b[:n]):
    d = x - y
    se += d * d
mse = se / n
print("  bytes compared: %d" % n)
if mse == 0:
    print("  PSNR: inf (identical)")
else:
    print("  PSNR: %.2f dB" % (10 * math.log10(255.0 * 255.0 / mse)))
PY
}

run -b nvdec --codec h264 -i cars_320x240.h264 -o rt_host.h264
run -b nvdec --codec h264 -i cars_320x240.h264 -o rt_dev.h264 -z
ls -l rt_host.h264 rt_dev.h264
sha256sum rt_host.h264 rt_dev.h264

# Decode each transcode output back and compare against the source decode.
for out in rt_host rt_dev; do
    echo "=== decode back $out.h264"
    ./hal_dec -b nvdec --codec h264 -i $out.h264 -o $out.yuv >/tmp/dec.log 2>&1
    echo "exit=$?"
    ls -l $out.yuv 2>/dev/null
    psnr $out.yuv $REF
done

# Both paths encode the same frames from the same decoder, so their decodes
# should agree with each other too.
echo "=== host vs device transcode output"
psnr rt_host.yuv rt_dev.yuv
