// VideoToolbox capability introspection for the "vtbox" backend.
//
// Everything this file answers comes off tables and one rule: a capability is
// only claimed once this machine has been seen to do it. VideoToolbox has no
// decoder-capability API (VTIsHardwareDecodeSupported probes hardware decode per
// codec type, and VTCopyVideoEncoderList lists the real encoders), so the
// structured answers are produced by exercising the real paths -- embedded probe
// streams through the same decode an application runs, one synthetic picture
// through a session that demands a hardware encoder -- and reading what comes
// back.
//
// The tables carry no version knowledge on purpose. Codec and pixel-format codes
// are the integers Apple names them by (see vt_fourcc.h), not SDK constants, so
// this compiles against any SDK, and an older macOS simply answers a row "no" or
// refuses a probe instead of the build failing. Nothing here asks which macOS it
// is running on.

#include <VideoToolbox/VideoToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

#include <dirent.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "capability.h"
#include "h264au.h"
#include "vt_fourcc.h"
#include "vt_probe_streams.h"
#include "vtdecoder.h"

namespace halcodec {
namespace vtbox {

namespace {

std::string FourCCToString(OSType cc) {
    char s[5];
    s[0] = static_cast<char>((cc >> 24) & 0xFF);
    s[1] = static_cast<char>((cc >> 16) & 0xFF);
    s[2] = static_cast<char>((cc >> 8) & 0xFF);
    s[3] = static_cast<char>(cc & 0xFF);
    s[4] = '\0';
    // A CMVideoCodecType is not always a printable fourcc: the encoder list also
    // carries plain pixel-format codes (the RGB "encoders" sit at 0x18), which
    // would otherwise print as blanks and look like a missing field.
    for (int i = 0; i < 4; i++) {
        if (s[i] < ' ' || s[i] == 0x7F) {
            char hex[16];
            snprintf(hex, sizeof(hex), "0x%08x", static_cast<unsigned>(cc));
            return hex;
        }
    }
    return std::string(s);
}

// Appends the string value of `key` to `out`; returns false if absent.
bool AppendDictString(CFDictionaryRef dict, CFStringRef key, CFMutableStringRef out) {
    CFTypeRef v = CFDictionaryGetValue(dict, key);
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) {
        return false;
    }
    CFStringAppend(out, static_cast<CFStringRef>(v));
    return true;
}

// Which spellings name one codec. The decoder and encoder both accept more than
// the canonical name (a stream says "h265" as readily as "hevc", and "mjpeg" as
// readily as "jpeg"), and the capability query has to answer for every spelling
// the data path answers for -- otherwise `--decode-caps h265` reports "unknown"
// about a codec this machine decodes in hardware.
struct CodecNames {
    const char* canonical;
    const char* aliases[4];  // nullptr-terminated
};

const CodecNames kCodecNames[] = {
    {"h264",       {"h264", "avc1", nullptr}},
    {"hevc",       {"hevc", "h265", nullptr}},
    {"av1",        {"av1", "av01", nullptr}},
    {"h263",       {"h263", nullptr}},
    {"jpeg",       {"jpeg", "mjpeg", nullptr}},
    {"prores",     {"prores", "appleprores", nullptr}},
    {"prores_raw", {"prores_raw", "proresraw", nullptr}},
};

const char* CanonicalCodec(const std::string& name) {
    for (const auto& c : kCodecNames) {
        for (const char* alias : c.aliases) {
            if (alias && name == alias) {
                return c.canonical;
            }
        }
    }
    return nullptr;
}

// One row of the human-readable decode table: a codec type the platform may see
// in media, the name to print, and the canonical HAL codec name whose data path
// actually takes those bytes (null when none does). The last field is what keeps
// the printout honest: VideoToolbox will say yes to HEVC-with-alpha or Dolby
// Vision on this machine, and the decoder here has no path for either, so
// claiming "hw decode supported" alone would advertise a capability the
// framework cannot use.
struct HwDecodeRow {
    OSType codecType;
    const char* name;
    const char* halCodec;
};

const HwDecodeRow kHwDecodeRows[] = {
    {kFccMpeg1,           "MPEG-1",             nullptr},
    {kFccMpeg2,           "MPEG-2",             nullptr},
    {kFccMpeg4,           "MPEG-4 Part 2",      nullptr},
    {kFccH263,            "H.263",              nullptr},
    {kFccH264,            "H.264",              "h264"},
    {kFccHevc,            "HEVC",               "hevc"},
    {kFccHevcAlpha,       "HEVC w/ alpha",      nullptr},
    {kFccDolbyVisionHevc, "Dolby Vision HEVC",  nullptr},
    {kFccDepthHevc,       "Depth HEVC",         nullptr},
    {kFccDisparityHevc,   "Disparity HEVC",     nullptr},
    {kFccJpeg,            "JPEG",               "jpeg"},
    {kFccJpegOpenDml,     "JPEG (OpenDML)",     nullptr},
    {kFccJpegXl,          "JPEG XL",            nullptr},
    {kFccVp8,             "VP8",                nullptr},
    {kFccVp9,             "VP9",                nullptr},
    {kFccAv1,             "AV1",                "av1"},
    {kFccProRes422Proxy,  "ProRes 422 Proxy",   "prores"},
    {kFccProRes422Lt,     "ProRes 422 LT",      "prores"},
    {kFccProRes422,       "ProRes 422",         "prores"},
    {kFccProRes422Hq,     "ProRes 422 HQ",      "prores"},
    {kFccProRes4444,      "ProRes 4444",        "prores"},
    {kFccProRes4444Xq,    "ProRes 4444 XQ",     "prores"},
    {kFccProResRaw,       "ProRes RAW",         "prores_raw"},
    {kFccProResRawHq,     "ProRes RAW HQ",      "prores_raw"},
};

// One input class the structured query can answer for, and the bytes it answers
// with. `stream` is an embedded probe; a null `stream` means the class has to be
// probed from a file on disk (only ProRes RAW, which no encoder on this platform
// can write -- see VideoToolbox/probes/README.md).
//
// Exactly one row per codec is marked `sanity`. When even that one refuses, the
// honest answer for the whole codec is Unknown rather than an empty table: a
// broken probe mechanism looks exactly like a machine that supports nothing, and
// only the sanity row tells the two apart. A non-sanity row that refuses is a
// truthful "not here".
enum class ProbeKind {
    AnnexB,            // H.264 / HEVC: parameter sets plus one picture
    Av1TemporalUnit,   // a bare OBU temporal unit, the way IVF carries it
    ProResElement,     // length-counted pictures, RAW ones included
    JpegCodestream,    // one JFIF image
};

struct ProbeClass {
    const char* codec;        // canonical, matches kCodecNames
    const char* decoderCodec; // the name the decoder dispatches on; differs only
                              // where one caps codec splits its input classes
                              // across data paths (ProRes RAW rides the ProRes
                              // path, which reads the raw flavour off the header)
    const char* profile;    // the input class, as the row labels it
    int bitDepth;           // 8/10/12; 0 = n/a
    int chroma;             // 420/422/444; 0 = monochrome or n/a
    ProbeKind kind;
    const unsigned char* stream;
    unsigned int streamLen;
    uint8_t paramTypes[3];  // AnnexB: the parameter-set NAL types the stream carries
    int paramTypeCount;
    bool sanity;
};

const ProbeClass kProbeClasses[] = {
    {"h264", "h264", "Constrained Baseline", 8, 420, ProbeKind::AnnexB,
     kH264_420_8Stream, kH264_420_8StreamLen, {7, 8}, 2, true},
    {"h264", "h264", "High", 8, 420, ProbeKind::AnnexB,
     kH264_High_420_8Stream, kH264_High_420_8StreamLen, {7, 8}, 2, false},

    {"hevc", "hevc", "Main", 8, 420, ProbeKind::AnnexB,
     kHevc_420_8Stream, kHevc_420_8StreamLen, {32, 33, 34}, 3, true},
    {"hevc", "hevc", "Main 10", 10, 420, ProbeKind::AnnexB,
     kHevc_420_10Stream, kHevc_420_10StreamLen, {32, 33, 34}, 3, false},
    {"hevc", "hevc", "Main 12", 12, 420, ProbeKind::AnnexB,
     kHevc_420_12Stream, kHevc_420_12StreamLen, {32, 33, 34}, 3, false},
    {"hevc", "hevc", "Main 4:2:2", 8, 422, ProbeKind::AnnexB,
     kHevc_422_8Stream, kHevc_422_8StreamLen, {32, 33, 34}, 3, false},
    {"hevc", "hevc", "Main 4:2:2 10", 10, 422, ProbeKind::AnnexB,
     kHevc_422_10Stream, kHevc_422_10StreamLen, {32, 33, 34}, 3, false},
    {"hevc", "hevc", "Main 4:4:4", 8, 444, ProbeKind::AnnexB,
     kHevc_444_8Stream, kHevc_444_8StreamLen, {32, 33, 34}, 3, false},
    {"hevc", "hevc", "Main Monochrome", 8, 0, ProbeKind::AnnexB,
     kHevc_Mono_8Stream, kHevc_Mono_8StreamLen, {32, 33, 34}, 3, false},
    {"hevc", "hevc", "Main 10 Monochrome", 10, 0, ProbeKind::AnnexB,
     kHevc_Mono_10Stream, kHevc_Mono_10StreamLen, {32, 33, 34}, 3, false},

    {"av1", "av1", "Main", 8, 420, ProbeKind::Av1TemporalUnit,
     kAv1_420_8Stream, kAv1_420_8StreamLen, {}, 0, true},
    {"av1", "av1", "Main 10", 10, 420, ProbeKind::Av1TemporalUnit,
     kAv1_420_10Stream, kAv1_420_10StreamLen, {}, 0, false},

    {"jpeg", "jpeg", "Baseline", 8, 420, ProbeKind::JpegCodestream,
     kJpeg_420_8Stream, kJpeg_420_8StreamLen, {}, 0, true},

    {"prores", "prores", "ProRes 422", 10, 422, ProbeKind::ProResElement,
     kProres_422Stream, kProres_422StreamLen, {}, 0, true},
    {"prores", "prores", "ProRes 4444", 12, 444, ProbeKind::ProResElement,
     kProres_4444Stream, kProres_4444StreamLen, {}, 0, false},

    // No bytes: ProRes RAW has no encoder on this platform (measured,
    // VTCopyVideoEncoderList lists only the six 422/4444 tiers and ffmpeg writes
    // no 'prrf'), so the stream has to come from a camera. Without one the answer
    // is Unknown, which the contract reserves for exactly this.
    {"prores_raw", "prores", "ProRes RAW", 12, 0, ProbeKind::ProResElement,
     nullptr, 0, {}, 0, true},
};

// The first ProRes RAW picture file this machine has: an explicit
// HALCODEC_PRORES_RAW_SAMPLE, else a tests/data/prores_raw_*.prrf next to the
// working directory (what tools/fetch_prores_raw_sample.sh writes). Empty when
// there is none, which the probe reports as Unknown rather than as "no".
std::string FindRawSample() {
    if (const char* env = getenv("HALCODEC_PRORES_RAW_SAMPLE")) {
        std::ifstream in(env, std::ios::binary);
        return in.good() ? std::string(env) : std::string();
    }
    DIR* dir = opendir("tests/data");
    if (!dir) {
        return std::string();
    }
    std::string found;
    while (struct dirent* e = readdir(dir)) {
        const std::string name(e->d_name);
        if (name.rfind("prores_raw_", 0) == 0 &&
            name.size() > 5 && name.compare(name.size() - 5, 5, ".prrf") == 0) {
            found = "tests/data/" + name;
            break;
        }
    }
    closedir(dir);
    return found;
}

bool ReadBytes(const std::string& path, std::vector<unsigned char>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out->empty();
}

// Decodes one probe class through the real VTDecoder path and returns the HAL
// pixel format of the frame that comes back, or PixelFormat::Unknown when this
// machine refuses it (session error, no frame out). `reason` describes a refusal
// that is the caller's own doing (no asset on disk) rather than the hardware's.
//
// This is the honest way to answer "what would decoding produce": the same
// feed/complete/drain path a real decode takes, on a stream whose format is
// known exactly. A session-creation probe cannot do this -- VideoToolbox happily
// opens a session for a format whose frames it then downgrades.
//
// The extradata each kind hands over mirrors what hal_dec builds from a file
// (app/hal_dec.cc, PrepareDecodeStream): parameter sets for Annex-B, the temporal
// unit for AV1, the frame header for ProRes, the whole codestream for JPEG. If
// the two drift, the probe stops describing the path it claims to exercise.
PixelFormat ProbeDecode(const ProbeClass& c, std::string* reason) {
    std::vector<unsigned char> asset;
    const unsigned char* stream = c.stream;
    unsigned int len = c.streamLen;
    if (!stream) {
        const std::string path = FindRawSample();
        if (path.empty() || !ReadBytes(path, &asset)) {
            if (reason) {
                *reason = "no ProRes RAW stream to probe with; set "
                          "HALCODEC_PRORES_RAW_SAMPLE or run "
                          "tools/fetch_prores_raw_sample.sh";
            }
            return PixelFormat::Unknown;
        }
        stream = asset.data();
        len = static_cast<unsigned int>(asset.size());
    }

    std::vector<uint8_t> extradata;
    if (c.kind == ProbeKind::AnnexB) {
        std::vector<Nal> nals = ScanNals(stream, len, 64);
        // ScanNals stores Nal.type in the H.264 convention (payload[0] & 0x1F) no
        // matter the codec; the HEVC paths recompute the real 6-bit type from the
        // byte. The probe does the same, so {32,33,34} can mean VPS/SPS/PPS.
        const bool hevc = std::string(c.codec) == "hevc";
        auto typeOf = [hevc](const Nal& n) -> uint8_t {
            return hevc ? static_cast<uint8_t>((n.payload[0] >> 1) & 0x3F) : n.type;
        };
        for (size_t i = 0; i < nals.size(); i++) {
            const uint8_t type = typeOf(nals[i]);
            if (std::find(c.paramTypes, c.paramTypes + c.paramTypeCount, type) ==
                c.paramTypes + c.paramTypeCount) {
                continue;
            }
            std::vector<uint8_t> avcc = BuildAvcc(nals, i, i + 1);
            extradata.insert(extradata.end(), avcc.begin(), avcc.end());
        }
    } else if (c.kind == ProbeKind::ProResElement) {
        extradata.assign(stream, stream + std::min<size_t>(len, 20));
    } else {
        // AV1 and JPEG both hand the decoder the bytes it reads its own header
        // from: the temporal unit's sequence header, the JFIF frame header.
        extradata.assign(stream, stream + len);
    }
    if (extradata.empty()) {
        if (reason) {
            *reason = "probe stream carried no parameter sets";
        }
        return PixelFormat::Unknown;
    }

    VTDecoder dec;
    CodecParams params;
    params.codec = c.decoderCodec;
    params.extradata = extradata;
    // A refused frame is what this probe measures, so the callback must not
    // present it as a fault of somebody else's decode. SetDecodeProbeLoggingQuiet
    // covers the window; the refusal still shows up in the answer, as a class the
    // table does not claim.
    struct QuietWhileProbing {
        QuietWhileProbing() { SetDecodeProbeLoggingQuiet(true); }
        ~QuietWhileProbing() { SetDecodeProbeLoggingQuiet(false); }
    } quiet;
    if (!dec.Initialize(params)) {
        return PixelFormat::Unknown;
    }
    dec.FillInput(stream, len);
    dec.SignalInputComplete();
    CodecFrame frame;
    PixelFormat fmt = PixelFormat::Unknown;
    while (dec.GetFrame(frame)) {
        fmt = frame.format;
    }
    dec.Finalize();
    return fmt;
}

// One input class the encode side can answer for. `encoderCodec` is the name
// VTEncoder dispatches on and `flavour` the ProRes tier it selects, so a row can
// only be claimed for a codec the data path can actually be asked to encode --
// `MapCodecType("av1")` answers H.264, so an AV1 row would advertise a bitstream
// nothing in this backend produces. `encodeProfile` is the -P string the class
// needs: HEVC 4:2:2 only keeps its chroma under main422-10 (measured, the same
// 'sv22' source under Main 10 emits a 4:2:0 stream), so leaving it unset would
// have the probe claim a capability by producing something else.
struct EncodeClass {
    const char* codec;         // canonical caps name
    const char* encoderCodec;  // name MapCodecType dispatches on
    const char* flavour;       // ProRes tier, or null
    const char* profile;       // the input class, as the row labels it
    int bitDepth;
    int chroma;
    PixelFormat input;         // the HAL raw format handed to the encoder
    const char* encodeProfile; // profile string for the session, "" = default
    bool sanity;
};

const EncodeClass kEncodeClasses[] = {
    {"h264", "h264", nullptr, "Main 4:2:0 8", 8, 420, PixelFormat::NV12, "", true},
    {"h264", "h264", nullptr, "Main 4:2:0 8", 8, 420, PixelFormat::I420, "", false},

    {"hevc", "hevc", nullptr, "Main 4:2:0 8", 8, 420, PixelFormat::NV12, "", true},
    {"hevc", "hevc", nullptr, "Main 4:2:0 8", 8, 420, PixelFormat::I420, "", false},
    {"hevc", "hevc", nullptr, "Main 10 4:2:0", 10, 420, PixelFormat::P010,
     "main10", false},
    {"hevc", "hevc", nullptr, "Main 4:2:2 10", 10, 422, PixelFormat::P210,
     "main422-10", false},
    // A 4:2:0 10-bit source under the 4:2:2 profile is a real combination the
    // data path takes (the coder upsamples chroma on its own), so it is probed as
    // its class rather than assumed from the row above.
    {"hevc", "hevc", nullptr, "Main 4:2:2 10", 10, 422, PixelFormat::P010,
     "main422-10", false},
    // The mono classes code a chroma-less picture, so their row says what raw
    // format feeds the profile: a 4:2:0 or 4:2:2 source loses its chroma to the
    // profile rather than to the session, and the encoder refuses the pair whose
    // depth it cannot match (8-bit input with main-mono-10).
    {"hevc", "hevc", nullptr, "Main Monochrome", 8, 0, PixelFormat::NV12,
     "main-mono", false},
    {"hevc", "hevc", nullptr, "Main 10 Monochrome", 10, 0, PixelFormat::P010,
     "main-mono-10", false},

    {"jpeg", "jpeg", nullptr, "Baseline 4:2:0 8", 8, 420, PixelFormat::NV12, "", true},
    {"jpeg", "jpeg", nullptr, "Baseline 4:2:0 8", 8, 420, PixelFormat::I420, "", false},

    {"prores", "prores", "422", "ProRes 422 10-bit 4:2:2", 10, 422, PixelFormat::P210,
     "", true},
    {"prores", "prores", "422", "ProRes 422 8-bit 4:2:0", 8, 420, PixelFormat::NV12,
     "", false},
    {"prores", "prores", "4444", "ProRes 4444 10-bit 4:2:2", 10, 422, PixelFormat::P210,
     "", false},

    {"h263", "h263", nullptr, "Profile 0 4:2:0 8", 8, 420, PixelFormat::NV12,
     "", true},
};

// The probe's own compression callback: it counts what came out. A session can
// answer noErr with nothing in the callback (measured for a profile a source
// format cannot feed), so a claimed class needs a real coded picture.
struct EncodeSink {
    OSStatus status = noErr;
    size_t frames = 0;
    size_t bytes = 0;
};

void EncodeSinkCallback(void* refcon, void* /*sourceFrameRefCon*/, OSStatus status,
                        VTEncodeInfoFlags /*infoFlags*/,
                        CMSampleBufferRef sampleBuffer) {
    auto* sink = static_cast<EncodeSink*>(refcon);
    sink->status = status;
    if (status != noErr || !sampleBuffer) {
        return;
    }
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!block) {
        return;
    }
    sink->frames++;
    sink->bytes += CMBlockBufferGetDataLength(block);
}

// Encodes one synthetic picture through a session that demands a hardware
// encoder (kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder)
// and says whether a coded frame came out.
//
// Why not VTCopyVideoEncoderList: its entries are per encoder, not per input
// class, and the keys that would describe accepted source formats are not part of
// the public contract. The claim a gate needs -- "this machine hardware-encodes
// 10-bit 4:2:2 from P210" -- only one real encode answers.
//
// The session is built from the same mappers the encoder uses (vt_fourcc.h), so
// the probe exercises the codec type, source pixel format and profile the data
// path would have chosen for this class; a row the two disagree on is a bug in
// whichever table drifted.
bool ProbeEncode(const EncodeClass& c, std::string* reason) {
    auto refuse = [&](const std::string& what) {
        if (reason) {
            *reason = what;
        }
        return false;
    };

    CMVideoCodecType codecType = MapCodecType(c.encoderCodec);
    if (c.flavour && !MapProResFlavour(c.flavour, &codecType)) {
        return refuse(std::string(c.codec) + ": unknown ProRes tier '" + c.flavour + "'");
    }
    const OSType pixFmt = SourcePixelFormat(c.input, codecType);

    CFMutableDictionaryRef spec = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(spec,
                         kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder,
                         kCFBooleanTrue);

    // Source attributes mirror VTEncoder::Initialize: the class's own pixel
    // format, and an IOSurface-backable pool so the hardware path is reachable.
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixFmt);
    CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, num);
    CFRelease(num);
    CFDictionaryRef emptyDict = CFDictionaryCreate(
        kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, emptyDict);
    CFRelease(emptyDict);

    const int kProbeSize = 64;
    EncodeSink sink;
    VTCompressionSessionRef session = nullptr;
    OSStatus st = VTCompressionSessionCreate(
        kCFAllocatorDefault, kProbeSize, kProbeSize, codecType, spec, attrs, nullptr,
        EncodeSinkCallback, &sink, &session);
    CFRelease(spec);
    char hex[24];
    snprintf(hex, sizeof(hex), "0x%08x", static_cast<unsigned>(st));
    if (st != noErr || !session) {
        CFRelease(attrs);
        return refuse(std::string(c.profile) + ": no hardware encoder for '" +
                      FourCCToString(codecType) + "' taking " + Name(c.input) +
                      " (session create " + hex + ")");
    }

    if (c.encodeProfile[0]) {
        CFStringRef profile = MapProfileLevel(codecType, c.encodeProfile);
        if (!profile) {
            CFRelease(attrs);
            VTCompressionSessionInvalidate(session);
            CFRelease(session);
            return refuse(std::string(c.profile) + ": profile \"" + c.encodeProfile +
                          "\" is not one this backend maps");
        }
        CFMutableDictionaryRef settings = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(settings, kVTCompressionPropertyKey_ProfileLevel, profile);
        VTSessionSetProperties(static_cast<VTSessionRef>(session), settings);
        CFRelease(settings);
    }

    // One synthetic picture: a horizontal ramp, so the encoder has real detail to
    // spend bits on rather than a flat field it may skip entirely.
    CVPixelBufferRef pb = nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault, kProbeSize, kProbeSize, pixFmt, attrs,
                            &pb) != kCVReturnSuccess || !pb) {
        CFRelease(attrs);
        VTCompressionSessionInvalidate(session);
        CFRelease(session);
        return refuse(std::string(c.profile) + ": cannot allocate a " +
                      Name(c.input) + " source buffer");
    }
    CFRelease(attrs);
    CVPixelBufferLockBaseAddress(pb, 0);
    for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(pb); plane++) {
        uint8_t* base =
            static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, plane));
        const size_t rowBytes = CVPixelBufferGetBytesPerRowOfPlane(pb, plane);
        const size_t planeHeight = CVPixelBufferGetHeightOfPlane(pb, plane);
        for (size_t y = 0; y < planeHeight; y++) {
            for (size_t x = 0; x < rowBytes; x++) {
                base[y * rowBytes + x] = static_cast<uint8_t>((x + y) & 0xFF);
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(pb, 0);

    VTCompressionSessionPrepareToEncodeFrames(session);
    OSStatus est = VTCompressionSessionEncodeFrame(
        session, pb, CMTimeMake(0, 25), CMTimeMake(1, 25), nullptr, nullptr, nullptr);
    VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
    CVPixelBufferRelease(pb);
    VTCompressionSessionInvalidate(session);
    CFRelease(session);

    snprintf(hex, sizeof(hex), "0x%08x", static_cast<unsigned>(est));
    if (est != noErr) {
        return refuse(std::string(c.profile) + ": EncodeFrame returned " + hex);
    }
    if (sink.frames == 0 || sink.bytes == 0) {
        return refuse(std::string(c.profile) + ": hardware encoder returned " +
                      std::to_string(static_cast<long long>(sink.frames)) +
                      " frames, " + std::to_string(static_cast<long long>(sink.bytes)) +
                      " bytes (callback status " +
                      std::to_string(static_cast<long long>(sink.status)) + ")");
    }
    return true;
}

} // namespace

