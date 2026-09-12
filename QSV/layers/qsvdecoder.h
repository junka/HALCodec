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
    int FillinFrame() override;
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