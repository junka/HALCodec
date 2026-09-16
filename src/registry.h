#ifndef SRC_REGISTRY_H
#define SRC_REGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace halcodec {

namespace detail {

// Type-erased factory entry. `make` returns a heap-allocated T as void*; the
// Registry<T> wrapper rehydrates it into std::unique_ptr<T>.
struct RegistryEntry {
    std::function<void*()> make; // returns a unique_ptr<T> as void*
};

// Exported from the core library (registry.cc). One process-wide storage
// shared across all .so's (linked or dlopen'd) so a backend registered at
// runtime is visible to the executable. Keyed by the type_info name of the
// Base class — stable process-wide under RTLD_GLOBAL, unlike an inline
// static address which would differ per .so.
std::unordered_map<std::string, RegistryEntry>& registry_storage(const char* key);

} // namespace detail

// Generic factory registry shared by Encoder/Decoder/CapabilityProvider.
// Implementations self-register by name at static-init time; callers
// instantiate via Create(type).
//
// The underlying map lives in the core library via registry_storage(), keyed
// by typeid(T).name() — stable process-wide so every .so agrees on the key.
// This guarantees a single process-wide instance regardless of whether a
// backend is linked at load time or dlopen'd later — the whole point of the
// plugin loader. No template specialization, no instantiation-order pitfalls.
template <class T>
class Registry {
public:
    using Creator = std::function<std::unique_ptr<T>()>;
    using Map = std::unordered_map<std::string, Creator>;

    static std::unique_ptr<T> Create(const std::string& type) {
        auto& storage = detail::registry_storage(typeid(T).name());
        auto it = storage.find(type);
        if (it == storage.end()) return nullptr;
        return std::unique_ptr<T>(static_cast<T*>(it->second.make()));
    }
    static bool Register(const std::string& type, Creator creator) {
        // Wrap the typed Creator into a void*-returning function so it can
        // live in the type-erased shared storage.
        detail::registry_storage(typeid(T).name())[type] =
            detail::RegistryEntry{
                [c = std::move(creator)]() -> void* { return c().release(); }};
        return true;
    }
    static std::vector<std::string> Names() {
        std::vector<std::string> names;
        auto& storage = detail::registry_storage(typeid(T).name());
        names.reserve(storage.size());
        for (const auto& entry : storage) {
            names.push_back(entry.first);
        }
        return names;
    }
};

} // namespace halcodec

// Self-register a concrete codec/device class into the halcodec::Registry
// under Base (Encoder/Decoder/CapabilityProvider) with the given name. Place
// at file scope of the implementation .cc; the static initializer runs when
// the shared library is dlopen'd (or linked at load time).
#define HALCODEC_CONNECT(Base, Name, Class, ...)                        \
    static bool halco_##Name =                                          \
        ::halcodec::Registry<::halcodec::Base>::Register(               \
            #Name, []() { return std::make_unique<Class>(__VA_ARGS__); }); \
    static_assert(true, "")

#endif // SRC_REGISTRY_H
