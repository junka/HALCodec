#ifndef QSV_LAYERS_QSVDECODER_H
#define QSV_LAYERS_QSVDECODER_H

#include <string>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

namespace halcodec {
namespace qsv {

class QSVDecoder : public halcodec::Decoder {
public:
    QSVDecoder() = default;

    bool Initialize(const CodecParams& params) override;
    // Async path (isAsync() == true): the caller feeds Annex-B chunks via
    // FillInput(), signals EOF, then drains frames with GetFrame() which
    // blocks until a frame is ready or the stream ends.
    int FillInput(const uint8_t* data, size_t size) override;
    bool SignalInputComplete() override;
    bool isAsync() const override { return true; }
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    std::string getName() const override;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace qsv
} // namespace halcodec

#endif // QSV_LAYERS_QSVDECODER_H