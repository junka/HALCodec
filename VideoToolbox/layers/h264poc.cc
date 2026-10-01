#include "h264poc.h"

#include <algorithm>
#include <cstring>

namespace halcodec {
namespace vtbox {

namespace {

// Profiles that carry the extra SPS fields (chroma format, bit depths,
// scaling lists) which have to be stepped over to reach the syntax elements
// this front end needs.
bool IsHighProfile(int profileIdc) {
    switch (profileIdc) {
    case 44: case 83: case 86: case 100: case 110: case 118: case 122:
    case 128: case 134: case 135: case 138: case 139: case 244:
        return true;
    default:
        return false;
    }
}

// MSB-first bit reader over an RBSP. Every accessor fails once the bitstream
// runs out, which the caller turns into "no usable parameters" instead of
// reading past the buffer.
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

// Removes the emulation prevention bytes (00 00 03 -> 00 00) and copies at
// most `want` payload bytes, since only a header prefix is ever parsed.
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

// 7.4.2.1.1.1: a scaling list is a run of deltas that stops early once the
// predicted value reaches 0.
void SkipScalingList(Bits& bits, int size) {
    int64_t last = 8;
    int64_t next = 8;
    for (int i = 0; i < size; ++i) {
        if (next == 0) {
            continue;
        }
        int64_t delta = 0;
        if (!bits.se(&delta)) {
            return;
        }
        next = (last + delta + 256) % 256;
        last = next != 0 ? next : last;
    }
}

const size_t kSpsBytes = 512;
const size_t kSlicePrefixBytes = 48;

} // namespace

void H264Poc::Reset() {
    ResetState();
    gopIndex_ = 0;
}

void H264Poc::ResetState() {
    havePrev_ = false;
    prevPocMsb_ = 0;
    prevPocLsb_ = 0;
    prevFrameNumMsb_ = 0;
    prevFrameNum_ = 0;
    lastPictureKey_ = 0;
}

// A SPS/PPS pair is enough to start ordering pictures, whichever of the two
// arrives first (in-band streams put them before every IDR, but extradata may
// be fed alone and out of order).
void H264Poc::RefreshUsable() {
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
            return;
        }
    }
}

void H264Poc::ParseSps(const uint8_t* nalu, size_t len) {
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 1, len - 1, kSpsBytes);
    Bits b(rbsp.data(), rbsp.size());

    Sps sps;
    int64_t v = 0;
    if (!b.u(8, &v)) {
        return;
    }
    const bool highProfile = IsHighProfile(static_cast<int>(v));
    int64_t spsId = 0;
    if (!b.u(8, &v) || !b.u(8, &v) || !b.ue(&spsId)) {
        return;
    }
    int chromaFormatIdc = 1;
    if (highProfile) {
        int64_t chroma = 1;
        if (!b.ue(&chroma)) {
            return;
        }
        chromaFormatIdc = static_cast<int>(chroma);
        if (chromaFormatIdc == 3 && (!b.u(1, &v))) {
            return;
        }
        sps.separateColourPlane = chromaFormatIdc == 3;
        int64_t skip = 0;
        if (!b.ue(&skip) || !b.ue(&skip) || !b.u(1, &skip)) {
            return;
        }
        int64_t scalingPresent = 0;
        if (!b.u(1, &scalingPresent)) {
            return;
        }
        if (scalingPresent) {
            const int lists = chromaFormatIdc == 3 ? 12 : 8;
            for (int i = 0; i < lists; ++i) {
                int64_t present = 0;
                if (!b.u(1, &present)) {
                    return;
                }
                if (present) {
                    SkipScalingList(b, i < 6 ? 16 : 64);
                }
            }
        }
    }
    int64_t log2FrameNum = 0, pocType = 0;
    if (!b.ue(&log2FrameNum)) {
        return;
    }
    sps.log2MaxFrameNum = static_cast<int>(log2FrameNum) + 4;
    if (!b.ue(&pocType)) {
        return;
    }
    sps.pocType = static_cast<int>(pocType);
    if (sps.pocType == 0) {
        int64_t log2Lsb = 0;
        if (!b.ue(&log2Lsb)) {
            return;
        }
        sps.log2MaxPocLsb = static_cast<int>(log2Lsb) + 4;
    } else if (sps.pocType == 1) {
        int64_t skip = 0;
        if (!b.u(1, &skip) || !b.se(&skip) || !b.se(&skip) || !b.ue(&skip)) {
            return;
        }
        for (int64_t i = 0; i < skip; ++i) {
            if (!b.se(&v)) {
                return;
            }
        }
    } else if (sps.pocType != 2) {
        return;
    }
    int64_t maxRef = 0;
    if (!b.ue(&maxRef)) {
        return;
    }
    sps.maxNumRefFrames = static_cast<int>(maxRef);
    int64_t width = 0, height = 0, flags = 0;
    if (!b.u(1, &flags) || !b.ue(&width) || !b.ue(&height) ||
        !b.u(1, &flags)) {
        return;
    }
    sps.frameMbsOnly = flags != 0;
    if (!sps.frameMbsOnly && !b.u(1, &flags)) {
        return;
    }

    // Type 1 needs the PPS cycle table, which this front end does not read.
    if (sps.pocType != 0 && sps.pocType != 2) {
        return;
    }
    if (spsById_.size() <= static_cast<size_t>(spsId)) {
        spsById_.resize(static_cast<size_t>(spsId) + 1);
    }
    sps.valid = true;
    spsById_[static_cast<size_t>(spsId)] = sps;
    reorderDelay_ = std::min(32, std::max(1, sps.maxNumRefFrames + 1));
    RefreshUsable();
}

