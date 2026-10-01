// Unit tests for the H.264 picture-order front end (ITU-T H.264 8.2.1), the
// code that turns a raw Annex-B stream into the display order VideoToolbox does
// not report for an elementary stream.

#include "h264poc.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "h264_bits.h"
#include "test_check.h"

using halcodec::vtbox::H264Poc;
using haltest::MakePpsNal;
using haltest::MakeSliceNal;
using haltest::MakeSpsNal;
using haltest::PpsCfg;
using haltest::SliceCfg;
using haltest::SpsCfg;

namespace {

// Keys are PicOrderCnt plus a per-GOP bias, so the first GOP's pictures carry
// 1<<32. Tests state the GOP they expect a picture to land in.
const int64_t kGop1 = int64_t(1) << 32;
const int64_t kGop2 = int64_t(2) << 32;

const SpsCfg kSps;   // pic_order_cnt_type 0, log2MaxPocLsb 4, 4 references
const PpsCfg kPps;

// Feeds the parameter sets a stream would carry ahead of its first picture.
void Seed(H264Poc* poc, const SpsCfg& sps, const PpsCfg& pps) {
    const std::vector<uint8_t> s = MakeSpsNal(sps);
    const std::vector<uint8_t> p = MakePpsNal(pps);
    poc->FeedParameterSet(s.data(), s.size());
    poc->FeedParameterSet(p.data(), p.size());
}

// Classifies one slice and returns its key, or INT64_MIN when the front end had
// no order for it. `kind` reports which of the three answers it gave.
int64_t KeyOf(H264Poc* poc, const SliceCfg& slice, const SpsCfg& sps,
              const PpsCfg& pps, H264Poc::Slice* kind = nullptr) {
    const std::vector<uint8_t> n = MakeSliceNal(slice, sps, pps);
    int64_t key = 0;
    const H264Poc::Slice got = poc->ClassifySlice(n.data(), n.size(), &key);
    if (kind != nullptr) {
        *kind = got;
    }
    return got == H264Poc::Slice::Unknown ? INT64_MIN : key;
}

inline bool IsUnknown(H264Poc::Slice s) { return s == H264Poc::Slice::Unknown; }
inline bool IsContinuation(H264Poc::Slice s) { return s == H264Poc::Slice::Continuation; }
inline bool IsNewPicture(H264Poc::Slice s) { return s == H264Poc::Slice::NewPicture; }

void UnknownWithoutParameters() {
    H264Poc poc;
    SliceCfg slice;
    slice.pocLsb = 2;
    const std::vector<uint8_t> n = MakeSliceNal(slice, kSps, kPps);
    int64_t key = 0;
    CHECK(!poc.usable());
    CHECK(IsUnknown(poc.ClassifySlice(n.data(), n.size(), &key)));
}

void PocType0KeysFollowPicOrderCntLsb() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);
    CHECK(poc.usable());

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1);

    SliceCfg p1;
    p1.pocLsb = 2;
    p1.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, p1, kSps, kPps), kGop1 + 2);

    SliceCfg p2;
    p2.pocLsb = 4;
    p2.frameNum = 2;
    CHECK_EQ(KeyOf(&poc, p2, kSps, kPps), kGop1 + 4);
}

// A B frame decoded out of display order has to come back with the key that
// orders it between its references -- the whole reason the front end exists.
void PocType0OrdersDecodedOutOfSequence() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1 + 0);
    SliceCfg p1;
    p1.pocLsb = 4;
    p1.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, p1, kSps, kPps), kGop1 + 4);
    SliceCfg b;
    b.pocLsb = 2;
    b.nalRefIdc = 0;  // a non-reference picture does not move the state
    CHECK_EQ(KeyOf(&poc, b, kSps, kPps), kGop1 + 2);
    SliceCfg p2;
    p2.pocLsb = 6;
    p2.frameNum = 2;
    CHECK_EQ(KeyOf(&poc, p2, kSps, kPps), kGop1 + 6);
}

// pic_order_cnt_lsb is only the low bits; when it rolls over the front end has
// to carry the msb (8.2.1.1), or every picture after the wrap looks like the
// start of the stream.
void PocType0LsbWrapCarriesMsb() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);  // maxLsb = 16

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1 + 0);

    for (int i = 1; i <= 10; ++i) {
        SliceCfg s;
        s.pocLsb = (2 * i) % 16;
        s.frameNum = i;
        CHECK_EQ(KeyOf(&poc, s, kSps, kPps), kGop1 + 2 * i);
    }
}

