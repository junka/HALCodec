#include "nvdecoder.h"

#include "nvdevice.h"

#include <memory>

namespace halcodec {
namespace nvenc {

void NVDecoder::Initialize(std::string inputfile) {
    device_ = std::make_unique<NVDevice>();
    if (!device_) {
        std::cout << "unable to create device" << std::endl;
        return;
    }
    device_->createCtx(0);
    auto cudaCtx = device_->getCtx();

    Rect cropRect = {};
    Dim resizeDim = {};

    demuxer_ = std::make_unique<FFmpegDemuxer>(inputfile.c_str());
#if NVENCAPI_MAJOR_VERSION > 12
    decoder_ = std::make_unique<NvDecoder>(cudaCtx, false, FFmpeg2NvCodecId(demuxer_->GetVideoCodec()),
        false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false, 0, nullptr);
#else
decoder_ = std::make_unique<NvDecoder>(cudaCtx, false, FFmpeg2NvCodecId(demuxer_->GetVideoCodec()),
false, false, &cropRect, &resizeDim, false, 0, 0, 1000, false);
#endif
    decoder_->SetOperatingPoint(0, false);

}

int NVDecoder::FillinFrame() {
    uint8_t *pVideo = nullptr;
    int nVideoBytes = 0;
    int nFrame = 0;
    do {
        demuxer_->Demux(&pVideo, &nVideoBytes);
        nFrame = decoder_->Decode(pVideo, nVideoBytes);
    } while (nFrame == 0 && nVideoBytes > 0);
    return nFrame;
}

void NVDecoder::Finalize() {
    std::cout << decoder_->GetVideoInfo();
    decoder_ = nullptr;
    demuxer_ = nullptr;
}

std::string NVDecoder::getName() const {
    return "nvdec";
}

uint8_t* NVDecoder::GetFrame(int *framesize) {
    uint8_t* frame = decoder_->GetLockedFrame();
    *framesize = decoder_->GetFrameSize();
    auto outFormat = decoder_->GetOutputFormat();
    return frame;
}

void NVDecoder::ReleaseFrame(uint8_t **pFrame) {
    decoder_->UnlockFrame(pFrame);
}

static bool registered = []() -> bool {
    NVDecoder::Register();
    return true;
}();

} // namespace layers
} // namespace nvenc
