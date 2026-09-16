#include "qsvencoder.h"

#include <cstring>
#include <cstdlib>
#include <memory>
#include <queue>
#include <string>

#include "qsv_common.h"
#include "registry.h"

namespace halcodec {
namespace qsv {

namespace {

// Output bitstream buffer for one encoded access unit. The encoder writes into
// Data[DataOffset .. DataOffset+DataLength]; we copy out and reset after each
// SyncOperation so the same buffer is reused for the next frame.
constexpr mfxU32 kBitstreamBytes = 2 * 1024 * 1024;
constexpr mfxU32 kSyncTimeoutMs = 1000;

// Align a dimension up to a multiple of 16 (oneVPL requires Width/Height
// aligned to macroblocks for hardware encode).
mfxU16 Align16(int v) {
    return static_cast<mfxU16>((v + 15) & ~15);
}

// Writes a tightly-packed NV12 CodecFrame (Y: w*h, interleaved UV: w*h/2) into
// a mapped internal-memory encode surface, pitch-aware. Returns MFX_ERR_NONE
// on success. The caller must have Map'd the surface for write already.
mfxStatus FillSurfaceFromFrame(mfxFrameSurface1* surf, const CodecFrame& in) {
    mfxU16 w = surf->Info.CropW;
    mfxU16 h = surf->Info.CropH;
    mfxU16 pitch = surf->Data.Pitch;
    if (w != in.width || h != in.height) {
        return MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;
    }
    size_t yBytes = static_cast<size_t>(w) * h;
    size_t uvBytes = static_cast<size_t>(w) * (h / 2);
    if (in.size < yBytes + uvBytes) {
        return MFX_ERR_NOT_ENOUGH_BUFFER;
    }
    // Y plane.
    for (mfxU16 i = 0; i < h; i++) {
        std::memcpy(surf->Data.Y + static_cast<size_t>(pitch) * i,
                    in.data + static_cast<size_t>(w) * i, w);
    }
    // Interleaved UV plane.
    const uint8_t* srcUV = in.data + yBytes;
    for (mfxU16 i = 0; i < h / 2; i++) {
        std::memcpy(surf->Data.UV + static_cast<size_t>(pitch) * i,
                    srcUV + static_cast<size_t>(w) * i, w);
    }
    return MFX_ERR_NONE;
}

// Copies the encoded access unit out of the bitstream into a malloc'd
// CodecFrame and resets the bitstream (DataOffset=0, DataLength=0) so the
// same buffer is reused for the next frame. The caller owns the returned
// frame's buffer and must free() it (wired via release).
CodecFrame TakeBitstream(mfxBitstream& bs) {
    CodecFrame frame;
    frame.size = bs.DataLength;
    frame.format = PixelFormat::Unknown; // encoded elementary stream
    frame.data = static_cast<uint8_t*>(std::malloc(frame.size ? frame.size : 1));
    if (frame.data && frame.size) {
        std::memcpy(frame.data, bs.Data + bs.DataOffset, frame.size);
    }
    uint8_t* owned = frame.data;
    frame.release = [owned]() { std::free(owned); };
    bs.DataOffset = 0;
    bs.DataLength = 0;
    return frame;
}

} // namespace

// PIMPL keeps the libvpl session handle out of the public header.
class QSVEncoder::Impl {
public:
    QSVRuntime runtime;
    mfxSession session = nullptr;
    bool inited = false;

    mfxVideoParam par{};
    mfxBitstream bs{};
    bool draining = false;  // EOS signaled; flushing delayed frames

    // Encoded access units queued for GetFrame() to drain.
    std::queue<CodecFrame> packets;

