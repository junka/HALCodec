#include "hevcpoc.h"

#include <algorithm>
#include <cstring>

namespace halcodec {
namespace vtbox {

namespace {

// MSB-first bit reader over an RBSP (same as H264Poc's Bits).
class Bits {
public:
    Bits(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool u(int bits, int64_t* out) {
        int64_t v = 0;
        for (int i = 0; i < bits; ++i) {
            int next = 0;
            if (!Read(&next)) {
                return false;
            }
            v = (v << 1) | next;
        }
        *out = v;
        return true;
    }

    bool ue(int64_t* out) {
        int zeros = 0;
        int next = 0;
        while (true) {
            if (!Read(&next)) {
                return false;
            }
            if (next) {
                break;
            }
            if (++zeros > 31) {
                return false;
            }
        }
        int64_t tail = 0;
        if (zeros > 0 && (!u(zeros, &tail))) {
            return false;
        }
        *out = (static_cast<int64_t>(1) << zeros) - 1 + tail;
        return true;
    }

    bool se(int64_t* out) {
        int64_t k = 0;
        if (!ue(&k)) {
            return false;
        }
        *out = (k & 1) ? (k + 1) / 2 : -(k / 2);
        return true;
    }

private:
    bool Read(int* out) {
        const size_t byte = pos_ >> 3;
        if (byte >= size_) {
            return false;
        }
        *out = (data_[byte] >> (7 - (pos_ & 7))) & 1;
        ++pos_;
        return true;
    }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

// Removes emulation prevention bytes and copies at most `want` payload bytes.
std::vector<uint8_t> Rbsp(const uint8_t* src, size_t len, size_t want) {
    std::vector<uint8_t> out;
    out.reserve(std::min(len, want) + 1);
    for (size_t i = 0; i < len && out.size() < want; ++i) {
        if (i + 2 < len && src[i] == 0 && src[i + 1] == 0 && src[i + 2] == 3) {
            out.push_back(0);
            out.push_back(0);
            i += 2;
            continue;
        }
        out.push_back(src[i]);
    }
    return out;
}

const size_t kSpsBytes = 512;
const size_t kSlicePrefixBytes = 48;

// Consumes the shared constraint-signalling block of a profile_tier_level entry
// (general_ or one sub_layer): profile_space/tier/idc, the 32 compatibility
// flags, the 4 source flags, and the reserved tail that follows them.
//
// That tail is ALWAYS 44 bits, whatever the profile: the Main family reads
// reserved_zero_43bits + reserved_zero_bit and the high-tier (Rext/MRange/SEG/
// SVC/MVC/SC/SCC, idc 4..13 -- what x265 and VideoToolbox emit for 4:2:2 / 4:4:4
// / high-bit-depth / screen content) branch reads nine named flags +
// reserved_zero_34bits + reserved_zero_bit; both sum to 44. Verified against
// ffmpeg's own SPS parser: flipping only the 5-bit profile_idc of a real Main
// stream (keeping the 44-bit tail) still yields sane width/height/level for
// every idc 1..13, so no branch changes the tail length and none needs a
// special case or a bail here.
bool SkipProfileConstraints(Bits& b) {
    int64_t v = 0;
    if (!b.u(2, &v) || !b.u(1, &v)) {  // profile_space, tier_flag
        return false;
    }
    if (!b.u(5, &v)) {  // profile_idc (consumed; the tail size does not depend on it)
        return false;
    }
    for (int j = 0; j < 32; ++j) {  // profile_compatibility_flag[32]
        if (!b.u(1, &v)) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {  // progressive/interlaced/non_packed/frame_only
        if (!b.u(1, &v)) {
            return false;
        }
    }
    return b.u(44, &v);  // fixed-size constraint tail, all profiles
}

// Consumes a full profile_tier_level() (7.3.2.1.1), including the sub-layer
// entries a temporally scalable SPS carries, so the reader lands on the fields
// that hold the picture-order parameters. profilePresentFlag is always 1: the
// only caller is the SPS.
bool SkipProfileTierLevel(Bits& b, int64_t maxSubLayersMinus1) {
    int64_t v = 0;
    if (!SkipProfileConstraints(b)) {
        return false;
    }
    if (!b.u(8, &v)) {  // general_level_idc
        return false;
    }
    if (maxSubLayersMinus1 < 0 || maxSubLayersMinus1 > 7) {
        return false;
    }
    // sub_layer profile/level presence flags.
    bool subProfilePresent[7] = {false};
    bool subLevelPresent[7] = {false};
    for (int i = 0; i < maxSubLayersMinus1; ++i) {
        int64_t p = 0, l = 0;
        if (!b.u(1, &p) || !b.u(1, &l)) {
            return false;
        }
        subProfilePresent[i] = p != 0;
        subLevelPresent[i] = l != 0;
    }
    // Padding to 8 sub-layer slots, present whenever there is more than one.
    for (int i = static_cast<int>(maxSubLayersMinus1); i < 8 &&
              maxSubLayersMinus1 > 0; ++i) {
        if (!b.u(2, &v)) {  // reserved_zero_2bits
            return false;
        }
    }
    for (int i = 0; i < maxSubLayersMinus1; ++i) {
        if (subProfilePresent[i] && !SkipProfileConstraints(b)) {
            return false;
        }
        if (subLevelPresent[i] && !b.u(8, &v)) {  // sub_layer_level_idc
            return false;
        }
    }
    return true;
}

} // namespace

void HEVCPoc::Reset() {
    ResetState();
    gopIndex_ = 0;
}

void HEVCPoc::ResetState() {
    havePrev_ = false;
    prevPocMsb_ = 0;
    prevPocLsb_ = 0;
    lastPictureKey_ = 0;
}

void HEVCPoc::RefreshUsable() {
    if (gaveUp_) {
        return;
    }
    for (const Pps& pps : ppsById_) {
        if (!pps.valid) {
            continue;
        }
        if (static_cast<size_t>(pps.spsId) < spsById_.size() &&
            spsById_[static_cast<size_t>(pps.spsId)].valid) {
            if (!usable_) {
                ResetState();
            }
            usable_ = true;
            const Sps& sps = spsById_[static_cast<size_t>(pps.spsId)];
            reorderDelay_ = std::max(1, sps.maxNumReorderPics);
            return;
        }
    }
}

void HEVCPoc::ParseVps(const uint8_t* nalu, size_t len) {
    // Minimal parse: just validate it exists. The VPS carries no picture-order
    // parameters, and its id is not needed to key the SPS/PPS tables.
    if (len < 2) {
        return;
    }
    if (vpsById_.empty()) {
        vpsById_.resize(1);
    }
    vpsById_[0].valid = true;
}

void HEVCPoc::ParseSps(const uint8_t* nalu, size_t len) {
    // HEVC NAL header is 2 bytes; the syntax element stream starts after it.
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 2, len - 2, kSpsBytes);
    Bits b(rbsp.data(), rbsp.size());

    Sps sps;
    int64_t v = 0;

    // sps_video_parameter_set_id u(4)
    if (!b.u(4, &v)) {
        return;
    }
    // sps_max_sub_layers_minus1 u(3)
    int64_t maxSubLayersMinus1 = 0;
    if (!b.u(3, &maxSubLayersMinus1)) {
        return;
    }
    // sps_temporal_id_nesting_flag u(1)
    if (!b.u(1, &v)) {
        return;
    }
    // profile_tier_level(1, maxSubLayersMinus1): consumes the general entry plus
    // any sub-layer entries a temporally scalable stream carries. A truncated or
    // corrupt header makes the reader run out of bits here, so the front end
    // gives up on reordering rather than desynchronising the stream.
    if (!SkipProfileTierLevel(b, maxSubLayersMinus1)) {
        gaveUp_ = true;
        return;
    }

    int64_t spsId = 0;
    if (!b.ue(&spsId)) {
        return;
    }

    int64_t chromaFormatIdc = 1;
    if (!b.ue(&chromaFormatIdc)) {
        return;
    }
    if (chromaFormatIdc == 3) {
        int64_t separateColourPlane = 0;
        if (!b.u(1, &separateColourPlane)) {
            return;
        }
    }

    // pic_width/height, conformance_window_flag (+ 4 offsets when set).
    int64_t skip = 0;
    if (!b.ue(&skip) || !b.ue(&skip) || !b.u(1, &skip)) {
        return;
    }
    if (skip) {
        for (int i = 0; i < 4; ++i) {
            if (!b.ue(&skip)) {
                return;
            }
        }
    }

    // bit_depth_luma_minus8, bit_depth_chroma_minus8.
    if (!b.ue(&skip) || !b.ue(&skip)) {
        return;
    }

    // log2_max_pic_order_cnt_lsb_minus4 -> MaxPicOrderCntLsb = 1 << (x + 4).
    int64_t log2MaxPocLsbMinus4 = 0;
    if (!b.ue(&log2MaxPocLsbMinus4)) {
        return;
    }
    sps.log2MaxPocLsb = static_cast<int>(log2MaxPocLsbMinus4) + 4;

    // sps_sub_layer_ordering_info_present_flag; one triple per layer, from
    // layer 0 when present else only the highest layer. The reorder delay takes
    // the largest bound across the layers, which is what a full-rate decode
    // (every temporal layer submitted) has to hold back.
    int64_t subLayerOrderingInfoPresent = 0;
    if (!b.u(1, &subLayerOrderingInfoPresent)) {
        return;
    }
    const int firstLayer = subLayerOrderingInfoPresent ? 0
                                                        : static_cast<int>(maxSubLayersMinus1);
    int maxReorder = 0;
    int maxDpb = 0;
    for (int i = firstLayer; i <= static_cast<int>(maxSubLayersMinus1); ++i) {
        int64_t maxDecPicBufferingMinus1 = 0;
        int64_t maxNumReorderPics = 0;
        int64_t maxLatencyIncreasePlus1 = 0;
        if (!b.ue(&maxDecPicBufferingMinus1) || !b.ue(&maxNumReorderPics) ||
            !b.ue(&maxLatencyIncreasePlus1)) {
            return;
        }
        maxReorder = std::max(maxReorder, static_cast<int>(maxNumReorderPics));
        maxDpb = std::max(maxDpb, static_cast<int>(maxDecPicBufferingMinus1) + 1);
    }
    sps.maxNumReorderPics = maxReorder;
    sps.maxDpbSize = maxDpb;

    sps.valid = true;
    if (static_cast<size_t>(spsId) >= spsById_.size()) {
        spsById_.resize(static_cast<size_t>(spsId) + 1);
    }
    spsById_[static_cast<size_t>(spsId)] = sps;

    RefreshUsable();
}

void HEVCPoc::ParsePps(const uint8_t* nalu, size_t len) {
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 2, len - 2, 64);
    Bits b(rbsp.data(), rbsp.size());