class VTCapsProvider : public CapabilityProvider {
public:
    std::string getName() const override { return "vtbox"; }

    std::vector<std::string> getDeviceNames() const override {
        // VideoToolbox is a system-wide backend; there is no device
        // enumeration API, so nothing is reported.
        return {};
    }

    void showDecoderCapability() const override {
        // VTIsHardwareDecodeSupported probes hardware decode availability per
        // codec type (public since macOS 10.13). Every row is printed, including
        // the ones this machine says no to, so the absence of a codec is an
        // answer rather than a gap in the listing.
        std::cout << std::left;
        for (const auto& r : kHwDecodeRows) {
            const bool hw = VTIsHardwareDecodeSupported(r.codecType);
            std::cout << "  " << std::setw(19) << r.name << ": hw decode "
                      << (hw ? "yes " : "no  ");
            if (hw) {
                std::cout << "  HAL: "
                          << (r.halCodec ? std::string(r.halCodec)
                                         : std::string("no data path"));
            }
            std::cout << std::endl;
        }
    }

    // Structured decoder capability, answered by decoding probe streams and
    // reading what comes back. See ProbeDecode for why the answer goes through a
    // real decode, and kProbeClasses for what each row claims.
    CapabilityProvider::CapsStatus queryDecoderCaps(
        const std::string& codec,
        std::vector<CapabilityProvider::DecodeCapsEntry>& out) const override {
        const char* canonical = CanonicalCodec(codec);
        if (!canonical) {
            // Not a name this backend has a probe table for -- which is not the
            // same as a codec it cannot decode, so it is Unknown, not an empty
            // Available.
            return CapsStatus::Unknown;
        }

        const ProbeClass* sanity = nullptr;
        for (const auto& c : kProbeClasses) {
            if (std::string(c.codec) == canonical && c.sanity) {
                sanity = &c;
                break;
            }
        }
        if (!sanity) {
            return CapsStatus::Unknown;
        }

        std::string reason;
        PixelFormat base = ProbeDecode(*sanity, &reason);
        if (base == PixelFormat::Unknown) {
            if (!reason.empty()) {
                std::cerr << "vtbox caps: " << reason << std::endl;
            }
            return CapsStatus::Unknown;
        }
        out.push_back({sanity->profile, sanity->bitDepth, sanity->chroma, {base}});

        for (const auto& c : kProbeClasses) {
            if (&c == sanity || std::string(c.codec) != canonical) {
                continue;
            }
            PixelFormat fmt = ProbeDecode(c, nullptr);
            if (fmt == PixelFormat::Unknown) {
                continue;  // a refused probe here is a truthful "not supported"
            }
            out.push_back({c.profile, c.bitDepth, c.chroma, {fmt}});
        }
        return CapsStatus::Available;
    }

