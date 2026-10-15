// Capability introspection for the "qsv" backend.
//
// libvpl has no hardware capability enumeration API (unlike CUDA); codec
// support is negotiated at session/component init time against the loaded
// Intel media driver. The provider reports the fixed component surface and
// whether the libvpl dispatcher is loadable on this host.
//
// The structured decode answer (queryDecoderCaps) does have a real source of
// truth: MFXVideoDECODE_Query, run per (codec, chroma, bitDepth, FourCC)
// combination on a transient session, is exactly the negotiation the decode
// path itself would perform, so its verdict is authoritative -- hardware or
// MFX_WRN_PARTIAL_ACCELERATION software fallback both count as "decodes".

#include <dlfcn.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "capability.h"
#include "qsv_common.h"

namespace halcodec {
namespace qsv {

namespace {

// MFXVideoDECODE_Query is not wrapped by QSVRuntime (qsv_common.h dlsyms only
// the decode data path), so resolve it here. dlopen'ing libvpl again merely
// bumps the refcount on the library QSVRuntime::init already loaded.
using MFXVideoDECODEQueryFn = mfxStatus(MFX_CDECL*)(mfxSession,
                                                    mfxVideoParam*,
                                                    mfxVideoParam*);

// One row of the structured decode matrix: a (codec, chroma, bitDepth,
// FourCC) combination to ask the driver about, plus the HAL pixel format the
// FourCC decodes to. Only FourCCs HAL can spell are probed -- a QSV answer
// naming Y210/AYUV would be unreportable (no HAL format exists), so HEVC
// 4:4:4 (AYUV/Y410) and 10-bit 4:2:2 (Y210) are left out of the matrix
// entirely rather than queried and dropped.
struct DecodeCapsProbe {
    const char* profile;  // human-readable label for the entry
    mfxU32 codecId;       // MFX_CODEC_*
    mfxU16 chroma;        // MFX_CHROMAFORMAT_*
    int depth;            // bits per component
    mfxU32 fourcc;        // MFX_FOURCC_*
    PixelFormat format;   // the HAL format this FourCC carries
};

const DecodeCapsProbe kDecodeCapsProbes[] = {
    {"High",       MFX_CODEC_AVC,   MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main",       MFX_CODEC_HEVC,  MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main 10",    MFX_CODEC_HEVC,  MFX_CHROMAFORMAT_YUV420, 10, MFX_FOURCC_P010, PixelFormat::P010},
    {"Main 4:2:2", MFX_CODEC_HEVC,  MFX_CHROMAFORMAT_YUV422, 8,  MFX_FOURCC_NV16, PixelFormat::NV16},
    {"Main",       MFX_CODEC_MPEG2, MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Baseline",   MFX_CODEC_JPEG,  MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main",       MFX_CODEC_AV1,   MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main 10",    MFX_CODEC_AV1,   MFX_CHROMAFORMAT_YUV420, 10, MFX_FOURCC_P010, PixelFormat::P010},
};

// Numeric chroma-subsampling value for a DecodeCapsEntry (Monochrome -> 0).
int chromaNumber(mfxU16 chromaFormat) {
    switch (chromaFormat) {
        case MFX_CHROMAFORMAT_YUV420: return 420;
        case MFX_CHROMAFORMAT_YUV422: return 422;
        case MFX_CHROMAFORMAT_YUV444: return 444;
        default: return 0;
    }
}

} // namespace

class QSVCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "qsv"; }

    std::vector<std::string> getDeviceNames() const override {
        // libvpl does not enumerate GPUs; the dispatcher picks the first Intel
        // adapter. Report nothing.
        return {};
    }

    void showDecoderCapability() const override {
        std::cout << "  hardware decode via libvpl: "
                     "H.264/HEVC/AV1/VP9/VP8/VC1/MPEG2/JPEG "
                     "(driver-dependent, negotiated at init)"
                  << std::endl;
    }

