#ifndef DECODER_H
#define DECODER_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>

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

    virtual void Initialize() {}

    // Fill input data and return  output
    virtual void FillinFrame(const std::vector<uint8_t>& rawData) {}

    // Finalize the encoding process
    virtual void Finalize() {}

    // Get the name of the encoder
    virtual std::string getName() const {return "";}

    virtual void GetFrame() {}
};

} // namespace halcodec

#endif // DECODER_H