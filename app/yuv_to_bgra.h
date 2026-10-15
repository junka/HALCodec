#ifndef APP_YUV_TO_BGRA_H
#define APP_YUV_TO_BGRA_H

// YUV (and Y-only) → BGRA, for the player's texture upload.
//
// This is hal_player's own consumer-side conversion, deliberately not part of
// the HAL and not in src/: the decoders hand over the layout the stream carries
// (see hal_dec's --format note -- only nvjpeg converts on request), so a player
// that uploads a BGRA texture is the one that has to know what it was given.
// The dispatch below covers every format the host-download paths can actually
// deliver:
//
//   semi-planar, 8-bit      NV12, NV16
//   semi-planar, 10/16-bit  P010, P016, P210      (top-aligned words, >>8)
//   planar, 8-bit           I420, YUV444P
//   planar, 10/16-bit       YUV444P10LE, YUV444P16LE  (>>8)
//   single plane            GRAY, GRAY10LE        (chroma taken as neutral)
//
// Not converted, and ConvertFrame() says so instead of uploading a misread
// picture: BAYER16LE (a sensel mosaic -- no colour without the `bayer` levels,
// and not a picture), the packed RGB/BGR/BGRA/ARGB/RGBA outputs (nvjpeg
// converts on request, so they do not reach a player that asks for NV12), and
// Unknown.
//
// Two conventions this file relies on, both of which the repo's decoders hold
// to:
//   - 10-bit samples sit in the TOP bits of their 16-bit word (frame.h's note on
//     P010/YUV444P16LE/GRAY10LE; VideoToolbox's UnpackPacked10Row left-shifts to
//     match; NVENC's pools are filled the same way). `v >> 8` therefore lands on
//     the 8-bit video range exactly: 512 (10-bit chroma neutral) → 128, 64 → 16,
//     940 → 235. The BT.601 integer arithmetic below needs no range change --
//     which is the whole reason one arithmetic core serves every format here.
//   - Planes are contiguous: interleaved chroma starts at luma + yStride*height,
//     and for planar formats a third plane follows the second. Every host
//     download path packs tight, so a plane offset carries no padding and the
//     only thing a stride describes is row padding.
//
// On P016 specifically: the vendored SDK documents it only as "16 bit
// Semi-Planar YUV" (cuviddec.h), with no word alignment -- unlike VT's packed-10
// path, which the decoder explicitly left-shifts. It is treated here as P010's
// storage with more bits (top-aligned, >>8), which is the convention cuvid's
// P016 output follows in practice, but that is an inference rather than
// something the headers state, and a 12-bit stream would settle it.
//
// A stride of 0 is CodecFrame's "tightly packed" sentinel, not a real stride.
// Forwarded verbatim it collapses every row onto row 0 (that is the bug that
// made the player show a flat tint), so the fallbacks below are what tight
// packing means for each plane and a frame with strides{} all zero still
// converts.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "frame.h"

