#ifndef TESTS_H264_BITS_H_
#define TESTS_H264_BITS_H_

#include <cstdint>
#include <vector>

// Hand-built H.264 syntax for the bitstream tests. Everything the picture-order
// front end and the access-unit grouper read is a handful of Exp-Golomb and
// fixed-length fields, so tests construct them directly instead of depending on
// an encoder (which would decide the slice layout, not the test).
namespace haltest {

// MSB-first writer for the subset of the syntax this project parses.
class BitWriter {
public:
    void u(int n, int64_t v) {
        for (int i = n - 1; i >= 0; --i) {
            bit(static_cast<int>((v >> i) & 1));
        }
    }

    void ue(int64_t v) {
        const int64_t code = v + 1;
        int nbits = 0;
        for (int64_t t = code; t != 0; t >>= 1) {
            ++nbits;
        }
        u(nbits - 1, 0);
        u(nbits, code);
    }

    // 9.10: even codeNum carries the positive half, odd the negative.
    void se(int64_t v) {
        ue(v > 0 ? 2 * v - 1 : -2 * v);
    }

    // rbsp_stop_one_bit, then zero padding to a byte boundary.
    void rbspTrailer() {
        bit(1);
        while (bitPos_ % 8 != 0) {
            bit(0);
        }
    }

    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    void bit(int b) {
        if (bitPos_ % 8 == 0) {
            bytes_.push_back(0);
        }
        if (b) {
            bytes_[bitPos_ / 8] =
                static_cast<uint8_t>(bytes_[bitPos_ / 8] | (0x80 >> (bitPos_ % 8)));
        }
        ++bitPos_;
    }

    std::vector<uint8_t> bytes_;
    size_t bitPos_ = 0;
};

// Inserts the emulation prevention byte a real encoder has to write after
// 00 00 when the next byte is 0, 1, 2 or 3 -- the parser under test steps over
// it again, so tests must produce it.
inline std::vector<uint8_t> EscapeRbsp(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    int zeros = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        if (zeros == 2 && in[i] <= 3) {
            out.push_back(3);
            zeros = 0;
        }
        zeros = in[i] == 0 ? zeros + 1 : 0;
        out.push_back(in[i]);
    }
    return out;
}

// One Annex-B NAL: start code, the forbidden-zero/nal_ref_idc/nal_unit_type
// header byte, then the escaped payload. Tests that need raw NAL units (without
// the start code) can use MakeRawNal instead.
inline std::vector<uint8_t> AnnexBNal(int type, int nalRefIdc,
                                      const std::vector<uint8_t>& rbsp,
                                      int startCodeLen = 4) {
    std::vector<uint8_t> out(startCodeLen, 0);
    out[startCodeLen - 1] = 1;
    out.push_back(static_cast<uint8_t>((nalRefIdc << 5) | type));
    const std::vector<uint8_t> escaped = EscapeRbsp(rbsp);
    out.insert(out.end(), escaped.begin(), escaped.end());
    return out;
}

// Raw NAL unit without the Annex-B start code — just the header byte plus RBSP.
// This is what H264Poc::FeedParameterSet and ClassifySlice expect.
inline std::vector<uint8_t> RawNal(int type, int nalRefIdc,
                                   const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>((nalRefIdc << 5) | type));
    const std::vector<uint8_t> escaped = EscapeRbsp(rbsp);
    out.insert(out.end(), escaped.begin(), escaped.end());
    return out;
}

inline void Append(std::vector<uint8_t>* stream, const std::vector<uint8_t>& nal) {
    stream->insert(stream->end(), nal.begin(), nal.end());
}

struct SpsCfg {
    int spsId = 0;
    int pocType = 0;
    // Stored as the transmitted minus-4 forms so a test can ask for a small
    // pic_order_cnt_lsb range and force the wrap the msb carry has to survive.
    int log2MaxPocLsbMinus4 = 0;
    int log2MaxFrameNumMinus4 = 0;
    int maxNumRefFrames = 4;
    int profileIdc = 66;  // 66 = baseline: no chroma/bit-depth fields to skip
    bool frameMbsOnly = true;
};

