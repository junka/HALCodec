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