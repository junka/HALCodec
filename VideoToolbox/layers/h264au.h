#ifndef LAYERS_H264AU_H_
#define LAYERS_H264AU_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "h264poc.h"

namespace halcodec {
namespace vtbox {

// Annex-B elementary-stream front end: cuts a byte stream into NALs, groups the
// NALs into access units (one picture each) and rewrites a unit as AVCC. Pure
// bitstream bookkeeping with no vendor framework in it, which is what lets the
// grouping rules a decoder has to get right be tested on their own.

// One NAL inside a scanned buffer. `payload` points into the buffer the scan was
// given, so a Nal vector must not outlive that buffer.
struct Nal {
    size_t startPos;  // offset of the NAL's start code in the buffer
    const uint8_t* payload;
    size_t len;
    uint8_t type;
};

// Annex-B NAL scanner, bounded by `maxNals`. See ScanNals() in the source for
// what the last NAL and the bound mean for a caller feeding chunks.
std::vector<Nal> ScanNals(const uint8_t* data, size_t size, size_t maxNals);

// One access unit as a half-open range over the NAL vector plus its display
// order (negative when the picture order is unknown).
struct Au {
    size_t firstNal;
    size_t endNal;
    int64_t key;
};

// Groups nals[0, limitNals) into access units, feeding parameter sets and
// classifying slices through `poc`. The unit still open at the limit is only
// returned when `closeOpen`.
std::vector<Au> GroupAUs(H264Poc& poc, const std::vector<Nal>& nals,
                         size_t limitNals, bool closeOpen);

// Advances `poc` over nals[0, limit) exactly as GroupAUs does over the same
// range, for committing state a trial grouping only planned.
void FeedPocRange(H264Poc& poc, const std::vector<Nal>& nals, size_t limit);

// Builds one AVCC access unit ([len:4][nalu]...) from nals[begin, end).
std::vector<uint8_t> BuildAvcc(const std::vector<Nal>& nals, size_t begin,
                               size_t end);

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_H264AU_H_
