#ifndef SRC_ANNEXB_H
#define SRC_ANNEXB_H

// Annex-B parameter-set extraction: the shared front end the apps use to turn
// a byte stream's SPS/PPS (H.264) or VPS/SPS/PPS (HEVC) into the AVCC
// extradata the hardware decoders want.
//
// This is deliberately the same algorithm as VideoToolbox/layers/h264au.cc's
// ScanNals()-based path: a single whole-buffer scan that keeps the last NAL
// (no following start code => it runs to the end of the buffer) and a codec
// verdict derived from the *type* values seen, not from forbidden_zero_bit --
// H.264 (7=SPS,8=PPS,5=IDR,6=SEI) and HEVC (32=VPS,33=SPS,34=PPS,16..21)
// occupy disjoint type ranges, so a VPS or 33-typed NAL can only be HEVC and a
// 7-typed NAL can only be H.264.
//
// Note: h264au.cc's front end scans by NAL *chunks* and holds the last NAL
// pending across FillInput calls; that is the right shape for a streaming
// decoder but wrong here, where the whole file (or a whole encoded frame) is
// already in hand. Calling ScanNals once per buffer would drop the final NAL
// each time, so this helper walks the buffer itself.
//
// The old app-local copies bounded their inner loop with `j + 3 < size`, which
// cut the final NAL short whenever the stream did not end with a trailing start
// code; the truncated AU made VideoToolbox reject the config with
// kVTVideoDecoderBadDataErr (-12909). The one-past-end tolerances below
// (`i + 3 <= size`, `j + 3 <= size`) are the same fix h264au.cc carries.
//
// Codec evidence is deliberately narrow: only an H.264 SPS (type 7) counts for
// H.264 and only a VPS (32, or a bare 33) for HEVC. The old app-local copies
// also accepted a type-8 NAL as H.264, but a real HEVC type-4 VCL NAL has
// b0 == 0x08 and reads as an H.264 PPS, so that "evidence" is a false positive;
// the only streams the difference shows on are ones carrying a PPS and no SPS
// at all, whose extradata no decoder can use anyway. hal_player was already
// type-7-only, so this matches it exactly.
//
// Header-only and platform-neutral on purpose: the apps compile it directly so
// they need no link edge on the core library.

#include <cstdint>
#include <cstddef>
#include <vector>

namespace halcodec {

// Detected stream codec. kUnknown means "no parameter sets were found" -- the
// caller falls back to its own extension/CLI heuristic.
enum class CodecHint { kUnknown = -1, kH264 = 0, kHEVC = 1 };

// One NAL unit, without its start code. `payload` points into the caller's
// buffer, so the returned vector must not outlive it.
struct AnnexBNal {
    const uint8_t* payload = nullptr;
    size_t size = 0;
};

// H.264 NAL type (5-bit field of the first header byte).
inline uint8_t h264NalType(const uint8_t* p) {
    return static_cast<uint8_t>(p[0] & 0x1F);
}
// HEVC NAL type (6-bit field; unlike H.264 the LSB of byte 0 is the
// forbidden_zero_bit and the type starts one bit higher).
inline uint8_t hevcNalType(const uint8_t* p) {
    return static_cast<uint8_t>((p[0] >> 1) & 0x3F);
}

// Split an Annex-B buffer into NAL units (start codes consumed, not returned).
// A NAL with no following start code runs to the end of the buffer.
// maxNals < 0 means "no limit".
inline std::vector<AnnexBNal> SplitAnnexB(const uint8_t* data, size_t size,
                                          int maxNals = -1) {
    std::vector<AnnexBNal> nals;
    size_t i = 0;
    while (i + 3 <= size) {
        size_t sc = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            sc = 3;
        } else if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 &&
                   data[i + 2] == 0 && data[i + 3] == 1) {
            sc = 4;
        }
        if (!sc) { ++i; continue; }
        if (maxNals >= 0 && static_cast<int>(nals.size()) >= maxNals) break;

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
                 (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
                nextStartFound = true;
                break;
            }
            ++j;
        }
        if (!nextStartFound) j = size;
        nals.push_back({data + i + sc, j - (i + sc)});
        i = j;
    }
    return nals;
}

// Sniff the codec from the NAL types present. Priority mirrors the app-local
// copies: an H.264 SPS (type 7) wins outright -- a 32-typed NAL can occur in
// H.264 (type 0..31 all exist) and is not proof of HEVC -- then an HEVC VPS.
// Without a VPS, a type-33 NAL is read as an HEVC SPS, which is the only way to
// recognise a VPS-less HEVC stream (common for single-layer video).
inline CodecHint DetectCodec(const std::vector<AnnexBNal>& nals) {
    bool seenH264Sps = false, seenHevcVps = false;
    for (const auto& n : nals) {
        if (n.size == 0) continue;
        if (h264NalType(n.payload) == 7) seenH264Sps = true;
        if (hevcNalType(n.payload) == 32) seenHevcVps = true;
    }
    if (seenH264Sps) return CodecHint::kH264;
    if (seenHevcVps) return CodecHint::kHEVC;
    for (const auto& n : nals) {
        if (n.size == 0) continue;
        if (hevcNalType(n.payload) == 33) return CodecHint::kHEVC;
    }
    return CodecHint::kUnknown;
}

