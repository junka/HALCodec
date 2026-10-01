// Unit tests for the Annex-B access-unit front end: NAL scanning, the grouping
// of NALs into one unit per picture, and the AVCC rewrite the decoder submits.

#include "h264au.h"

#include <cstdint>
#include <vector>

#include "h264_bits.h"
#include "test_check.h"

using halcodec::vtbox::Au;
using halcodec::vtbox::GroupAUs;
using halcodec::vtbox::H264Poc;
using halcodec::vtbox::Nal;
using halcodec::vtbox::ScanNals;
using haltest::AnnexBNal;
using haltest::Append;
using haltest::MakeOtherNal;
using haltest::MakePpsNal;
using haltest::MakeSliceNal;
using haltest::MakeSpsNal;
using haltest::ToAnnexB;
using haltest::PpsCfg;
using haltest::SliceCfg;
using haltest::SpsCfg;

namespace {

const int64_t kGop1 = int64_t(1) << 32;

const SpsCfg kSps;
const PpsCfg kPps;

std::vector<Nal> Scan(const std::vector<uint8_t>& stream,
                      size_t maxNals = 1024) {
    return ScanNals(stream.data(), stream.size(), maxNals);
}

// Parameter sets, as a stream carries them ahead of a random access point.
void AppendParameterSets(std::vector<uint8_t>* stream) {
    Append(stream, ToAnnexB(MakeSpsNal(kSps)));
    Append(stream, ToAnnexB(MakePpsNal(kPps)));
}

// A picture split into `slices` NALs: the first reports first_mb_in_slice 0, the
// rest a nonzero macroblock index, which is all a decoder has to go on.
void AppendPicture(std::vector<uint8_t>* stream, int64_t* frameNum,
                   int64_t* pocLsb, int type, int slices) {
    for (int i = 0; i < slices; ++i) {
        SliceCfg slice;
        slice.type = type;
        slice.firstMb = i * 100;
        slice.pocLsb = *pocLsb;
        slice.frameNum = *frameNum;
        slice.idrPicId = *frameNum;
        Append(stream, ToAnnexB(MakeSliceNal(slice, kSps, kPps)));
    }
    ++*frameNum;
    *pocLsb += 2;
}

// Three pictures of eight slices each, with the parameter sets in front.
std::vector<uint8_t> SlicedStream() {
    std::vector<uint8_t> stream;
    AppendParameterSets(&stream);
    int64_t frameNum = 0, pocLsb = 0;
    AppendPicture(&stream, &frameNum, &pocLsb, 5, 8);
    AppendPicture(&stream, &frameNum, &pocLsb, 1, 8);
    AppendPicture(&stream, &frameNum, &pocLsb, 1, 8);
    return stream;
}

void ScansThreeAndFourByteStartCodes() {
    std::vector<uint8_t> stream;
    Append(&stream, AnnexBNal(7, 3, {0x42, 0xF0}, 4));
    Append(&stream, AnnexBNal(8, 3, {0xCE, 0x38}, 3));
    Append(&stream, AnnexBNal(1, 2, {0x88, 0x80}, 4));

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(3));
    CHECK_EQ(nals[0].type, uint8_t(7));
    CHECK_EQ(nals[1].type, uint8_t(8));
    CHECK_EQ(nals[2].type, uint8_t(1));
    // Length includes the NAL header byte (payload + 1).
    CHECK_EQ(nals[0].len, size_t(3));
    CHECK_EQ(nals[1].len, size_t(3));
    CHECK_EQ(nals[2].len, size_t(3));
    // 4-byte start code + header + 2 payload, then a 3-byte code + header.
    CHECK_EQ(nals[0].startPos, size_t(0));
    CHECK_EQ(nals[1].startPos, size_t(7));
    CHECK_EQ(nals[2].startPos, size_t(13));
}

// The last NAL of a chunk has no following start code, so it must reach the end
// of the buffer. Stopping the walk at the scan bound instead truncated the last
// NAL of every stream by a few bytes, and VideoToolbox rejected the unit with
// kVTVideoDecoderBadDataErr (-12909), losing one frame per stream.
void LastNalReachesTheEndOfTheBuffer() {
    std::vector<uint8_t> stream;
    Append(&stream, ToAnnexB(MakeOtherNal(6, 10)));
    Append(&stream, ToAnnexB(MakeOtherNal(1, 25)));

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(2));
    // Length includes the NAL header byte.
    CHECK_EQ(nals[0].len, size_t(11));
    CHECK_EQ(nals[1].len, size_t(26));
    CHECK_EQ(nals[1].payload + nals[1].len, stream.data() + stream.size());
}

