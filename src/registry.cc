// Single process-wide Registry storage, shared across all shared objects so a
// backend dlopen'd at runtime writes into the same map the executable reads.
// See registry.h for the rationale and the type-erasure design.

#include "registry.h"

#include <mutex>
#include <unordered_map>

namespace halcodec {
namespace detail {

// One exported function; see registry.h. The storage is keyed by a string and
// holds a void* per key, so every SharedTable<T> in the process -- whatever
// .so's template instantiation asked for it -- resolves to the same table.
// Deliberately leaked: a process-wide registry must outlive static destruction
// (backends unregister from their own destructors, which may run after this
// function's statics would have been torn down).
__attribute__((visibility("default")))
void* shared_object_storage(const char* key, void* (*create)()) {
    static std::mutex mu;
    static std::unordered_map<std::string, void*> objects;
    std::lock_guard<std::mutex> lk(mu);
    void*& slot = objects[key];
    if (!slot) slot = create();
    return slot;
}

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
