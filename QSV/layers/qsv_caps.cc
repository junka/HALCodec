// Capability introspection for the "qsv" backend.
//
// libvpl has no hardware capability enumeration API (unlike CUDA); codec
// support is negotiated at session/component init time against the loaded
// Intel media driver. The provider reports the fixed component surface and
// whether the libvpl dispatcher is loadable on this host.
//
// The structured decode answer (queryDecoderCaps) does have a real source of
// truth: MFXVideoDECODE_Init, run per (codec, chroma, bitDepth, FourCC)
// combination on a fresh transient session, is exactly the negotiation the
// decode path itself performs, so its verdict is authoritative -- hardware and
// the driver's software fallback (MFX_WRN_PARTIAL_ACCELERATION) both count as
// "decodes".

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "capability.h"
#include "qsv_common.h"

namespace halcodec {
namespace qsv {

namespace {

// One row of the structured decode matrix: a (codec, profile, chroma,
// bitDepth, FourCC) combination to ask the driver about, plus the HAL pixel
// format the FourCC decodes to. Only FourCCs HAL can spell are probed -- a QSV
// answer naming Y210/AYUV would be unreportable (no HAL format exists), so HEVC
// 4:4:4 (AYUV/Y410) and 10-bit 4:2:2 (Y210) are left out of the matrix
// entirely rather than queried and dropped.
//
// The profile column is not decoration: Init validates profile, chroma, depth
// and FourCC together, and HEVC is refused (MFX_ERR_INVALID_VIDEO_PARAM) when
// it is left at 0 -- measured on this box, where HEVC Main with a profile
// opens and the same row without one does not. With the profile supplied the
// statuses separate cleanly: MFX_ERR_NONE for a supported row and
// MFX_ERR_UNSUPPORTED (-3, not the ambiguous -15) for one the driver really
// cannot do, such as HEVC 4:2:2 here.
struct DecodeCapsProbe {
    const char* profile;  // human-readable label for the entry
    mfxU32 codecId;       // MFX_CODEC_*
    mfxU16 codecProfile;  // MFX_PROFILE_*
    mfxU16 chroma;        // MFX_CHROMAFORMAT_*
    int depth;            // bits per component
    mfxU32 fourcc;        // MFX_FOURCC_*
    PixelFormat format;   // the HAL format this FourCC carries
};

const DecodeCapsProbe kDecodeCapsProbes[] = {
    {"High",       MFX_CODEC_AVC,   MFX_PROFILE_AVC_HIGH,      MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main",       MFX_CODEC_HEVC,  MFX_PROFILE_HEVC_MAIN,     MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main 10",    MFX_CODEC_HEVC,  MFX_PROFILE_HEVC_MAIN10,   MFX_CHROMAFORMAT_YUV420, 10, MFX_FOURCC_P010, PixelFormat::P010},
    {"Main 4:2:2", MFX_CODEC_HEVC,  MFX_PROFILE_HEVC_REXT,     MFX_CHROMAFORMAT_YUV422, 8,  MFX_FOURCC_NV16, PixelFormat::NV16},
    {"Main",       MFX_CODEC_MPEG2, MFX_PROFILE_MPEG2_MAIN,    MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Baseline",   MFX_CODEC_JPEG,  MFX_PROFILE_JPEG_BASELINE, MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main",       MFX_CODEC_AV1,   MFX_PROFILE_AV1_MAIN,      MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Main 10",    MFX_CODEC_AV1,   MFX_PROFILE_AV1_MAIN,      MFX_CHROMAFORMAT_YUV420, 10, MFX_FOURCC_P010, PixelFormat::P010},
    {"Profile 0",  MFX_CODEC_VP9,   MFX_PROFILE_VP9_0,         MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
    {"Profile 0",  MFX_CODEC_VP8,   MFX_PROFILE_VP8_0,         MFX_CHROMAFORMAT_YUV420, 8,  MFX_FOURCC_NV12, PixelFormat::NV12},
};

// Rows deliberately absent, because a synthetic param cannot answer for them
// and a made-up entry would be a false claim:
//   - VC1: Init reports MFX_ERR_UNSUPPORTED for the row, which is the truthful
//     answer as far as this probe can tell (the driver wants a sequence header
//     before it will commit).
//   - 10-bit VP9 (Profile 2) and 10-bit AV1: MFX_ERR_INVALID_VIDEO_PARAM with
//     every profile/chroma/depth/FourCC/memory combination tried, so the
//     driver will not commit from parameters alone. HEVC Main 10 does open,
//     so this is per-codec, not a blanket 10-bit refusal.
// Each of those makes queryDecoderCaps answer Unknown for the codec rather
// than return a short matrix, which is the honest outcome: the caller falls
// back to running the codec and seeing.

// Numeric chroma-subsampling value for a DecodeCapsEntry (Monochrome -> 0).
int chromaNumber(mfxU16 chromaFormat) {
    switch (chromaFormat) {
        case MFX_CHROMAFORMAT_YUV420: return 420;
        case MFX_CHROMAFORMAT_YUV422: return 422;
        case MFX_CHROMAFORMAT_YUV444: return 444;
        default: return 0;
    }
}

// The Init statuses that mean "this row decodes": the component opened,
// it opened but is waiting for the sequence header, or it opened on the
// driver's software fallback. Every other status is a no.
bool accepted(mfxStatus sts) {
    return sts == MFX_ERR_NONE || sts == MFX_ERR_MORE_DATA ||
           sts == MFX_WRN_PARTIAL_ACCELERATION;
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

    // Structured decode caps. Source of truth: MFXVideoDECODE_Init per
    // (codec, chroma, bitDepth, FourCC) row of kDecodeCapsProbes, on a fresh
    // session from the same QSVRuntime::createSession path the decoder uses --
    // Init is the negotiation the decode path performs, so its verdict is what
    // the hardware (or the driver's software fallback) will actually honor.
    //
    // Init, not Query: on this dispatcher MFXVideoDECODE_Query answers
    // MFX_ERR_UNSUPPORTED for every row regardless of the parameters -- a
    // verdict that is not about the codec at all, and which would make the
    // provider report an empty matrix (an "available" backend that appears to
    // decode nothing). Init discriminates correctly when it is handed a
    // complete synthetic param: MFX_ERR_NONE for a codec the driver opens,
    // MFX_ERR_MORE_DATA when it accepts the parameters and only then waits for
    // the sequence header -- both mean "decodes" -- and MFX_ERR_UNSUPPORTED
    // for a row it really cannot do. MFX_WRN_PARTIAL_ACCELERATION (software
    // fallback) counts too; every other status drops the row.
    //
    // A row missing any of profile / chroma / depth / FourCC / geometry /
    // PicStruct does not fail as "unsupported", it fails as
    // MFX_ERR_INVALID_VIDEO_PARAM for a codec the box decodes perfectly well,
    // so an incomplete param turns into a false negative rather than an
    // error. See kDecodeCapsProbes and probeWith for which field each codec
    // needed. A codec outside the probe matrix (VP9/VP8/VC1 decode exists in
    // this backend but has no row here) and a session that cannot even be
    // created both answer Unknown, per the CapsStatus contract.
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

        // Init needs a session; creating one is the same runtime the decode
        // path needs, so failing here means the backend cannot answer, not
        // that the codecs are unsupported.
        QSVRuntime runtime;
        if (!runtime.init()) {
            return CapsStatus::Unknown;
        }

        for (const auto& probe : kDecodeCapsProbes) {
            if (probe.codecId != codecId) {
                continue;
            }
            // A fresh session per row: Init mutates both the session state and
            // the mfxVideoParam it is handed, so reusing either would let an
            // earlier row's verdict, or a half-initialized component, color the
            // next. The param is rebuilt per attempt for the same reason.
            auto probeWith = [&](mfxU16 ioPattern) -> mfxStatus {
                mfxSession session = nullptr;
                if (runtime.createSession(&session) != MFX_ERR_NONE) {
                    return MFX_ERR_UNKNOWN;
                }
                mfxVideoParam par = {};
                par.mfx.CodecId = probe.codecId;
                par.mfx.CodecProfile = probe.codecProfile;
                par.mfx.FrameInfo.FourCC = probe.fourcc;
                par.mfx.FrameInfo.ChromaFormat = probe.chroma;
                par.mfx.FrameInfo.BitDepthLuma = static_cast<mfxU16>(probe.depth);
                par.mfx.FrameInfo.BitDepthChroma = static_cast<mfxU16>(probe.depth);
                // A nominal 1080p geometry. Init negotiates a frame layout, so
                // the sizes cannot be left at zero: with Width/Height 0 every
                // row -- including H.264 NV12, which this box plainly decodes --
                // comes back MFX_ERR_INVALID_VIDEO_PARAM and the matrix is
                // empty, which is a lie about the codec rather than about the
                // geometry. Height 1088, not 1080: surfaces are the rounded-up
                // dimensions and the crop carries the real 1080.
                par.mfx.FrameInfo.Width = 1920;
                par.mfx.FrameInfo.Height = 1088;
                par.mfx.FrameInfo.CropW = 1920;
                par.mfx.FrameInfo.CropH = 1080;
                // Progressive, also not decoration: without it JPEG is refused
                // while every other row still opens.
                par.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
                par.AsyncDepth = 1;
                par.IOPattern = ioPattern;
                mfxStatus sts = runtime.decodeInit(session, &par);
                // Tear the component down whenever Init went as far as creating
                // one -- after MFX_ERR_MORE_DATA it is half-initialized and
                // still reclaims. A rejected row never had a component, so
                // terminating it would only be a no-op error.
                if (accepted(sts)) {
                    runtime.decodeTerminate(session);
                }
                runtime.close(session);
                return sts;
            };
            // Video memory first -- that is the surface the zero-copy path
            // ships -- then system memory. The memory type is not what this
            // matrix reports: a row means "this input class decodes to this
            // pixel format", and a driver that only hands 10-bit back through
            // system memory still decodes it. Measured: HEVC Main 10 into P010
            // is refused with VIDEO_MEMORY and accepted with SYSTEM_MEMORY, so
            // reporting the row as plainly unsupported would have been wrong.
            mfxStatus sts = probeWith(MFX_IOPATTERN_OUT_VIDEO_MEMORY);
            if (!accepted(sts)) {
                sts = probeWith(MFX_IOPATTERN_OUT_SYSTEM_MEMORY);
            }
            if (!accepted(sts)) {
                continue;
            }
            DecodeCapsEntry entry;
            entry.profile = probe.profile;
            entry.bitDepth = probe.depth;
            entry.chroma = chromaNumber(probe.chroma);
            entry.outputFormats.push_back(probe.format);
            out.push_back(std::move(entry));
        }

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