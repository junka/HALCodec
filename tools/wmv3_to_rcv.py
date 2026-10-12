#!/usr/bin/env python3
"""Build a VC1 Simple/Main RCV container from a WMV3/ASF source.

RCV layout (matches NVIDIA ParseRCVHeader in misc_utils.c):
  3 bytes  : numFrames (LE, 24-bit)
  1 byte   : type = 0xC5  (RCV_VC1_TYPE=0x85 | RCV_V2_MASK=0x40 -> V2 extended)
  4 bytes  : cbSeqHdr    (sequence-header byte count; SP/MP == 4)
  cbSeqHdr : SeqHdrData  (the VC1 sequence header = ASF codec extradata)
  -- STRUCT_A --
  4 bytes  : maxCodedHeight (LE)
  4 bytes  : maxCodedWidth  (LE)
  4 bytes  : 0x0000000C
  -- STRUCT_B --
  4 bytes  : (level<<29 | cbr<<28 | hrd_buffer)  -- level 0, cbr 1, hrd 0
  4 bytes  : bitRate (LE, 24-bit used)
  4 bytes  : frameRate (LE)
  -- per frame --
  4 bytes  : frameSize (LE, low 24 bits valid)
  4 bytes  : timeStamp (LE)
  frameSize: frame bitstream
"""
import struct, subprocess, sys, os

def ffprobe_packets(path):
    out = subprocess.check_output([
        "ffprobe", "-hide_banner", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "packet=pts,size",
        "-of", "csv=p=0", path,
    ]).decode()
    pkts = []
    for line in out.strip().splitlines():
        pts, size = line.split(",")
        pkts.append((int(pts), int(size)))
    return pkts

def ffprobe_extradata(path):
    # -show_data prints a hex dump under "extradata=". Each dump line is
    # "OFFSET: HH HH HH HH   ASCII". We want only the hex bytes between the
    # offset colon and the double-space that precedes the ASCII preview.
    out = subprocess.check_output([
        "ffprobe", "-hide_banner", "-v", "error",
        "-select_streams", "v:0",
        "-show_data", "-show_entries", "stream=extradata",
        "-of", "default", path,
    ]).decode()
    hexbytes = []
    cap = False
    for ln in out.splitlines():
        if ln.startswith("extradata="):
            cap = True
            continue
        if cap:
            if ln.strip() == "" or ln.startswith("["):
                break
            if ":" in ln:
                # "00000000: 4e39 1a01   N9.."
                hexpart = ln.split(":", 1)[1]
                # cut off the ASCII preview (after a run of 2+ spaces)
                hexpart = hexpart.split("  ")[0]
                for p in hexpart.split():
                    if all(c in "0123456789abcdefABCDEF" for c in p):
                        hexbytes.append(bytes.fromhex(p))
    return b"".join(hexbytes)

def ffprobe_dims(path):
    out = subprocess.check_output([
        "ffprobe", "-hide_banner", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,r_frame_rate",
        "-of", "csv=p=0", path,
    ]).decode().strip()
    w, h, fr = out.split(",")
    return int(w), int(h), fr

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "/tmp/b52crash2.wmv"
    dst = sys.argv[2] if len(sys.argv) > 2 else "/tmp/b52_vc1.rcv"
    raw = "/tmp/vc1_raw_frames.bin"

    pkts = ffprobe_packets(src)
    seqhdr = ffprobe_extradata(src)
    w, h, fr_str = ffprobe_dims(src)
    # frame rate as integer (25/1 -> 25)
    num, den = fr_str.split("/")
    fps = int(int(num) / int(den)) if int(den) else 25

    print(f"source: {src}")
    print(f"  {w}x{h} {fps}fps, {len(pkts)} frames")
    print(f"  seqhdr ({len(seqhdr)}B): {seqhdr.hex()}")
    assert len(seqhdr) == 4, f"expected 4-byte SP/MP seqhdr, got {len(seqhdr)}"

    # Extract raw concatenated frame bitstreams via ffmpeg copy.
    subprocess.check_call([
        "ffmpeg", "-hide_banner", "-v", "error",
        "-i", src, "-c", "copy", "-f", "vc1", raw,
    ])

    with open(raw, "rb") as f:
        frames_blob = f.read()

    # Build RCV.
    num_frames = len(pkts)
    buf = bytearray()
    # header
    buf += struct.pack("<I", num_frames)[:3]      # 3-byte LE numFrames
    buf += bytes([0xC5])                           # type: V2 extended
    buf += struct.pack("<I", len(seqhdr))          # cbSeqHdr
    buf += seqhdr                                  # SeqHdrData
    # STRUCT_A
    buf += struct.pack("<I", h)                    # maxCodedHeight
    buf += struct.pack("<I", w)                    # maxCodedWidth
    buf += struct.pack("<I", 0x0000000C)
    # STRUCT_B: level=0<<29, cbr=1<<28, hrd=0
    buf += struct.pack("<I", (0 << 29) | (1 << 28) | 0)
    buf += struct.pack("<I", 1000_000)             # bitRate (placeholder)
    buf += struct.pack("<I", fps)                  # frameRate
    # frames
    off = 0
    for i, (pts, size) in enumerate(pkts):
        frame = frames_blob[off:off + size]
        if len(frame) != size:
            print(f"  WARN frame {i}: expected {size}, got {len(frame)}")
        buf += struct.pack("<I", size)             # frameSize (24-bit valid)
        buf += struct.pack("<I", pts & 0xFFFFFFFF) # timeStamp
        buf += frame
        off += size

    with open(dst, "wb") as f:
        f.write(buf)
    print(f"wrote {dst}: {len(buf)} bytes, {num_frames} frames")

if __name__ == "__main__":
    main()
