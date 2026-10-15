#ifndef APP_CODEC_NAMES_H
#define APP_CODEC_NAMES_H

// The app-side name tables: --format -> PixelFormat, file extension -> codec
// name, codec name -> stream extension.
//
// One copy each, with union semantics: every spelling any app accepts is listed
// once here, so a flag that works in one app is not silently unknown in
// another. The tables had drifted three ways -- hal_enc mapped bmp to BGR and
// knew nv12/p010/p210/p016, hal_transcode knew nv16, hal_dec/hal_session knew
// the BMP pixel formats -- and hal_session's extension table answered "mjpeg"
// where every other site and most backends say "jpeg".

#include <cctype>
#include <string>

#include "codec_config.h"

namespace halcodec {

// --format -> the PixelFormat the pixels arrive in. A format the table does not
// know returns false and leaves *out untouched: the callers' defaults differ
// (hal_enc falls back to I420 for raw input, hal_transcode to NV12, and hal_dec
// leaves outputFormat Unknown unless a converting format was asked for, since
// only nvjpeg reads it), so the fallback stays at the call site.
//
// "bmp" is BGR: the file carries BGR rows, which is also what the BMP writer
// means by it.
inline bool FormatToPixelFormat(const std::string& format, PixelFormat* out) {
    if (format == "yuv" || format == "iyuv" || format == "i420") {
        *out = PixelFormat::I420;
    } else if (format == "nv12") {
        *out = PixelFormat::NV12;
    } else if (format == "nv16") {
        *out = PixelFormat::NV16;
    } else if (format == "p010") {
        *out = PixelFormat::P010;
    } else if (format == "p210") {
        *out = PixelFormat::P210;
    } else if (format == "p016") {
        *out = PixelFormat::P016;
    } else if (format == "yuv444") {
        *out = PixelFormat::YUV444P;
    } else if (format == "y") {
        *out = PixelFormat::GRAY;
    } else if (format == "rgb" || format == "rgbi") {
        *out = PixelFormat::RGB;
    } else if (format == "bgr" || format == "bgri" || format == "bmp") {
        *out = PixelFormat::BGR;
    } else if (format == "bgra") {
        *out = PixelFormat::BGRA;
    } else if (format == "rgba") {
        *out = PixelFormat::RGBA;
    } else {
        return false;
    }
    return true;
}

// The --format values that ask a decoder for converted pixels (RGB/BGR/GRAY)
// rather than a dump of the frame's own bytes. A dump must leave outputFormat
// Unknown so every backend stays on its native layout; only these reach
// FormatToPixelFormat on the decode side. Shared so hal_dec and hal_session
// cannot drift on which formats are converters.
inline bool IsConverterFormat(const std::string& format) {
    return format == "y" || format == "bgr" || format == "rgb" ||
           format == "rgbi" || format == "bgri";
}

// A file extension -> the codec name the backends take. The extension may carry
// its dot and any case ("JPG", ".jpg", "jpg" all work); empty when the
// extension says nothing, and callers keep their own default ("h264" for every
// app that has one).
//
// JPEG is normalised to "jpeg": NvMedia and AMF accept only that spelling,
// while nvenc/qsv/vtbox accept both, so "jpeg" is the one name that reaches
// every backend. "avi" is here because the container is JPEG-coded in the
// sample material these apps are used on.
inline std::string CodecFromExtension(std::string ext) {
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (ext == "h264" || ext == "264") return "h264";
    if (ext == "hevc" || ext == "h265" || ext == "265") return "hevc";
    if (ext == "av1" || ext == "obu") return "av1";
    if (ext == "vp8") return "vp8";
    if (ext == "vp9") return "vp9";
    if (ext == "jpg" || ext == "jpeg" || ext == "mjpeg" || ext == "mjpg" ||
        ext == "avi") {
        return "jpeg";
    }
    if (ext == "vc1" || ext == "wmv" || ext == "wmv3") return "vc1";
    if (ext == "mpeg2" || ext == "mpg" || ext == "mpg2" || ext == "m2v") {
        return "mpeg2";
    }
    if (ext == "mp4" || ext == "mpeg4") return "mpeg4";
    if (ext == "prores" || ext == "appleprores") return "appleprores";
    return std::string();
}

// The extension a stream of this codec gets, dot included. Only the names that
// differ from the codec string need a case: h264, av1, vp8 and vp9 already read
// as elementary-stream extensions. JPEG lands on ".jpg" so that a directory of
// .nv12 frames fed to `-c jpeg -f nv12` does not overwrite its own inputs.
inline std::string CodecStreamExtension(const std::string& codec) {
    if (codec == "hevc" || codec == "h265") return ".h265";
    if (codec == "jpeg" || codec == "mjpeg") return ".jpg";
    if (codec == "appleprores") return ".prores";
    if (codec == "mpeg2") return ".mpg2";
    return "." + codec;
}

} // namespace halcodec

#endif // APP_CODEC_NAMES_H
