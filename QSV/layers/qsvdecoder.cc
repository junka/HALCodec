#include "qsvdecoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "qsv_common.h"
#include "registry.h"

namespace halcodec {
namespace qsv {

namespace {

// Bitstream buffer size matches the hello-decode reference (oneVPL samples):
// large enough to hold several access units of a small test stream in one
// ReadEncodedStream refill.
constexpr mfxU32 kBitstreamBytes = 2 * 1024 * 1024;
// MFXVideoCORE_SyncOperation / Synchronize wait, in milliseconds.
constexpr mfxU32 kSyncTimeoutMs = 1000;

// Moves the unconsumed tail of the bitstream to the front and tops it up from
// the input file, mirroring ReadEncodedStream() from the oneVPL samples.
// Returns false on EOF (no bytes read this refill).
bool RefillBitstream(mfxBitstream& bs, std::ifstream& in) {
    if (bs.DataOffset > 0 && bs.DataLength > 0) {
        std::memmove(bs.Data, bs.Data + bs.DataOffset, bs.DataLength);
    }
    bs.DataOffset = 0;
    mfxU32 room = bs.MaxLength - bs.DataLength;
    if (room == 0) {
        return true; // buffer full, nothing to read
    }
    in.read(reinterpret_cast<char*>(bs.Data + bs.DataLength), room);
    auto got = static_cast<mfxU32>(in.gcount());
    bs.DataLength += got;
    return got > 0;
}

// Copies an NV12 internal-memory surface into a tightly-packed malloc'd buffer
// (Y plane w*h followed by interleaved UV plane w*h/2), pitch-aware. The
// caller owns the returned buffer and must free() it.
uint8_t* CopyNV12(const mfxFrameSurface1* surf) {
    mfxU16 w = surf->Info.CropW;
    mfxU16 h = surf->Info.CropH;
    mfxU16 pitch = surf->Data.Pitch;
    size_t yBytes = static_cast<size_t>(w) * h;
    size_t uvBytes = static_cast<size_t>(w) * (h / 2);
    uint8_t* dst = static_cast<uint8_t*>(std::malloc(yBytes + uvBytes));
    if (!dst) {
        return nullptr;
    }
    // Y plane.
    for (mfxU16 i = 0; i < h; i++) {
        std::memcpy(dst + static_cast<size_t>(w) * i,
                    surf->Data.Y + static_cast<size_t>(pitch) * i, w);
    }
    // Interleaved UV plane (one row per pair of luma rows, same pitch).
    uint8_t* dstUV = dst + yBytes;
    for (mfxU16 i = 0; i < h / 2; i++) {
        std::memcpy(dstUV + static_cast<size_t>(w) * i,
                    surf->Data.UV + static_cast<size_t>(pitch) * i, w);
    }
    return dst;
}

} // namespace

// PIMPL keeps the libvpl session handle out of the public header.
class QSVDecoder::Impl {
public:
    QSVRuntime runtime;
    mfxSession session = nullptr;
    bool inited = false;

    // Input elementary stream (Annex-B H.264/HEVC/AV1).
    std::ifstream source;
    mfxBitstream bs{};
    bool eof = false;       // input file exhausted
    bool draining = false;  // feeding NULL bitstream to flush delayed frames

    // Decoded frames queued for GetFrame() to drain.
    std::queue<CodecFrame> frames;

