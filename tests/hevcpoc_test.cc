// Unit tests for the HEVC picture-order front end (ITU-T H.265 8.3.1): the code
// that derives a picture's display order from a raw Annex-B HEVC stream, which
// carries no container timing and gets no display order reported by VideoToolbox.

#include "hevcpoc.h"

#include <cstdint>
#include <vector>

#include "h264_bits.h"
#include "test_check.h"

using halcodec::vtbox::HEVCPoc;
using haltest::HevcPpsCfg;
using haltest::HevcSliceCfg;
using haltest::HevcSpsCfg;
using haltest::MakeHevcPpsNal;
using haltest::MakeHevcSliceNal;
using haltest::MakeHevcSpsNal;
using haltest::MakeHevcVpsNal;

namespace {

bool IsUnknown(HEVCPoc::Slice s) { return s == HEVCPoc::Slice::Unknown; }
bool IsContinuation(HEVCPoc::Slice s) {
    return s == HEVCPoc::Slice::Continuation;
}
bool IsNewPicture(HEVCPoc::Slice s) { return s == HEVCPoc::Slice::NewPicture; }

const HevcSpsCfg kSps;
const HevcPpsCfg kPps;

// Feeds the VPS/SPS/PPS triplet a stream carries ahead of its first picture.
void Seed(HEVCPoc* poc, const HevcSpsCfg& sps, const HevcPpsCfg& pps) {
    const std::vector<uint8_t> v = MakeHevcVpsNal();
    const std::vector<uint8_t> s = MakeHevcSpsNal(sps);
    const std::vector<uint8_t> p = MakeHevcPpsNal(pps);
    poc->FeedParameterSet(v.data(), v.size());
    poc->FeedParameterSet(s.data(), s.size());
    poc->FeedParameterSet(p.data(), p.size());
}

// Classifies one slice and reports which of the three answers the front end gave.
HEVCPoc::Slice Classify(HEVCPoc* poc, int nalType, const HevcSliceCfg& cfg,
                        int64_t* key) {
    const std::vector<uint8_t> n = MakeHevcSliceNal(nalType, cfg);
    return poc->ClassifySlice(n.data(), n.size(), key);
}

void TestUnusableBeforeParameters() {
    HEVCPoc poc;
    CHECK(!poc.usable());
    int64_t key = 0;
    // With no PPS fed yet a slice has no parameters to order against.
    CHECK(IsUnknown(Classify(&poc, 1, HevcSliceCfg{true, 0}, &key)));
}

void TestUsableAfterVpsSpsPps() {
    HEVCPoc poc;
    Seed(&poc, kSps, kPps);
    CHECK(poc.usable());
}

void TestSequentialPictures() {
    HEVCPoc poc;
    Seed(&poc, kSps, kPps);
    CHECK(poc.usable());

    // A monotonic run of POC LSBs inside one GOP maps straight onto the keys:
    // the front end is at gop 0 and no LSB wrap has happened.
    for (int64_t i = 0; i < 5; ++i) {
        int64_t key = -1;
        CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, i}, &key)));
        CHECK_EQ(key, i);
    }
}

void TestMultiSlicePicture() {
    HEVCPoc poc;
    Seed(&poc, kSps, kPps);

    int64_t key0 = -1;
    CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, 0}, &key0)));
    CHECK_EQ(key0, 0);

    // A second slice segment (first_slice flag 0) belongs to the same picture
    // and must report the same display order.
    int64_t key1 = -1;
    CHECK(IsContinuation(Classify(&poc, 1, HevcSliceCfg{false, 0}, &key1)));
    CHECK_EQ(key1, key0);
}

void TestIrapResetsGop() {
    HEVCPoc poc;
    Seed(&poc, kSps, kPps);

    int64_t key0 = -1;
    CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, 0}, &key0)));
    CHECK_EQ(key0, 0);

    // An IDR starts a fresh GOP: its POC is 0 but the per-GOP bias pushes its
    // key past every picture of the previous GOP.
    int64_t keyIdr = -1;
    CHECK(IsNewPicture(Classify(&poc, 19, HevcSliceCfg{true, 0}, &keyIdr)));
    CHECK(keyIdr > key0);
    const int64_t kGop1 = int64_t(1) << 32;
    CHECK_EQ(keyIdr, kGop1);

    // The picture right after the IDR counts up from the new GOP's zero.
    int64_t keyNext = -1;
    CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, 2}, &keyNext)));
    CHECK_EQ(keyNext, kGop1 + 2);
}