// The same carry in reverse. A jump of more than half the lsb range is how the
// standard says the counter wrapped the other way, which is what a stream whose
// lsb runs backwards has to produce.
void PocType0LsbWrapBackwards() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1 + 0);
    SliceCfg a;
    a.pocLsb = 2;
    CHECK_EQ(KeyOf(&poc, a, kSps, kPps), kGop1 + 2);
    SliceCfg far;
    far.pocLsb = 14;  // 12 ahead of the last count, over half of maxLsb 16
    CHECK_EQ(KeyOf(&poc, far, kSps, kPps), kGop1 - 2);
    SliceCfg after;
    after.pocLsb = 4;
    after.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, after, kSps, kPps), kGop1 + 4);
}

// POC restarts at every IDR, so a reorder buffer that spans GOPs cannot compare
// raw values; the key carries a GOP bias that keeps it monotonic.
void IdrKeepsKeysMonotonicAcrossGops() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);

    SliceCfg idr;
    idr.type = 5;
    const int64_t first = KeyOf(&poc, idr, kSps, kPps);
    SliceCfg p1;
    p1.pocLsb = 6;
    p1.frameNum = 1;
    const int64_t second = KeyOf(&poc, p1, kSps, kPps);
    const int64_t third = KeyOf(&poc, idr, kSps, kPps);
    SliceCfg p2;
    p2.pocLsb = 4;
    p2.frameNum = 1;
    const int64_t fourth = KeyOf(&poc, p2, kSps, kPps);

    CHECK(first < second);
    // The second GOP's pictures sort after the first even though their raw POC
    // is smaller.
    CHECK(second < third);
    CHECK(third < fourth);
    CHECK_EQ(first, kGop1 + 0);
    CHECK_EQ(third, kGop2 + 0);
    CHECK_EQ(fourth, kGop2 + 4);
}

// Slices 2..n of a picture are not pictures: they must return the key of the
// slice that started it, which is what keeps them in one access unit.
void ContinuationSliceSharesPictureKey() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);

    SliceCfg idr;
    idr.type = 5;
    const int64_t key = KeyOf(&poc, idr, kSps, kPps);
    CHECK_EQ(key, kGop1);

    SliceCfg cont;
    cont.firstMb = 100;
    H264Poc::Slice kind = H264Poc::Slice::Unknown;
    CHECK_EQ(KeyOf(&poc, cont, kSps, kPps, &kind), key);
    CHECK(IsContinuation(kind));

    SliceCfg next;
    next.pocLsb = 2;
    next.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, next, kSps, kPps, &kind), kGop1 + 2);
    CHECK(IsNewPicture(kind));
}

void PocType2OrdersByFrameNum() {
    SpsCfg sps;
    sps.pocType = 2;
    H264Poc poc;
    Seed(&poc, sps, kPps);
    CHECK(poc.usable());

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, sps, kPps), kGop1 + 0);
    SliceCfg a;
    a.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, a, sps, kPps), kGop1 + 2);
    SliceCfg b;
    b.frameNum = 3;
    CHECK_EQ(KeyOf(&poc, b, sps, kPps), kGop1 + 6);
}

// Type 1 needs the PPS reference-cycle table; the front end declines rather than
// guessing, and the caller then keeps emitting decode order.
void PocType1IsReportedUnsupported() {
    SpsCfg sps;
    sps.pocType = 1;
    H264Poc poc;
    Seed(&poc, sps, kPps);
    CHECK(!poc.usable());

    SliceCfg slice;
    slice.pocLsb = 2;
    int64_t key = 0;
    const std::vector<uint8_t> n = MakeSliceNal(slice, sps, kPps);
    CHECK(IsUnknown(poc.ClassifySlice(n.data(), n.size(), &key)));
}

// High profile SPSs carry chroma/bit-depth/scaling-list fields that have to be
// stepped over; getting that wrong shifts every later field by a few bits.
void HighProfileSpsParses() {
    SpsCfg sps;
    sps.profileIdc = 100;
    sps.log2MaxPocLsbMinus4 = 2;  // maxLsb = 64, so pocLsb=8 won't wrap backward
    H264Poc poc;
    Seed(&poc, sps, kPps);
    CHECK(poc.usable());

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, sps, kPps), kGop1 + 0);
    SliceCfg p;
    p.pocLsb = 8;
    p.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, p, sps, kPps), kGop1 + 8);
}