    ~Impl() {
        while (!packets.empty()) {
            if (packets.front().release) {
                packets.front().release();
            }
            packets.pop();
        }
        if (inited && session) {
            runtime.encodeTerminate(session);
        }
        if (session) {
            runtime.close(session);
        }
        if (bs.Data) {
            std::free(bs.Data);
        }
    }
};

bool QSVEncoder::Initialize(const CodecParams& params) {
    if (impl_) {
        return false; // already initialized
    }
    if (params.width <= 0 || params.height <= 0) {
        std::cerr << "QSVEncoder: width/height required in CodecParams" << std::endl;
        return false;
    }
    impl_ = new Impl;
    if (!impl_->runtime.init()) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    if (impl_->runtime.createSession(&impl_->session) != MFX_ERR_NONE) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    mfxVideoParam& par = impl_->par;
    par.mfx.CodecId = mapCodec(params.codec);
    par.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    par.mfx.TargetKbps = 8000; // ~8 Mbps
    // CBR is used over VBR because the libmfx-gen implementation on this
    // (Arrow Lake) iGPU rejects VBR at frame-submit time for AVC (-5
    // MFX_ERR_INVALID_VIDEO_PARAM), even though Init accepts it; HEVC tolerates
    // VBR. CBR works for both, so it is the safe default here.
    par.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    par.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    par.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    par.mfx.FrameInfo.CropW = static_cast<mfxU16>(params.width);
    par.mfx.FrameInfo.CropH = static_cast<mfxU16>(params.height);
    par.mfx.FrameInfo.Width = Align16(params.width);
    par.mfx.FrameInfo.Height = Align16(params.height);
    par.mfx.FrameInfo.FrameRateExtN = 30;
    par.mfx.FrameInfo.FrameRateExtD = 1;
    par.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    par.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    // Validate / clamp parameters against what the implementation supports.
    // MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is benign: the encoder adjusts the
    // structure to the nearest supported config.
    mfxStatus sts = impl_->runtime.encodeQuery(impl_->session, &par, &par);
    if (sts == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        sts = MFX_ERR_NONE;
    }
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: MFXVideoENCODE_Query failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    sts = impl_->runtime.encodeInit(impl_->session, &par);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: MFXVideoENCODE_Init failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    impl_->bs.MaxLength = kBitstreamBytes;
    impl_->bs.Data = static_cast<mfxU8*>(std::calloc(impl_->bs.MaxLength, 1));
    if (!impl_->bs.Data) {
        std::cerr << "QSVEncoder: bitstream alloc failed" << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->inited = true;
    std::cout << "QSVEncoder: libvpl session up, codec=" << params.codec << " "
              << params.width << "x" << params.height << std::endl;
    return true;
}

bool QSVEncoder::FillFrame(const CodecFrame& input) {
    if (!impl_ || !impl_->inited) {
        return false;
    }
    mfxSession session = impl_->session;

    // End-of-stream marker (size == 0): drain delayed frames by submitting
    // NULL surfaces until the encoder returns MFX_ERR_MORE_DATA.
    if (input.size == 0) {
        impl_->draining = true;
        while (true) {
            mfxSyncPoint syncp{};
            mfxStatus sts = impl_->runtime.encodeFrameAsync(session, nullptr,
                                                           &impl_->bs, &syncp);
            if (sts == MFX_ERR_NONE && syncp) {
                sts = impl_->runtime.syncOperation(session, syncp, kSyncTimeoutMs);
                if (sts != MFX_ERR_NONE && sts != MFX_WRN_IN_EXECUTION) {
                    std::cerr << "QSVEncoder: drain SyncOperation failed: "
                              << sts << std::endl;
                    return false;
                }
                impl_->packets.push(TakeBitstream(impl_->bs));
            } else if (sts == MFX_ERR_MORE_DATA || sts == MFX_ERR_NOT_ENOUGH_BUFFER) {
                return true; // encoder fully flushed
            } else {
                std::cerr << "QSVEncoder: drain EncodeFrameAsync error: " << sts
                          << std::endl;
                return false;
            }
        }
    }

    // Normal frame: get an internal surface, copy the NV12 input in, submit.
    mfxFrameSurface1* surf = nullptr;
    mfxStatus sts = impl_->runtime.getSurfaceForEncode(session, &surf);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: GetSurfaceForEncode failed: " << sts << std::endl;
        return false;
    }
    sts = surf->FrameInterface->Map(surf, MFX_MAP_WRITE);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: surface Map failed: " << sts << std::endl;
        surf->FrameInterface->Release(surf);
        return false;
    }
    sts = FillSurfaceFromFrame(surf, input);
    mfxStatus unmapSts = surf->FrameInterface->Unmap(surf);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: input frame incompatible: " << sts << std::endl;
        surf->FrameInterface->Release(surf);
        return false;
    }
    if (unmapSts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: surface Unmap failed: " << unmapSts << std::endl;
        surf->FrameInterface->Release(surf);
        return false;
    }

    mfxSyncPoint syncp{};
    sts = impl_->runtime.encodeFrameAsync(session, surf, &impl_->bs, &syncp);
    // The encoder holds its own reference once submitted; release ours.
    surf->FrameInterface->Release(surf);

    if (sts == MFX_ERR_NONE && syncp) {
        sts = impl_->runtime.syncOperation(session, syncp, kSyncTimeoutMs);
        if (sts != MFX_ERR_NONE && sts != MFX_WRN_IN_EXECUTION) {
            std::cerr << "QSVEncoder: SyncOperation failed: " << sts << std::endl;
            return false;
        }
        impl_->packets.push(TakeBitstream(impl_->bs));
    } else if (sts == MFX_ERR_MORE_DATA) {
        // Encoder buffered the frame; output comes with a later submission.
    } else if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVEncoder: EncodeFrameAsync error: " << sts << std::endl;
        return false;
    }
    return true;
}

bool QSVEncoder::GetFrame(CodecFrame& out) {
    if (!impl_ || impl_->packets.empty()) {
        return false;
    }
    out = std::move(impl_->packets.front());
    impl_->packets.pop();
    return true;
}

void QSVEncoder::Finalize() {
    delete impl_;
    impl_ = nullptr;
}

std::string QSVEncoder::getName() const {
    return "qsvenc";
}

HALCODEC_CONNECT(Encoder, qsvenc, QSVEncoder);

} // namespace qsv
} // namespace halcodec