    Pps pps;

    // pps_pic_parameter_set_id, pps_seq_parameter_set_id.
    int64_t ppsId = 0;
    int64_t spsId = 0;
    if (!b.ue(&ppsId) || !b.ue(&spsId)) {
        return;
    }
    pps.spsId = static_cast<int>(spsId);
    pps.valid = true;

    if (static_cast<size_t>(ppsId) >= ppsById_.size()) {
        ppsById_.resize(static_cast<size_t>(ppsId) + 1);
    }
    ppsById_[static_cast<size_t>(ppsId)] = pps;

    RefreshUsable();
}

void HEVCPoc::FeedParameterSet(const uint8_t* nalu, size_t len) {
    if (len < 2) {
        return;
    }
    // HEVC NAL type is in bits 1-6 of the first byte (after forbidden_zero_bit)
    const int nalUnitType = (nalu[0] >> 1) & 0x3F;
    if (nalUnitType == 32) {
        ParseVps(nalu, len);
    } else if (nalUnitType == 33) {
        ParseSps(nalu, len);
    } else if (nalUnitType == 34) {
        ParsePps(nalu, len);
    }
}

HEVCPoc::Slice HEVCPoc::ClassifySlice(const uint8_t* nalu, size_t len,
                                      int64_t* poc) {
    if (len < 2 || gaveUp_) {
        return Slice::Unknown;
    }
    
    // HEVC NAL header is 2 bytes: forbidden_zero_bit (1) + nal_unit_type (6) + 
    // nuh_layer_id (6) + nuh_temporal_id_plus1 (3)
    const int nalUnitType = (nalu[0] >> 1) & 0x3F;
    
    // VCL NAL types: TRAIL_N/R (0/1), TSA_N/R (2/3), STSA_N/R (4/5), 
    // RADL_N/R (6/7), RASL_N/R (8/9), BLA/W_LP/W_RADL (16/17/18), 
    // IDR/N_LP (19/20), CRA_NUT (21)
    const bool isVcl = (nalUnitType <= 9) || (nalUnitType >= 16 && nalUnitType <= 23);
    if (!isVcl) {
        return Slice::Unknown;
    }
    
    // Parse slice segment header to get first_slice_segment_in_pic_flag and POC
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 2, len - 2, kSlicePrefixBytes);
    Bits b(rbsp.data(), rbsp.size());
    
