#include "nvdevice.h"
#include <cstring>

namespace halcodec {
namespace nvenc {

void NVDevice::createCudaContext(int idx, unsigned int flags)
{
    int ret = cuDeviceGet(&cuDevice_, idx);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGet error" << std::endl;
        return;
    }
    char szDeviceName[80];
    ret = cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGetName error" << std::endl;
        return;
    }
    name_ = std::string(szDeviceName);
    ret = cuCtxCreate(&cuContext_, flags, cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuCtxCreate error" << std::endl;
        return;
    }
}

void NVDevice::destroyCudaContext()
{
    if (cuContext_) {
        CUresult ret = cuCtxDestroy(cuContext_);
        if (ret != CUDA_SUCCESS) {
            std::cout << "cuCtxDestroy error" << std::endl;
        }
        cuContext_ = nullptr;
    }
}
#if NVENCAPI_MAJOR_VERSION > 12
#define NUM_VIDEO_SURFACE_FORMAT 6
#elif NVENCAPI_MAJOR_VERSION == 12
#define NUM_VIDEO_SURFACE_FORMAT 4
#endif
void NVDevice::showDecoderCapability()
{
    std::cout << std::left << std::setw(7) << "Codec" <<
        std::setw(12) << "BitDepth  " <<
        std::setw(16) << "ChromaFormat  " <<
        std::setw(12) << "MaxWidth  " <<
        std::setw(12) << "MaxHeight  " <<
        std::setw(12) << "MaxMBCount  " <<
        std::setw(12) << "MinWidth  " <<
        std::setw(12) << "MinHeight  " <<
        std::setw(16) << "SurfaceFormat  " << std::endl;

    for (int codec = 0; codec < static_cast<int>(cudaVideoCodec_NumCodecs); codec ++) {
        for (int format = 0; format <= static_cast<int>(cudaVideoChromaFormat_444); format ++) {
            for (int bits = 0; bits <= 4; bits += 2) {
                CUVIDDECODECAPS decodeCaps = {};
                decodeCaps.eCodecType = static_cast<cudaVideoCodec>(codec);
                decodeCaps.eChromaFormat = static_cast<cudaVideoChromaFormat>(format);
                decodeCaps.nBitDepthMinus8 = bits;

                cuvidGetDecoderCaps(&decodeCaps);

                if (decodeCaps.bIsSupported) {
                    std::string ouput = [](unsigned short nOutputFormatMask) -> std::string {
                        if (nOutputFormatMask == 0) {
                            return "N/A";
                        }
                        std::string formatNames[] = {
                            "NV12", "P016", "YUV444", "YUV444P16", "NV16", "P216"
                        };
                        std::string outputFormats;

                        for (int i = 0; i <= NUM_VIDEO_SURFACE_FORMAT; i++) {

                            if (nOutputFormatMask & (1U << i)) {
                                if (!outputFormats.empty()) {
                                    outputFormats += ", ";
                                }
                                outputFormats += formatNames[i];
                            }
                        }

                        return outputFormats;
                    }(decodeCaps.nOutputFormatMask);
                    std::cout << std::left << std::setw(7) << kCodecNames[codec] <<
                        std::setw(12) << decodeCaps.nBitDepthMinus8 + 8 <<
                        std::setw(16) << kChromaFormat[decodeCaps.eChromaFormat] <<
                        std::setw(12) << decodeCaps.nMaxWidth <<
                        std::setw(12) << decodeCaps.nMaxHeight <<
                        std::setw(12) << decodeCaps.nMaxMBCount <<
                        std::setw(12) << decodeCaps.nMinWidth <<
                        std::setw(12) << decodeCaps.nMinHeight <<
                        std::setw(16) << ouput << std::endl;
                }
            }
        }
    }
}

