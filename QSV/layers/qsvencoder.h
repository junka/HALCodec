#ifndef QSV_LAYERS_QSVENCODER_H
#define QSV_LAYERS_QSVENCODER_H

#include <string>

#include "codec_config.h"
#include "encoder.h"
#include "frame.h"

namespace halcodec {
namespace qsv {

class QSVEncoder : public halcodec::Encoder {
public:
    QSVEncoder() = default;

    bool Initialize(const CodecParams& params) override;
    bool FillFrame(const CodecFrame& input) override;
    bool GetFrame(CodecFrame& out) override;
    void Finalize() override;
    std::string getName() const override;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace qsv
} // namespace halcodec

#endif // QSV_LAYERS_QSVENCODER_H