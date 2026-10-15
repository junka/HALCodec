#ifndef OUTPUT_PATHS_H
#define OUTPUT_PATHS_H

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// One buffer per live stream, installed before the file is opened: libc++ gives
// a filebuf 4096 bytes otherwise, which turns a multi-megabyte read or write
// into per-chunk work. Measured on an M5 with incompressible payloads, 622 MB of
// 3 MiB writes through one held std::ofstream takes 462.9 ms at the default and
// 46.3 ms with a 4 MiB buffer (1.34 GB/s against 13.4 GB/s); the curve is flat
// from 1 MiB on and drifts slightly back the other way by 16 MiB. The SSD is
// not what separates either end of that range, and a 1080p NV12 frame is exactly
// that 3 MiB.
constexpr size_t kIoBufBytes = 4u << 20;

// A fresh stream per file. The buffer has to be installed before the first open
// and it has to outlive the stream, and a libc++ filebuf that has been closed
// and opened again permanently loses its bulk write path -- so reopening the
// same object is not an option, and this returns a constructed-and-buffered
// stream to move-assign over the old one. Measured on the same machine: 100
// 1080p q90 JPEGs take 254 ms through a fresh stream per picture and 532 ms
// through one reopened stream.
template <class Stream>
Stream OpenBuffered(const std::string& path, std::ios::openmode mode,
                    std::vector<char>& buf) {
    Stream s;
    s.rdbuf()->pubsetbuf(buf.data(), static_cast<std::streamsize>(buf.size()));
    s.open(path, mode);
    return s;
}

// Everything up to the last dot, but only when that dot is inside the final
// path component -- "in.put/f0" has no extension to strip.
inline std::string StripExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return path;
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && dot < slash) {
        return path;
    }
    return path.substr(0, dot);
}

// The last dot and everything after it, including the dot; "" when the path has
// no extension to keep. The pair with StripExt: StripExt(p) + ExtOf(p) == p.
inline std::string ExtOf(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return std::string();
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && dot < slash) {
        return std::string();
    }
    return path.substr(dot);
}

// -o names a directory when the input is one, because each picture needs a file
// of its own. Create it if it is missing so `-o out/` works on a clean tree.
inline bool EnsureDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (st.st_mode & S_IFDIR) {
            return true;
        }
        std::cerr << "-o " << path << " is a file; with a directory input it "
                  << "names the output directory" << std::endl;
        return false;
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        if (!EnsureDir(path.substr(0, slash))) {
            return false;
        }
    }
    if (mkdir(path.c_str(), 0755) != 0) {
        std::cerr << "Cannot create output directory " << path << std::endl;
        return false;
    }
    return true;
}

// Drop trailing slashes, but never the final character: a lone "/" stays "/",
// and every real path keeps at least its first byte. hal_dec and hal_enc both
// trimmed `-i`/`-o` directory arguments by hand before joining names onto them.
inline std::string TrimTrailingSlashes(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    return p;
}

// The part of `path` after its final slash (the whole string when there is
// none).
inline std::string BasenameOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The regular files directly inside `dirPath` (not recursive), each spelled
// `<dirPath>/<name>`, in readdir order -- the order hal_dec and hal_enc both
// fed and named their pictures in, so directory runs stay reproducible
// relative to what the filesystem reports. False when opendir fails; *out is
// still emptied first either way.
inline bool ListRegularFiles(const std::string& dirPath,
                             std::vector<std::string>* out) {
    out->clear();
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type == DT_REG) {
            out->push_back(dirPath + '/' + ent->d_name);
        }
    }
    closedir(dir);
    return true;
}

#endif  // OUTPUT_PATHS_H
