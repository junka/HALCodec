// NVENC/NVDEC capability introspection for the "nvidia" backend.
//
// Replaces nvdevice_caps.cc: capability queries belong to the codec
// backends, not to a device/context abstraction (Device was removed).
// The `#if NVENCAPI_MAJOR_VERSION` branches cannot be compiled on this
// machine (macOS, no CUDA). Keep their inner logic untouched.

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include <cuda.h>

#include "nvcuvid.h"
#include "cuviddec.h"
#include "nvEncodeAPI.h"

#include "capability.h"
#include "cuda_context.h"

namespace halcodec {
namespace nvenc {

namespace {

constexpr const char *kCodecNames[] = {
    "MPEG1", "MPEG2", "MPEG4", "VC1", "H264", "JPEG",
    "H264_SVC", "H264_MVC", "HEVC", "VP8", "VP9", "AV1",
    "YUV420", "YV12", "NV12", "YUYV", "UYVY"
};
constexpr const char *kChromaFormat[] = { "4:0:0", "4:2:0", "4:2:2", "4:4:4" };

#if NVENCAPI_MAJOR_VERSION > 12
constexpr int kNumVideoSurfaceFormat = 6;
#elif NVENCAPI_MAJOR_VERSION == 12
constexpr int kNumVideoSurfaceFormat = 4;
#endif

// Maps the nOutputFormatMask bitmask to a comma-separated surface format
// string. kNumVideoSurfaceFormat is the last valid index, so iterate
// strictly below it.
std::string decoderFormatMaskToString(unsigned short nOutputFormatMask) {
    if (nOutputFormatMask == 0) {
        return "N/A";
    }
    const char* formatNames[] = {
        "NV12", "P016", "YUV444", "YUV444P16", "NV16", "P216"
    };
    std::string outputFormats;
    for (int i = 0; i < kNumVideoSurfaceFormat; ++i) {
        if (nOutputFormatMask & (1U << i)) {
            if (!outputFormats.empty()) {
                outputFormats += ", ";
            }
            outputFormats += formatNames[i];
        }
    }
    return outputFormats;
}

void printDecoderHeader() {
    std::cout << std::left << std::setw(7) << "Codec" <<
        std::setw(12) << "BitDepth  " <<
        std::setw(16) << "ChromaFormat  " <<
        std::setw(12) << "MaxWidth  " <<
        std::setw(12) << "MaxHeight  " <<
        std::setw(12) << "MaxMBCount  " <<
        std::setw(12) << "MinWidth  " <<
        std::setw(12) << "MinHeight  " <<
        std::setw(16) << "SurfaceFormat  " << std::endl;
}

void fillCapsParams(NV_ENC_CAPS_PARAM* params, NV_ENC_CAPS cap) {
    params->version = NV_ENC_CAPS_PARAM_VER;
    params->capsToQuery = cap;
}

const char* codecName(GUID guid) {
    if (std::memcmp(&guid, &NV_ENC_CODEC_H264_GUID, 16) == 0) {
        return "H264";
    }
    if (std::memcmp(&guid, &NV_ENC_CODEC_HEVC_GUID, 16) == 0) {
        return "HEVC";
    }
#if NVENCAPI_MAJOR_VERSION > 11
    if (std::memcmp(&guid, &NV_ENC_CODEC_AV1_GUID, 16) == 0) {
        return "AV1";
    }
#endif
    return "Unknown";
}

const char* presetName(GUID guid) {
    if (std::memcmp(&guid, &NV_ENC_PRESET_P1_GUID, 16) == 0) return "P1";
    if (std::memcmp(&guid, &NV_ENC_PRESET_P2_GUID, 16) == 0) return "P2";
    if (std::memcmp(&guid, &NV_ENC_PRESET_P3_GUID, 16) == 0) return "P3";
    if (std::memcmp(&guid, &NV_ENC_PRESET_P4_GUID, 16) == 0) return "P4";
    if (std::memcmp(&guid, &NV_ENC_PRESET_P5_GUID, 16) == 0) return "P5";
    if (std::memcmp(&guid, &NV_ENC_PRESET_P6_GUID, 16) == 0) return "P6";
    if (std::memcmp(&guid, &NV_ENC_PRESET_P7_GUID, 16) == 0) return "P7";
    return "Unknown";
}

std::string rcModeName(const NV_ENC_CONFIG& presetCfg) {
    switch (presetCfg.rcParams.rateControlMode) {
        case NV_ENC_PARAMS_RC_CONSTQP:
            return "CONSTQP[" + std::to_string(presetCfg.rcParams.constQP.qpIntra) + "," +
                   std::to_string(presetCfg.rcParams.constQP.qpInterB) + "," +
                   std::to_string(presetCfg.rcParams.constQP.qpInterP) + "]";
        case NV_ENC_PARAMS_RC_VBR:
            return "VBR";
        case NV_ENC_PARAMS_RC_CBR:
            return "CBR";
        default:
            return "Unknown";
    }
}

// Appends "<tuningName> (<rcMode>) " when the preset is available under the
// given tuning info. Encapsulates the memset/version boilerplate.
void appendTuningPresetInfo(NV_ENCODE_API_FUNCTION_LIST& encodeApi, void* nvencoder,
                            GUID encodeGUID, GUID preset, const char* tuningName,
                            NV_ENC_TUNING_INFO tuning, std::string* out) {
    NV_ENC_PRESET_CONFIG config = {};
    config.version = NV_ENC_PRESET_CONFIG_VER;
    config.presetCfg.version = NV_ENC_CONFIG_VER;
    int ret = encodeApi.nvEncGetEncodePresetConfigEx(nvencoder, encodeGUID, preset,
                                                     tuning, &config);
    if (ret == NV_ENC_SUCCESS) {
        *out += std::string(" ") + tuningName + " (" + rcModeName(config.presetCfg) + ") ";
    }
}

std::string presetSupportToString(NV_ENCODE_API_FUNCTION_LIST& encodeApi,
                                  void* nvencoder, GUID encodeGUID,
                                  const std::vector<GUID>& presets) {
    std::string out;
    for (auto f : presets) {
        out += presetName(f);
        appendTuningPresetInfo(encodeApi, nvencoder, encodeGUID, f, "HIGH_QUALITY",
                               NV_ENC_TUNING_INFO_HIGH_QUALITY, &out);
        appendTuningPresetInfo(encodeApi, nvencoder, encodeGUID, f, "LOW_LATENCY",
                               NV_ENC_TUNING_INFO_LOW_LATENCY, &out);
        appendTuningPresetInfo(encodeApi, nvencoder, encodeGUID, f, "ULTRA_LOW_LATENCY",
                               NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &out);
        appendTuningPresetInfo(encodeApi, nvencoder, encodeGUID, f, "LOSSLESS",
                               NV_ENC_TUNING_INFO_LOSSLESS, &out);
#if NVENCAPI_MAJOR_VERSION > 12
        appendTuningPresetInfo(encodeApi, nvencoder, encodeGUID, f, "ULTRA_HIGH_QUALITY",
                               NV_ENC_TUNING_INFO_ULTRA_HIGH_QUALITY, &out);
#endif
        out += "\n";
    }
    return out;
}

std::string profileName(GUID guid) {
    if (std::memcmp(&guid, &NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID, 16) == 0) {
        return "Auto Select";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_BASELINE_GUID, 16) == 0) {
        return "H264 Baseline";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_MAIN_GUID, 16) == 0) {
        return "H264 Main";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_HIGH_GUID, 16) == 0) {
        return "H264 High";
    }
#if NVENCAPI_MAJOR_VERSION > 12
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_HIGH_10_GUID, 16) == 0) {
        return "H264 High 10";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_HIGH_422_GUID, 16) == 0) {
        return "H264 High 422";
    }
