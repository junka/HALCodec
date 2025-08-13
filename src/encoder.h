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
    virtual bool initialize(const std::string& config) = 0;

    // Encode raw input data and return encoded output
    virtual std::vector<uint8_t> encode(const std::vector<uint8_t>& rawData) = 0;

    // Finalize the encoding process
    virtual void finalize() = 0;

    // Get the name of the encoder
    virtual std::string getName() const = 0;
};

} // namespace halcodec

#endif // ENCODER_H