// Append one AVCC entry: [4-byte big-endian length][NAL bytes].
inline void AppendAvccNal(std::vector<uint8_t>* out, const AnnexBNal& nal) {
    uint32_t len = static_cast<uint32_t>(nal.size);
    out->push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    out->push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    out->push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(len & 0xFF));
    out->insert(out->end(), nal.payload, nal.payload + nal.size);
}

// Which parameter sets the buffer actually carries, for `codec`:
// bit 0 = VPS, bit 1 = SPS, bit 2 = PPS. The callers differ on what they
// accept (hal_player needs both SPS and PPS; the decoders only need one of
// them to bother filling extradata), so the mask is reported rather than
// judged here.
enum ParamSetBits { kVpsBit = 1, kSpsBit = 2, kPpsBit = 4 };

inline unsigned ParamSetMask(const std::vector<AnnexBNal>& nals,
                             CodecHint codec) {
    unsigned mask = 0;
    for (const auto& n : nals) {
        if (n.size == 0) continue;
        if (codec == CodecHint::kH264) {
            uint8_t t = h264NalType(n.payload);
            if (t == 7) mask |= kSpsBit;
            else if (t == 8) mask |= kPpsBit;
        } else {
            switch (hevcNalType(n.payload)) {
                case 32: mask |= kVpsBit; break;
                case 33: mask |= kSpsBit; break;
                case 34: mask |= kPpsBit; break;
                default: break;
            }
        }
    }
    return mask;
}

// Emit the parameter sets in the order the decoders expect: VPS, SPS, PPS for
// HEVC and SPS, PPS for H.264. At most one of each.
inline void AppendParamSetsAvcc(const std::vector<AnnexBNal>& nals,
                                CodecHint codec, std::vector<uint8_t>* out) {
    unsigned mask = ParamSetMask(nals, codec);
    const uint8_t wantTypes[3] = {32, 33, 34};   // HEVC VPS/SPS/PPS
    const unsigned wantBits[3] = {kVpsBit, kSpsBit, kPpsBit};
    const uint8_t h264Types[2] = {7, 8};         // H.264 SPS/PPS
    const unsigned h264Bits[2] = {kSpsBit, kPpsBit};

    const int count = (codec == CodecHint::kH264) ? 2 : 3;
    for (int k = 0; k < count; ++k) {
        uint8_t want = (codec == CodecHint::kH264) ? h264Types[k] : wantTypes[k];
        unsigned bit = (codec == CodecHint::kH264) ? h264Bits[k] : wantBits[k];
        if (!(mask & bit)) continue;
        for (const auto& n : nals) {
            if (n.size == 0) continue;
            uint8_t t = (codec == CodecHint::kH264) ? h264NalType(n.payload)
                                                    : hevcNalType(n.payload);
            if (t == want) { AppendAvccNal(out, n); break; }
        }
    }
}

// Append each NAL of the buffer to `out` in AVCC form (every NAL, not just the
// parameter sets) -- what the encoders emit and the decoders' Annex-B path
// accepts.
inline void AppendAllNalsAvcc(const uint8_t* data, size_t size,
                             std::vector<uint8_t>* out) {
    for (const auto& n : SplitAnnexB(data, size)) {
        if (n.size) AppendAvccNal(out, n);
    }
}

// The one-stop front end the apps used to each wrap in their own
// ExtractParameterSets: scan `data`, sniff the codec from the parameter-set
// NAL types, and pack the sets it carries into AVCC extradata
// ([len:4][nalu]...), which is what the decoders' Initialize() consumes.
//
// Returns the detected codec hint (kUnknown when nothing usable was seen).
// `mask`, when not null, receives the ParamSetMask the caller wants to judge
// itself (hal_player requires both SPS and PPS before trusting the verdict).
// `onlyWithSpsPps` keeps the decode-side policy of appending nothing unless at
// least an SPS or a PPS was seen; the plain form appends whatever the stream
// carries for the detected codec.
inline CodecHint ExtractParameterSets(const uint8_t* data, size_t size,
                                      std::vector<uint8_t>* extradata,
                                      unsigned* mask = nullptr,
                                      bool onlyWithSpsPps = false) {
    std::vector<AnnexBNal> nals = SplitAnnexB(data, size);
    CodecHint codec = DetectCodec(nals);
    unsigned m = codec == CodecHint::kUnknown ? 0u : ParamSetMask(nals, codec);
    if (mask) *mask = m;
    if (codec == CodecHint::kUnknown) return codec;
    if (!onlyWithSpsPps || (m & (kSpsBit | kPpsBit))) {
        AppendParamSetsAvcc(nals, codec, extradata);
    }
    return codec;
}

} // namespace halcodec

#endif // SRC_ANNEXB_H
