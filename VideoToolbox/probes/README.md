# Probe streams

Each `.bin` here is one complete elementary stream for one input class:
parameter sets plus a single 64x64 intra picture, or that codec's equivalent.
`tools/gen_vt_probe_streams.py` turns them into the C arrays in
`VideoToolbox/layers/vt_probe_streams.h`, which the vtbox capability query feeds
through its own decoder -- so the table can state what the hardware *produces*
for a class, not merely what a session-creation call admits.

Add a class: put the stream here with the name the row in `vt_caps.cc` expects
(`<codec>_<chroma>_<depth>`, e.g. `hevc_422_10`), re-run the generator, and the
row lights up on every machine that decodes it.

## How each was made

`make_probes.sh` in this directory regenerates every stream from `ffmpeg` --
one frame of `testsrc` at 64x64 per class, no bytes hand-written anywhere. Read
it for what each class is encoded with; it needs an ffmpeg with libx264,
libx265, libsvtav1, prores_ks and mjpeg (the Homebrew build has all of them).

Two shapes in that script are deliberate:

- **AV1 is written as IVF and the first frame record's payload is cut out**,
  because what VideoToolbox wants is a bare temporal unit, and IVF is the only
  container ffmpeg writes AV1 into without an `av1C` box -- so the probe keeps
  exercising the sample entry the decoder synthesises from the in-band sequence
  header.
- **ProRes is written into a `.mov` and then demuxed with `-c:v copy -f
  rawvideo`**, which is the element stream `hal_dec` is fed, one 32-bit byte
  count per picture.

## What is not here

**ProRes RAW has no probe stream, and cannot have one.** Measured on this
machine, neither VideoToolbox (`VTCopyVideoEncoderList` lists only the six
422/4444 tiers) nor ffmpeg writes a `prrf` frame -- the only RAW streams in
existence come from cameras. `tools/fetch_prores_raw_sample.sh` pulls one from
the FFmpeg sample site into the gitignored `tests/data/`, so the RAW class is
probed from that file when it is present (`HALCODEC_PRORES_RAW_SAMPLE`, or the
default path the fetch script writes to) and the capability query honestly
answers *unknown* when it is not.