inline std::vector<uint8_t> MakeSpsNal(const SpsCfg& cfg) {
    BitWriter w;
    w.u(8, cfg.profileIdc);
    w.u(8, 0);  // constraint_set flags
    w.u(8, 30);  // level_idc
    w.ue(cfg.spsId);
    if (cfg.profileIdc != 66 && cfg.profileIdc != 77 && cfg.profileIdc != 88) {
        w.ue(1);   // chroma_format_idc 4:2:0
        w.ue(0);   // bit_depth_luma_minus8
        w.ue(0);   // bit_depth_chroma_minus8
        w.u(1, 0); // qpprime_y_zero_transform_bypass_flag
        w.u(1, 0); // seq_scaling_matrix_present_flag
    }
    w.ue(cfg.log2MaxFrameNumMinus4);
    w.ue(cfg.pocType);
    if (cfg.pocType == 0) {
        w.ue(cfg.log2MaxPocLsbMinus4);
    } else if (cfg.pocType == 1) {
        w.u(1, 0);    // delta_pic_order_always_zero_flag
        w.se(0);      // offset_for_non_ref_pic
        w.se(0);      // offset_for_top_to_bottom_field
        w.ue(0);      // num_ref_frames_in_pic_order_cnt_cycle
    }
    w.ue(cfg.maxNumRefFrames);
    w.u(1, 0);  // gaps_in_frame_num_value_allowed_flag
    w.ue(19);   // pic_width_in_mbs_minus1 (320 px)
    w.ue(14);   // pic_height_in_map_units_minus1 (240 px)
    w.u(1, cfg.frameMbsOnly ? 1 : 0);
    if (!cfg.frameMbsOnly) {
        w.u(1, 0);  // mb_adaptive_frame_field_flag
    }
    w.rbspTrailer();
    return RawNal(7, 3, w.bytes());
}

struct PpsCfg {
    int ppsId = 0;
    int spsId = 0;
    bool bottomFieldPresent = false;
};

inline std::vector<uint8_t> MakePpsNal(const PpsCfg& cfg) {
    BitWriter w;
    w.ue(cfg.ppsId);
    w.ue(cfg.spsId);
    w.u(1, 0);  // entropy_coding_mode_flag (CAVLC)
    w.u(1, cfg.bottomFieldPresent ? 1 : 0);
    w.rbspTrailer();
    return RawNal(8, 3, w.bytes());
}

struct SliceCfg {
    int type = 1;      // 1 = non-IDR, 5 = IDR
    int nalRefIdc = 2;
    int64_t firstMb = 0;  // nonzero marks a continuation slice of a picture
    int sliceType = 7;    // P
    int ppsId = 0;
    int64_t frameNum = 0;
    int64_t pocLsb = 0;
    int64_t idrPicId = 0;
    int64_t deltaBottom = 0;
};

// The slice header has to be written against the parameters it refers to, which
// is exactly the coupling a real stream has.
inline std::vector<uint8_t> MakeSliceNal(const SliceCfg& slice, const SpsCfg& sps,
                                         const PpsCfg& pps) {
    BitWriter w;
    w.ue(slice.firstMb);
    w.ue(slice.sliceType);
    w.ue(slice.ppsId);
    w.u(sps.log2MaxFrameNumMinus4 + 4, slice.frameNum);
    if (slice.type == 5) {
        w.ue(slice.idrPicId);
    }
    if (sps.pocType == 0) {
        w.u(sps.log2MaxPocLsbMinus4 + 4, slice.pocLsb);
        if (pps.bottomFieldPresent) {
            w.se(slice.deltaBottom);
        }
    }
    w.rbspTrailer();
    return RawNal(slice.type, slice.nalRefIdc, w.bytes());
}

// A NAL the front end should treat as a preamble (SEI 6 / AUD 9) or as filler
// (12) with an arbitrary payload.
inline std::vector<uint8_t> MakeOtherNal(int type, size_t payloadBytes,
                                         int startCodeLen = 4) {
    std::vector<uint8_t> payload(payloadBytes, 0xAA);
    return RawNal(type, 0, payload);
}

// Wraps a raw NAL unit into Annex-B format by prepending a start code. Tests
// that feed ScanNals() need this; H264Poc methods expect raw NALs.
inline std::vector<uint8_t> ToAnnexB(const std::vector<uint8_t>& rawNal,
                                     int startCodeLen = 4) {
    std::vector<uint8_t> out(startCodeLen, 0);
    out[startCodeLen - 1] = 1;
    out.insert(out.end(), rawNal.begin(), rawNal.end());
    return out;
}