    ~Impl() {
        while (!frames.empty()) {
            if (frames.front().release) {
                frames.front().release();
            }
            frames.pop();
        }
        if (inited && session) {
            runtime.decodeTerminate(session);
        }
        if (session) {
            runtime.close(session);
        }
        if (bs.Data) {
            std::free(bs.Data);
        }
    }
};

bool QSVDecoder::Initialize(const CodecParams& params) {
    if (impl_) {
        return false; // already initialized
    }
    if (params.inputs.empty()) {
        std::cerr << "QSVDecoder: input file required" << std::endl;
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

    // Open the Annex-B elementary stream and prepare the bitstream buffer.
    impl_->source.open(params.inputs[0], std::ios::binary);
    if (!impl_->source) {
        std::cerr << "QSVDecoder: cannot open input " << params.inputs[0]
                  << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->bs.MaxLength = kBitstreamBytes;
    impl_->bs.Data = static_cast<mfxU8*>(std::calloc(impl_->bs.MaxLength, 1));
    if (!impl_->bs.Data) {
        std::cerr << "QSVDecoder: bitstream alloc failed" << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->bs.CodecId = mapCodec(params.codec);

    // Prime the bitstream and pre-parse the header so the decoder is
    // initialized with the stream's real width/height/FourCC rather than a
    // guess. DecodeHeader consumes SPS/PPS and advances DataOffset.
    RefillBitstream(impl_->bs, impl_->source);
    mfxVideoParam par{};
    par.mfx.CodecId = impl_->bs.CodecId;
    par.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    mfxStatus sts = impl_->runtime.decodeHeader(impl_->session, &impl_->bs, &par);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVDecoder: MFXVideoDECODE_DecodeHeader failed: " << sts
                  << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    sts = impl_->runtime.decodeInit(impl_->session, &par);
    if (sts != MFX_ERR_NONE) {
        std::cerr << "QSVDecoder: MFXVideoDECODE_Init failed: " << sts << std::endl;
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->inited = true;
    std::cout << "QSVDecoder: libvpl session up, codec=" << params.codec << " "
              << par.mfx.FrameInfo.Width << "x" << par.mfx.FrameInfo.Height
              << std::endl;
    return true;
}

int QSVDecoder::PullFrames() {
    if (!impl_ || !impl_->inited) {
        return -1;
    }
    int produced = 0;
    mfxSession session = impl_->session;

    // Decode a batch: keep feeding the bitstream until either a few frames are
    // ready or the stream is fully drained. Returning with produced>0 lets the
    // caller drain via GetFrame() and re-enter PullFrames() for the next batch.
    while (produced < 4) {
        if (!impl_->draining) {
            // Top up the bitstream whenever there is room; once EOF is hit we
            // switch to drain mode (NULL bitstream) on the next MORE_DATA.
            if (impl_->bs.DataLength == 0 ||
                impl_->bs.DataLength < impl_->bs.MaxLength) {
                if (!impl_->eof) {
                    bool got = RefillBitstream(impl_->bs, impl_->source);
                    if (!got) {
                        impl_->eof = true;
                    }
                }
            }
        }

        mfxBitstream* bsPtr = impl_->draining ? nullptr : &impl_->bs;
        mfxFrameSurface1* surfOut = nullptr;
        mfxSyncPoint syncp{};
        mfxStatus sts = impl_->runtime.decodeFrameAsync(session, bsPtr, nullptr,
                                                       &surfOut, &syncp);

        switch (sts) {
            case MFX_ERR_NONE: {
                // Internal-memory surface: synchronize, map, copy out, release.
                mfxFrameSurfaceInterface* fi = surfOut->FrameInterface;
                sts = fi->Synchronize(surfOut, kSyncTimeoutMs);
                if (sts == MFX_WRN_IN_EXECUTION) {
                    // Not ready yet; keep the surface, retry next iteration.
                    fi->Release(surfOut);
                    break;
                }
                if (sts != MFX_ERR_NONE) {
                    std::cerr << "QSVDecoder: Synchronize failed: " << sts
                              << std::endl;
                    fi->Release(surfOut);
                    return produced > 0 ? produced : -1;
                }
                sts = fi->Map(surfOut, MFX_MAP_READ);
                if (sts != MFX_ERR_NONE) {
                    std::cerr << "QSVDecoder: Map failed: " << sts << std::endl;
                    fi->Release(surfOut);
                    return produced > 0 ? produced : -1;
                }
                CodecFrame frame;
                frame.width = surfOut->Info.CropW;
                frame.height = surfOut->Info.CropH;
                frame.format = PixelFormat::NV12;
                frame.size = static_cast<size_t>(frame.width) * frame.height * 3 / 2;
                frame.strides[0] = static_cast<size_t>(frame.width);
                frame.data = CopyNV12(surfOut);
                // Capture the malloc'd pointer by value for cleanup.
                uint8_t* owned = frame.data;
                frame.release = [owned]() { std::free(owned); };
                fi->Unmap(surfOut);
                fi->Release(surfOut);
                if (frame.data) {
                    impl_->frames.push(std::move(frame));
                    produced++;
                }
                break;
            }
            case MFX_ERR_MORE_DATA:
                if (impl_->draining) {
                    // No more frames will come out; stream fully drained.
                    return produced;
                }
                if (impl_->eof) {
                    impl_->draining = true;
                }
                // otherwise the refill at the top of the loop feeds more data.
                break;
            case MFX_ERR_MORE_SURFACE:
                // Internal-memory mode allocates surfaces itself; this should
                // not occur, but if it does just continue.
                break;
            case MFX_WRN_DEVICE_BUSY:
                // Retry after a brief yield.
                break;
            default:
                std::cerr << "QSVDecoder: DecodeFrameAsync error: " << sts
                          << std::endl;
                return produced > 0 ? produced : -1;
        }
    }
    return produced;
}

void QSVDecoder::Finalize() {
    delete impl_;
    impl_ = nullptr;
}

bool QSVDecoder::GetFrame(CodecFrame& out) {
    if (!impl_ || impl_->frames.empty()) {
        return false;
    }
    out = std::move(impl_->frames.front());
    impl_->frames.pop();
    return true;
}

std::string QSVDecoder::getName() const {
    return "qsvdec";
}

HALCODEC_CONNECT(Decoder, qsvdec, QSVDecoder);

} // namespace qsv
} // namespace halcodec