#endif
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_HIGH_444_GUID, 16) == 0) {
        return "H264 High 444";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_STEREO_GUID, 16) == 0) {
        return "H264 Stereo";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID, 16) == 0) {
        return "H264 Progressive High";
    }
    if (std::memcmp(&guid, &NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID, 16) == 0) {
        return "H264 Contrained High";
    }
    if (std::memcmp(&guid, &NV_ENC_HEVC_PROFILE_MAIN_GUID, 16) == 0) {
        return "HEVC Main";
    }
    if (std::memcmp(&guid, &NV_ENC_HEVC_PROFILE_MAIN10_GUID, 16) == 0) {
        return "HEVC Main 10";
    }
    if (std::memcmp(&guid, &NV_ENC_HEVC_PROFILE_FREXT_GUID, 16) == 0) {
        return "HEVC FREXT";
    }
    if (std::memcmp(&guid, &NV_ENC_AV1_PROFILE_MAIN_GUID, 16) == 0) {
        return "AV1 Main";
    }
    return "Unknown";
}

std::string profilesToString(const std::vector<GUID>& profiles) {
    std::string out;
    for (auto f : profiles) {
        out += profileName(f);
        out += " ";
    }
    return out;
}

const char* inputFormatName(NV_ENC_BUFFER_FORMAT format) {
    switch (format) {
        case NV_ENC_BUFFER_FORMAT_NV12: return "NV12";
        case NV_ENC_BUFFER_FORMAT_YV12: return "YV12";
        case NV_ENC_BUFFER_FORMAT_IYUV: return "IYUV";
        case NV_ENC_BUFFER_FORMAT_YUV444: return "YUV444";
        case NV_ENC_BUFFER_FORMAT_YUV420_10BIT: return "YUV420_10bit";
        case NV_ENC_BUFFER_FORMAT_YUV444_10BIT: return "YUV444_10bit";
        case NV_ENC_BUFFER_FORMAT_ARGB: return "ARGB";
        case NV_ENC_BUFFER_FORMAT_ARGB10: return "ARGB10";
        case NV_ENC_BUFFER_FORMAT_AYUV: return "AYUV";
        case NV_ENC_BUFFER_FORMAT_ABGR: return "ABGR";
        case NV_ENC_BUFFER_FORMAT_ABGR10: return "ABGR10";
#if NVENCAPI_MAJOR_VERSION > 12
        case NV_ENC_BUFFER_FORMAT_NV16: return "NV16";
        case NV_ENC_BUFFER_FORMAT_P210: return "P210";
#endif
        default: return "Unknown";
    }
}

