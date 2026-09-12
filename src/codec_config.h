#ifndef SRC_CODEC_CONFIG_H
#define SRC_CODEC_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "frame.h"

namespace halcodec {

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
    std::vector<uint8_t> extradata;   // optional codec extradata (e.g. H264 SPS/PPS)
};

} // namespace halcodec

#endif // SRC_CODEC_CONFIG_H