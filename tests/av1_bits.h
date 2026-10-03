#ifndef TESTS_AV1_BITS_H_
#define TESTS_AV1_BITS_H_

#include <cstdint>
#include <vector>

#include "h264_bits.h"

// Hand-built AV1 syntax for the ingest tests, in the shape the local ffmpeg
// (cbs_av1) and dav1d were shown to parse: every branch here was fed through
// `ffmpeg -bsf:v trace_headers` and the reported field bit positions compared
// against this writer's layout. Two of those measurements contradict how the
// field names read -- the screen-content group is two bits whether or not an
// order hint is enabled, and the three superblock/filter flags are not guarded
// by seq_reduced_still_picture_header -- so they are written as measured here.
namespace haltest {

// OBU header byte: forbidden_zero_bit(1) | obu_type(4) | obu_extension_flag(1) |
// obu_has_size_field(1) | reserved(1).
inline std::vector<uint8_t> MakeAv1Obu(int type, const std::vector<uint8_t>& payload,
                                  bool hasSizeField = true) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>((type & 0x0F) << 3) |
                  (hasSizeField ? 0x02 : 0x00));
    if (hasSizeField) {
        size_t n = payload.size();
        for (;;) {
            const uint8_t b = static_cast<uint8_t>(n & 0x7F);
            n >>= 7;
            out.push_back(n ? static_cast<uint8_t>(b | 0x80) : b);
            if (!n) {
                break;
            }
        }
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

inline std::vector<uint8_t> Av1TemporalDelimiter() {
    return MakeAv1Obu(2, std::vector<uint8_t>());
}

struct Av1ShCfg {
    int profile = 0;
    bool stillPicture = false;
    bool reduced = false;
    bool timing = false;
    bool equalPictureInterval = false;
    uint32_t numTicksPerPictureMinus1 = 4;
    bool decoderModelInfo = false;
    bool initialDisplayDelay = false;
    bool iddForOp = false;
    int iddMinus1 = 3;
    int opCountMinus1 = 0;
    int level = 0;
    bool tier = false;  // only signalled when level > 7
    unsigned widthBitsMinus1 = 8;
    unsigned heightBitsMinus1 = 7;
    int widthMinus1 = 319;   // 320 px
    int heightMinus1 = 239;  // 240 px
    bool frameIdNumbers = false;
    bool superblock128 = false;
    bool filterIntra = false;
    bool intraEdgeFilter = true;
    bool orderHint = true;
    // seq_choose_screen_content_tools, then the one bit it selects between
    // seq_choose_integer_mv and seq_force_screen_content_tools.
    bool chooseScreenContent = true;
    bool secondScreenContentBit = true;
    int orderHintBitsMinus1 = 6;
    bool superres = false;
    bool cdef = true;
    bool restoration = true;
    bool highBitdepth = false;
    bool monoChrome = false;
    bool colorDescription = false;
    bool colorRange = false;
    int chromaSamplePosition = 0;
    bool separateUvDeltaQ = false;
    bool filmGrain = false;
    // Set to 0 to write a header whose syntax does not close, which the parser
    // under test must refuse instead of mis-reading.
    int trailingOneBit = 1;
};

inline std::vector<uint8_t> MakeAv1SequenceHeaderPayload(const Av1ShCfg& cfg) {
    BitWriter w;
    w.u(3, cfg.profile);
    w.u(1, cfg.stillPicture ? 1 : 0);
    w.u(1, cfg.reduced ? 1 : 0);
    if (cfg.reduced) {
        w.u(5, cfg.level);
    } else {
        w.u(1, cfg.timing ? 1 : 0);
        if (cfg.timing) {
            w.u(32, 1);   // num_units_in_display_tick
            w.u(32, 30);  // time_scale
            w.u(1, cfg.equalPictureInterval ? 1 : 0);
            if (cfg.equalPictureInterval) {
                w.ue(cfg.numTicksPerPictureMinus1);
            }
            w.u(1, cfg.decoderModelInfo ? 1 : 0);
            // The decoder-model fields themselves are not walked: no case in
            // this build claims to decode them, and the parser refuses them.
        }
        w.u(1, cfg.initialDisplayDelay ? 1 : 0);
        w.u(5, cfg.opCountMinus1);
        for (int i = 0; i <= cfg.opCountMinus1; ++i) {
            w.u(12, 0);  // operating_point_idc[i]
            w.u(5, cfg.level);
            if (cfg.level > 7) {
                w.u(1, cfg.tier ? 1 : 0);
            }
            if (cfg.initialDisplayDelay) {
                w.u(1, cfg.iddForOp ? 1 : 0);
                if (cfg.iddForOp) {
                    w.u(4, cfg.iddMinus1);
                }
            }
        }
    }
    w.u(4, cfg.widthBitsMinus1);
    w.u(4, cfg.heightBitsMinus1);
    w.u(static_cast<int>(cfg.widthBitsMinus1) + 1, cfg.widthMinus1);
    w.u(static_cast<int>(cfg.heightBitsMinus1) + 1, cfg.heightMinus1);
    if (!cfg.reduced) {
        w.u(1, cfg.frameIdNumbers ? 1 : 0);
        if (cfg.frameIdNumbers) {
            w.u(4, 5);  // delta_frame_id_length_minus_2
            w.u(3, 2);  // additional_frame_id_length_minus_1
        }
    }
    w.u(1, cfg.superblock128 ? 1 : 0);
    w.u(1, cfg.filterIntra ? 1 : 0);
    w.u(1, cfg.intraEdgeFilter ? 1 : 0);
    if (!cfg.reduced) {
        w.u(1, 1);    // enable_interintra_compound
        w.u(1, 0);    // enable_masked_compound
        w.u(1, 1);    // enable_warped_motion
        w.u(1, 0);    // enable_dual_filter
        w.u(1, cfg.orderHint ? 1 : 0);
        if (cfg.orderHint) {
            w.u(1, 0);  // enable_jnt_comp
            w.u(1, 1);  // enable_ref_frame_mvs
        }
        w.u(1, cfg.chooseScreenContent ? 1 : 0);
        w.u(1, cfg.secondScreenContentBit ? 1 : 0);
        if (cfg.orderHint) {
            w.u(3, cfg.orderHintBitsMinus1);
        }
    }
    w.u(1, cfg.superres ? 1 : 0);
    w.u(1, cfg.cdef ? 1 : 0);
    w.u(1, cfg.restoration ? 1 : 0);
    if (cfg.profile != 2 && cfg.profile != 3) {
        w.u(1, cfg.highBitdepth ? 1 : 0);
    }
    w.u(1, cfg.monoChrome ? 1 : 0);
    w.u(1, cfg.colorDescription ? 1 : 0);
    if (cfg.colorDescription) {
        w.u(8, 1);  // colour_primaries
        w.u(8, 1);  // transfer_characteristics
        w.u(8, 1);  // matrix_coefficients
    }
    w.u(1, cfg.colorRange ? 1 : 0);
    if (!cfg.monoChrome) {
        if (cfg.profile == 0) {
            w.u(2, cfg.chromaSamplePosition);
        }
        w.u(1, cfg.separateUvDeltaQ ? 1 : 0);
    }
    w.u(1, cfg.filmGrain ? 1 : 0);
    w.u(1, cfg.trailingOneBit);  // trailing_one_bit
    while (w.bitPos() % 8 != 0) {
        w.u(1, 0);               // trailing_zero_bit
    }
    return w.bytes();
}

inline std::vector<uint8_t> MakeAv1SequenceHeaderObu(const Av1ShCfg& cfg = Av1ShCfg(),
                                                    bool hasSizeField = true) {
    return MakeAv1Obu(1, MakeAv1SequenceHeaderPayload(cfg), hasSizeField);
}

// One temporal unit: a delimiter followed by `obus`, which is what a container
// stores as a sample.
inline std::vector<uint8_t> Av1Unit(const std::vector<std::vector<uint8_t>>& obus) {
    std::vector<uint8_t> out = Av1TemporalDelimiter();
    for (const auto& o : obus) {
        out.insert(out.end(), o.begin(), o.end());
    }
    return out;
}

} // namespace haltest

#endif // TESTS_AV1_BITS_H_
