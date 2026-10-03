#include "av1desc.h"

#include <algorithm>
#include <cstring>

namespace halcodec {
namespace vtbox {

namespace {

// MSB-first bit reader over an OBU payload (same shape as HEVCPoc's Bits).
// A null output pointer means the field only had to be walked past.
class Bits {
public:
    Bits(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool u(int count, uint32_t* out) {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) {
            int bit = 0;
            if (!Read(&bit)) {
                return false;
            }
            v = (v << 1) | static_cast<uint32_t>(bit);
        }
        if (out) {
            *out = v;
        }
        return true;
    }

    bool flag(bool* out) {
        int bit = 0;
        if (!Read(&bit)) {
            return false;
        }
        if (out) {
            *out = bit != 0;
        }
        return true;
    }

    // Unsigned exponential Golomb, the coding sequence_header() uses for
    // num_ticks_per_picture_minus_1.
    bool vlc(uint32_t* out) {
        int zeros = 0;
        int bit = 0;
        for (;;) {
            if (!Read(&bit)) {
                return false;
            }
            if (bit) {
                break;
            }
            if (++zeros > 31) {
                return false;
            }
        }
        uint32_t tail = 0;
        if (zeros > 0 && !u(zeros, &tail)) {
            return false;
        }
        if (out) {
            *out = (static_cast<uint32_t>(1) << zeros) - 1 + tail;
        }
        return true;
    }

