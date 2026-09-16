#ifndef SRC_PLUGIN_LOADER_H
#define SRC_PLUGIN_LOADER_H

// Runtime backend loader. Vendor backends (nvenc_layers, nvjpeg_layers,
// amf_layers, qsv_layers, vtbox_layers) are shared libraries that
// self-register their Encoder/Decoder/CapabilityProvider via HALCODEC_CONNECT
// static initializers. Instead of link-time binding the .so's into every
// executable (which forces the dynamic loader to resolve their NEEDED entries
// — e.g. libnvidia-encode.so.1 — at process start, making the binary unusable
// on machines lacking that driver), we dlopen each backend lazily at startup.
//
// A backend whose dependencies are missing simply fails to dlopen and is
// skipped with a warning; the remaining backends still load. So a machine with
// only an Intel GPU runs codecinfo/hal_dec against qsv while nvenc/nvjpeg are
// silently absent.
//
// Search order for the backend directory:
//   1. $HALCODEC_BACKEND_DIR
//   2. the directory of the running executable (build-tree friendly)
//   3. <exe-dir>/../lib/halcodec (install-tree friendly)

#include <dlfcn.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace halcodec {

inline std::string ExeDir() {
    char link[4096] = {0};
    ssize_t n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n <= 0) return ".";
    std::string path(link, static_cast<size_t>(n));
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

inline bool EndsWithLayersSo(const char* name) {
    if (!name) return false;
    size_t n = std::strlen(name);
    const char* suffix = ".so";
    size_t s = std::strlen(suffix);
    if (n < s) return false;
    // accept both lib*_layers.so and *_layers.so
    if (std::strcmp(name + n - s, suffix) != 0) return false;
    return std::strstr(name, "_layers") != nullptr;
}

// Load every lib*_layers.so found in the resolved backend directory.
// Returns the number of backends successfully loaded. Logs a warning per
// failure but never aborts.
inline int LoadBackends(const std::string& explicitDir = "") {
    std::vector<std::string> candidates;
    if (!explicitDir.empty()) {
        candidates.push_back(explicitDir);
    }
    const char* env = std::getenv("HALCODEC_BACKEND_DIR");
    if (env && *env) candidates.push_back(env);
    candidates.push_back(ExeDir());
    candidates.push_back(ExeDir() + "/../lib/halcodec");

    std::string dir;
    for (const auto& c : candidates) {
        struct stat st;
        if (::stat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            dir = c;
            break;
        }
    }
    if (dir.empty()) {
        std::cerr << "LoadBackends: no backend directory found" << std::endl;
        return 0;
    }

    DIR* d = ::opendir(dir.c_str());
    if (!d) {
        std::cerr << "LoadBackends: cannot open " << dir << std::endl;
        return 0;
    }

    int loaded = 0;
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        if (!EndsWithLayersSo(ent->d_name)) continue;
        std::string full = dir + "/" + ent->d_name;
        // RTLD_GLOBAL so the backend's symbols are visible to other backends
        // and to the executable's Registry lookups. RTLD_NOW surfaces missing
        // dependencies immediately (and lets us skip cleanly).
        void* h = ::dlopen(full.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            std::cerr << "LoadBackends: skip " << ent->d_name
                      << " (" << ::dlerror() << ")" << std::endl;
            continue;
        }
        ++loaded;
    }
    ::closedir(d);
    return loaded;
}

} // namespace halcodec

#endif // SRC_PLUGIN_LOADER_H