// The reorder window the decoder holds back is sized from max_num_ref_frames,
// which is the decoded picture buffer bound the standard publishes.
void ReorderDelayFollowsMaxNumRefFrames() {
    for (int ref : {0, 1, 4, 16, 50}) {
        SpsCfg sps;
        sps.maxNumRefFrames = ref;
        H264Poc poc;
        Seed(&poc, sps, kPps);
        CHECK_EQ(poc.reorderDelay(), std::min(32, std::max(1, ref + 1)));
    }
}

// A PPS that sets bottom_field_pic_order_in_frame_present_flag adds a signed
// delta after the lsb. If it were not stepped over the slice would parse out of
// sync, so the key is the check.
void BottomFieldPresentStepsOverTheDelta() {
    PpsCfg pps;
    pps.bottomFieldPresent = true;
    H264Poc poc;
    Seed(&poc, kSps, pps);

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, pps), kGop1 + 0);
    SliceCfg p;
    p.pocLsb = 4;
    p.deltaBottom = -3;
    p.frameNum = 1;
    CHECK_EQ(KeyOf(&poc, p, kSps, pps), kGop1 + 4);
}

// Slices referring to a PPS or SPS that was never fed cannot be placed; the
// front end says so instead of inventing an order, and stays usable for the
// parameter sets it does know.
void SliceWithUnknownParameterSetIsUnknown() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);

    SliceCfg wrongPps;
    wrongPps.ppsId = 3;
    wrongPps.pocLsb = 2;
    H264Poc::Slice kind = H264Poc::Slice::Unknown;
    KeyOf(&poc, wrongPps, kSps, kPps, &kind);
    CHECK(IsUnknown(kind));
    CHECK(poc.usable());

    // A PPS whose SPS is missing: parsed, but it cannot order anything.
    PpsCfg secondPps;
    secondPps.ppsId = 1;
    secondPps.spsId = 1;
    const std::vector<uint8_t> p = MakePpsNal(secondPps);
    poc.FeedParameterSet(p.data(), p.size());
    SliceCfg noSps;
    noSps.ppsId = 1;
    noSps.pocLsb = 2;
    KeyOf(&poc, noSps, kSps, secondPps, &kind);
    CHECK(IsUnknown(kind));
    CHECK(poc.usable());
}

// One slice header that cannot be read at all means the reader is desynchronised
// and every key after it would be invented, so the front end gives up
// permanently and the decoder falls back to decode order.
void CorruptSliceDisablesOrdering() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);
    CHECK(poc.usable());

    // A NAL with only the header byte and one RBSP byte: not enough to parse
    // first_mb_in_slice, slice_type, and pps_id.
    const uint8_t broken[] = {0x41, 0x00};  // type=1, nalRefIdc=2; truncated payload
    int64_t key = 0;
    CHECK(IsUnknown(poc.ClassifySlice(broken, sizeof(broken), &key)));

    SliceCfg good;
    good.pocLsb = 2;
    CHECK_EQ(KeyOf(&poc, good, kSps, kPps), INT64_MIN);
    CHECK(!poc.usable());
}

void ATooShortNalIsIgnoredRatherThanGivingUp() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);
    const uint8_t one = 0x41;
    int64_t key = 0;
    CHECK(IsUnknown(poc.ClassifySlice(&one, 1, &key)));
    // Not a desynchronised read, so ordering survives.
    CHECK(poc.usable());
    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1);
}

void ResetClearsTheGopBias() {
    H264Poc poc;
    Seed(&poc, kSps, kPps);

    SliceCfg idr;
    idr.type = 5;
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1);
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop2);
    poc.Reset();
    CHECK_EQ(KeyOf(&poc, idr, kSps, kPps), kGop1);
}

} // namespace

int main() {
    UnknownWithoutParameters();
    PocType0KeysFollowPicOrderCntLsb();
    PocType0OrdersDecodedOutOfSequence();
    PocType0LsbWrapCarriesMsb();
    PocType0LsbWrapBackwards();
    IdrKeepsKeysMonotonicAcrossGops();
    ContinuationSliceSharesPictureKey();
    PocType2OrdersByFrameNum();
    PocType1IsReportedUnsupported();
    HighProfileSpsParses();
    ReorderDelayFollowsMaxNumRefFrames();
    BottomFieldPresentStepsOverTheDelta();
    SliceWithUnknownParameterSetIsUnknown();
    CorruptSliceDisablesOrdering();
    ATooShortNalIsIgnoredRatherThanGivingUp();
    ResetClearsTheGopBias();
    return haltest::finish("h264poc_test");
}