// Feed chunks are fixed-size reads, so a chunk can end inside a start code. Those
// bytes are ambiguous and stay with the open NAL until the next chunk says
// whether a NAL starts there.
void TrailingPartialStartCodeStaysWithTheOpenNal() {
    std::vector<uint8_t> stream;
    Append(&stream, ToAnnexB(MakeOtherNal(1, 8)));
    Append(&stream, std::vector<uint8_t>{0x00, 0x00});

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(1));
    CHECK_EQ(nals[0].payload + nals[0].len, stream.data() + stream.size());
}

// An encoder inserts an emulation prevention byte so 00 00 01 can never appear
// inside a NAL; the scanner must not mistake the escaped run for a start code.
void EscapedZerosInsideANalDoNotSplitIt() {
    std::vector<uint8_t> stream;
    // {2A, 00, 00, 00, 2A} escapes to {2A, 00, 00, 03, 00, 2A}.
    Append(&stream, AnnexBNal(1, 2, {0x2A, 0x00, 0x00, 0x00, 0x2A}));
    Append(&stream, ToAnnexB(MakeOtherNal(1, 4)));

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(2));
    // First NAL: header + escaped payload (00 00 00 -> 00 00 03 00).
    CHECK_EQ(nals[0].len, size_t(7));
    // Second NAL: header + 4 payload bytes.
    CHECK_EQ(nals[1].len, size_t(5));
}

void BytesBeforeTheFirstStartCodeAreSkipped() {
    std::vector<uint8_t> stream{0xAA, 0xBB, 0xCC};
    Append(&stream, ToAnnexB(MakeOtherNal(7, 6)));

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(1));
    CHECK_EQ(nals[0].startPos, size_t(3));
    CHECK_EQ(nals[0].type, uint8_t(7));

    const std::vector<uint8_t> noise{0x11, 0x22, 0x33, 0x44};
    CHECK_EQ(Scan(noise).size(), size_t(0));
    const std::vector<uint8_t> tooShort{0x00, 0x00};
    CHECK_EQ(ScanNals(tooShort.data(), tooShort.size(), 8).size(), size_t(0));
}

// maxNals cuts the list *between* NALs: a caller that only wants a few access
// units never walks the rest of the buffer, and the unit the cut lands inside
// simply stays open until the next scan.
void MaxNalsTruncatesBetweenNals() {
    std::vector<uint8_t> stream;
    for (int i = 0; i < 5; ++i) {
        Append(&stream, ToAnnexB(MakeOtherNal(1, 20)));
    }
    const std::vector<Nal> all = Scan(stream);
    CHECK_EQ(all.size(), size_t(5));

    const std::vector<Nal> two = Scan(stream, 2);
    CHECK_EQ(two.size(), size_t(2));
    CHECK_EQ(two[0].startPos, all[0].startPos);
    CHECK_EQ(two[1].startPos, all[1].startPos);
    CHECK_EQ(two[1].len, all[1].len);
    // The second NAL ends where the third one's start code begins, so it is
    // complete even though the scan stopped there.
    CHECK_EQ(two[1].payload + two[1].len, stream.data() + all[2].startPos);
}

// The regression this grouping exists for: a picture split into slices is ONE
// access unit. Slicing it apart hands VideoToolbox lone secondary slices, which
// decode to no picture at all, so a multi-slice stream produced nothing.
void SlicesOfOnePictureFormOneAccessUnit() {
    const std::vector<uint8_t> stream = SlicedStream();
    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(26));  // SPS, PPS, then 3 pictures x 8 slices

    // The last picture is only closed by the end of the stream, so without
    // closeOpen the pump holds it back for the next feed.
    H264Poc poc;
    const std::vector<Au> aus = GroupAUs(poc, nals, nals.size(), false);
    CHECK_EQ(aus.size(), size_t(2));
    CHECK_EQ(aus[0].firstNal, size_t(0));
    CHECK_EQ(aus[0].endNal, size_t(10));  // SPS, PPS and 8 slices
    CHECK_EQ(aus[1].firstNal, size_t(10));
    CHECK_EQ(aus[1].endNal, size_t(18));

    H264Poc eofPoc;
    const std::vector<Au> withTail =
        GroupAUs(eofPoc, nals, nals.size(), /*closeOpen=*/true);
    CHECK_EQ(withTail.size(), size_t(3));
    CHECK_EQ(withTail[2].firstNal, size_t(18));
    CHECK_EQ(withTail[2].endNal, nals.size());

    // Grouping the whole stream must account for every NAL, so no slice is
    // dropped between units.
    size_t covered = 0;
    for (const Au& au : withTail) {
        covered += au.endNal - au.firstNal;
    }
    CHECK_EQ(covered, nals.size());
}

