#ifndef DECODER_H
#define DECODER_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>
#include <iostream>

namespace halcodec {
    
class Decoder {
protected:
    Decoder() = default;
public:
    virtual ~Decoder() = default;

    using Creator = std::function<std::unique_ptr<Decoder>()>;
    static std::unordered_map<std::string, Decoder::Creator>& getRegistry() {
        static std::unordered_map<std::string, Decoder::Creator> registry;
        return registry;
    }
    // Factory method to create a Decoder instance
    static std::unique_ptr<Decoder> Create(const std::string& type) {
        auto& registry = getRegistry();
        auto it = registry.find(type);
        if (it != registry.end()) {
            return it->second();
        } else {
            return nullptr;
        }
    }

    static void RegisterDecoder(const std::string& type, Creator creator) {
        getRegistry()[type] = creator;
    }

    virtual void Initialize(std::string input) {}

    // Fill input data and return  output
    virtual int FillinFrame() { return -1; }

    // Finalize the encoding process
    virtual void Finalize() {}

    // Get the name of the encoder
    virtual std::string getName() const {return "";}

    virtual uint8_t* GetFrame(int *framesize) {return nullptr;}

    virtual void ReleaseFrame(uint8_t **pFrame) {}
};

} // namespace halcodec

#endif // DECODER_H