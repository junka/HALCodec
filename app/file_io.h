#ifndef APP_FILE_IO_H
#define APP_FILE_IO_H

// The whole-file read every app that feeds a codec itself needs. It was
// spelled out identically in hal_dec, hal_transcode and the multi-stream
// app before living here.

#include <fstream>
#include <string>
#include <vector>

namespace halcodec {

// Reads `path` fully into *buf. False when the file cannot be opened or the
// read comes up short; buf is left resized either way.
inline bool ReadFile(const std::string& path, std::vector<uint8_t>* buf) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    buf->resize(static_cast<size_t>(size));
    return static_cast<bool>(
        in.read(reinterpret_cast<char*>(buf->data()), size));
}

} // namespace halcodec

#endif // APP_FILE_IO_H