// ---------------------------------------------------------------------------
// HEVC helpers — same idea as the H.264 builders above but with the 2-byte
// NAL header (forbidden_zero_bit + nal_unit_type[6] + nuh_layer_id[6] +
// nuh_temporal_id_plus1[3]).
// ---------------------------------------------------------------------------

struct HevcSpsCfg {
    int spsId = 0;
    int log2MaxPocLsbMinus4 = 0;  // stored as minus-4 form for wrap testing
    int maxNumReorderPics = 4;    // default: allow some reordering
};

// HEVC raw NAL (no start code): 2-byte header + escaped RBSP.
inline std::vector<uint8_t> MakeHevcRawNal(int nalUnitType,
                                           const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> out;
    // forbidden_zero_bit(1) + nal_unit_type(6)
    out.push_back(static_cast<uint8_t>((nalUnitType & 0x3F) << 1));
    // nuh_layer_id(6) + nuh_temporal_id_plus1(3), both zero for base layer
    out.push_back(0x01);  // temporal_id_plus1 = 1 means TID = 0
    const std::vector<uint8_t> escaped = EscapeRbsp(rbsp);
    out.insert(out.end(), escaped.begin(), escaped.end());
    return out;
}

// HEVC VPS (type 32) — minimal valid syntax to satisfy VideoToolbox/POC parser.
inline std::vector<uint8_t> MakeHevcVpsNal() {
    BitWriter w;
    w.u(2, 3);  // vps_video_parameter_set_id
    w.u(1, 0);  // vps_base_layer_internal_flag
    w.u(1, 0);  // vps_base_layer_available_flag
    w.u(6, 0);  // vps_max_layers_minus1
    w.u(3, 0);  // vps_max_sub_layers_minus1
    w.u(1, 1);  // vps_timing_info_present_flag
    w.u(32, 0); // vps_num_units_in_tick
    w.u(32, 0); // vps_time_scale
    w.u(1, 0);  // vps_poc_proportional_to_timing_flag
    w.u(1, 0);  // vps_nuh_default_max_one_active_ref_pic_list_sps_flag
    w.ue(0);    // vps_num_layer_sets_minus1
    w.u(1, 0);  // vps_timing_info_not_present_flag
    w.rbspTrailer();
    return MakeHevcRawNal(32, w.bytes());
}

inline std::vector<uint8_t> MakeHevcSpsNal(const HevcSpsCfg& cfg) {
    BitWriter w;
    w.u(4, cfg.spsId);  // sps_video_parameter_set_id
    w.u(3, 0);          // sps_max_sub_layers_minus1
    w.u(1, 1);          // sps_temporal_id_nesting_flag
    
    // profile_tier_level (simplified: skip most fields)
    w.u(2, 1);          // general_profile_space
    w.u(1, 0);          // general_tier_flag
    w.u(5, 1);          // general_profile_idc (Main)
    w.u(32, 0);         // general_profile_compatibility_flags
    w.u(1, 0);          // general_progressive_source_flag
    w.u(1, 0);          // general_interlaced_source_flag
    w.u(1, 0);          // general_non_packed_constraint_flag
    w.u(1, 0);          // general_frame_only_constraint_flag
    w.u(8, 0);          // general_level_idc
    
    w.ue(cfg.spsId);    // sps_seq_parameter_set_id
    w.ue(1);            // chroma_format_idc (4:2:0)
    w.u(1, 0);          // separate_colour_plane_flag
    w.ue(19);           // pic_width_in_luma_samples (320)
    w.ue(14);           // pic_height_in_luma_samples (240)
    
    w.u(1, 0);          // conformance_window_flag
    w.ue(8);            // bit_depth_luma_minus8
    w.ue(8);            // bit_depth_chroma_minus8
    w.ue(cfg.log2MaxPocLsbMinus4);  // log2_max_pic_order_cnt_lsb_minus4
    
    // HEVC doesn't have pic_order_cnt_type like H.264; it's always type 0 equivalent
    // Read sub-layer ordering info
    w.u(1, 0);          // sps_sub_layer_ordering_info_present_flag
    w.ue(0);            // sps_max_dec_pic_buffering_minus1[0]
    w.ue(cfg.maxNumReorderPics);  // sps_max_num_reorder_pics[0]
    w.ue(0);            // sps_max_latency_increase_plus1[0]
    
    w.ue(1);            // log2_min_luma_coding_block_size_minus3
    w.ue(0);            // log2_diff_max_min_luma_coding_block_size
    w.ue(0);            // log2_min_transform_block_size_minus2
    w.ue(0);            // log2_diff_max_min_transform_block_size
    w.ue(0);            // max_transform_hierarchy_depth_inter
    w.ue(0);            // max_transform_hierarchy_depth_intra
    
    w.u(1, 0);          // scaling_list_enabled_flag
    w.u(1, 0);          // amp_enabled_flag
    w.u(1, 0);          // sample_adaptive_offset_enabled_flag
    w.u(1, 0);          // pcm_enabled_flag
    
    w.ue(0);            // num_short_term_ref_pic_sets
    w.ue(0);            // num_long_term_ref_pics_sps
    w.u(1, 0);          // sps_temporal_mvp_enabled_flag
    w.u(1, 0);          // strong_intra_smoothing_enabled_flag
    
    w.rbspTrailer();
    return MakeHevcRawNal(33, w.bytes());
}