void H264Poc::ParsePps(const uint8_t* nalu, size_t len) {
    const std::vector<uint8_t> rbsp = Rbsp(nalu + 1, len - 1, 32);
    Bits b(rbsp.data(), rbsp.size());
    Pps pps;
    int64_t ppsId = 0, spsId = 0, flags = 0;
    if (!b.ue(&ppsId) || !b.ue(&spsId) || !b.u(1, &flags) ||
        !b.u(1, &flags)) {
        return;
    }
    pps.spsId = static_cast<int>(spsId);
    pps.bottomFieldPresent = flags != 0;
    pps.valid = true;
    if (ppsById_.size() <= static_cast<size_t>(ppsId)) {
        ppsById_.resize(static_cast<size_t>(ppsId) + 1);
    }
    ppsById_[static_cast<size_t>(ppsId)] = pps;
    RefreshUsable();
}

void H264Poc::FeedParameterSet(const uint8_t* nalu, size_t len) {
    if (len < 2) {
        return;
    }
    const int type = nalu[0] & 0x1F;
    if (type == 7) {
        ParseSps(nalu, len);
    } else if (type == 8) {
        ParsePps(nalu, len);
    }
}

H264Poc::Slice H264Poc::ClassifySlice(const uint8_t* nalu, size_t len,
                                      int64_t* poc) {
    if (len < 2 || gaveUp_) {
        return Slice::Unknown;
    }
    const int nalRefIdc = (nalu[0] >> 5) & 3;
    const int nalType = nalu[0] & 0x1F;
    if (nalType < 1 || nalType > 5) {
        return Slice::Unknown;
    }

    const std::vector<uint8_t> rbsp = Rbsp(nalu + 1, len - 1, kSlicePrefixBytes);
    Bits b(rbsp.data(), rbsp.size());
    int64_t firstMb = 0, sliceType = 0, ppsId = 0;
    if (!b.ue(&firstMb) || !b.ue(&sliceType) || !b.ue(&ppsId)) {
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

    int64_t frameNum = 0, flags = 0;
    if (sps.separateColourPlane && !b.u(2, &flags)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
    }
    if (!b.u(sps.log2MaxFrameNum, &frameNum)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
    }
    int fieldPic = 0;
    if (!sps.frameMbsOnly) {
        if (!b.u(1, &flags)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
        fieldPic = flags;
        if (fieldPic && !b.u(1, &flags)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
    }
    const bool idr = nalType == 5;
    if (idr && !b.ue(&flags)) {
        gaveUp_ = true;
        usable_ = false;
        return Slice::Unknown;
    }

    // PicOrderCntLsb plus the fields/bottom-offset syntax that has to be
    // stepped over to reach it. Field pictures are given one key (the frame's),
    // which is all a progressive consumer needs.
    int64_t lsb = 0, deltaBottom = 0;
    if (sps.pocType == 0) {
        if (!b.u(sps.log2MaxPocLsb, &lsb)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
        if (pps.bottomFieldPresent && !fieldPic && !b.se(&deltaBottom)) {
            gaveUp_ = true;
            usable_ = false;
            return Slice::Unknown;
        }
    }

    if (firstMb != 0) {
        // Another slice of the picture already classified: same display slot.
        *poc = lastPictureKey_;
        return Slice::Continuation;
    }
    if (idr) {
        // PicOrderCnt restarts at every random access point, so a raw POC only
        // orders pictures inside one GOP. Biasing it by the GOP count keeps the
        // key monotonic over the whole stream, which is what a reorder buffer
        // spanning GOP boundaries needs.
        ++gopIndex_;
        ResetState();
    }

    int64_t value = 0;
    if (sps.pocType == 0) {
        // 8.2.1.1: the transmitted lsb wraps, so the msb is carried from the
        // last reference picture and stepped when the lsb rolls over.
        const int64_t maxLsb = static_cast<int64_t>(1) << sps.log2MaxPocLsb;
        int64_t msb = 0;
        if (!idr && havePrev_) {
            if (lsb < prevPocLsb_ &&
                (lsb - prevPocLsb_ + maxLsb) < maxLsb / 2) {
                msb = prevPocMsb_ + maxLsb;
            } else if (lsb > prevPocLsb_ && (lsb - prevPocLsb_) >= maxLsb / 2) {
                msb = prevPocMsb_ - maxLsb;
            } else {
                msb = prevPocMsb_;
            }
        }
        value = msb + lsb;
        if (nalRefIdc != 0) {
            prevPocMsb_ = msb;
            prevPocLsb_ = lsb;
            havePrev_ = true;
        }
    } else {
        // 8.2.1.3: order count rides on frame_num, which wraps the same way.
        const int64_t maxFrameNum =
            static_cast<int64_t>(1) << sps.log2MaxFrameNum;
        int64_t msb = prevFrameNumMsb_;
        if (!idr) {
            if (frameNum < prevFrameNum_ &&
                (frameNum - prevFrameNum_ + maxFrameNum) < maxFrameNum / 2) {
                msb = prevFrameNumMsb_ + maxFrameNum;
            } else if (frameNum > prevFrameNum_ &&
                       (frameNum - prevFrameNum_) >= maxFrameNum / 2) {
                msb = prevFrameNumMsb_ - maxFrameNum;
            }
        } else {
            msb = 0;
        }
        value = idr ? 0 : 2 * (msb + frameNum);
        if (nalRefIdc != 0) {
            prevFrameNumMsb_ = msb;
            prevFrameNum_ = frameNum;
        }
    }

    const int64_t key = (gopIndex_ << kGopIndexShift) + value;
    lastPictureKey_ = key;
    *poc = key;
    return Slice::NewPicture;
}

} // namespace vtbox
} // namespace halcodec