    int64_t firstSliceFlag = 0;
    if (!b.u(1, &firstSliceFlag)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
    }
    
    // If not first slice, this is a continuation
    if (!firstSliceFlag) {
        *poc = lastPictureKey_;
        return Slice::Continuation;
    }
    
    // Slice segment header (7.3.2.2), in the order the syntax defines it.
    const bool isIrap = (nalUnitType >= 16 && nalUnitType <= 23);
    const bool isIdr = (nalUnitType == 19 || nalUnitType == 20);

    // IRAP pictures carry no_output_of_prior_pics_flag before the PPS id.
    if (isIrap) {
        int64_t noOutputOfPriorPicsFlag = 0;
        if (!b.u(1, &noOutputOfPriorPicsFlag)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
    }

    // slice_pic_parameter_set_id, then the PPS/SPS this picture refers to.
    int64_t ppsId = 0;
    if (!b.ue(&ppsId)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
    }

    if (static_cast<size_t>(ppsId) >= ppsById_.size() ||
        !ppsById_[static_cast<size_t>(ppsId)].valid) {
        return Slice::Unknown;
    }

    const Pps& pps = ppsById_[static_cast<size_t>(ppsId)];
    if (static_cast<size_t>(pps.spsId) >= spsById_.size() ||
        !spsById_[static_cast<size_t>(pps.spsId)].valid) {
        return Slice::Unknown;
    }

    const Sps& sps = spsById_[static_cast<size_t>(pps.spsId)];

    // slice_type sits between the PPS id and the picture order count; it is
    // read and discarded -- the front end orders every picture the same way.
    int64_t sliceType = 0;
    if (!b.ue(&sliceType)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
    }

    // A random access point starts a fresh display-order run.
    if (isIrap) {
        ++gopIndex_;
        ResetState();
    }

    // IDR pictures have an implicit order count of 0; every other type carries
    // pic_order_cnt_lsb. (HEVC has no pic_order_cnt_type field: the LSB-plus-
    // carry derivation of 8.3.1 is the only scheme, always "type 0".)
    int64_t lsb = 0;
    if (!isIdr) {
        if (!b.u(sps.log2MaxPocLsb, &lsb)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
    }
    
    // Compute PicOrderCntVal using the same algorithm as H.264 type 0
    const int64_t maxLsb = static_cast<int64_t>(1) << sps.log2MaxPocLsb;
    int64_t msb = 0;
    if (!isIrap && havePrev_) {
        if (lsb < prevPocLsb_ &&
            (prevPocLsb_ - lsb) >= maxLsb / 2) {
            msb = prevPocMsb_ + maxLsb;
        } else if (lsb > prevPocLsb_ &&
                   (lsb - prevPocLsb_) > maxLsb / 2) {
            msb = prevPocMsb_ - maxLsb;
        } else {
            msb = prevPocMsb_;
        }
    }
    
    const int64_t value = msb + lsb;
    
    // Update state for reference pictures (simplified: treat all as reference)
    prevPocMsb_ = msb;
    prevPocLsb_ = lsb;
    havePrev_ = true;
    
    const int64_t key = (gopIndex_ << kGopIndexShift) + value;
    lastPictureKey_ = key;
    *poc = key;
    return Slice::NewPicture;
}

} // namespace vtbox
} // namespace halcodec