// Each unit carries the display order of the picture it starts. A stream that
// decodes a B frame after the picture it sits between gives the pump keys that
// are deliberately out of order.
void UnitsCarryTheDisplayOrderOfTheirPrimarySlice() {
    std::vector<uint8_t> stream;
    AppendParameterSets(&stream);
    {
        SliceCfg idr;
        idr.type = 5;
        Append(&stream, ToAnnexB(MakeSliceNal(idr, kSps, kPps)));      // display 0
        SliceCfg p;
        p.pocLsb = 4;
        p.frameNum = 1;
        Append(&stream, ToAnnexB(MakeSliceNal(p, kSps, kPps)));        // display 4
        SliceCfg b;
        b.pocLsb = 2;
        b.nalRefIdc = 0;
        b.frameNum = 2;
        Append(&stream, ToAnnexB(MakeSliceNal(b, kSps, kPps)));        // display 2
    }
    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(5));

    H264Poc poc;
    const std::vector<Au> aus = GroupAUs(poc, nals, nals.size(), true);
    CHECK_EQ(aus.size(), size_t(3));
    CHECK_EQ(aus[0].key, kGop1 + 0);
    CHECK_EQ(aus[1].key, kGop1 + 4);
    CHECK_EQ(aus[2].key, kGop1 + 2);
    CHECK(aus[1].key > aus[2].key);
}

// A window that begins inside a picture (the pump resized its plan, so the
// picture's first slice was already submitted) still gets that picture's key,
// which is what lands its remaining slices beside the frames they belong to.
void ContinuationAtTheWindowStartKeepsItsPictureKey() {
    const std::vector<uint8_t> stream = SlicedStream();
    const std::vector<Nal> nals = Scan(stream);

    // The first pump consumes the parameter sets and three of the eight slices of
    // picture 1, and advances the state over exactly those NALs.
    H264Poc poc;
    FeedPocRange(poc, nals, 5);
    const std::vector<Nal> window(nals.begin() + 5, nals.end());
    const std::vector<Au> aus = GroupAUs(poc, window, window.size(), true);
    CHECK(!aus.empty());
    CHECK_EQ(aus[0].firstNal, size_t(0));
    // The five remaining slices of picture 1 stay together.
    CHECK_EQ(aus[0].endNal, size_t(5));
    CHECK_EQ(aus[0].key, kGop1);
    CHECK_EQ(aus.size(), size_t(3));
}

// A preamble (SEI/SPS/PPS/AUD) after a picture starts a new unit, while filler
// and other non-VCL NALs stay with the picture they follow.
void PreambleClosesAPictureAndFillerDoesNot() {
    std::vector<uint8_t> stream;
    AppendParameterSets(&stream);
    int64_t frameNum = 0, pocLsb = 0;
    AppendPicture(&stream, &frameNum, &pocLsb, 5, 1);
    Append(&stream, ToAnnexB(MakeOtherNal(12, 6)));  // FILLER: stays with the picture
    Append(&stream, ToAnnexB(MakeOtherNal(6, 6)));   // SEI: starts the next unit
    AppendPicture(&stream, &frameNum, &pocLsb, 1, 1);

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(6));

    H264Poc poc;
    const std::vector<Au> aus = GroupAUs(poc, nals, nals.size(), true);
    CHECK_EQ(aus.size(), size_t(2));
    CHECK_EQ(aus[0].firstNal, size_t(0));
    CHECK_EQ(aus[0].endNal, size_t(4));  // SPS, PPS, slice, filler
    CHECK_EQ(aus[1].firstNal, size_t(4));
    CHECK_EQ(aus[1].endNal, size_t(6));
}

// An access unit delimiter arrives before the picture it announces, so it joins
// that picture's unit rather than closing the previous one.
void AccessUnitDelimiterJoinsThePictureItAnnounces() {
    std::vector<uint8_t> stream;
    AppendParameterSets(&stream);
    Append(&stream, ToAnnexB(MakeOtherNal(9, 2)));
    int64_t frameNum = 0, pocLsb = 0;
    AppendPicture(&stream, &frameNum, &pocLsb, 5, 2);

    const std::vector<Nal> nals = Scan(stream);
    H264Poc poc;
    const std::vector<Au> aus = GroupAUs(poc, nals, nals.size(), true);
    CHECK_EQ(aus.size(), size_t(1));
    CHECK_EQ(aus[0].firstNal, size_t(0));
    CHECK_EQ(aus[0].endNal, nals.size());
    CHECK_EQ(aus[0].key, kGop1);
}

