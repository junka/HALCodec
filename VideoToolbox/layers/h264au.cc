#include "h264au.h"

#include <algorithm>
#include <cstdint>

namespace halcodec {
namespace vtbox {

// Annex-B NAL scanner. Every NAL except the last is complete (delimited by the
// following start code); the final NAL may continue inside the next feed chunk
// and is therefore held pending until its end is seen. `maxNals` bounds the scan
// so a caller that only wants a few access units never walks the whole pending
// buffer: NALs are only cut off *between* units, so a truncated scan simply
// lacks the NAL that would close the last access unit.
std::vector<Nal> ScanNals(const uint8_t* data, size_t size, size_t maxNals) {
    std::vector<Nal> nals;
    size_t i = 0;
    while (i + 3 <= size && nals.size() < maxNals) {
        if (data[i] != 0 || data[i + 1] != 0) {
            ++i;
            continue;
        }
        size_t sc = 3;
        if (data[i + 2] == 1) {
            sc = 3;
        } else if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
            sc = 4;
        } else {
            ++i;
            continue;
        }
        const uint8_t* payload = data + i + sc;
        size_t j = i + sc;
        // A 3-byte start code can begin at size-3, so the bound is inclusive.
        // When no next start code is found the NAL runs to the end of the
        // buffer: stopping the scan loop at its bound instead (the old
        // `j + 3 < size` form) cut the last NAL of the stream short, and
        // VideoToolbox rejected the truncated AU with
        // kVTVideoDecoderBadDataErr (-12909).
        bool nextStartFound = false;
        while (j + 3 <= size) {
            if (data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 ||
                 (j + 4 <= size && data[j + 2] == 0 && data[j + 3] == 1))) {
                nextStartFound = true;
                break;
            }
            ++j;
        }
        if (!nextStartFound) {
            j = size;
        }
        const size_t len = j - (i + sc);
        nals.push_back(
            {i, payload, len,
             static_cast<uint8_t>(len > 0 ? payload[0] & 0x1F : 0)});
        i = j;
    }
    return nals;
}

// Builds one AVCC access unit ([len:4][nalu]...) from nals[begin, end).
std::vector<uint8_t> BuildAvcc(const std::vector<Nal>& nals, size_t begin,
                               size_t end) {
    size_t total = 0;
    for (size_t k = begin; k < end; ++k) {
        total += nals[k].len + 4;
    }
    std::vector<uint8_t> au;
    au.reserve(total);
    for (size_t k = begin; k < end; ++k) {
        const uint32_t len = static_cast<uint32_t>(nals[k].len);
        au.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        au.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        au.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        au.push_back(static_cast<uint8_t>(len & 0xFF));
        au.insert(au.end(), nals[k].payload, nals[k].payload + nals[k].len);
    }
    return au;
}

// Groups NALs into access units and reports their ranges, walking them the way
// a decoder's picture state machine would: parameter sets are fed and every VCL
// NAL classified, exactly once and in stream order.
//
// The boundary rule matches FFmpeg's h264_mp4toannexb (a preamble of
// SEI/SPS/PPS/AUD after a picture starts a new unit) with the one thing that
// file-oriented rule leaves out: a picture can be split into several slices, and
// every slice after the first reports first_mb_in_slice != 0. H264Poc surfaces
// that as a continuation, and those slices stay in the unit they belong to --
// handing VideoToolbox a lone secondary slice yields no picture at all.
//
// Only nals[0, limitNals) is looked at, because state a caller has not consumed
// must not be advanced. The unit still open at the limit is returned only when
// `closeOpen` (the input ended, so no further slice of that picture can exist);
// otherwise its bytes are held for the next feed.
std::vector<Au> GroupAUs(H264Poc& poc, const std::vector<Nal>& nals,
                         size_t limitNals, bool closeOpen) {
    std::vector<Au> aus;
    size_t auBegin = 0;
    int64_t auKey = -1;
    bool hasVcl = false;
    const size_t limit = std::min(limitNals, nals.size());
    for (size_t idx = 0; idx < limit; ++idx) {
        const Nal& nal = nals[idx];
        const bool vcl = nal.type >= 1 && nal.type <= 5;
        int64_t key = -1;
        bool continuation = false;
        bool preamble = false;
        if (vcl) {
            continuation = poc.ClassifySlice(nal.payload, nal.len, &key) ==
                           H264Poc::Slice::Continuation;
        } else {
            poc.FeedParameterSet(nal.payload, nal.len);
            preamble = nal.type >= 6 && nal.type <= 9;
        }
        // Where the current picture ends: any NAL that starts a new one.
        if (hasVcl && (vcl ? !continuation : preamble)) {
            aus.push_back({auBegin, idx, auKey});
            auBegin = idx;
            auKey = -1;
            hasVcl = false;
        }
        if (vcl) {
            // The unit's primary slice carries its display order; a lone
            // continuation (a picture whose start was already submitted, which a
            // well-formed stream never lets happen) keeps the key it reports so
            // it still lands beside its sibling frames.
            if (!hasVcl && poc.usable()) {
                auKey = key;
            }
            hasVcl = true;
        }
    }
    if (closeOpen && hasVcl) {
        aus.push_back({auBegin, limit, auKey});
    }
    return aus;
}