void NVDevice::showEncoderCapability() {
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
    
    encode_api_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    ret = NvEncodeAPICreateInstance(&encode_api_);
    if (ret != NV_ENC_SUCCESS) {
        std::cerr << "Fail to create instance" << std::endl;
        return;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };
    params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.apiVersion = NVENCAPI_VERSION;
    params.device     = cuContext_;
    params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    void *nvencoder;

    ret = encode_api_.nvEncOpenEncodeSessionEx(&params, &nvencoder);
    if (ret != NV_ENC_SUCCESS) {
        std::cerr << "Fail to open encode session " << ret << std::endl;
        return;
    }

    uint32_t count;
    ret = encode_api_.nvEncGetEncodeGUIDCount(nvencoder, &count);
    if (ret != NV_ENC_SUCCESS) {
        std::cerr << "Fail to get guid count" << std::endl;
        return;
    } 

    std::vector<GUID> guids(count);
    ret = encode_api_.nvEncGetEncodeGUIDs(nvencoder, guids.data(), count, &count);
    if (ret != NV_ENC_SUCCESS) {
        return;
    }

    const char *caps_str[] = {
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
    };
    for (int i = 0; i < count; i++) {
        auto codecname = [](GUID guid) {
            if (std::memcmp(&guid, &NV_ENC_CODEC_H264_GUID, 16) == 0) {
                return "H264";
            } else if (std::memcmp(&guid, &NV_ENC_CODEC_HEVC_GUID, 16) == 0) {
                return "HEVC";
#if NVENCAPI_MAJOR_VERSION > 11
            } else if (std::memcmp(&guid, &NV_ENC_CODEC_AV1_GUID, 16) == 0) {
                return "AV1";
#endif
            } else {
                return "Unknown";
            }
        }(guids[i]);
        std::cout << codecname << ":" << std::endl;
        for (int cap = 0; cap < NV_ENC_CAPS_EXPOSED_COUNT; cap ++) {
            NV_ENC_CAPS_PARAM params = { 0 };
            int val = 0;
            params.version = NV_ENC_CAPS_PARAM_VER;
            params.capsToQuery = static_cast<NV_ENC_CAPS>(cap);
            encode_api_.nvEncGetEncodeCaps(nvencoder, guids[i], &params, &val);
            if (val != 0) {
                std::cout << caps_str[cap] << ": " << val << std::endl;
            }
        }
        uint32_t preset_count;
        std::vector<GUID> presetGUID;
        ret = encode_api_.nvEncGetEncodePresetCount(nvencoder, guids[i], &preset_count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "fail to get encoder prest count" << std::endl;
            return;
        }

        presetGUID.resize(preset_count);
        ret = encode_api_.nvEncGetEncodePresetGUIDs(nvencoder, guids[i], presetGUID.data(), preset_count, &preset_count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "fail to get encoder prest" << std::endl;
            return;
        }

        auto supportpresets = [&nvencoder, this](GUID encodeGUID, std::vector<GUID> presets){
            std::string out;
            for (auto f: presets) {
                if (std::memcmp(&f, &NV_ENC_PRESET_P1_GUID, 16) == 0) {
                    out += "P1";
                } else if (std::memcmp(&f, &NV_ENC_PRESET_P2_GUID, 16) == 0) {
                    out += "P2";
                } else if (std::memcmp(&f, &NV_ENC_PRESET_P3_GUID, 16) == 0) {
                    out += "P3";
                } else if (std::memcmp(&f, &NV_ENC_PRESET_P4_GUID, 16) == 0) {
                    out += "P4";
                } else if (std::memcmp(&f, &NV_ENC_PRESET_P5_GUID, 16) == 0) {
                    out += "P5";
                } else if (std::memcmp(&f, &NV_ENC_PRESET_P6_GUID, 16) == 0) {
                    out += "P6";
                } else if (std::memcmp(&f, &NV_ENC_PRESET_P7_GUID, 16) == 0) {
                    out += "P7";
                } else {
                    out += "Unknown";
                }
                NV_ENC_PRESET_CONFIG config;
                config.version = NV_ENC_PRESET_CONFIG_VER;
                auto getRcmode = [](NV_ENC_PRESET_CONFIG &config) -> std::string {
                    switch (config.presetCfg.rcParams.rateControlMode) {
                        case NV_ENC_PARAMS_RC_CONSTQP:
                            return "CONSTQP";
                        case NV_ENC_PARAMS_RC_VBR:
                            return "VBR";
                        case NV_ENC_PARAMS_RC_CBR:
                            return "CBR";
                        default:
                            return "Unknown";
                    }
                };
                encode_api_.nvEncGetEncodePresetConfigEx(nvencoder, encodeGUID, f, NV_ENC_TUNING_INFO_HIGH_QUALITY, &config);
                out += " HIGH_QUALITY (" + getRcmode(config) + ") ";
                encode_api_.nvEncGetEncodePresetConfigEx(nvencoder, encodeGUID, f, NV_ENC_TUNING_INFO_LOW_LATENCY, &config);
                out += " LOW_LATENCY (" + getRcmode(config) + ") ";
                encode_api_.nvEncGetEncodePresetConfigEx(nvencoder, encodeGUID, f, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &config);
                out += " ULTRA_LOW_LATENCY (" + getRcmode(config) + ") ";
                encode_api_.nvEncGetEncodePresetConfigEx(nvencoder, encodeGUID, f, NV_ENC_TUNING_INFO_LOSSLESS, &config);
                out += " LOSSLESS (" + getRcmode(config) + ") ";
#if NVENCAPI_MAJOR_VERSION > 12
                encode_api_.nvEncGetEncodePresetConfigEx(nvencoder, encodeGUID, f, NV_ENC_TUNING_INFO_ULTRA_HIGH_QUALITY, &config);
                out += " ULTRA_HIGH_QUALITY (" + getRcmode(config) + ") ";
#endif
                out += "\n";
            }
            return out;
        } (guids[i], presetGUID);
        std::cout << "supported presets:" << std::endl << supportpresets << std::endl;

        uint32_t profile_count;
        ret = encode_api_.nvEncGetEncodeProfileGUIDCount(nvencoder, guids[i], &profile_count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "fail to get encoder profile count" << std::endl;
            return;
        }
        std::vector<GUID> profileGUID(profile_count);
        ret = encode_api_.nvEncGetEncodeProfileGUIDs(nvencoder, guids[i], profileGUID.data(), profile_count, &profile_count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "fail to get encoder profile" << std::endl;
            return;
        }
        auto supportprofiles = [](std::vector<GUID> profiles){
            std::string out;
            for (auto f: profiles) {
                if (std::memcmp(&f, &NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID, 16) == 0) {
                    out += "Auto Select";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_BASELINE_GUID, 16) == 0) {
                    out += "H264 Baseline";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_MAIN_GUID, 16) == 0) {
                    out += "H264 Main";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_HIGH_GUID, 16) == 0) {
                    out += "H264 High";
                }
#if NVENCAPI_MAJOR_VERSION > 12
                else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_HIGH_10_GUID, 16) == 0) {
                    out += "H264 High 10";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_HIGH_422_GUID, 16) == 0) {
                    out += "H264 High 422";
                }
