#ifndef DECODER_H
#define DECODER_H

#include <string>
#include <vector>
#include <cstdint>

namespace halcodec {

class Decoder {
public:
    Decoder() = default;
    virtual ~Decoder() = default;

    // Initialize the encoder with specific settings
    virtual bool Initialize(const std::string& config) = 0;

    // Fill input data and return  output
    virtual void FillinFrame(const std::vector<uint8_t>& rawData) = 0;

    // Finalize the encoding process
    virtual void Finalize() = 0;

    // Get the name of the encoder
    virtual std::string getName() const = 0;

    virtual void GetFrame() = 0;
};

} // namespace halcodec

#endif // DECODER_H