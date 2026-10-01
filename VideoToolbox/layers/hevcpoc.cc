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
    // Minimal parse: just validate it exists. VPS ID is not needed for POC.
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 1, len - 1, 64);
    if (rbsp.size() < 2) {
        return;
    }
    // Skip vps_video_parameter_set_id (4 bits) and reserved bits.
    // We just mark VPS as present.
    if (vpsById_.empty()) {
        vpsById_.resize(1);
    }
    vpsById_[0].valid = true;
}

void HEVCPoc::ParseSps(const uint8_t* nalu, size_t len) {
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 1, len - 1, kSpsBytes);
    Bits b(rbsp.data(), rbsp.size());

    Sps sps;
    int64_t v = 0;

    // sps_video_parameter_set_id (4 bits)
    if (!b.u(4, &v)) {
        return;
    }
    
    int64_t spsMaxSubLayersMinus1 = 0;
    int64_t spsTemporallIdNestingFlag = 0;
    if (!b.u(4, &v) || !b.u(1, &spsTemporallIdNestingFlag)) {
        return;
    }
    
    // profile_tier_level (skip for now, complex structure)
    // Just skip enough to reach pic_order_cnt_type
    // general_profile_space (2) + general_tier_flag (1) + general_profile_idc (5)
    // + general_profile_compatibility_flag[32] + general_progressive_source_flag (1)
    // + general_interlaced_source_flag (1) + general_non_packed_constraint_flag (1)
    // + general_frame_only_constraint_flag (1) + general_max_12bit_constraint_flag (1)
    // + general_max_10bit_constraint_flag (1) + general_max_8bit_constraint_flag (1)
    // + general_max_422chroma_constraint_flag (1) + general_max_420chroma_constraint_flag (1)
    // + general_max_monochrome_constraint_flag (1) + general_intra_constraint_flag (1)
    // + general_one_picture_only_constraint_flag (1) + general_lower_bit_rate_constraint_flag (1)
    // + general_max_14bit_constraint_flag (1) + reserved_zero_33bits (33)
    // This is very long, let's use a simpler approach: read until we find what we need
    
    // Simplified: skip profile_tier_level by reading known fields
    int64_t skip = 0;
    if (!b.u(2, &skip) || !b.u(1, &skip) || !b.u(5, &skip)) {
        return;
    }
    // general_profile_compatibility_flag[32]
    for (int i = 0; i < 32; ++i) {
        if (!b.u(1, &skip)) {
            return;
        }
    }
    // progressive_source_flag + interlaced_source_flag + non_packed_constraint_flag + frame_only_constraint_flag
    if (!b.u(1, &skip) || !b.u(1, &skip) || !b.u(1, &skip) || !b.u(1, &skip)) {
        return;
    }
    // Skip remaining constraint flags (variable length based on profile_idc)
    // For simplicity, assume main profile and skip the rest
    // max_12bit + max_10bit + max_8bit + max_422chroma + max_420chroma + max_monochrome + intra + one_picture + lower_bit_rate + max_14bit
    for (int i = 0; i < 10; ++i) {
        if (!b.u(1, &skip)) {
            return;
        }
    }
    // reserved_zero_33bits (actually 32 + 1 in newer specs, but we'll read 33)
    for (int i = 0; i < 33; ++i) {
        if (!b.u(1, &skip)) {
            return;
        }
    }
    
    // sps_seq_parameter_set_id
    int64_t spsId = 0;
    if (!b.ue(&spsId)) {
        return;
    }
    
    // chroma_format_idc
    int64_t chromaFormatIdc = 1;
    if (!b.ue(&chromaFormatIdc)) {
        return;
    }
    if (chromaFormatIdc == 3) {
        if (!b.u(1, &skip)) {
            return;
        }
    }
    
    // pic_width_in_luma_samples, pic_height_in_luma_samples, conformance_window_flag
    if (!b.ue(&skip) || !b.ue(&skip) || !b.u(1, &skip)) {
        return;
    }
    if (skip) {
        // conf_win_* offsets
        for (int i = 0; i < 4; ++i) {
            if (!b.ue(&skip)) {
                return;
            }
        }
    }
    
    // bit_depth_luma_minus8, bit_depth_chroma_minus8
    if (!b.ue(&skip) || !b.ue(&skip)) {
        return;
    }
    
    // log2_max_pic_order_cnt_lsb_minus4
    int64_t log2MaxPocLsbMinus4 = 0;
    if (!b.ue(&log2MaxPocLsbMinus4)) {
        return;
    }
    sps.log2MaxPocLsb = static_cast<int>(log2MaxPocLsbMinus4) + 4;
    
    // sps_sub_layer_ordering_info_present_flag
    int64_t subLayerOrderingInfoPresent = 0;
    if (!b.u(1, &subLayerOrderingInfoPresent)) {
        return;
    }
    
    // For each temporal layer, read max_num_reorder_pics and max_dec_pic_buffering_minus1
    int startLayer = subLayerOrderingInfoPresent ? 0 : static_cast<int>(spsMaxSubLayersMinus1);
    for (int i = startLayer; i <= static_cast<int>(spsMaxSubLayersMinus1); ++i) {
        int64_t maxNumReorderPics = 0;
        int64_t maxDecPicBufferingMinus1 = 0;
        if (!b.ue(&maxNumReorderPics) || !b.ue(&maxDecPicBufferingMinus1)) {
            return;
        }
        if (i == static_cast<int>(spsMaxSubLayersMinus1)) {
            sps.maxNumReorderPics = static_cast<int>(maxNumReorderPics);
            sps.maxDpbSize = static_cast<int>(maxDecPicBufferingMinus1) + 1;
        }
    }
    
    // log2_min_luma_coding_block_size_minus3, log2_diff_max_min_luma_coding_block_size
    if (!b.ue(&skip) || !b.ue(&skip)) {
        return;
    }
    // ... many more fields, skip to end
    
    // Mark SPS as valid with the fields we extracted
    if (static_cast<size_t>(spsId) >= spsById_.size()) {
        spsById_.resize(static_cast<size_t>(spsId) + 1);
    }
    spsById_[static_cast<size_t>(spsId)] = sps;
    sps.valid = true;
    
    RefreshUsable();
}

