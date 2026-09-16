#include "nvmediaencoder.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "registry.h"

namespace halcodec {
namespace nvmedia {

bool NvMediaEncoder::Initialize(const CodecParams& params) {
    (void)params;
    // GetVersion -> CreateCtx (no args), mirroring image_encoder.c. The
    // CreateCtx/Init pair is only available in non-safety builds
    // (#if !NV_IS_SAFETY in nvmedia_iep.h), which is the default here.
    NvMediaVersion version{};
    if (NvMediaIEPGetVersion(&version) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPGetVersion failed" << std::endl;
        return false;
    }

    encoder_ = NvMediaIEPCreateCtx();
    if (!encoder_) {
        std::cerr << "NvMediaEncoder: NvMediaIEPCreateCtx failed" << std::endl;
        return false;
    }

    // Real init TODO: build a reconciled NvSciBufAttrList for the input
    // surface plus a codec-specific init param struct, then call
    //   NvMediaIEPInit(encoder_, NVMEDIA_IMAGE_ENCODE_H264, &initParams,
    //                  bufReconciledList, maxBuffering, instanceId);
    // (see samples/nvmedia_6x/iep/image_encoder.c).
    std::cout << "NvMediaEncoder: IEP instance created (stub)" << std::endl;
    return true;
}

bool NvMediaEncoder::FillFrame(const CodecFrame&) {
    // Data-path TODO: wrap `in` into an NvSciBufObj surface and submit with
    // NvMediaIEPFeedFrame.
    return false;
}

bool NvMediaEncoder::GetFrame(CodecFrame&) {
    // Data-path TODO: drain encoded bitstream via NvMediaIEPGetBits.
    return false;
}

void NvMediaEncoder::Finalize() {
    if (encoder_) {
        NvMediaIEPDestroy(encoder_);
        encoder_ = nullptr;
    }
}

std::string NvMediaEncoder::getName() const {
    return "nvmedia";
}

HALCODEC_CONNECT(Encoder, nvmedia, NvMediaEncoder);

} // namespace nvmedia
} // namespace halcodec