// Advances the picture-order state over nals[0, limit) exactly as GroupAUs does
// over the same range: parameter sets fed, every VCL NAL classified once and in
// stream order. Used to commit the state after a group planned against a copy.
void FeedPocRange(H264Poc& poc, const std::vector<Nal>& nals, size_t limit) {
    for (size_t idx = 0; idx < std::min(limit, nals.size()); ++idx) {
        const Nal& nal = nals[idx];
        if (nal.type >= 1 && nal.type <= 5) {
            int64_t key = 0;
            poc.ClassifySlice(nal.payload, nal.len, &key);
        } else {
            poc.FeedParameterSet(nal.payload, nal.len);
        }
    }
}

// HEVC access unit grouping. Same logic as H.264 but with HEVC NAL types and
// 2-byte NAL headers. VCL NALs are types 0-9 (non-IRAP) and 16-23 (IRAP).
std::vector<Au> GroupHevcAUs(HEVCPoc& poc, const std::vector<Nal>& nals,
                             size_t limitNals, bool closeOpen) {
    std::vector<Au> aus;
    size_t auBegin = 0;
    int64_t auKey = -1;
    bool hasVcl = false;
    const size_t limit = std::min(limitNals, nals.size());
    for (size_t idx = 0; idx < limit; ++idx) {
        const Nal& nal = nals[idx];
        // HEVC VCL NAL types: 0-9 (TRAIL_, TSA_, STSA_, RADL_, RASL_) and
        // 16-23 (BLA_, IDR_, CRA_). Non-VCL preamble types include 32-34
        // (VPS/SPS/PPS), 39 (SEI_PREFIX), 40 (SEI_SUFFIX).
        const bool isVcl = (nal.type <= 9) || (nal.type >= 16 && nal.type <= 23);
        const bool isPreamble = (nal.type >= 32 && nal.type <= 34) ||
                                nal.type == 39 || nal.type == 40;
        int64_t key = -1;
        bool continuation = false;
        bool preamble = false;
        if (isVcl) {
            continuation = poc.ClassifySlice(nal.payload, nal.len, &key) ==
                           HEVCPoc::Slice::Continuation;
        } else {
            poc.FeedParameterSet(nal.payload, nal.len);
            preamble = isPreamble;
        }
        // Where the current picture ends: any NAL that starts a new one.
        if (hasVcl && (isVcl ? !continuation : preamble)) {
            aus.push_back({auBegin, idx, auKey});
            auBegin = idx;
            auKey = -1;
            hasVcl = false;
        }
        if (isVcl) {
            if (!hasVcl && poc.usable()) {
                auKey = key;
            }
            hasVcl = true;
        }
    }
    if (closeOpen && hasVcl) {
        aus.push_back({auBegin, limit, auKey});
    }
    return aus;
}

// Advances the HEVC picture-order state over nals[0, limit).
void FeedHevcPocRange(HEVCPoc& poc, const std::vector<Nal>& nals, size_t limit) {
    for (size_t idx = 0; idx < std::min(limit, nals.size()); ++idx) {
        const Nal& nal = nals[idx];
        const bool isVcl = (nal.type <= 9) || (nal.type >= 16 && nal.type <= 23);
        if (isVcl) {
            int64_t key = 0;
            poc.ClassifySlice(nal.payload, nal.len, &key);
        } else {
            poc.FeedParameterSet(nal.payload, nal.len);
        }
    }
}

} // namespace vtbox
} // namespace halcodec