void HEVCPoc::ParsePps(const uint8_t* nalu, size_t len) {
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 1, len - 1, 64);
    Bits b(rbsp.data(), rbsp.size());
    
    Pps pps;
    int64_t v = 0;
    
    // pps_pic_parameter_set_id
    int64_t ppsId = 0;
    if (!b.ue(&ppsId)) {
        return;
    }
    
    // pps_seq_parameter_set_id
    int64_t spsId = 0;
    if (!b.ue(&spsId)) {
        return;
    }
    pps.spsId = static_cast<int>(spsId);
    
    // Mark PPS as valid
    if (static_cast<size_t>(ppsId) >= ppsById_.size()) {
        ppsById_.resize(static_cast<size_t>(ppsId) + 1);
    }
    ppsById_[static_cast<size_t>(ppsId)] = pps;
    pps.valid = true;
    
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
    
    // Find the PPS this slice refers to
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
    
    // Check if this is an IRAP (random access point) picture
    const bool isIrap = (nalUnitType >= 16 && nalUnitType <= 23);
    if (isIrap) {
        int64_t noOutputOfPriorPicsFlag = 0;
        if (!b.u(1, &noOutputOfPriorPicsFlag)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
        ++gopIndex_;
        ResetState();
    }
    
    // Parse pic_order_cnt_lsb (only for pic_order_cnt_type 0)
    if (sps.pocType != 0) {
        // Type 1 or 2 not supported yet
        return Slice::Unknown;
    }
    
    int64_t lsb = 0;
    if (!b.u(sps.log2MaxPocLsb, &lsb)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
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
