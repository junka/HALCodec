// Single process-wide Registry storage, shared across all shared objects so a
// backend dlopen'd at runtime writes into the same map the executable reads.
// See registry.h for the rationale and the type-erasure design.

#include "registry.h"

#include <unordered_map>

namespace halcodec {
namespace detail {

// One exported function. Each Registry<T> passes typeid(T).name() as the key;
// we maintain a separate inner map per Base type. Because this symbol is
// exported from the core library and resolved by the dynamic linker in every
// TU, all backends share the same storage. typeid().name() is stable
// process-wide (RTLD_GLOBAL merges the type_info), unlike an inline static
// address which would differ per .so.
__attribute__((visibility("default")))
std::unordered_map<std::string, RegistryEntry>& registry_storage(const char* key) {
    static std::unordered_map<std::string, std::unordered_map<std::string, RegistryEntry>> storage;
    return storage[key];
}

} // namespace detail
} // namespace halcodec