namespace halplayer {

namespace detail {

// How the chroma of a format is laid out, and how to read one of its samples.
struct Layout {
    bool valid = false;
    bool interleaved = false;  // chroma in one plane, Cb and Cr alternating
    bool grey = false;         // no chroma at all
    int subX = 1;              // chroma horizontal subsample (1 = 4:4:4)
    int subY = 1;              // chroma vertical subsample (1 = 4:2:2/4:4:4)
    int bpp = 1;               // bytes per luma/chroma sample
    int shift = 0;             // right shift onto the 8-bit scale
};

inline Layout LayoutFor(halcodec::PixelFormat f) {
    Layout l;
    l.valid = true;
    switch (f) {
        case halcodec::PixelFormat::NV12:
            l.interleaved = true; l.subX = 2; l.subY = 2; break;
        case halcodec::PixelFormat::NV16:
            l.interleaved = true; l.subX = 2; l.subY = 1; break;
        case halcodec::PixelFormat::P010:
        case halcodec::PixelFormat::P016:
            l.interleaved = true; l.subX = 2; l.subY = 2; l.bpp = 2; l.shift = 8; break;
        case halcodec::PixelFormat::P210:
            l.interleaved = true; l.subX = 2; l.subY = 1; l.bpp = 2; l.shift = 8; break;
        case halcodec::PixelFormat::I420:
            l.subX = 2; l.subY = 2; break;
        case halcodec::PixelFormat::YUV444P:
            break;
        case halcodec::PixelFormat::YUV444P10LE:
        case halcodec::PixelFormat::YUV444P16LE:
            l.bpp = 2; l.shift = 8; break;
        case halcodec::PixelFormat::GRAY:
            l.grey = true; break;
        case halcodec::PixelFormat::GRAY10LE:
            l.grey = true; l.bpp = 2; l.shift = 8; break;
        default:
            l.valid = false; break;
    }
    return l;
}

// One sample, reduced to the 8-bit scale. 16-bit samples are little-endian and
// top-aligned (see the header note), so a plain >>8 is the reduction.
inline int Sample8(const uint8_t* p, int bpp, int shift) {
    if (bpp == 1) return p[0];
    uint16_t v = 0;
    memcpy(&v, p, sizeof(v));
    return v >> shift;
}

} // namespace detail

inline const char* FormatName(halcodec::PixelFormat format) {
    switch (format) {
        case halcodec::PixelFormat::NV12: return "NV12";
        case halcodec::PixelFormat::P010: return "P010";
        case halcodec::PixelFormat::I420: return "I420";
        case halcodec::PixelFormat::P016: return "P016";
        case halcodec::PixelFormat::NV16: return "NV16";
        case halcodec::PixelFormat::P210: return "P210";
        case halcodec::PixelFormat::YUV444P: return "YUV444P";
        case halcodec::PixelFormat::YUV444P10LE: return "YUV444P10LE";
        case halcodec::PixelFormat::YUV444P16LE: return "YUV444P16LE";
        case halcodec::PixelFormat::RGB: return "RGB";
        case halcodec::PixelFormat::BGR: return "BGR";
        case halcodec::PixelFormat::GRAY: return "GRAY";
        case halcodec::PixelFormat::GRAY10LE: return "GRAY10LE";
        case halcodec::PixelFormat::BGRA: return "BGRA";
        case halcodec::PixelFormat::ARGB: return "ARGB";
        case halcodec::PixelFormat::RGBA: return "RGBA";
        case halcodec::PixelFormat::BAYER16LE: return "BAYER16LE";
        case halcodec::PixelFormat::Unknown: break;
    }
    return "unknown";
}

// Fill `outBGRA` with width*height packed BGRA (row-major, A=255). Returns false
// for a format with no path here, or a frame with no host pixels -- the caller
// reports it rather than showing a texture it cannot vouch for.
//
// BT.601 limited range, integer form. The -128 is mandatory for chroma: it is
// stored unsigned with neutral 128 (512 in the 10-bit formats, which >>8 makes
// 128 again). Reading it as a signed byte is the bug that saturated the picture
// into colour blocks.
inline bool ConvertFrame(const halcodec::CodecFrame& frame, uint8_t* outBGRA) {
    using namespace detail;
    const Layout L = LayoutFor(frame.format);
    if (!L.valid || !frame.data || !outBGRA) return false;
    if (frame.width <= 0 || frame.height <= 0) return false;

    const int w = frame.width;
    const int h = frame.height;
    const int chromaH = h / L.subY;

    // Row strides, resolving the 0 sentinel to tight packing.
    const size_t yStride =
        frame.strides[0] ? frame.strides[0] : static_cast<size_t>(w) * L.bpp;
    // Chroma follows luma in every layout here.
    const uint8_t* chroma = frame.data + yStride * h;
    // Interleaved rows hold chromaW pairs (w*bpp bytes); planar rows hold
    // chromaW samples.
    const size_t tightChroma = L.interleaved
        ? static_cast<size_t>(w) * L.bpp
        : static_cast<size_t>(w / L.subX) * L.bpp;
    const size_t cStride = frame.strides[1] ? frame.strides[1] : tightChroma;

    for (int row = 0; row < h; row++) {
        const uint8_t* yRow = frame.data + static_cast<size_t>(row) * yStride;
        const uint8_t* cRow = chroma + static_cast<size_t>(row / L.subY) * cStride;
        uint8_t* outRow = outBGRA + static_cast<size_t>(row) * w * 4;

        // Cb and Cr row pointers, and the step between neighbouring chroma
        // samples on that row (one pair for interleaved, one plane apart for
        // planar). Both describe the same addressing in the column loop below.
        const uint8_t* uRow = nullptr;
        const uint8_t* vRow = nullptr;
        int cStep = 0;
        if (!L.grey) {
            if (L.interleaved) {
                uRow = cRow;
                vRow = cRow + L.bpp;
                cStep = 2 * L.bpp;
            } else {
                uRow = cRow;
                vRow = cRow + cStride * chromaH;
                cStep = L.bpp;
            }
        }

        for (int col = 0; col < w; col++) {
            const int Yv = Sample8(yRow + static_cast<size_t>(col) * L.bpp,
                                   L.bpp, L.shift);
            int U = 0, V = 0;
            if (!L.grey) {
                const size_t off = static_cast<size_t>(col / L.subX) * cStep;
                U = Sample8(uRow + off, L.bpp, L.shift) - 128;
                V = Sample8(vRow + off, L.bpp, L.shift) - 128;
            }
            // R = Y + 1.402*V, G = Y - 0.34414*U - 0.71414*V, B = Y + 1.772*U.
            int R = Yv + (141 * V) / 100;
            int G = Yv - (34 * U) / 100 - (71 * V) / 100;
            int B = Yv + (177 * U) / 100;
            if (R < 0) R = 0; else if (R > 255) R = 255;
            if (G < 0) G = 0; else if (G > 255) G = 255;
            if (B < 0) B = 0; else if (B > 255) B = 255;
            outRow[col * 4 + 0] = static_cast<uint8_t>(B);
            outRow[col * 4 + 1] = static_cast<uint8_t>(G);
            outRow[col * 4 + 2] = static_cast<uint8_t>(R);
            outRow[col * 4 + 3] = 0xFF;
        }
    }
    return true;
}

} // namespace halplayer

#endif // APP_YUV_TO_BGRA_H
