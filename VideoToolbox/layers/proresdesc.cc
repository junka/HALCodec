#include "proresdesc.h"

#include <cstring>

namespace halcodec {
namespace vtbox {

namespace {

// The fixed fields ahead of the picture data: byte count(4), frame tag(4),
// constant(2), subsampling(2), producer tag(4), width(2), height(2).
constexpr size_t kHeaderBytes = 20;

// The value both Apple's and ffmpeg's encoders write at offset 8 of a 'icpf'
// frame, for all six flavours. Not documented anywhere, so it is checked as a
// fingerprint of a real header rather than for what it means.
constexpr uint16_t kConstantField = 0x0094;

uint32_t ReadBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint16_t ReadBe16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}

bool TagIs(const uint8_t* data, const char* tag) {
    return std::memcmp(data + 4, tag, 4) == 0;
}

}  // namespace

bool LooksLikeProRes(const uint8_t* data, size_t size) {
    if (!data || size < 8) {
        return false;
    }
    // A frame counts its own bytes, so a number shorter than the header it would
    // have to carry means the count was never a count.
    return ReadBe32(data) >= kHeaderBytes &&
           (TagIs(data, "icpf") || TagIs(data, "prrf"));
}

bool ReadProresFrame(const uint8_t* data, size_t size, ProresFrame* out) {
    if (!LooksLikeProRes(data, size) || size < kHeaderBytes) {
        return false;
    }
    const bool isRaw = TagIs(data, "prrf");
    // Producer tags differ ('apl0' from VideoToolbox, 'appl' for RAW, 'Lavc' from
    // ffmpeg) and the bitrate flavour does not appear in the codestream at all, so
    // neither is looked at here.
    // The constant field is checked on a 'icpf' frame only. A RAW frame carries
    // 0x0088 in that slot in every frame measured here, but no second piece of
    // RAW material exists to say whether the value is a constant or a field that
    // moves -- refusing a real stream on the strength of one sample is the worse
    // mistake, and the count and the dimensions below still have to agree.
    if (!isRaw && ReadBe16(data + 8) != kConstantField) {
        return false;
    }

    ProresFrame frame;
    frame.size = ReadBe32(data);
    // The subsampling flag sits at offset 10 in both headers, where a RAW frame
    // says 0; RAW is told apart by its tag, so the flag is not read for one.
    frame.isRaw = isRaw;
    frame.is444 = !isRaw && ReadBe16(data + 10) != 0;
    frame.width = ReadBe16(data + 16);
    frame.height = ReadBe16(data + 18);
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    frame.complete = frame.size <= size;
    *out = frame;
    return true;
}

}  // namespace vtbox
}  // namespace halcodec