std::string inputFormatsToString(const std::vector<NV_ENC_BUFFER_FORMAT>& formats) {
    std::string out;
    for (auto f : formats) {
        out += inputFormatName(f);
        out += " ";
    }
    return out;
}

// One row of the structured decode matrix queryDecoderCaps() probes: a
// codec x chroma x bit-depth combination, tagged with the human-readable
// profile label the entry reports. The table only says what to ask; whether
// a row yields an entry is decided at runtime by cuvidGetDecoderCaps.
struct DecodeCapsProbe {
    const char* codec;    // registry codec key ("h264", "hevc", ...)
    cudaVideoCodec id;
    cudaVideoChromaFormat chroma;
    int depth;            // bits per component: 8/10/12
    const char* profile;  // human-readable label for the entry
};

const DecodeCapsProbe kDecodeCapsProbes[] = {
    {"h264",  cudaVideoCodec_H264,  cudaVideoChromaFormat_420, 8,  "High"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_420, 8,  "Main"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_420, 10, "Main 10"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_420, 12, "Main 12"},
    // NVDEC has no 4:2:2 decode: the cudaVideoChromaFormat_422 query comes
    // back bIsSupported=0 on every GPU. The rows are enumerated anyway (the
    // honest answer is the query's, not an assumption) and can never produce
    // an entry.
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_422, 8,  "Main 4:2:2"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_422, 10, "Main 4:2:2 10"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_422, 12, "Main 4:2:2 12"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_444, 8,  "Main 4:4:4"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_444, 10, "Main 4:4:4 10"},
    {"hevc",  cudaVideoCodec_HEVC,  cudaVideoChromaFormat_444, 12, "Main 4:4:4 12"},
    {"vp9",   cudaVideoCodec_VP9,   cudaVideoChromaFormat_420, 8,  "Profile 0"},
    {"vp9",   cudaVideoCodec_VP9,   cudaVideoChromaFormat_420, 10, "Profile 2 10"},
    // AV1 needs Video Codec SDK >= 10.0 for the cudaVideoCodec_AV1 enum; the
    // driver rejects the query on GPUs without AV1 decode, which skips the
    // rows at runtime.
#if NVENCAPI_MAJOR_VERSION >= 10
    {"av1",   cudaVideoCodec_AV1,   cudaVideoChromaFormat_420, 8,  "Main"},
    {"av1",   cudaVideoCodec_AV1,   cudaVideoChromaFormat_420, 10, "Main 10"},
#endif
    {"mpeg1", cudaVideoCodec_MPEG1, cudaVideoChromaFormat_420, 8,  "Main"},
    {"mpeg2", cudaVideoCodec_MPEG2, cudaVideoChromaFormat_420, 8,  "Main"},
    {"mpeg4", cudaVideoCodec_MPEG4, cudaVideoChromaFormat_420, 8,  "Advanced Simple"},
    {"jpeg",  cudaVideoCodec_JPEG,  cudaVideoChromaFormat_420, 8,  "Baseline"},
};

