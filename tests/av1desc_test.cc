// Unit tests for the AV1 elementary-stream front end: OBU framing, temporal-unit
// grouping, the sequence-header walk, and the `av01`/`av1C` sample entry handed
// to VideoToolbox. The expected values come from measurement, not recollection:
// every crafted case below was fed through `ffmpeg -bsf:v trace_headers`, and the
// fixture pair was produced by ffmpeg's own encoder and muxer.

#include "av1desc.h"

#include <cstring>
#include <string>
#include <vector>

#include "av1_bits.h"
#include "test_check.h"

using namespace halcodec::vtbox;
using haltest::MakeAv1Obu;

using haltest::Av1ShCfg;
using haltest::Av1TemporalDelimiter;
using haltest::Av1Unit;
using haltest::MakeAv1SequenceHeaderObu;

namespace {

void ExpectOk(const std::vector<uint8_t>& obu, const char* what) {
    Av1SequenceHeader h;
    CHECK(haltest::check(ParseAv1SequenceHeader(obu.data(), obu.size(), &h),
                         __FILE__, __LINE__,
                         std::string(what) + ": parse must succeed"));
    haltest::check(h.unsupported == nullptr, __FILE__, __LINE__,
                   std::string(what) +
                       (h.unsupported
                            ? std::string(": refused -- ") + h.unsupported
                            : std::string(": should be supported")));
}

Av1SequenceHeader Parse(const std::vector<uint8_t>& obu, bool* ok) {
    Av1SequenceHeader h;
    *ok = ParseAv1SequenceHeader(obu.data(), obu.size(), &h);
    return h;
}

void TestRealEncoderHeader() {
    // OBU_SEQUENCE_HEADER of a 320x240 8-bit 4:2:0 libsvtav1 encode, taken from
    // the elementary stream ffmpeg then muxed into MP4.
    const uint8_t kReal[] = {0x0a, 0x0b, 0x00, 0x00, 0x00, 0x04, 0x3c,
                             0xff, 0xbc, 0x6a, 0xf9, 0x80, 0x40};
    bool ok = false;
    const Av1SequenceHeader h = Parse(std::vector<uint8_t>(kReal, kReal + sizeof(kReal)), &ok);
    CHECK(ok);
    CHECK(h.unsupported == nullptr);
    CHECK_EQ(h.profile, 0);
    CHECK_EQ(h.levelIdx, 0);
    CHECK_EQ(h.bitDepth, 8);
    CHECK(!h.monoChrome);
    CHECK_EQ(h.width, 320);
    CHECK_EQ(h.height, 240);
    CHECK_EQ(h.chromaSamplePosition, 0);

    // The same stream's av1C box as ffmpeg wrote it: 4-byte record plus the
    // sequence header OBU. VideoToolbox is handed the identical bytes.
    const uint8_t kFfmpegAv1C[] = {0x00, 0x00, 0x00, 0x19, 'a',  'v',  '1',  'C',
                                   0x81, 0x00, 0x0c, 0x00, 0x0a, 0x0b, 0x00, 0x00,
                                   0x00, 0x04, 0x3c, 0xff, 0xbc, 0x6a, 0xf9, 0x80,
                                   0x40};
    const std::vector<uint8_t> entry =
        BuildAv01SampleEntry(h, kReal, sizeof(kReal));
    const size_t offset = entry.size() - sizeof(kFfmpegAv1C);
    CHECK_EQ(memcmp(entry.data() + offset, kFfmpegAv1C, sizeof(kFfmpegAv1C)), 0);
}

// ffmpeg's own VisualSampleEntry header for a 320x240 AV1 track: the 86 bytes
// ahead of its av1C child. The box size (which covers the pasp/btrt/fiel
// children ffmpeg adds on top) and the 32-byte compressorname are the only
// fields a hand-built entry legitimately differs in.
const uint8_t kFfmpegEntry[] = {
    0x00, 0x00, 0x00, 0x9d, 0x61, 0x76, 0x30, 0x31, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0xf0,
    0x00, 0x48, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x17, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x32, 0x2e, 0x32, 0x38,
    0x2e, 0x31, 0x30, 0x31, 0x20, 0x6c, 0x69, 0x62, 0x73, 0x76, 0x74, 0x61,
    0x76, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
    0xff, 0xff};

void TestSampleEntryLayout() {
    const uint8_t kReal[] = {0x0a, 0x0b, 0x00, 0x00, 0x00, 0x04, 0x3c,
                             0xff, 0xbc, 0x6a, 0xf9, 0x80, 0x40};
    bool ok = false;
    const Av1SequenceHeader h = Parse(std::vector<uint8_t>(kReal, kReal + sizeof(kReal)), &ok);
    CHECK(ok);
    const std::vector<uint8_t> entry =
        BuildAv01SampleEntry(h, kReal, sizeof(kReal));
    CHECK_EQ(entry.size(), static_cast<size_t>(sizeof(kFfmpegEntry)) + 0x19);
    // Box size covers what this builder emits: the entry alone, no children.
    CHECK_EQ(entry[3], 0x6f);
    for (size_t i = 0; i < sizeof(kFfmpegEntry); ++i) {
        if (i < 4 || (i >= 0x32 && i < 0x52)) {
            continue;  // size field, compressorname
        }
        CHECK_EQ(entry[i], kFfmpegEntry[i]);
    }
    // Pascal string: length byte, name, then zero padding to 32 bytes.
    CHECK_EQ(entry[0x32], 7);
    CHECK_EQ(memcmp(entry.data() + 0x33, "HAL av1", 7), 0);
    for (size_t i = 0x3a; i < 0x52; ++i) {
        CHECK_EQ(entry[i], 0);
    }
    // Without a configOBUs payload the record still stands on its own: the
    // sequence header arrives in-band instead.
    const std::vector<uint8_t> bare = BuildAv01SampleEntry(h, nullptr, 0);
    CHECK_EQ(bare.size(), entry.size() - sizeof(kReal));
}

void TestCraftedBranches() {
    // Each of these walks a branch a hand-written parser gets wrong by guessing.
    ExpectOk(MakeAv1SequenceHeaderObu(Av1ShCfg()), "base");

    Av1ShCfg timing;
    timing.timing = true;
    ExpectOk(MakeAv1SequenceHeaderObu(timing), "timing, unequal interval");
    timing.equalPictureInterval = true;
    timing.numTicksPerPictureMinus1 = 4;
    ExpectOk(MakeAv1SequenceHeaderObu(timing), "timing, exp-Golomb ticks");

    Av1ShCfg idd;
    idd.initialDisplayDelay = true;
    idd.iddForOp = true;
    ExpectOk(MakeAv1SequenceHeaderObu(idd), "initial display delay");

    Av1ShCfg ops;
    ops.opCountMinus1 = 1;
    ops.level = 10;  // > 7, so seq_tier_0 follows each operating point
    ops.tier = true;
    ExpectOk(MakeAv1SequenceHeaderObu(ops), "two operating points");

    Av1ShCfg frameId;
    frameId.frameIdNumbers = true;
    ExpectOk(MakeAv1SequenceHeaderObu(frameId), "frame id numbers");

    Av1ShCfg noHint;
    noHint.orderHint = false;
    noHint.chooseScreenContent = false;
    noHint.secondScreenContentBit = false;
    ExpectOk(MakeAv1SequenceHeaderObu(noHint), "no order hint");

    // The screen-content group costs two bits with an order hint too, whatever
    // seq_choose_screen_content_tools selects. Reading only one of them shifts
    // every later field and is what this case catches.
    Av1ShCfg sct0;
    sct0.chooseScreenContent = false;
    sct0.secondScreenContentBit = false;
    ExpectOk(MakeAv1SequenceHeaderObu(sct0), "order hint, forced screen content");

    Av1ShCfg colour;
    colour.colorDescription = true;
    colour.colorRange = true;
    colour.chromaSamplePosition = 2;
    colour.separateUvDeltaQ = true;
    colour.filmGrain = true;
    {
        bool ok = false;
        const Av1SequenceHeader h = Parse(MakeAv1SequenceHeaderObu(colour), &ok);
        CHECK(ok);
        CHECK(h.unsupported == nullptr);
        CHECK_EQ(h.chromaSamplePosition, 2);
    }

    Av1ShCfg big;
    big.superblock128 = true;
    big.filterIntra = true;
    big.superres = true;
    big.widthBitsMinus1 = 10;  // 11 bits: 1919 does not fit the default 9
    big.heightBitsMinus1 = 10;
    big.widthMinus1 = 1919;  // 1920 px
    big.heightMinus1 = 1079;
    {
        bool ok = false;
        const Av1SequenceHeader h = Parse(MakeAv1SequenceHeaderObu(big), &ok);
        CHECK(ok);
        CHECK_EQ(h.width, 1920);
        CHECK_EQ(h.height, 1080);
    }

    // The width and height fields have independently signalled widths, which a
    // parser that assumed one shared width would get wrong.
    Av1ShCfg otherSize;
    otherSize.widthBitsMinus1 = 7;   // 8 bits: up to 256 px
    otherSize.heightBitsMinus1 = 8;  // 9 bits
    otherSize.widthMinus1 = 191;     // 192 px
    otherSize.heightMinus1 = 239;    // 240 px
    {
        bool ok = false;
        const Av1SequenceHeader h = Parse(MakeAv1SequenceHeaderObu(otherSize), &ok);
        CHECK(ok);
        CHECK_EQ(h.width, 192);
        CHECK_EQ(h.height, 240);
    }
}

void TestRefusedButParsable() {
    // Parsed cleanly, then refused because no sample available here exercises
    // them against a decoder: the caller must get a reason, not a wrong entry.
    struct Refusal {
        const char* what;
        Av1ShCfg cfg;
        const char* reason;
    };
    Av1ShCfg dmi;
    dmi.timing = true;
    dmi.decoderModelInfo = true;

    Av1ShCfg mono;
    mono.monoChrome = true;

    Av1ShCfg reduced;
    reduced.stillPicture = true;
    reduced.reduced = true;
    reduced.widthMinus1 = 63;
    reduced.heightMinus1 = 63;
    reduced.widthBitsMinus1 = 6;
    reduced.heightBitsMinus1 = 6;

    Av1ShCfg p1;
    p1.profile = 1;

    const Refusal cases[] = {
        {"decoder model info", dmi, "AV1 decoder model info is not supported"},
        {"grayscale", mono, "grayscale AV1 is not supported"},
        {"reduced still picture", reduced,
         "AV1 reduced still picture is not supported"},
        {"profile 1", p1, "only AV1 main profile (4:2:0) is supported"},
    };
    for (const Refusal& r : cases) {
        bool ok = false;
        const Av1SequenceHeader h = Parse(MakeAv1SequenceHeaderObu(r.cfg), &ok);
        CHECK(ok);
        CHECK(h.unsupported != nullptr);
        if (h.unsupported) {
            CHECK_EQ(std::string(h.unsupported), std::string(r.reason));
        }
    }
    // The reduced header is refused only once the walk has actually reached the
    // end of it: its dimensions come out right, which is what proves the three
    // superblock/filter flags are not inside the reduced guard.
    bool ok = false;
    const Av1SequenceHeader h = Parse(MakeAv1SequenceHeaderObu(reduced), &ok);
    CHECK(ok);
    CHECK_EQ(h.width, 64);
    CHECK_EQ(h.height, 64);
}

void TestRejectsMalformed() {
    // A header whose syntax does not close on the trailing bits must be refused:
    // that is the difference between a walked field count and a guessed one.
    Av1ShCfg bad;
    bad.trailingOneBit = 0;
    bool ok = true;
    Parse(MakeAv1SequenceHeaderObu(bad), &ok);
    CHECK(!ok);

    const std::vector<uint8_t> good = MakeAv1SequenceHeaderObu(Av1ShCfg());
    // Truncated payload: the walk runs off the buffer.
    std::vector<uint8_t> cut(good.begin(), good.begin() + 6);
    Parse(cut, &ok);
    CHECK(!ok);
    // A frame OBU is not a sequence header.
    Parse(MakeAv1Obu(7, std::vector<uint8_t>(8, 0x11)), &ok);
    CHECK(!ok);
    // forbidden_zero_bit set.
    std::vector<uint8_t> forbidden = good;
    forbidden[0] = 0x8a;
    Parse(forbidden, &ok);
    CHECK(!ok);
    Parse(std::vector<uint8_t>(), &ok);
    CHECK(!ok);
}

void TestObuFraming() {
    const std::vector<uint8_t> td = Av1TemporalDelimiter();
    CHECK(IsAv1ObuStream(td.data(), td.size()));
    // An H.264 SPS (0x67) and an HEVC VPS (0x40) both fail the delimiter test.
    const std::vector<uint8_t> h264Sps = {0x67, 0x42};
    const std::vector<uint8_t> hevcVps = {0x40, 0x01};
    CHECK(!IsAv1ObuStream(h264Sps.data(), h264Sps.size()));
    CHECK(!IsAv1ObuStream(hevcVps.data(), hevcVps.size()));
    CHECK(!IsAv1ObuStream(nullptr, 0));
    // A frame OBU (type 7 -> 0x3a) is AV1 but cannot announce the stream: only
    // the delimiter is guaranteed to be the first byte.
    const std::vector<uint8_t> frame = {0x3a, 0x00};
    CHECK(!IsAv1ObuStream(frame.data(), frame.size()));

    const std::vector<uint8_t> sh = MakeAv1SequenceHeaderObu(Av1ShCfg());
    const std::vector<uint8_t> body = Av1Unit({sh, MakeAv1Obu(7, std::vector<uint8_t>(16, 0x5a))});
    const std::vector<Av1Obu> obus = ScanObus(body.data(), body.size(), 64);
    CHECK_EQ(obus.size(), static_cast<size_t>(3));
    CHECK_EQ(obus[0].type, kAv1ObuTemporalDelimiter);
    CHECK_EQ(obus[0].size, td.size());
    CHECK_EQ(obus[1].type, kAv1ObuSequenceHeader);
    CHECK_EQ(obus[1].size, sh.size());
    CHECK_EQ(obus[2].type, kAv1ObuFrame);
    CHECK_EQ(obus[2].start, td.size() + sh.size());
    CHECK(!obus[2].runsToEnd);

    // Without a size field the OBU has no extent of its own, so it runs to the
    // end of the buffer and the caller has to bound it by the temporal unit.
    const std::vector<uint8_t> nosize = MakeAv1Obu(7, std::vector<uint8_t>(16, 0x5a), false);
    const std::vector<Av1Obu> one = ScanObus(nosize.data(), nosize.size(), 8);
    CHECK_EQ(one.size(), static_cast<size_t>(1));
    CHECK(one[0].runsToEnd);
    CHECK_EQ(one[0].size, nosize.size());

    // The bound stops the walk where it is told to, not at a picture boundary.
    CHECK_EQ(ScanObus(body.data(), body.size(), 2).size(), static_cast<size_t>(2));
    // A size field promising more bytes than the buffer holds is an incomplete
    // OBU, not a corrupt one: the walk reports nothing past it.
    std::vector<uint8_t> partial = td;
    partial.push_back(0x0a);
    partial.push_back(static_cast<uint8_t>(0x7f));
    partial.push_back(0x7f);
    const std::vector<Av1Obu> none = ScanObus(partial.data(), partial.size(), 8);
    CHECK_EQ(none.size(), static_cast<size_t>(1));
    CHECK(!IsAv1ObuStream(partial.data() + 2, 2));
}

void TestTemporalUnitGrouping() {
    const std::vector<uint8_t> sh = MakeAv1SequenceHeaderObu(Av1ShCfg());
    const std::vector<uint8_t> u1 = Av1Unit({sh, MakeAv1Obu(7, std::vector<uint8_t>(8, 0x11))});
    const std::vector<uint8_t> u2 = Av1Unit({MakeAv1Obu(7, std::vector<uint8_t>(8, 0x22))});
    std::vector<uint8_t> stream = u1;
    stream.insert(stream.end(), u2.begin(), u2.end());

    bool truncated = true;
    std::vector<Av1TemporalUnit> units =
        GroupAv1TemporalUnits(stream.data(), stream.size(), 8, 64, false, &truncated);
    // The second delimiter closes the first unit; nothing closes the second
    // until the caller says no more input is coming.
    CHECK_EQ(units.size(), static_cast<size_t>(1));
    CHECK_EQ(units[0].firstByte, static_cast<size_t>(0));
    CHECK_EQ(units[0].endByte, u1.size());
    CHECK(!truncated);

    units = GroupAv1TemporalUnits(stream.data(), stream.size(), 8, 64, true, &truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(2));
    CHECK_EQ(units[1].firstByte, u1.size());
    CHECK_EQ(units[1].endByte, stream.size());

    // OBUs ahead of the first delimiter belong to no unit. Arbitrary bytes
    // before it are a different matter: AV1 has no start codes, so a buffer that
    // does not parse as OBUs from its first byte yields nothing rather than a
    // guess at where a unit begins.
    std::vector<uint8_t> withJunk = {0xaa, 0xbb};
    withJunk.insert(withJunk.end(), u1.begin(), u1.end());
    units = GroupAv1TemporalUnits(withJunk.data(), withJunk.size(), 8, 64, true, &truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(0));
    std::vector<uint8_t> leadingSh = sh;
    leadingSh.insert(leadingSh.end(), u1.begin(), u1.end());
    units = GroupAv1TemporalUnits(leadingSh.data(), leadingSh.size(), 8, 64, true, &truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(1));
    CHECK_EQ(units[0].firstByte, sh.size());
    CHECK_EQ(units[0].endByte, leadingSh.size());

    // A unit count below the number of units present bounds the result, and the
    // OBU bound reports itself so a caller knows the trailing range is not a
    // complete unit even when it asked to close one.
    units = GroupAv1TemporalUnits(stream.data(), stream.size(), 1, 64, true, &truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(1));
    units = GroupAv1TemporalUnits(stream.data(), stream.size(), 8, 3, true, &truncated);
    CHECK(truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(1));

    // A chunk that ends inside an OBU leaves the open unit unclosed: the rest of
    // the picture has not arrived, and submitting the prefix would decode half a
    // frame.
    std::vector<uint8_t> chunk(stream.begin(), stream.begin() + u1.size() + 3);
    units = GroupAv1TemporalUnits(chunk.data(), chunk.size(), 8, 64, false, &truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(1));
    units = GroupAv1TemporalUnits(chunk.data(), chunk.size(), 8, 64, true, &truncated);
    CHECK_EQ(units.size(), static_cast<size_t>(2));
    CHECK_EQ(units[1].endByte, chunk.size());
    CHECK_EQ(GroupAv1TemporalUnits(nullptr, 0, 8, 64, true, &truncated).size(),
             static_cast<size_t>(0));
}

} // namespace

int main() {
    TestRealEncoderHeader();
    TestSampleEntryLayout();
    TestCraftedBranches();
    TestRefusedButParsable();
    TestRejectsMalformed();
    TestObuFraming();
    TestTemporalUnitGrouping();
    return haltest::finish("av1desc");
}
