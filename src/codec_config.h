#ifndef SRC_CODEC_CONFIG_H
#define SRC_CODEC_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "frame.h"

namespace halcodec {

// Encoder tuning parameters. Cross-backend: each backend's Initialize reads
// the subset it supports and maps unified names (e.g. rateControl="vbr") to
// its native enum. Fields left at their sentinel defaults (-1 / "" / 0-as-
// unset) fall back to the backend's built-in default, so an empty EncodeConfig
// preserves prior behavior.
struct EncodeConfig {
    std::string preset;          // NVENC: p1..p7; QSV: "fast"/"balanced"/"slow"/"best"; AMF: "speed"/"balanced"/"quality"
    std::string tuningInfo;      // NVENC only: hq/ll/ull/low_latency_p...
    std::string rateControl;     // "cbr" / "vbr" / "cqp" / "icq"
    int bitrateKbps = -1;        // target bitrate; -1 = unset
    int maxBitrateKbps = -1;     // VBR ceiling; -1 = unset (defaults to bitrate)
    int qp = -1;                 // CQP constant QP; -1 = unset (when >=0, overrides bitrate)
    int quality = -1;            // image encoders (JPEG): 1-100, higher is better; -1 = unset
    int gopLength = -1;          // IDR interval; -1 = unset (backend default)
    int numBFrames = -1;         // B-frame count; -1 = unset (set 0 for low delay)
    int frameRateNum = -1;       // fps numerator; -1 = unset
    int frameRateDen = -1;       // fps denominator; -1 = unset
    std::string profile;         // "baseline"/"main"/"high"; "" = unset
    std::string level;           // "auto"/"4.0"/"4.1"...; "" = unset
    bool lowDelay = false;       // semantic shortcut: forces numBFrames=0, short gop, no reordering
};

// Parameterized configuration handed to Encoder::Initialize /
// Decoder::Initialize. Kept as one struct (instead of many arguments) so
// the interface signature stays stable when fields are added.
struct CodecParams {
    std::string codec;                // "h264" / "hevc" / "av1" / "jpeg"
    int deviceIndex = 0;              // device ordinal to use
    std::vector<std::string> inputs;  // decode: file/dir paths; encode: raw input
    int width = 0;                    // required for encode
    int height = 0;                   // required for encode
    PixelFormat inputFormat = PixelFormat::Unknown;   // encode input
    PixelFormat outputFormat = PixelFormat::Unknown;  // decode output
    std::vector<uint8_t> extradata;   // optional codec extradata: H.264/HEVC want
                                      // AVCC-packed parameter sets ([len:4][nalu]),
                                      // AV1 a raw OBU range holding at least one
                                      // OBU_SEQUENCE_HEADER
    EncodeConfig encode;              // encoder tuning (decoders ignore this)
    bool zeroCopy = false;            // decoder: keep frames in device memory (opt-in per backend)
    // Opaque device-shared handle propagated from a producing decoder to a
    // consuming encoder when zeroCopy is on. For QSV this is the decode
    // session's VADisplay (MFX_HANDLE_VA_DISPLAY); the encoder SetHandle's it
    // before Init so ImportFrameSurface matches the imported surface's
    // vaDisplay. Stored as void* so codec_config.h stays vendor-header-free.
    void* sharedDeviceHandle = nullptr;
};

} // namespace halcodec

#endif // SRC_CODEC_CONFIG_H