// Maps one bit of CUVIDDECODECAPS::nOutputFormatMask (a cudaVideoSurfaceFormat
// index) to the HAL pixel format the decoded surface carries. Unknown for
// surfaces HAL has no spelling for -- which is what keeps 10-bit 4:2:0 out of
// the structured answer: the vendor headers define no P010 surface format
// (10-bit 4:2:0 decode reports P016, a 12-bit-word layout HAL's P016 does not
// claim for 10-bit content), so those rows are skipped by queryDecoderCaps
// instead of mis-mapped. NV16/P216 (SDK >= 13) stay unmapped too: NVDEC does
// not decode 4:2:2, and P216 has no HAL format either.
PixelFormat halFormatForSurfaceBit(int bit) {
    switch (bit) {
        case cudaVideoSurfaceFormat_NV12: return PixelFormat::NV12;
        case cudaVideoSurfaceFormat_P016: return PixelFormat::P016;
        case cudaVideoSurfaceFormat_YUV444: return PixelFormat::YUV444P;
        case cudaVideoSurfaceFormat_YUV444_16Bit: return PixelFormat::YUV444P16LE;
        default: return PixelFormat::Unknown;
    }
}

// Numeric chroma-subsampling value for a DecodeCapsEntry (Monochrome -> 0).
int chromaNumber(cudaVideoChromaFormat chroma) {
    switch (chroma) {
        case cudaVideoChromaFormat_420: return 420;
        case cudaVideoChromaFormat_422: return 422;
        case cudaVideoChromaFormat_444: return 444;
        default: return 0;
    }
}

} // namespace

// Codec-level NVDEC support probe: a single cuvidGetDecoderCaps query at
// 8-bit 4:2:0. Exposed so backends can refuse Initialize() before attempting
// NvDecoder construction. Creates a transient CUDA context (cuvidGetDecoderCaps
// requires one to be current on the calling thread).
const char* nvdecCodecName(cudaVideoCodec codec); // defined below

bool nvdecSupportsCodec(int deviceIndex, cudaVideoCodec codec) {
    if (codec < 0 || codec >= cudaVideoCodec_NumCodecs) {
        return false;
    }
    CUDAContext cudaCtx;
    if (!cudaCtx.create(deviceIndex)) {
        return false;
    }
    CUVIDDECODECAPS decodeCaps = {};
    decodeCaps.eCodecType = codec;
    decodeCaps.eChromaFormat = cudaVideoChromaFormat_420;
    decodeCaps.nBitDepthMinus8 = 0;
    // A failing call leaves decodeCaps zeroed, which is indistinguishable from
    // a genuine "unsupported" and sends callers off hunting for a missing codec
    // on a GPU that has it. Report the CUDA error so a library-version or
    // context problem names itself instead.
    CUresult rc = cuvidGetDecoderCaps(&decodeCaps);
    if (rc != CUDA_SUCCESS) {
        const char* errName = nullptr;
        cuGetErrorName(rc, &errName);
        std::cerr << "nvdecSupportsCodec: cuvidGetDecoderCaps failed on device "
                  << deviceIndex << " for " << nvdecCodecName(codec) << ": "
                  << (errName ? errName : "?") << " (" << static_cast<int>(rc)
                  << ")" << std::endl;
    } else if (!decodeCaps.bIsSupported) {
        std::cerr << "nvdecSupportsCodec: " << nvdecCodecName(codec)
                  << " not advertised as supported on device " << deviceIndex
                  << " (cuvidGetDecoderCaps rc=SUCCESS)" << std::endl;
    }
    return decodeCaps.bIsSupported != 0;
}

const char* nvdecCodecName(cudaVideoCodec codec) {
    if (codec >= 0 && codec < cudaVideoCodec_NumCodecs) {
        return kCodecNames[codec];
    }
    return "Unknown";
}

class NVCodecCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "nvidia"; }

    std::vector<std::string> getDeviceNames() const override {
        std::vector<std::string> names;
        int ngpu = 0;
        if (cuInit(0) != CUDA_SUCCESS || cuDeviceGetCount(&ngpu) != CUDA_SUCCESS) {
            return names;
        }
        for (int i = 0; i < ngpu; i++) {
            char name[80];
            if (cuDeviceGetName(name, sizeof(name), i) == CUDA_SUCCESS) {
                names.emplace_back(name);
            }
        }
        return names;
    }

    void showDecoderCapability() const override {
        // cuvidGetDecoderCaps requires a CUDA context to be current on the
        // calling thread; create a transient one for the query (mirrors
        // showEncoderCapability and nvdecSupportsCodec). Without this every
        // probe returns bIsSupported=0 and the table prints empty.
        CUDAContext cudaCtx;
        if (!cudaCtx.create(0)) {
            std::cerr << "NVCodecCapsProvider: failed to create CUDA context"
                      << std::endl;
            return;
        }
        printDecoderHeader();

        for (int codec = 0; codec < static_cast<int>(cudaVideoCodec_NumCodecs); ++codec) {
            for (int format = 0; format <= static_cast<int>(cudaVideoChromaFormat_444); ++format) {
                for (int bits = 0; bits <= 4; bits += 2) {
                    CUVIDDECODECAPS decodeCaps = {};
                    decodeCaps.eCodecType = static_cast<cudaVideoCodec>(codec);
                    decodeCaps.eChromaFormat = static_cast<cudaVideoChromaFormat>(format);
                    decodeCaps.nBitDepthMinus8 = bits;

                    cuvidGetDecoderCaps(&decodeCaps);

                    if (decodeCaps.bIsSupported) {
                        std::cout << std::left << std::setw(7) << kCodecNames[codec] <<
                            std::setw(12) << decodeCaps.nBitDepthMinus8 + 8 <<
                            std::setw(16) << kChromaFormat[decodeCaps.eChromaFormat] <<
                            std::setw(12) << decodeCaps.nMaxWidth <<
                            std::setw(12) << decodeCaps.nMaxHeight <<
                            std::setw(12) << decodeCaps.nMaxMBCount <<
                            std::setw(12) << decodeCaps.nMinWidth <<
                            std::setw(12) << decodeCaps.nMinHeight <<
                            std::setw(16) << decoderFormatMaskToString(decodeCaps.nOutputFormatMask) <<
                            std::endl;
                    }
                }
            }
        }
    }

    // Structured decode caps. Source of truth: cuvidGetDecoderCaps, queried
    // per (codec, chroma, bit-depth) row of kDecodeCapsProbes on the same
    // transient context showDecoderCapability() uses -- the identical driver
    // call, so the human-readable table and this answer cannot drift apart.
    // A row becomes an entry only when the driver reports bIsSupported AND
    // its nOutputFormatMask intersects a surface format HAL can spell; a
    // per-row CUDA error skips just that row. The whole answer degrades to
    // Unknown only when the query mechanism itself is unavailable (no CUDA
    // context), per the CapsStatus contract. Available with an empty vector
    // is a real verdict: this GPU's NVDEC decodes none of the probed classes.
    CapsStatus queryDecoderCaps(const std::string& codec,
                                std::vector<DecodeCapsEntry>& out) const override {
        // cuvidGetDecoderCaps needs a context current on the calling thread.
        CUDAContext cudaCtx;
        if (!cudaCtx.create(0)) {
            return CapsStatus::Unknown;
        }
        // "mjpeg" is the same NVDEC decoder as "jpeg" (see codecFromName in
        // nvdecoder.cc).
        const std::string key = (codec == "mjpeg") ? "jpeg" : codec;
        bool codecProbed = false;
        for (const auto& probe : kDecodeCapsProbes) {
            if (probe.codec != key) {
                continue;
            }
            codecProbed = true;
            // 10-bit 4:2:0: the vendor headers have no P010 surface format
            // and NVDEC reports P016 for these streams, which HAL does not
            // claim for 10-bit content -- no truthful mapping, no entry.
            if (probe.chroma == cudaVideoChromaFormat_420 && probe.depth == 10) {
                continue;
            }
            CUVIDDECODECAPS caps = {};
            caps.eCodecType = probe.id;
            caps.eChromaFormat = probe.chroma;
            caps.nBitDepthMinus8 = probe.depth - 8;
            // A failing row query skips just that combination (the driver
            // refused to answer for it); bIsSupported=0 means the GPU really
            // cannot decode it.
            if (cuvidGetDecoderCaps(&caps) != CUDA_SUCCESS || !caps.bIsSupported) {
                continue;
            }
            std::vector<PixelFormat> formats;
            for (int bit = 0; bit < 16; ++bit) {
                if (!(caps.nOutputFormatMask & (1U << bit))) {
                    continue;
                }
                PixelFormat format = halFormatForSurfaceBit(bit);
                if (format != PixelFormat::Unknown) {
                    formats.push_back(format);
                }
            }
            if (formats.empty()) {
                continue;
            }
            DecodeCapsEntry entry;
            entry.profile = probe.profile;
            entry.bitDepth = probe.depth;
            entry.chroma = chromaNumber(probe.chroma);
            entry.outputFormats = std::move(formats);
            out.push_back(std::move(entry));
        }
        // Codecs outside the probe matrix ("vc1"/"vp8"/...): no authoritative
        // answer, fall back to showDecoderCapability's full sweep.
        if (!codecProbed) {
            return CapsStatus::Unknown;
        }
        return CapsStatus::Available;
    }

    void showEncoderCapability() const override {
        const char* caps_str[] = {
            "MAX B-FRAMES",
            "RATECONTROL MODES",
            "FIELD ENCODING",
            "MONOCHROME",
            "FMO",
            "QPELMV",
            "BDIRECT_MODE",
            "CABAC",
            "ADAPTIVE_TRANSFORM",
            "STEREO_MVC",
            "TEMPORAL_LAYERS",
            "HIERARCHICAL_PFRAMES",
            "HIERARCHICAL_BFRAMES",
            "MAX Encoding LEVEL",
            "MIN Encoding LEVEL",
            "SEPARATE_COLOUR_PLANE",
            "MAX WIDTH",
            "MAX HEIGHT",
            "TEMPORAL_SVC",
            "DYN_RES_CHANGE",
            "DYN BIRATE_CHANGE",
            "DYN_FORCE_CONSTQP",
            "DYN_RCMODE_CHANGE",
            "SUBFRAME_READBACK",
            "CONSTRAINED_ENCODING",
            "INTRA_REFRESH",
            "CUSTOM_VBV_BUF_SIZE",
            "DYNAMIC_SLICE_MODE",
            "REF_PIC_INVALIDATION",
            "PREPROC",
            "ASYNC_ENCODE",
            "MAX MB",
            "MAX MB_PER_SEC",
            "YUV444_ENCODE",
            "LOSSLESS_ENCODE",
            "SAO",
            "MEONLY_MODE",
            "LOOKAHEAD",
            "TEMPORAL_AQ",
            "10BIT_ENCODE",
            "MAX LTR_FRAMES",
            "WEIGHTED_PREDICTION",
            "DYNAMIC_QUERY_ENCODER",
            "BFRAME_REF_MODE",
            "EMPHASIS_LEVEL_MAP",
            "MIN WIDTH",
            "MIN HEIGHT",
            "MULTIPLE_REF_FRAMES",
            "ALPHA_LAYER_ENCODING",
            "NUM ENCODER_ENGINES",
            "SINGLE_SLICE_INTRA_REFRESH",
            "DISABLE_ENC_STATE_ADVANCE",
            "OUTPUT_RECON_SURFACE",
            "OUTPUT_BLOCK_STATS",
            "OUTPUT_ROW_STATS",
            // These five were added to NV_ENC_CAPS after this table was written.
            // The query loop runs to NV_ENC_CAPS_EXPOSED_COUNT, so a short table
            // made it print caps_str[55..59] — five pointers past the end of the
            // array — and segfault. Keep this in sync with the enum.
            "TEMPORAL_FILTER",
            "LOOKAHEAD_LEVEL",
            "UNIDIRECTIONAL_B",
            "MVHEVC_ENCODE",
            "YUV422_ENCODE",
        };
        static_assert(sizeof(caps_str) / sizeof(caps_str[0]) == NV_ENC_CAPS_EXPOSED_COUNT,
                      "caps_str must name every NV_ENC_CAPS value up to EXPOSED_COUNT");

        uint32_t nvenc_max_ver;
        int ret = NvEncodeAPIGetMaxSupportedVersion(&nvenc_max_ver);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "Fail to get max version" << std::endl;
            return;
        }

        if ((NVENCAPI_MAJOR_VERSION << 4 | NVENCAPI_MINOR_VERSION) > nvenc_max_ver) {
            printf("Driver does not support the required nvenc API version. "
                   "Required: %d.%d Found: %d.%d\n",
                   NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION,
                   nvenc_max_ver >> 4, nvenc_max_ver & 0xf);
            return;
        }

        // NVENC exposes caps through an encoder session, which is bound to a
        // CUDA context; create one locally for the query.
        CUDAContext cudaCtx;
        if (!cudaCtx.create(0)) {
            std::cerr << "Fail to create CUDA context" << std::endl;
            return;
        }

        NV_ENCODE_API_FUNCTION_LIST encodeApi;
        encodeApi.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        ret = NvEncodeAPICreateInstance(&encodeApi);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "Fail to create instance" << std::endl;
            return;
        }

        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };
        params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        params.apiVersion = NVENCAPI_VERSION;
        params.device = cudaCtx.get();
        params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
        void* nvencoder = nullptr;

        ret = encodeApi.nvEncOpenEncodeSessionEx(&params, &nvencoder);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "Fail to open encode session " << ret << std::endl;
            return;
        }

        uint32_t count;
        ret = encodeApi.nvEncGetEncodeGUIDCount(nvencoder, &count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "Fail to get guid count" << std::endl;
            return;
        }

        std::vector<GUID> guids(count);
        ret = encodeApi.nvEncGetEncodeGUIDs(nvencoder, guids.data(), count, &count);
        if (ret != NV_ENC_SUCCESS) {
            return;
        }

        for (int i = 0; i < count; i++) {
            std::cout << codecName(guids[i]) << ":" << std::endl;
            for (int cap = 0; cap < NV_ENC_CAPS_EXPOSED_COUNT; ++cap) {
                NV_ENC_CAPS_PARAM capParams = { 0 };
                int val = 0;
                fillCapsParams(&capParams, static_cast<NV_ENC_CAPS>(cap));
                encodeApi.nvEncGetEncodeCaps(nvencoder, guids[i], &capParams, &val);
                if (val != 0) {
                    std::cout << caps_str[cap] << ": " << val << std::endl;
                }
            }

            uint32_t preset_count;
            ret = encodeApi.nvEncGetEncodePresetCount(nvencoder, guids[i], &preset_count);
            if (ret != NV_ENC_SUCCESS) {
                std::cerr << "fail to get encoder prest count" << std::endl;
                return;
            }
            std::vector<GUID> presetGUID(preset_count);
            ret = encodeApi.nvEncGetEncodePresetGUIDs(nvencoder, guids[i], presetGUID.data(),
                                                      preset_count, &preset_count);
            if (ret != NV_ENC_SUCCESS) {
                std::cerr << "fail to get encoder prest" << std::endl;
                return;
            }
            std::cout << "supported presets:" << std::endl <<
                presetSupportToString(encodeApi, nvencoder, guids[i], presetGUID) << std::endl;

            uint32_t profile_count;
            ret = encodeApi.nvEncGetEncodeProfileGUIDCount(nvencoder, guids[i], &profile_count);
            if (ret != NV_ENC_SUCCESS) {
                std::cerr << "fail to get encoder profile count" << std::endl;
                return;
            }
            std::vector<GUID> profileGUID(profile_count);
            ret = encodeApi.nvEncGetEncodeProfileGUIDs(nvencoder, guids[i], profileGUID.data(),
                                                       profile_count, &profile_count);
            if (ret != NV_ENC_SUCCESS) {
                std::cerr << "fail to get encoder profile" << std::endl;
                return;
            }
            std::cout << "supported profiles: " << profilesToString(profileGUID) << std::endl;

            uint32_t format_count;
            ret = encodeApi.nvEncGetInputFormatCount(nvencoder, guids[i], &format_count);
            if (ret != NV_ENC_SUCCESS) {
                std::cerr << "fail to get input format count" << std::endl;
                return;
            }
            std::vector<NV_ENC_BUFFER_FORMAT> formats(format_count);
            ret = encodeApi.nvEncGetInputFormats(nvencoder, guids[i], formats.data(),
                                                 format_count, &format_count);
            if (ret != NV_ENC_SUCCESS) {
                std::cerr << "fail to get input format" << std::endl;
                return;
            }
            std::cout << "supported input format: " << inputFormatsToString(formats) << std::endl;

            std::cout << std::endl;
        }

        encodeApi.nvEncDestroyEncoder(nvencoder);
    }
};

HALCODEC_CONNECT(CapabilityProvider, nvidia, NVCodecCapsProvider);

} // namespace nvenc
} // namespace halcodec