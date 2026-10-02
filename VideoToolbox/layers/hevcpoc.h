#ifndef LAYERS_HEVCPOC_H_
#define LAYERS_HEVCPOC_H_

#include <cstdint>
#include <cstddef>
#include <vector>

namespace halcodec {
namespace vtbox {

// HEVC picture order count front end (ITU-T H.265 / ISO/IEC 23008-2 8.3.1).
//
// Mirrors H264Poc but handles the HEVC parameter-set hierarchy (VPS/SPS/PPS)
// and its slice-segment header syntax. The POC derivation algorithm is
// structurally similar to H.264 type 0 (PicOrderCntVal = PicOrderCntMsb +
// pic_order_cnt_lsb) with different bitstream layout.
//
// Supports the Main/Main10 profile layout that camera and screen encoders emit,
// including temporally scalable streams (sps_max_sub_layers_minus1 > 0) and the
// Rext/MRange family (profile_idc 4-7: 4:2:2 / 4:4:4 / high-bit-depth), whose
// 44-bit constraint tail this front end skips so the picture-order fields are
// still reached. The remaining high-tier profiles (SEG/SVC/MVC/SC, idc 8-12),
// whose profile_tier_level layout differs and is unverified here, are reported
// unsupported, so the caller keeps emitting decode order rather than guessing.
class HEVCPoc {
public:
    // Feeds a non-VCL NAL payload including its 2-byte HEVC NAL header.
    // HEVC NAL types: VPS=32, SPS=33, PPS=34.
    void FeedParameterSet(const uint8_t* nalu, size_t len);

    // Result of looking at one VCL NAL.
    enum class Slice {
        NewPicture,   // key holds the display order
        Continuation, // another slice segment of the same picture: same key
        Unknown,      // no parsable parameters (or a corrupt slice header)
    };

    // Returns the picture's display-order key in `key`: PicOrderCntVal plus a
    // per-GOP bias, so keys are monotonic across the whole stream.
    Slice ClassifySlice(const uint8_t* nalu, size_t len, int64_t* key);

    // True once a parsable VPS/SPS/PPS triplet has been fed and the POC scheme
    // is one this front end implements.
    bool usable() const { return usable_; }

    // How many pictures the decoder may have to hold back before the display
    // order is settled. Bounded by sps_max_num_reorder_pics[0] or
    // MaxDpbSize, whichever is larger.
    int reorderDelay() const { return reorderDelay_; }

    // Resets the per-GOP state (called on an IDR/CRA by ClassifySlice itself).
    void Reset();

private:
    struct Vps {
        bool valid = false;
    };
    struct Sps {
        bool valid = false;
        int log2MaxPocLsb = 4;  // MaxPicOrderCntLsb = 1 << log2MaxPocLsb
        int maxNumReorderPics = 0;
        int maxDpbSize = 16;
    };
    struct Pps {
        bool valid = false;
        int spsId = 0;
    };

    void ParseVps(const uint8_t* rbsp, size_t len);
    void ParseSps(const uint8_t* rbsp, size_t len);
    void ParsePps(const uint8_t* rbsp, size_t len);
    void RefreshUsable();
    void ResetState();

    static constexpr int64_t kGopIndexShift = 32;

    std::vector<Vps> vpsById_;
    std::vector<Sps> spsById_;
    std::vector<Pps> ppsById_;

    // 8.3.1 state for pic_order_cnt_type 0.
    bool havePrev_ = false;
    int64_t prevPocMsb_ = 0;
    int64_t prevPocLsb_ = 0;
    int64_t lastPictureKey_ = 0;
    int64_t gopIndex_ = 0;

    bool usable_ = false;
    bool gaveUp_ = false;
    int reorderDelay_ = 0;
};

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_HEVCPOC_H_