    // Structured encoder capability, answered by building a session that demands
    // a hardware encoder and coding one frame through it. Rows sharing an input
    // class merge into one entry listing every raw format that class accepts, so
    // a gate asking "can this backend encode 10-bit 4:2:0 from P010" matches the
    // same shape it matches on the decode side.
    CapabilityProvider::CapsStatus queryEncoderCaps(
        const std::string& codec,
        std::vector<CapabilityProvider::EncodeCapsEntry>& out) const override {
        const char* canonical = CanonicalCodec(codec);
        if (!canonical) {
            return CapsStatus::Unknown;
        }

        const EncodeClass* sanity = nullptr;
        bool anyRows = false;
        for (const auto& c : kEncodeClasses) {
            if (std::string(c.codec) != canonical) {
                continue;
            }
            anyRows = true;
            if (c.sanity) {
                sanity = &c;
                break;
            }
        }
        // No rows is not "cannot encode": it means this backend has no encode data
        // path for the name at all (AV1 on every macOS so far), which is a question
        // the table cannot answer.
        if (!anyRows || !sanity) {
            return CapsStatus::Unknown;
        }

        std::string reason;
        if (!ProbeEncode(*sanity, &reason)) {
            if (!reason.empty()) {
                std::cerr << "vtbox encode caps: " << reason << std::endl;
            }
            return CapsStatus::Unknown;
        }
        for (const auto& c : kEncodeClasses) {
            if (std::string(c.codec) != canonical) {
                continue;
            }
            // The sanity row was just answered; do not pay for its session twice.
            if (&c != sanity && !ProbeEncode(c, nullptr)) {
                continue;  // a refused probe here is a truthful "not supported"
            }
            EncodeCapsEntry* entry = nullptr;
            for (auto& e : out) {
                if (e.profile == c.profile && e.bitDepth == c.bitDepth &&
                    e.chroma == c.chroma) {
                    entry = &e;
                    break;
                }
            }
            if (!entry) {
                out.push_back({c.profile, c.bitDepth, c.chroma, {}});
                entry = &out.back();
            }
            entry->inputFormats.push_back(c.input);
        }
        return CapsStatus::Available;
    }

