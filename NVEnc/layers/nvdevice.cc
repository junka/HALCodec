#include "nvdevice.h"
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

                        for (int i = 0; i <= static_cast<int>(cudaVideoSurfaceFormat_P216); i++) {
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

static bool registered = []() -> bool {
    NVDevice::Register();
    return true;
}();

} // namespace nvenc
} // namespace halcodec