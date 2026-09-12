#ifndef SRC_FRAME_H
#define SRC_FRAME_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace halcodec {

enum class PixelFormat {
    Unknown = 0,
    NV12,          // 8-bit YUV 4:2:0, semi-planar
    P010,          // 10-bit YUV 4:2:0 (16-bit storage), semi-planar
    I420,          // 8-bit YUV 4:2:0, planar
    P016,          // 12-bit YUV 4:2:0 (16-bit storage), semi-planar
    NV16,          // 8-bit YUV 4:2:2, semi-planar
    P210,          // 10-bit YUV 4:2:2 (16-bit storage), semi-planar
    YUV444P,       // 8-bit YUV 4:4:4, planar
    YUV444P10LE,   // 10-bit planar 4:4:4, little endian
    RGB,
    BGR,
    GRAY,          // 8-bit single plane, greyscale (nvjpeg "y" output)
    BGRA,
    ARGB,
    RGBA,
};

// Unified frame container exchanged between the HAL interface and the
// vendor adaptation layers. The frame object is owned by the caller (stack
// or container); the underlying buffer pointed to by `data` is allocated by
// the adaptation layer and must be released via `release` when done. An
// empty `release` means the buffer is managed by the framework and needs no
// explicit cleanup.
struct CodecFrame {
    uint8_t* data = nullptr;       // first byte of the frame buffer
    size_t size = 0;               // total bytes across all planes
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    std::array<size_t, 4> strides{}; // row stride per plane (0 = tightly packed)
    int64_t pts = 0;

    // Releases the buffer pointed to by `data`. May capture the pointer by
    // value; do not reference `this` unless the frame struct outlives the call.
    std::function<void()> release;
};

} // namespace halcodec

#endif // SRC_FRAME_H