struct HevcPpsCfg {
    int ppsId = 0;
    int spsId = 0;
};

inline std::vector<uint8_t> MakeHevcPpsNal(const HevcPpsCfg& cfg) {
    BitWriter w;
    w.ue(cfg.ppsId);    // pps_pic_parameter_set_id
    w.ue(cfg.spsId);    // pps_seq_parameter_set_id
    w.u(1, 0);          // dependent_slice_segments_enabled_flag
    w.u(1, 0);          // output_flag_present_flag
    w.u(3, 0);          // num_extra_slice_header_bits
    w.u(1, 0);          // sign_data_hiding_enabled_flag
    w.u(1, 0);          // cabac_init_present_flag
    w.ue(0);            // num_ref_idx_l0_default_active_minus1
    w.ue(0);            // num_ref_idx_l1_default_active_minus1
    w.se(0);            // init_qp_minus26
    w.u(1, 0);          // constrained_intra_pred_flag
    w.u(1, 0);          // transform_skip_enabled_flag
    w.u(1, 0);          // cu_qp_delta_enabled_flag
    w.ue(0);            // diff_cu_qp_delta_depth
    w.se(0);            // pps_cb_qp_offset
    w.se(0);            // pps_cr_qp_offset
    w.u(1, 0);          // pps_slice_chroma_qp_offsets_present_flag
    w.u(1, 0);          // weighted_pred_flag
    w.u(1, 0);          // weighted_bipred_flag
    w.u(1, 0);          // transquant_bypass_enabled_flag
    w.u(1, 0);          // tiles_enabled_flag
    w.u(1, 0);          // entropy_coding_sync_enabled_flag
    
    w.rbspTrailer();
    return MakeHevcRawNal(34, w.bytes());
}

struct HevcSliceCfg {
    bool firstSlice = true;
    int64_t pocLsb = 0;
    int nalRefIdc = 1;
};

inline std::vector<uint8_t> MakeHevcSliceNal(int nalUnitType,
                                             const HevcSliceCfg& cfg) {
    BitWriter w;
    w.u(1, cfg.firstSlice ? 1 : 0);  // first_slice_segment_in_pic_flag
    if (nalUnitType >= 16 && nalUnitType <= 23) {
        w.u(1, 0);  // no_output_of_prior_pics_flag (IRAP only)
    }
    w.ue(0);        // slice_pic_parameter_set_id
    w.u(1, 0);      // dependent_slice_segment_flag
    
    if (!cfg.firstSlice) {
        w.ue(0);    // slice_segment_address
    }
    
    // Skip slice_type, prediction weights, etc. — not needed for POC test
    // Just write enough to make it parseable
    
    if (cfg.firstSlice) {
        // For POC type 0, we need pic_order_cnt_lsb
        w.u(4, static_cast<int>(cfg.pocLsb & 0xF));  // simplified: use small range
    }
    
    w.rbspTrailer();
    return MakeHevcRawNal(nalUnitType, w.bytes());
}

} // namespace haltest

#endif // TESTS_H264_BITS_H_
