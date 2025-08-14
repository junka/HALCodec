#ifndef ENCODER_H
#define ENCODER_H

#include <string>
#include <vector>
#include <cstdint>

namespace halcodec {

class Encoder {
public:
    Encoder() = default;
    virtual ~Encoder() = default;

    // Initialize the encoder with specific settings
    virtual void Initialize() {}

    // Encode raw input data and return encoded output
    virtual void FillData(const std::vector<uint8_t>& rawData) {}

    virtual void GetFrame() {}

    // Finalize the encoding process
    virtual void finalize() {}

    // Get the name of the encoder
    virtual std::string getName() const { return ""; }
};

} // namespace halcodec

#endif // ENCODER_H