void TestScalableSubLayers() {
    // A 4-temporal-layer SPS (sps_max_sub_layers_minus1 = 3, as a camera/screen
    // scalable encoder emits) must still reach the picture-order fields: the
    // sub-layer profile_tier_level entries have to be skipped exactly, or every
    // later ue() desyncs and the front end gives up.
    HEVCPoc poc;
    HevcSpsCfg sps;
    sps.maxSubLayersMinus1 = 3;
    sps.maxNumReorderPics = 4;  // generator emits reorder 4,5,6,7 across layers
    Seed(&poc, sps, kPps);
    CHECK(poc.usable());
    // The delay takes the largest bound across the layers (layer 3 -> 4 + 3).
    CHECK_EQ(poc.reorderDelay(), 7);

    // Ordering still works over the scalable stream.
    for (int64_t i = 0; i < 4; ++i) {
        int64_t key = -1;
        CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, i}, &key)));
        CHECK_EQ(key, i);
    }
}

void TestPocMsbCarry() {
    // log2_max_pic_order_cnt_lsb = 4 -> MaxPicOrderCntLsb 16: the LSB wraps after
    // 16 pictures and the msb carry has to keep the display order monotonic.
    HEVCPoc poc;
    HevcSpsCfg sps;
    sps.log2MaxPocLsbMinus4 = 0;
    Seed(&poc, sps, kPps);

    for (int64_t i = 0; i < 20; ++i) {
        int64_t lsb = i % 16;  // the transmitted field wraps at MaxPicOrderCntLsb
        int64_t key = -1;
        CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, lsb}, &key)));
        CHECK_EQ(key, i);  // carried msb makes the wrap invisible to the order
    }
}

void TestRextHighTier() {
    // Rext/MRange (profile_idc 4-7): 4:2:2 / 4:4:4 / high-bit-depth, the family
    // x265 and VideoToolbox emit for those pixel formats. Its constraint region
    // is 44 bits (9 named flags + 34 reserved_zero_34bits + 1 reserved_zero_bit),
    // so the picture-order fields stay reachable and reordering is enabled.
    HEVCPoc poc;
    HevcSpsCfg sps;
    sps.profileIdc = 4;
    sps.chromaFormatIdc = 3;       // 4:4:4
    sps.bitDepthLumaMinus8 = 2;    // 10-bit
    sps.maxNumReorderPics = 2;
    Seed(&poc, sps, kPps);
    CHECK(poc.usable());
    CHECK_EQ(poc.reorderDelay(), 2);

    // POC derivation runs the same over a Rext picture as over Main.
    for (int64_t i = 0; i < 3; ++i) {
        int64_t key = -1;
        CHECK(IsNewPicture(Classify(&poc, 1, HevcSliceCfg{true, i}, &key)));
        CHECK_EQ(key, i);
    }
}

void TestUnsupportedHighTierBails() {
    // SEG (profile_idc 8) has a different, unmodelled constraint size, so the
    // front end must give up rather than read a desynchronised POC: usable()
    // stays false and slices classify as Unknown, leaving the caller in the
    // safe decode-order passthrough.
    HEVCPoc poc;
    HevcSpsCfg sps;
    sps.profileIdc = 8;
    Seed(&poc, sps, kPps);
    CHECK(!poc.usable());
    int64_t key = 0;
    CHECK(IsUnknown(Classify(&poc, 1, HevcSliceCfg{true, 0}, &key)));
}

}  // namespace

int main() {
    TestUnusableBeforeParameters();
    TestUsableAfterVpsSpsPps();
    TestSequentialPictures();
    TestMultiSlicePicture();
    TestIrapResetsGop();
    TestScalableSubLayers();
    TestPocMsbCarry();
    TestRextHighTier();
    TestUnsupportedHighTierBails();
    return haltest::finish("hevcpoc");
}
