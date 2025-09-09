#ifndef ENCODER_H
#define ENCODER_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>

namespace halcodec {

class Encoder {
public:
    Encoder() = default;
    virtual ~Encoder() = default;
    using Creator = std::function<std::unique_ptr<Encoder>()>;
    static std::unordered_map<std::string, Encoder::Creator>& getRegistry() {
        static std::unordered_map<std::string, Encoder::Creator> registry;
        return registry;
    }
    // Factory method to create a Decoder instance
    static std::unique_ptr<Encoder> Create(const std::string& type) {
        auto& registry = getRegistry();
        auto it = registry.find(type);
        if (it != registry.end()) {
            return it->second();
        } else {
            return nullptr;
        }
    }

    static void RegisterEncoder(const std::string& type, Creator creator) {
        getRegistry()[type] = creator;
    }

    // Initialize the encoder with specific settings
    virtual void Initialize(std::string input, std::string format) {}
    // Finalize the encoding process
    virtual void Finalize() {}

    // Encode raw input data and return encoded output
    virtual int FillData() { return 0;}
    virtual uint8_t* GetFrame(int *framesize, int *height, int *width, int *n_chan) { return nullptr;}
    virtual void ReleaseFrame(uint8_t **pFrame) {}


    // Get the name of the encoder
    virtual std::string getName() const { return ""; }
};

} // namespace halcodec

#endif // ENCODER_H