    size_t bitsLeft() const { return size_ * 8 - pos_; }

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

// Locates an OBU's payload. Returns the offset of the first payload byte, or 0
// when the header is not a readable AV1 OBU. `payloadSize` is clamped to the
// buffer so a caller working through a chunk can still parse a final OBU whose
// size field was written but whose body has not arrived. `hasSizeField` reports
// whether the OBU carries one, which is the difference between an extent the
// stream stated and an extent the buffer merely happens to end at.
size_t ObuPayload(const uint8_t* data, size_t size, int* type,
                  size_t* payloadSize, bool* hasSizeField) {
    *type = -1;
    *payloadSize = 0;
    *hasSizeField = false;
    if (size < 2 || (data[0] & 0x80)) {
        return 0;
    }
    *type = (data[0] >> 3) & 0x0F;
    size_t pos = 1 + (((data[0] >> 2) & 1) ? 1 : 0);
    if ((data[0] >> 1) & 1) {
        *hasSizeField = true;
        uint64_t v = 0;
        int shift = 0;
        bool closed = false;
        for (; pos < size; shift += 7) {
            const uint8_t b = data[pos++];
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) {
                closed = true;
                break;
            }
            if (shift >= 56) {
                break;
            }
        }
        if (!closed || pos + v > size) {
            *type = -1;
            return 0;
        }
        *payloadSize = static_cast<size_t>(v);
        return pos;
    }
    // No size field: the OBU ends with its temporal unit.
    *payloadSize = size - pos;
    return pos;
}

void PushBE(std::vector<uint8_t>* out, uint32_t value, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) {
        out->push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void AppendAtom(std::vector<uint8_t>* out, const char* type,
    const std::vector<uint8_t>& payload) {
    PushBE(out, static_cast<uint32_t>(payload.size() + 8), 4);
    out->insert(out->end(), type, type + 4);
    out->insert(out->end(), payload.begin(), payload.end());
}

} // namespace

bool IsAv1ObuStream(const uint8_t* data, size_t size) {
    // Every AV1 temporal unit opens with a delimiter, and an elementary stream
    // opens with its first unit, so one header byte identifies the codec: under
    // either NAL reading a delimiter (0x12) is never a parameter set.
    if (!data || size < 1 || (data[0] & 0x80)) {
        return false;
    }
    return ((data[0] >> 3) & 0x0F) == kAv1ObuTemporalDelimiter;
}

std::vector<Av1Obu> ScanObus(const uint8_t* data, size_t size, size_t maxObus) {
    std::vector<Av1Obu> obus;
    if (!data) {
        return obus;
    }
    size_t pos = 0;
    while (pos < size && obus.size() < maxObus) {
        int type = -1;
        size_t payloadSize = 0;
        bool hasSizeField = false;
        const size_t payload = ObuPayload(data + pos, size - pos, &type,
                                         &payloadSize, &hasSizeField);
        if (type < 0) {
            break;
        }
        Av1Obu obu;
        obu.type = type;
        obu.start = pos;
        // An OBU without a size field states no extent of its own: it ends with
        // its temporal unit, which the scan cannot see yet.
        obu.runsToEnd = !hasSizeField;
        obu.size = payload + payloadSize;
        if (obu.size == 0) {
            break;
        }
        obus.push_back(obu);
        pos += obu.size;
    }
    return obus;
}

std::vector<Av1TemporalUnit> GroupAv1TemporalUnits(const uint8_t* data,
    size_t size, size_t maxUnits, size_t maxObus, bool closeOpen,
    bool* scanTruncated) {
    std::vector<Av1TemporalUnit> units;
    if (scanTruncated) {
        *scanTruncated = false;
    }
    if (!data || size == 0) {
        return units;
    }
    const std::vector<Av1Obu> obus = ScanObus(data, size, maxObus);
    if (scanTruncated) {
        *scanTruncated = obus.size() == maxObus;
    }
    // A delimiter opens a temporal unit, so the unit before it ends where this
    // one starts. The delimiter belongs to the unit it opens, which is what a
    // container stores in a sample and what VideoToolbox expects back.
    size_t openUnit = 0;
    bool open = false;
    for (size_t i = 0; i < obus.size(); ++i) {
        if (obus[i].type != kAv1ObuTemporalDelimiter) {
            continue;
        }
        if (open) {
            units.push_back({obus[openUnit].start, obus[i].start});
            if (units.size() == maxUnits) {
                return units;
            }
        }
        openUnit = i;
        open = true;
    }
    // The last unit of a stream has no delimiter after it, so only a caller that
    // knows no further input is coming may close it.
    if (open && closeOpen) {
        units.push_back({obus[openUnit].start, size});
    }
    return units;
}

bool ParseAv1SequenceHeader(const uint8_t* obu, size_t size,
                            Av1SequenceHeader* out) {
    if (!obu || !out) {
        return false;
    }
    *out = Av1SequenceHeader();
    int type = -1;
    size_t payloadSize = 0;
    bool hasSizeField = false;
    const size_t payload = ObuPayload(obu, size, &type, &payloadSize,
                                      &hasSizeField);
    if (type != kAv1ObuSequenceHeader || payload == 0) {
        return false;
    }
    Bits bits(obu + payload, payloadSize);
    uint32_t v = 0;

    if (!bits.u(3, &v)) {
        return false;
    }
    out->profile = static_cast<int>(v);
    // Profile 0 only: for 1 and 2 the bit depth and chroma signalling below are
    // not encodings any sample available here can put in front of VideoToolbox.
    if (out->profile != 0) {
        out->unsupported = "only AV1 main profile (4:2:0) is supported";
        return true;
    }
    bool stillPicture = false;
    bool reduced = false;
    if (!bits.flag(&stillPicture) || !bits.flag(&reduced)) {
        return false;
    }

    bool decoderModelInfo = false;
    if (!reduced) {
        bool timing = false;
        if (!bits.flag(&timing)) {
            return false;
        }
        if (timing) {
            bool equalPictureInterval = false;
            if (!bits.u(32, &v) || !bits.u(32, &v) ||
                !bits.flag(&equalPictureInterval)) {
                return false;
            }
            // num_ticks_per_picture_minus_1 is Golomb-coded, so its width has to
            // be read rather than skipped.
            if (equalPictureInterval && !bits.vlc(&v)) {
                return false;
            }
            if (!bits.flag(&decoderModelInfo)) {
                return false;
            }
            if (decoderModelInfo) {
                // HRD signalling: the per-operating-point fields that follow have
                // widths that could not be pinned down against a decoder here, so
                // the walk stops instead of guessing at the rest of the header.
                out->unsupported = "AV1 decoder model info is not supported";
                return true;
            }
        }
        bool initialDisplayDelay = false;
        uint32_t opCount = 0;
        if (!bits.flag(&initialDisplayDelay) || !bits.u(5, &opCount)) {
            return false;
        }
        for (uint32_t i = 0; i <= opCount; ++i) {
            uint32_t level = 0;
            if (!bits.u(12, &v) || !bits.u(5, &level)) {
                return false;
            }
            if (level > 7 && !bits.flag(nullptr /* seq_tier */)) {
                return false;
            }
            bool delayForOp = false;
            if (initialDisplayDelay &&
                (!bits.flag(&delayForOp) ||
                 (delayForOp && !bits.u(4, nullptr)))) {
                return false;
            }
            if (i == 0) {
                out->levelIdx = static_cast<int>(level);
            }
        }
    } else if (!bits.u(5, &v)) {
        return false;
    }
    if (reduced) {
        out->levelIdx = static_cast<int>(v);
    }

    uint32_t widthBits = 0;
    uint32_t heightBits = 0;
    if (!bits.u(4, &widthBits) || !bits.u(4, &heightBits) ||
        !bits.u(static_cast<int>(widthBits) + 1, &v)) {
        return false;
    }
    out->width = static_cast<int>(v) + 1;
    if (!bits.u(static_cast<int>(heightBits) + 1, &v)) {
        return false;
    }
    out->height = static_cast<int>(v) + 1;

    if (!reduced) {
        bool frameIdNumbers = false;
        if (!bits.flag(&frameIdNumbers)) {
            return false;
        }
        // delta_frame_id_length_minus_2 plus additional_frame_id_length_minus_1
        // is 4+3 bits, not the 7+3 the names might suggest.
        if (frameIdNumbers && !bits.u(7, nullptr)) {
            return false;
        }
    }
    if (!bits.flag(nullptr /* use_128x128_superblock */) ||
        !bits.flag(nullptr /* enable_filter_intra */) ||
        !bits.flag(nullptr /* enable_intra_edge_filter */)) {
        return false;
    }
    if (!reduced) {
        if (!bits.flag(nullptr /* enable_interintra_compound */) ||
            !bits.flag(nullptr /* enable_masked_compound */) ||
            !bits.flag(nullptr /* enable_warped_motion */) ||
            !bits.flag(nullptr /* enable_dual_filter */)) {
            return false;
        }
        bool orderHint = false;
        if (!bits.flag(&orderHint)) {
            return false;
        }
        if (orderHint &&
            (!bits.flag(nullptr /* enable_jnt_comp */) ||
             !bits.flag(nullptr /* enable_ref_frame_mvs */))) {
            return false;
        }
        // The screen-content group is two bits whatever enable_order_hint says:
        // seq_choose_screen_content_tools and then either seq_choose_integer_mv
        // or the seq_force_* it selects. Walking it as anything narrower shifts
        // every field after it, which the trailing bits below would catch.
        bool chooseScreenContent = false;
        if (!bits.flag(&chooseScreenContent) ||
            !bits.flag(nullptr /* seq_choose_integer_mv or
                                 seq_force_screen_content_tools */)) {
            return false;
        }
        if (orderHint && !bits.u(3, nullptr /* order_hint_bits_minus_1 */)) {
            return false;
        }
    }
    if (!bits.flag(nullptr /* enable_superres */) ||
        !bits.flag(nullptr /* enable_cdef */) ||
        !bits.flag(nullptr /* enable_restoration */)) {
        return false;
    }
    bool highBitdepth = false;
    if (!bits.flag(&highBitdepth)) {
        return false;
    }
    if (!bits.flag(&out->monoChrome)) {
        return false;
    }
    // Even a grayscale sequence carries the colour description and range bits;
    // only the subsampling and uv-delta signalling are dropped.
    bool colorDescription = false;
    if (!bits.flag(&colorDescription)) {
        return false;
    }
    if (colorDescription && !bits.u(24, nullptr)) {
        return false;
    }
    if (!bits.flag(nullptr /* color_range */)) {
        return false;
    }
    if (!out->monoChrome) {
        if (!bits.u(2, &v)) {
            return false;
        }
        out->chromaSamplePosition = static_cast<int>(v);
        if (!bits.flag(nullptr /* separate_uv_delta_q */)) {
            return false;
        }
    }
    if (!bits.flag(nullptr /* film_grain_params_present */)) {
        return false;
    }
    // sequence_header() closes with a 1 bit and zero padding. Requiring the
    // padding to be empty is what turns a bit walked twice or skipped into a
    // refusal here, rather than a sample entry VideoToolbox rejects later.
    uint32_t trailing = 0;
    if (!bits.u(1, &trailing) || trailing != 1) {
        return false;
    }
    while (bits.bitsLeft() > 0) {
        if (!bits.u(1, &trailing) || trailing != 0) {
            return false;
        }
    }

    out->bitDepth = highBitdepth ? 10 : 8;
    out->chromaSubX = 1;
    out->chromaSubY = 1;
    if (out->monoChrome) {
        out->unsupported = "grayscale AV1 is not supported";
    } else if (reduced) {
        out->unsupported = "AV1 reduced still picture is not supported";
    }
    return true;
}

std::vector<uint8_t> BuildAv01SampleEntry(const Av1SequenceHeader& header,
    const uint8_t* configObus, size_t configObusSize) {
    // VisualSampleEntry (ISO/IEC 14496-12) followed by the av1C box. Two fields
    // are load-bearing for CoreMedia: compressorname is a Pascal string (length
    // byte plus 31 bytes), and the two bytes after `depth` must be 0xFFFF --
    // zeroed, the description factory refuses the entry with -12714.
    std::vector<uint8_t> body;
    body.insert(body.end(), 6, 0);   // reserved
    PushBE(&body, 1, 2);             // data_reference_index
    body.insert(body.end(), 16, 0);  // pre_defined, reserved, pre_defined[3]
    PushBE(&body, static_cast<uint32_t>(header.width), 2);
    PushBE(&body, static_cast<uint32_t>(header.height), 2);
    PushBE(&body, 0x00480000, 4);    // horizresolution, 72 dpi
    PushBE(&body, 0x00480000, 4);    // vertresolution
    PushBE(&body, 0, 4);             // reserved
    PushBE(&body, 1, 2);             // frame_count
    static const char kName[] = "HAL av1";
    const size_t nameLen = sizeof(kName) - 1;
    body.push_back(static_cast<uint8_t>(nameLen));
    body.insert(body.end(), kName, kName + nameLen);
    body.insert(body.end(), 31 - nameLen, 0);
    PushBE(&body, 0x0018, 2);        // depth
    PushBE(&body, 0xFFFF, 2);

    // AV1CodecConfigurationRecord. VideoToolbox checks the profile, the marker
    // nibble, the bit depth, the mono flag and both chroma subsampling bits
    // against the stream; the level is only range-checked (0..31, i.e. the 5-bit
    // seq_level_idx field with the three bits above it reserved).
    std::vector<uint8_t> record;
    record.push_back(static_cast<uint8_t>(0x80 |
        ((header.profile & 0x07) << 4) | 0x01));
    record.push_back(static_cast<uint8_t>(header.levelIdx & 0x1F));
    record.push_back(static_cast<uint8_t>(
        ((header.bitDepth == 10 ? 1 : 0) << 6) |
        ((header.monoChrome ? 1 : 0) << 4) |
        ((header.chromaSubX ? 1 : 0) << 3) |
        ((header.chromaSubY ? 1 : 0) << 2) |
        (header.chromaSamplePosition & 0x03)));
    record.push_back(0);
    if (configObus && configObusSize) {
        record.insert(record.end(), configObus, configObus + configObusSize);
    }
    AppendAtom(&body, "av1C", record);

    std::vector<uint8_t> entry;
    AppendAtom(&entry, "av01", body);
    return entry;
}

} // namespace vtbox
} // namespace halcodec
