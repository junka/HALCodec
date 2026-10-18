#!/usr/bin/env bash
# make_probes.sh -- regenerate every probe stream in this directory from ffmpeg.
# Run from anywhere; then re-run tools/gen_vt_probe_streams.py from the repository
# root so VideoToolbox/layers/vt_probe_streams.h carries the new bytes.
#
# Nothing here is hand-written: each class is one frame of testsrc at 64x64, which
# is small enough to embed in the backend and large enough for the hardware to have
# something to decode. A probe that does not decode the way this script says it does
# is a bug in either the script or the capability table, so re-run both after
# changing anything.
#
# Requires an ffmpeg with libx264, libx265, libsvtav1, prores_ks and mjpeg -- the
# Homebrew build on this machine has all of them.
set -euo pipefail
cd "$(dirname "$0")"

SRC=(-f lavfi -i testsrc=size=64x64:rate=25:duration=0.04 -frames:v 1)

enc() { local out=$1; shift; ffmpeg -y -hide_banner -loglevel error "${SRC[@]}" "$@" "$out"; }

# H.264 and HEVC: Annex-B elementary streams, parameter sets plus one I picture.
enc h264_420_8.bin      -c:v libx264 -profile:v baseline -pix_fmt yuv420p -g 1 -f h264
enc h264_high_420_8.bin -c:v libx264 -profile:v high     -pix_fmt yuv420p -g 1 -f h264
enc hevc_420_8.bin      -c:v libx265 -pix_fmt yuv420p     -g 1 -f hevc
enc hevc_420_10.bin     -c:v libx265 -pix_fmt yuv420p10le -g 1 -f hevc
enc hevc_420_12.bin     -c:v libx265 -pix_fmt yuv420p12le -g 1 -f hevc
enc hevc_422_8.bin      -c:v libx265 -pix_fmt yuv422p     -g 1 -f hevc
enc hevc_422_10.bin     -c:v libx265 -pix_fmt yuv422p10le -g 1 -f hevc
enc hevc_444_8.bin      -c:v libx265 -pix_fmt yuv444p     -g 1 -f hevc
enc hevc_mono_8.bin     -c:v libx265 -x265-params mono=1  -pix_fmt gray     -g 1 -f hevc
enc hevc_mono_10.bin    -c:v libx265 -x265-params mono=1  -pix_fmt gray10le -g 1 -f hevc

# AV1: IVF, then the first frame record's payload -- what VideoToolbox wants is a
# bare temporal unit, and IVF is the only container ffmpeg writes AV1 into without
# an av1C box, so the probe keeps exercising the sample entry the decoder
# synthesises from the in-band sequence header.
ffmpeg -y -hide_banner -loglevel error "${SRC[@]}" -c:v libsvtav1 -preset 8 \
       -pix_fmt yuv420p -b:v 200k -f ivf av1_420_8.ivf
ffmpeg -y -hide_banner -loglevel error "${SRC[@]}" -c:v libsvtav1 -preset 8 \
       -pix_fmt yuv420p10le -b:v 200k -f ivf av1_420_10.ivf
python3 - av1_420_8 av1_420_10 <<'PY'
import struct, sys, pathlib
for name in sys.argv[1:]:
    b = pathlib.Path(name + ".ivf").read_bytes()
    assert b[:4] == b"DKIF", name
    headersize = struct.unpack_from("<H", b, 6)[0]          # IVF file header length
    off = headersize
    size = struct.unpack_from("<I", b, off)[0]              # first frame record
    payload = b[off + 12: off + 12 + size]                  # 4-byte size + 8-byte pts
    assert payload and not payload[0] & 0x80, (name, hex(payload[0]))
    pathlib.Path(name + ".bin").write_bytes(payload)
    print(name, "->", len(payload), "bytes, first OBU type", (payload[0] >> 3) & 0xF)
PY
rm -f av1_420_*.ivf

# ProRes: an element stream, which is what hal_dec is fed -- the .mov is only the
# quickest way to get the encoder's own packets out, cut loose from the container.
enc4() { local prof=$1 pix=$2 out=$3 mov=$4
    ffmpeg -hide_banner -loglevel error "${SRC[@]}" -c:v prores_ks -profile:v "$prof" \
           -pix_fmt "$pix" -f mov "$mov"
    ffmpeg -y -hide_banner -loglevel error -i "$mov" -c:v copy -f rawvideo "$out"
    rm -f "$mov"
}
enc4 2 yuv422p10le  prores_422.bin  pr422.mov
enc4 3 yuva444p10le prores_4444.bin pr4444.mov

# JPEG: one JFIF codestream.
ffmpeg -y -hide_banner -loglevel error "${SRC[@]}" -c:v mjpeg -pix_fmt yuvj420p -q:v 2 \
       -f image2 jpeg_420_8.tmp.jpg
mv jpeg_420_8.tmp.jpg jpeg_420_8.bin

ls -la *.bin