#endif
                else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_HIGH_444_GUID, 16) == 0) {
                    out += "H264 High 444";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_STEREO_GUID, 16) == 0) {
                    out += "H264 Stereo";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID, 16) == 0) {
                    out += "H264 Progressive High";
                } else if (std::memcmp(&f, &NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID, 16) == 0) {
                    out += "H264 Contrained High";
                } else if (std::memcmp(&f, &NV_ENC_HEVC_PROFILE_MAIN_GUID, 16) == 0) {
                    out += "HEVC Main";
                } else if (std::memcmp(&f, &NV_ENC_HEVC_PROFILE_MAIN10_GUID, 16) == 0) {
                    out += "HEVC Main 10";
                } else if (std::memcmp(&f, &NV_ENC_HEVC_PROFILE_FREXT_GUID, 16) == 0) {
                    out += "HEVC FREXT";
                } else if (std::memcmp(&f, &NV_ENC_AV1_PROFILE_MAIN_GUID, 16) == 0) {
                    out += "AV1 Main";
                }
                out += " ";
            }
            return out;
        }(profileGUID);
        std::cout << "supported profiles: " << supportprofiles << std::endl;

        uint32_t format_count;
        ret = encode_api_.nvEncGetInputFormatCount(nvencoder, guids[i], &format_count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "fail to get input format count" << std::endl;
            return;
        }
        std::vector<NV_ENC_BUFFER_FORMAT> formats(format_count);
        ret = encode_api_.nvEncGetInputFormats(nvencoder, guids[i], formats.data(), format_count, &format_count);
        if (ret != NV_ENC_SUCCESS) {
            std::cerr << "fail to get input format" << std::endl;
            return;
        }
        auto supportformat = [](std::vector<NV_ENC_BUFFER_FORMAT> formats){
            std::string out;
            for (auto f: formats) {
                if (f == NV_ENC_BUFFER_FORMAT_NV12) {
                    out += "NV12";
                } else if (f == NV_ENC_BUFFER_FORMAT_YV12) {
                    out += "YV12";
                } else if (f == NV_ENC_BUFFER_FORMAT_IYUV) {
                    out += "IYUV";
                } else if (f == NV_ENC_BUFFER_FORMAT_YUV444) {
                    out += "YUV444";
                } else if (f == NV_ENC_BUFFER_FORMAT_YUV420_10BIT) {
                    out += "YUV420_10bit";
                } else if (f == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) {
                    out += "YUV444_10bit";
                } else if (f == NV_ENC_BUFFER_FORMAT_ARGB) {
                    out += "ARGB";
                } else if (f == NV_ENC_BUFFER_FORMAT_ARGB10) {
                    out += "ARGB10";
                } else if (f == NV_ENC_BUFFER_FORMAT_AYUV) {
                    out += "AYUV";
                } else if (f == NV_ENC_BUFFER_FORMAT_ABGR) {
                    out += "ABGR";
                } else if (f == NV_ENC_BUFFER_FORMAT_ABGR10) {
                    out += "ABGR10";
                }
#if NVENCAPI_MAJOR_VERSION > 12
                else if (f == NV_ENC_BUFFER_FORMAT_NV16) {
                    out += "NV16";
                } else if (f == NV_ENC_BUFFER_FORMAT_P210) {
                    out += "P210";
                }
#endif
                out += " ";
            }
            return out;
        }(formats);
        std::cout << "supported input format: " << supportformat << std::endl;


        std::cout << std::endl;
    }

    encode_api_.nvEncDestroyEncoder(nvencoder);

}

static bool registered = []() -> bool {
    NVDevice::Register();
    return true;
}();

} // namespace nvenc
} // namespace halcodec