    // Structured decode caps. Source of truth: MFXVideoDECODE_Query per
    // (codec, chroma, bitDepth, FourCC) row of kDecodeCapsProbes, on a
    // transient session from the same QSVRuntime::createSession path the
    // decoder uses -- Query is the negotiation the decode path performs at
    // Init, so its verdict is what the hardware (or the driver's software
    // fallback) will actually honor. MFX_ERR_NONE and
    // MFX_WRN_PARTIAL_ACCELERATION count as "decodes"; every other status
    // (MFX_ERR_UNSUPPORTED, but also warnings that mean the driver had to
    // rewrite the ask) drops the row. A codec outside the probe matrix
    // (VP9/VP8/VC1 decode exists in this backend but has no row here) and a
    // session/query that cannot even be created both answer Unknown, per the
    // CapsStatus contract.
    CapsStatus queryDecoderCaps(const std::string& codec,
                                std::vector<DecodeCapsEntry>& out) const override {
        // Same codec-string mapping the encode/decode paths use (0 = unknown
        // string entirely).
        mfxU32 codecId = mapCodec(codec);
        if (codecId == 0) {
            return CapsStatus::Unknown;
        }
        bool codecProbed = false;
        for (const auto& probe : kDecodeCapsProbes) {
            if (probe.codecId == codecId) {
                codecProbed = true;
                break;
            }
        }
        if (!codecProbed) {
            return CapsStatus::Unknown;
        }

        // Query needs a session; creating one is the same runtime the decode
        // path needs, so failing here means the backend cannot answer, not
        // that the codecs are unsupported.
        QSVRuntime runtime;
        if (!runtime.init()) {
            return CapsStatus::Unknown;
        }
        mfxSession session = nullptr;
        if (runtime.createSession(&session) != MFX_ERR_NONE) {
            return CapsStatus::Unknown;
        }
        void* vpl = dlopen("libvpl.so.2", RTLD_NOW | RTLD_GLOBAL);
        if (!vpl) {
            vpl = dlopen("libvpl.so", RTLD_NOW | RTLD_GLOBAL);
        }
        MFXVideoDECODEQueryFn decodeQuery = nullptr;
        if (vpl) {
            decodeQuery = reinterpret_cast<MFXVideoDECODEQueryFn>(
                dlsym(vpl, "MFXVideoDECODE_Query"));
        }
        if (!decodeQuery) {
            if (vpl) {
                dlclose(vpl);
            }
            runtime.close(session);
            return CapsStatus::Unknown;
        }

        for (const auto& probe : kDecodeCapsProbes) {
            if (probe.codecId != codecId) {
                continue;
            }
            mfxVideoParam in = {};
            in.mfx.CodecId = probe.codecId;
            in.mfx.FrameInfo.FourCC = probe.fourcc;
            in.mfx.FrameInfo.ChromaFormat = probe.chroma;
            in.mfx.FrameInfo.BitDepthLuma = static_cast<mfxU16>(probe.depth);
            in.mfx.FrameInfo.BitDepthChroma = static_cast<mfxU16>(probe.depth);
            // Video memory output: the FourCC check then hits the native
            // decode surface table, matching the zero-copy path this backend
            // ships.
            in.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
            mfxVideoParam fixed = {};
            mfxStatus sts = decodeQuery(session, &in, &fixed);
            if (sts != MFX_ERR_NONE && sts != MFX_WRN_PARTIAL_ACCELERATION) {
                continue;
            }
            DecodeCapsEntry entry;
            entry.profile = probe.profile;
            entry.bitDepth = probe.depth;
            entry.chroma = chromaNumber(probe.chroma);
            entry.outputFormats.push_back(probe.format);
            out.push_back(std::move(entry));
        }

        dlclose(vpl);
        runtime.close(session);
        return CapsStatus::Available;
    }

    void showEncoderCapability() const override {
        QSVRuntime runtime;
        bool loaded = runtime.init();
        std::cout << "  hardware encode via libvpl: H.264/HEVC/AV1/JPEG on Intel iGPU "
                     "(VP9/MPEG2 encode not implemented on this hardware)"
                  << (loaded ? "" : " (libvpl not loadable on this host)")
                  << std::endl;
    }
};

HALCODEC_CONNECT(CapabilityProvider, qsv, QSVCapsProvider);

} // namespace qsv
} // namespace halcodec