    // Encoder rows, printed as a table alongside the platform's own encoder list:
    // the list says which encoders exist, the rows say which input classes this
    // machine will actually code. Every row is printed, including the ones that
    // answer no, so an absent capability is a stated answer rather than a gap.
    void showEncoderCapability() const override {
        std::cout << std::left;
        for (const auto& r : kEncodeClasses) {
            std::string reason;
            const bool hw = ProbeEncode(r, &reason);
            std::cout << "  " << std::setw(8) << r.codec << std::setw(26) << r.profile
                      << " <- " << std::setw(9) << Name(r.input) << ": hw encode "
                      << (hw ? "yes" : "no ") << std::endl;
            if (!hw && !reason.empty()) {
                std::cout << "      " << reason << std::endl;
            }
        }

        // VTCopyVideoEncoderList reports the encoders the platform offers; entries
        // this backend cannot be asked to reach are marked, so the listing stays
        // the machine's own answer without implying a data path exists.
        CFArrayRef encoders = nullptr;
        OSStatus status = VTCopyVideoEncoderList(nullptr, &encoders);
        if (status != noErr || !encoders) {
            std::cout << "  (VTCopyVideoEncoderList failed: " << status << ")" << std::endl;
            return;
        }

        CFIndex count = CFArrayGetCount(encoders);
        if (count == 0) {
            std::cout << "  (no hardware encoders reported)" << std::endl;
            CFRelease(encoders);
            return;
        }

        for (CFIndex i = 0; i < count; i++) {
            CFDictionaryRef dict =
                static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(encoders, i));
            CFMutableStringRef line = CFStringCreateMutable(kCFAllocatorDefault, 0);
            CFStringAppend(line, CFSTR("    "));

            CFTypeRef codecType = CFDictionaryGetValue(dict, kVTVideoEncoderList_CodecType);
            if (codecType && CFGetTypeID(codecType) == CFNumberGetTypeID()) {
                OSType cc = 0;
                CFNumberGetValue(static_cast<CFNumberRef>(codecType),
                                 kCFNumberSInt32Type, &cc);
                CFStringAppendFormat(line, nullptr, CFSTR("codec=%s  "),
                                     FourCCToString(cc).c_str());
            }
            CFStringRef nameKey = CFDictionaryContainsKey(dict,
                              kVTVideoEncoderList_DisplayName)
                                  ? kVTVideoEncoderList_DisplayName
                                  : kVTVideoEncoderList_EncoderName;
            if (!AppendDictString(dict, nameKey, line)) {
                AppendDictString(dict, kVTVideoEncoderList_EncoderID, line);
            }

            char buf[512];
            if (CFStringGetCString(line, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                std::cout << buf << std::endl;
            }
            CFRelease(line);
        }
        CFRelease(encoders);
    }
};

HALCODEC_CONNECT(CapabilityProvider, vtbox, VTCapsProvider);

} // namespace vtbox
} // namespace halcodec
