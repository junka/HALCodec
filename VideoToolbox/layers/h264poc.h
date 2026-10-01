#ifndef LAYERS_H264POC_H_
#define LAYERS_H264POC_H_

#include <cstdint>
#include <cstddef>
#include <vector>

namespace halcodec {
namespace vtbox {

// H.264 picture order count front end (ITU-T H.264 8.2.1).
//
// A raw Annex-B elementary stream carries no container timing, and
// VideoToolbox does not report a display order for it either (the decoded
// pixel buffers carry nothing but colour/field attachments), so a decoder
// that wants presentation order has to derive it from the bitstream itself.
// This class consumes SPS/PPS NALs to learn the picture-order parameters and
// then turns each VCL NAL into its PicOrderCnt value, which orders pictures
// for display within an IDR GOP.
//
// Supports pic_order_cnt_type 0 (the common case) and 2. Type 1 needs the
// PPS reference-cycle offset table and is reported unsupported: the caller
// then keeps emitting decode order rather than guessing.
class H264Poc {
public:
    // Feeds a non-VCL NAL payload (including its 1-byte NAL header). Types 7
    // (SPS) and 8 (PPS) are parsed; anything else is ignored.
    void FeedParameterSet(const uint8_t* nalu, size_t len);

    // Result of looking at one VCL NAL.
    enum class Slice {
        NewPicture,   // key holds the display order
        Continuation, // another slice of the picture just seen: same key
        Unknown,      // no parsable parameters (or a corrupt slice header)
    };

    // Returns the picture's display-order key in `key`: PicOrderCnt plus a
    // per-GOP bias, so keys are monotonic across the whole stream (a raw POC
    // restarts at every IDR and could not be compared across GOPs).
    Slice ClassifySlice(const uint8_t* nalu, size_t len, int64_t* key);

    // True once a parsable SPS/PPS pair has been fed and the POC scheme is one
    // this front end implements.
    bool usable() const { return usable_; }

    // How many pictures the decoder may have to hold back before the display
    // order is settled. Bounded by the decoded picture buffer, which the SPS
    // sizes with max_num_ref_frames; the stream's own num_reorder_frames (VUI)
    // is never larger than that.
    int reorderDelay() const { return reorderDelay_; }

    // Resets the per-GOP state (called on an IDR by ClassifySlice itself, and
    // by the caller when it wants a clean break).
    void Reset();

private:
    struct Sps {
        bool valid = false;
        int log2MaxFrameNum = 0;
        int pocType = -1;
        int log2MaxPocLsb = 0;
        bool frameMbsOnly = true;
        bool separateColourPlane = false;
        int maxNumRefFrames = 0;
    };
    struct Pps {
        bool valid = false;
        int spsId = 0;
        bool bottomFieldPresent = false;
    };

    void ParseSps(const uint8_t* rbsp, size_t len);
    void ParsePps(const uint8_t* rbsp, size_t len);
    void RefreshUsable();
    void ResetState();

    // Plenty above the largest POC a conforming stream can reach (bounded by
    // 2*MaxFrameNum), far below the point where the GOP counter overflows.
    static constexpr int64_t kGopIndexShift = 32;

    std::vector<Sps> spsById_;
    std::vector<Pps> ppsById_;

    // 8.2.1.1 state: the last reference picture's order-count parts.
    bool havePrev_ = false;
    int64_t prevPocMsb_ = 0;
    int64_t prevPocLsb_ = 0;
    // 8.2.1.3 state for pic_order_cnt_type 2.
    int64_t prevFrameNumMsb_ = 0;
    int64_t prevFrameNum_ = 0;
    int64_t lastPictureKey_ = 0;
    int64_t gopIndex_ = 0;

    bool usable_ = false;
    bool gaveUp_ = false;
    int reorderDelay_ = 0;
};

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_H264POC_H_
