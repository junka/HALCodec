#ifndef SRC_REGISTRY_H
#define SRC_REGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace halcodec {

// Generic factory registry shared by Device/Encoder/Decoder.
// Implementations self-register by name at static-init time; callers
// instantiate via Create(type).
template <class T>
class Registry {
public:
    using Creator = std::function<std::unique_ptr<T>()>;
    using Map = std::unordered_map<std::string, Creator>;

    static Map& get() {
        static Map registry;
        return registry;
    }

    static std::unique_ptr<T> Create(const std::string& type) {
        auto it = get().find(type);
        return it == get().end() ? nullptr : it->second();
    }

    static bool Register(const std::string& type, Creator creator) {
        get()[type] = std::move(creator);
        return true;
    }

    static std::vector<std::string> Names() {
        std::vector<std::string> names;
        names.reserve(get().size());
        for (const auto& entry : get()) {
            names.push_back(entry.first);
        }
        return names;
    }
};

} // namespace halcodec

// Self-register a concrete codec/device class into the halcodec::Registry
// under Base (Device/Encoder/Decoder) with the given name. Place at file
// scope of the implementation .cc; the static initializer force-loads the
// registration when the shared library is linked (see app CMake --no-as-needed).
#define HALCODEC_CONNECT(Base, Name, Class, ...)                        \
    static bool halco_##Name =                                          \
        ::halcodec::Registry<::halcodec::Base>::Register(               \
            #Name, []() { return std::make_unique<Class>(__VA_ARGS__); }); \
    static_assert(true, "")

#endif // SRC_REGISTRY_H