// Without a parsable parameter set the front end cannot tell a second slice from
// a new picture, so grouping falls back to one unit per VCL NAL. The decoder
// still submits frames, just possibly one per slice: recorded so the fallback is
// a known behaviour rather than an accident.
void UnparsableStreamFallsBackToPerSliceUnits() {
    std::vector<uint8_t> stream;
    Append(&stream, ToAnnexB(MakeOtherNal(1, 8)));
    Append(&stream, ToAnnexB(MakeOtherNal(1, 8)));
    Append(&stream, ToAnnexB(MakeOtherNal(1, 8)));

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(3));
    H264Poc poc;
    const std::vector<Au> aus = GroupAUs(poc, nals, nals.size(), true);
    CHECK_EQ(aus.size(), size_t(3));
    CHECK(!poc.usable());
    for (const Au& au : aus) {
        CHECK(au.key < 0);
    }
}

// Grouping may only advance the picture-order state over the range the caller has
// committed to, so the pump plans against a copy, drops the units that did not
// fit its window, and replans them later from the same state.
void GroupingStopsAtTheRequestedLimit() {
    std::vector<uint8_t> stream;
    AppendParameterSets(&stream);
    int64_t frameNum = 0, pocLsb = 0;
    AppendPicture(&stream, &frameNum, &pocLsb, 5, 1);
    AppendPicture(&stream, &frameNum, &pocLsb, 1, 1);
    AppendPicture(&stream, &frameNum, &pocLsb, 1, 1);

    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(5));

    // Nothing past the third NAL was seen, so no picture closed.
    H264Poc limited;
    CHECK_EQ(GroupAUs(limited, nals, 3, /*closeOpen=*/false).size(), size_t(0));

    H264Poc full;
    const std::vector<Au> two = GroupAUs(full, nals, nals.size(), false);
    CHECK_EQ(two.size(), size_t(2));
    CHECK_EQ(two[0].key, kGop1);
    CHECK_EQ(two[1].key, kGop1 + 2);

    // Committing the state over the consumed range reproduces the trial's keys,
    // which is what lets the pump resize a plan it built against a copy.
    H264Poc committed;
    FeedPocRange(committed, nals, two[1].endNal);
    const std::vector<Nal> rest(nals.begin() + two[1].endNal, nals.end());
    H264Poc replanned = committed;
    const std::vector<Au> tail = GroupAUs(replanned, rest, rest.size(), true);
    CHECK_EQ(tail.size(), size_t(1));
    CHECK_EQ(tail[0].key, kGop1 + 4);
}

void AvccLayoutIsLengthThenNal() {
    std::vector<uint8_t> stream;
    Append(&stream, ToAnnexB(MakeOtherNal(7, 3)));
    Append(&stream, ToAnnexB(MakeOtherNal(8, 5)));
    const std::vector<Nal> nals = Scan(stream);
    CHECK_EQ(nals.size(), size_t(2));

    // AVCC: [4-byte big-endian length][NAL unit]...
    // First NAL: 4 bytes (header + 3 payload), second: 6 bytes (header + 5).
    const std::vector<uint8_t> au = halcodec::vtbox::BuildAvcc(nals, 0, 2);
    CHECK_EQ(au.size(), size_t((4 + 4) + (4 + 6)));
    CHECK_EQ(au[0], uint8_t(0));
    CHECK_EQ(au[3], uint8_t(4));       // big-endian length of the first NAL
    CHECK_EQ(au[4], uint8_t(0x07));    // SPS header byte
    CHECK_EQ(au[8], uint8_t(0));
    CHECK_EQ(au[11], uint8_t(6));      // big-endian length of the second NAL
    CHECK_EQ(au[12], uint8_t(0x08));   // PPS header byte
    for (size_t k = 0; k < nals[1].len; ++k) {
        CHECK_EQ(au[12 + k], nals[1].payload[k]);
    }
    CHECK_EQ(halcodec::vtbox::BuildAvcc(nals, 1, 1).size(), size_t(0));
}

} // namespace

int main() {
    ScansThreeAndFourByteStartCodes();
    LastNalReachesTheEndOfTheBuffer();
    TrailingPartialStartCodeStaysWithTheOpenNal();
    EscapedZerosInsideANalDoNotSplitIt();
    BytesBeforeTheFirstStartCodeAreSkipped();
    MaxNalsTruncatesBetweenNals();
    SlicesOfOnePictureFormOneAccessUnit();
    UnitsCarryTheDisplayOrderOfTheirPrimarySlice();
    ContinuationAtTheWindowStartKeepsItsPictureKey();
    PreambleClosesAPictureAndFillerDoesNot();
    AccessUnitDelimiterJoinsThePictureItAnnounces();
    UnparsableStreamFallsBackToPerSliceUnits();
    GroupingStopsAtTheRequestedLimit();
    AvccLayoutIsLengthThenNal();
    return haltest::finish("h264au_test");
}
