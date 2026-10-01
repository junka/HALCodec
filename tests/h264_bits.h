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

} // namespace haltest

#endif // TESTS_H264_BITS_H_
