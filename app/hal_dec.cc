#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "parse_cli.h"
#include "bmp_write.h"
#include "file_io.h"
#include "output_paths.h"

#include "codec_config.h"
#include "codec_names.h"
#include "annexb.h"
#include "capability.h"
#include "decoder.h"
#include "frame.h"
#include "plugin_loader.h"
#include "session.h"

namespace {

// Scan an Annex-B H.264/HEVC stream, sniff its codec from the parameter-set
// NAL types, and pack the sets it carries into AVCC extradata
// ([len:4][nalu]...), which is what the decoders' Initialize() consumes.
// The scan itself is shared (src/annexb.h). The verdict it returns
// (kH264/kHEVC/kUnknown) is this app's content-first codec answer, used only
// when -c was not given -- an explicit -c always wins.
halcodec::CodecHint ExtractParameterSets(const uint8_t* data, size_t size,
                                         std::vector<uint8_t>* extradata) {
    return halcodec::ExtractParameterSets(data, size, extradata, nullptr,
                                          /*onlyWithSpsPps=*/true);
}

// IVF: the container AV1 and VP9 test streams travel in (a 32-byte file header
// -- 'DKIF', version, header length, fourcc, width, height, timebase, frame
// count -- then one record per frame: a 4-byte little-endian payload size, an
// 8-byte presentation counter, and the packet). It carries no codec
// description, so the first frame's in-band sequence header is what a decoder
// has to open with.

bool IsIvf(const uint8_t* data, size_t size, size_t* headerSize,
           std::string* codec) {
    if (!data || size < 32 || memcmp(data, "DKIF", 4) != 0) {
        return false;
    }
    // The header length is stated rather than fixed, so the records start where
    // the file says they do.
    *headerSize = static_cast<size_t>(data[6]) |
                  (static_cast<size_t>(data[7]) << 8);
    // The fourcc at offset 8 names the codec. AV01 -> av1, VP90 -> vp9,
    // VP80 -> vp8; any other fourcc is still a valid IVF but one we don't
    // decode, so leave the codec empty and let the caller fall through to the
    // Annex-B/JPEG sniff.
    const char* cc = reinterpret_cast<const char*>(data + 8);
    if (memcmp(cc, "AV01", 4) == 0) {
        *codec = "av1";
    } else if (memcmp(cc, "VP90", 4) == 0) {
        *codec = "vp9";
    } else if (memcmp(cc, "VP80", 4) == 0) {
        *codec = "vp8";
    } else {
        codec->clear();
    }
    return *headerSize >= 32 && *headerSize <= size;
}

bool FirstIvfPacket(const uint8_t* data, size_t size, size_t headerSize,
                    const uint8_t** out, size_t* outSize) {
    if (headerSize + 12 > size) {
        return false;
    }
    const uint8_t* record = data + headerSize;
    const size_t n = static_cast<size_t>(record[0]) |
                     (static_cast<size_t>(record[1]) << 8) |
                     (static_cast<size_t>(record[2]) << 16) |
                     (static_cast<size_t>(record[3]) << 24);
    if (headerSize + 12 + n > size) {
        return false;
    }
    *out = record + 12;
    *outSize = n;
    return true;
}

// Hands out an IVF file's packets as elementary-stream spans, dropping the file
// header and every frame record. Containers stay outside the decoder layer, and
// the split has to be exact: AV1 packets carry no start code, so container bytes
// reaching the feed would be indistinguishable from a corrupt stream.
class IvfPayloadReader {
public:
    explicit IvfPayloadReader(size_t headerSize) : skip_(headerSize) {}

    // Feeds `sink` one span per run of packet bytes this buffer holds. Both a
    // record and a packet can straddle a read boundary, which is what the carried
    // position is for.
    template <class Sink>
    void Feed(const uint8_t* data, size_t size, const Sink& sink) {
        size_t pos = 0;
        if (skip_ > 0) {
            const size_t drop = skip_ < size ? skip_ : size;
            skip_ -= drop;
            pos = drop;
            if (skip_ > 0) {
                return;
            }
        }
        while (pos < size) {
            if (payloadLeft_ == 0) {
                while (pos < size && recordLen_ < kRecordSize) {
                    record_[recordLen_++] = data[pos++];
                }
                if (recordLen_ < kRecordSize) {
                    return;
                }
                payloadLeft_ = static_cast<size_t>(record_[0]) |
                    (static_cast<size_t>(record_[1]) << 8) |
                    (static_cast<size_t>(record_[2]) << 16) |
                    (static_cast<size_t>(record_[3]) << 24);
                // record_[4..11] is the frame's presentation counter. The decode
                // API has no slot for it, and AV1 needs no order key: the decoder
                // layer returns what VideoToolbox gives it, which is submission
                // order.
                recordLen_ = 0;
                if (payloadLeft_ == 0) {
                    continue;
                }
            }
            const size_t n = payloadLeft_ < size - pos ? payloadLeft_ : size - pos;
            sink(data + pos, n);
            pos += n;
            payloadLeft_ -= n;
        }
    }

private:
    static constexpr size_t kRecordSize = 12;
    size_t skip_;
    size_t payloadLeft_ = 0;
    size_t recordLen_ = 0;
    uint8_t record_[kRecordSize];
};

// ProRes: a bare element stream is a concatenation of pictures that each open
// with a 32-bit big-endian count of their own bytes, followed by the frame tag --
// 'icpf' for the YUV flavours, 'prrf' for RAW. Unlike every video codec here it
// has no parameter set, and that header is the only place the picture dimensions
// live, so the decoder needs it before a session can open -- and nothing else of
// the stream, which runs to gigabytes.
bool IsProRes(const uint8_t* data, size_t size) {
    if (!data || size < 8) {
        return false;
    }
    const size_t declared = static_cast<size_t>(data[0]) << 24 |
                            static_cast<size_t>(data[1]) << 16 |
                            static_cast<size_t>(data[2]) << 8 |
                            static_cast<size_t>(data[3]);
    return declared >= 20 && (memcmp(data + 4, "icpf", 4) == 0 ||
                              memcmp(data + 4, "prrf", 4) == 0);
}

#ifdef __APPLE__
const char* kDefaultDecoder = "vtbox";
#else
// No hard-coded default: resolved at runtime to the first available decoder
// backend. (NvMedia is aarch64's DRIVE-OS backend but its data path is still
// a stub, so it must not be the silent default — users who want it can pass
// -b nvmedia explicitly.)
const char* kDefaultDecoder = "";
#endif

// The --format values whose file is a BMP rather than a dump of the frame's own
// bytes. They are the ones that ask the decoder for converted pixels; the list
// itself lives with the other name tables (codec_names.h), so every app that
// answers --format agrees on which ones convert.
bool IsBmpFormat(const std::string& format) {
    return halcodec::IsConverterFormat(format);
}

const char* PixelFormatName(halcodec::PixelFormat format) {
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

// Everything one decode stream needs: its bytes, the codec/extradata verdicts
// the sniffs produced, and — for IVF — the record walker that strips the
// container while feeding. Shared by the single-stream path below and the
// multi-stream Session path at the bottom of main().
struct DecodeStream {
    std::vector<uint8_t> raw;
    std::unique_ptr<IvfPayloadReader> ivf;
};

// What a single input file is, judged once, before any decoder opens: IVF
// (codec from the fourcc, first packet as extradata, record walker attached),
// ProRes (header as extradata), JPEG (whole file as extradata), or an
// Annex-B elementary stream (parameter sets as extradata, codec sniffed from
// the SPS/VPS NAL types). Each sniff fills params->codec only when it is
// still empty, so an explicit -c is never overridden. Fills *params and
// *stream; false when the file cannot be read.
bool PrepareDecodeStream(const std::string& path, halcodec::CodecParams* params,
                         DecodeStream* stream) {
    if (!halcodec::ReadFile(path, &stream->raw)) {
        return false;
    }
    size_t headerSize = 0;
    std::string ivfCodec;
    const uint8_t* packet = nullptr;
    size_t packetSize = 0;
    if (IsIvf(stream->raw.data(), stream->raw.size(), &headerSize, &ivfCodec) &&
        !ivfCodec.empty() &&
        FirstIvfPacket(stream->raw.data(), stream->raw.size(), headerSize,
                       &packet, &packetSize)) {
        // An IVF/AV01 or VP90 input says which codec its bytes are, which
        // the name has to carry because the frame records make the stream
        // look like nothing to the Annex-B sniff below.
        if (params->codec.empty()) params->codec = ivfCodec;
        params->extradata.assign(packet, packet + packetSize);
        stream->ivf.reset(new IvfPayloadReader(headerSize));
    } else if (IsProRes(stream->raw.data(), stream->raw.size())) {
        // The name has to say ProRes before the feed starts: a picture's own
        // byte count is what the decoder splits on, and no other codec's
        // bytes are delimited that way.
        if (params->codec.empty()) params->codec = "prores";
        const size_t headerBytes = stream->raw.size() < 20 ? stream->raw.size() : 20;
        params->extradata.assign(stream->raw.begin(),
                                 stream->raw.begin() + headerBytes);
    } else if (stream->raw.size() >= 2 && stream->raw[0] == 0xFF &&
               stream->raw[1] == 0xD8) {
        // A JPEG file is one image and says so with its own two magic bytes.
        // It has no parameter set to hand over, so the codestream itself is
        // the extradata: the decoder reads the picture size out of its frame
        // header before opening a session, and the feed below then streams
        // the same bytes as usual.
        if (params->codec.empty()) params->codec = "jpeg";
        params->extradata = stream->raw;
    } else {
        // Annex-B elementary stream: the parameter-set NAL types tell H.264
        // and HEVC apart, the same content-first verdict hal_player uses. Every
        // other codec keeps whatever name -c or the extension gave it.
        halcodec::CodecHint hint = ExtractParameterSets(
            stream->raw.data(), stream->raw.size(), &params->extradata);
        if (params->codec.empty()) {
            if (hint == halcodec::CodecHint::kH264) params->codec = "h264";
            else if (hint == halcodec::CodecHint::kHEVC) params->codec = "hevc";
        }
    }
    return true;
}

// A -f rgb/bgr/y request asks the decoder to *convert*, which is not the same ask
// as dumping the frame's own bytes. The backend's structured decode capability is
// the list of layouts it can ever hand out, so a request that does not appear in
// it is refused here -- before any frame is decoded -- rather than after the first
// picture lands in the layout the stream carries.
//
// Returns "" when the request can proceed, the reason to print when it cannot. An
// Unknown answer (no probe for this codec, capability provider missing) also
// returns "": this gate only sharpens an error the per-frame check in writeFrame
// already raises, and a backend that has not adopted the structured query keeps
// that older, run-and-see behavior instead of being blocked by a guess.
std::string UnreachableOutputFormat(const std::string& backend,
                                   const std::string& codec,
                                   const std::string& cliFormat,
                                   halcodec::PixelFormat want) {
    if (want == halcodec::PixelFormat::Unknown) {
        return {};  // not a conversion request; the frame keeps its own layout
    }
    if (halcodec::IsInterleavedFormat(cliFormat)) {
        // No HAL format names interleaved 24-bit pixels, so the table below has
        // nothing truthful to say about the request; the check that judges the
        // frame that arrives spells out what the writer would do with it.
        return {};
    }
    // One probe sweep per (backend, codec): the answer is a property of the
    // machine, and a multi-stream run asks the same question for every input it
    // was handed. cached = (did the backend answer, what it can deliver).
    static std::map<std::string, std::pair<bool, std::set<halcodec::PixelFormat>>> cache;
    const std::string key = backend + '\0' + codec;
    auto it = cache.find(key);
    if (it == cache.end()) {
        auto provider = halcodec::CapabilityProvider::Create(backend);
        std::vector<halcodec::CapabilityProvider::DecodeCapsEntry> entries;
        const bool answered =
            provider && provider->queryDecoderCaps(codec, entries) ==
                            halcodec::CapabilityProvider::CapsStatus::Available;
        std::set<halcodec::PixelFormat> seen;
        for (const auto& e : entries) {
            for (const auto& f : e.formats()) {
                seen.insert(f);
            }
        }
        it = cache.emplace(key, std::make_pair(answered, seen)).first;
    }
    if (!it->second.first) {
        return {};
    }
    const std::set<halcodec::PixelFormat>& reachable = it->second.second;
    if (reachable.count(want) != 0) {
        return {};  // some input class of this codec really does deliver it
    }
    std::string layouts;
    for (const auto& f : reachable) {
        layouts += (layouts.empty() ? "" : ", ") + std::string(halcodec::Name(f));
    }
    std::string why = "--format " + cliFormat + " asks for " +
                      std::string(halcodec::Name(want)) + " pixels, and " + backend +
                      " never decodes " + codec + " into that layout. ";
    if (layouts.empty()) {
        return why + "It claims no decode output for this codec at all.";
    }
    return why + "It delivers " + layouts + "; dump one of those (no converting "
                 "-f, or -f matching it) and convert afterwards if the pixels "
                 "really have to be " + std::string(halcodec::Name(want)) + ".";
}

} // namespace

// The multi-stream decode driver: N inputs through one halcodec::Session,
// drained in completion order. It shares the single-stream app's per-file
// container sniffs: an IVF payload reader strips the frame records on the
// way in, which a plain chunk feed would have sent to the decoders as
// garbage between packets.
static int RunMultiStream(CommandLineParser& cli) {
    const std::vector<std::string>& inputs = cli.getInputFiles();
    const size_t n = inputs.size();

    std::string backend = cli.getBackend();
#ifndef __APPLE__
    if (backend.empty()) {
        auto names = halcodec::Registry<halcodec::Decoder>::Names();
        if (names.empty()) {
            std::cerr << "No decoder backend available" << std::endl;
            return -1;
        }
        backend = names.front();
    }
#endif

    // One stream per -i. Each is read fully and fed from memory in chunks
    // below; the Session's fan-in is built on the async backends, so a
    // backend that drains synchronously (PullFrames) has no place here.
    {
        auto probe = halcodec::Decoder::Create(backend);
        if (!probe) {
            std::cerr << "Fail to create decoder backend: " << backend
                      << std::endl;
            return -1;
        }
        if (!probe->isAsync()) {
            std::cerr << "multi-stream decode needs an async backend; "
                      << backend << " is synchronous" << std::endl;
            return -1;
        }
    }
    std::cout << "decoder backend: " << backend << " (" << n << " streams)"
              << std::endl;

    std::vector<DecodeStream> streams(n);
    std::vector<halcodec::CodecParams> params(n);
    for (size_t s = 0; s < n; s++) {
        params[s].deviceIndex = cli.getGpuIndex();
        params[s].zeroCopy = cli.getZeroCopy();
        // Feed mode: the codec comes from -c, else the stream itself (content
        // sniff, then the extension), never from params.inputs — passing the
        // path would make NVDEC-style backends open an internal demuxer and
        // ignore FillInput. An explicit -c is seeded here and left alone: the
        // sniffs below only fill an empty name.
        params[s].codec = cli.isCodecExplicit() ? cli.getCodec() : std::string();
        if (!PrepareDecodeStream(inputs[s], &params[s], &streams[s])) {
            std::cerr << "Cannot read " << inputs[s] << std::endl;
            return -1;
        }
        // Content sniffs (IVF/ProRes/JPEG/Annex-B parameter sets) have run;
        // fall back to the extension, then to h264. The feed-mode backends
        // that cannot sniff (nvdec) need a name to build their decoder.
        if (params[s].codec.empty()) {
            params[s].codec = halcodec::CodecFromExtension(ExtOf(inputs[s]));
        }
        if (params[s].codec.empty()) {
            params[s].codec = "h264";
        }
        // Converting formats only; anything else leaves outputFormat Unknown
        // so the backend hands out its native layout.
        if (IsBmpFormat(cli.getFormat())) {
            halcodec::FormatToPixelFormat(cli.getFormat(),
                                          &params[s].outputFormat);
        }
        const std::string why = UnreachableOutputFormat(
            backend, params[s].codec, cli.getFormat(), params[s].outputFormat);
        if (!why.empty()) {
            std::cerr << why << std::endl;
            return -1;
        }
    }

    halcodec::Session session;
    if (session.open(halcodec::Session::Kind::Decode, backend, params) != n) {
        std::cerr << "Failed to open " << n << " decode streams on " << backend
                  << std::endl;
        return -1;
    }

    // Per-stream outputs: alongside each input, or into -o when it names a
    // directory. The single-stream path derives names the same way.
    std::string outDir;
    if (cli.hasOutputFile()) {
        outDir = TrimTrailingSlashes(cli.getOutputFile());
        if (!EnsureDir(outDir)) {
            return -1;
        }
    }
    const std::string outExt =
        IsBmpFormat(cli.getFormat()) ? "bmp" : cli.getFormat();
    // Buffers must outlive the streams that pubsetbuf into them -- declared
    // before `outs`, since locals destruct in reverse order and a flushed-at-
    // exit filebuf whose buffer died first writes nothing (measured: both
    // outputs stayed 0 bytes with the declarations swapped).
    std::vector<std::vector<char>> bufs(n, std::vector<char>(kIoBufBytes));
    std::vector<std::ofstream> outs(n);
    std::set<std::string> claimed;
    for (size_t s = 0; s < n; s++) {
        std::string name = BasenameOf(inputs[s]);
        std::string path = outDir.empty()
                               ? StripExt(inputs[s])
                               : outDir + '/' + StripExt(name);
        path += "." + outExt;
        // Two inputs can derive the same name (the same file listed twice, or
        // two directories of stills with identical names). The second ofstream
        // would truncate the first's file mid-run, so a colliding stream gets
        // its index stamped in.
        if (!claimed.insert(path).second) {
            path = StripExt(path) + "." + std::to_string(s) + ExtOf(path);
            claimed.insert(path);
        }
        outs[s] = OpenBuffered<std::ofstream>(
            path, std::ios::out | std::ios::binary, bufs[s]);
        if (!outs[s]) {
            std::cerr << "unable to open output file: " << path << std::endl;
            return -1;
        }
        std::cout << path << std::endl;
    }

    // Feed every stream its bytes, then signal EOF. IVF packets go in as
    // their own spans, container bytes left behind.
    const size_t kChunk = 1 << 20;
    for (size_t s = 0; s < n; s++) {
        size_t off = 0;
        while (off < streams[s].raw.size()) {
            size_t chunk = std::min(kChunk, streams[s].raw.size() - off);
            const uint8_t* data = streams[s].raw.data() + off;
            if (streams[s].ivf) {
                streams[s].ivf->Feed(data, chunk,
                                     [&session, s](const uint8_t* span,
                                                   size_t spanSize) {
                                         session.feedDecode(s, span, spanSize);
                                     });
            } else {
                session.feedDecode(s, data, chunk);
            }
            off += chunk;
        }
        session.signalDecodeEOF(s);
    }

    // Fan-in drain: pull frames in completion order until every stream is done.
    std::vector<uint64_t> perStream(n, 0);
    int total = 0;
    halcodec::CodecFrame frame;
    size_t which = 0;
    while (session.getAnyDecodeFrame(frame, which)) {
        if (!halcodec::DownloadToHost(frame)) {
            std::cerr << "decoder produced a device frame this build cannot "
                         "download to host" << std::endl;
        }
        outs[which].write(reinterpret_cast<const char*>(frame.data), frame.size);
        perStream[which]++;
        total++;
        if (frame.release) frame.release();
    }
    for (size_t s = 0; s < n; s++) {
        std::cout << "stream " << s << " (" << inputs[s] << "): "
                  << perStream[s] << " frames" << std::endl;
    }
    std::cout << "Decode total frames: " << total << std::endl;

    session.close();
    return 0;
}

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    if (cli.getInputFile().empty()) {
        std::cerr << "missing input file (-i <file|dir>)" << std::endl;
        return 2;
    }

    // Load vendor backends lazily so the binary runs without vendor libs.
    halcodec::LoadBackends();

    // More than one -i means concurrent decode: one Session, N streams,
    // fan-in drain. The single-stream path below keeps the directory/BMP
    // machinery and the verified zero-copy feed loop.
    if (cli.getInputFiles().size() > 1) {
        return RunMultiStream(cli);
    }

    std::string backend = cli.getBackend().empty() ? kDefaultDecoder
                                                   : cli.getBackend();
#ifndef __APPLE__
    if (backend.empty()) {
        auto names = halcodec::Registry<halcodec::Decoder>::Names();
        if (names.empty()) {
            std::cerr << "No decoder backend available" << std::endl;
            return -1;
        }
        backend = names.front();
    }
#endif
    auto dec = halcodec::Decoder::Create(backend);
    if (!dec) {
        std::cerr << "Fail to create decoder backend: " << backend << std::endl;
        return -1;
    }
    std::cout << "decoder backend: " << dec->getName() << std::endl;

    halcodec::CodecParams params;
    params.inputs.push_back(cli.getInputFile());
    params.deviceIndex = cli.getGpuIndex();
    // Forward the explicit codec (-c) for backends that have no internal
    // demuxer and derive the NvMedia/cuvid codec id from the name (e.g.
    // nvmedia). Backends with their own demuxer (nvdec file mode) ignore it.
    // Without -c the name is left empty and resolved from the stream below.
    params.codec = cli.isCodecExplicit() ? cli.getCodec() : std::string();
    // -z/--zero-copy: ask the decoder to keep frames in device memory. Backends
    // that don't support it ignore the flag (still produce host frames); the
    // output writer calls DownloadToHost on every frame regardless.
    params.zeroCopy = cli.getZeroCopy();
    // Only the converting formats are looked up in the shared table: a dump of
    // the frame's own bytes (-f nv12/p010/...) is not an output-format request,
    // and leaving outputFormat Unknown keeps every backend on its native layout.
    if (IsBmpFormat(cli.getFormat())) {
        halcodec::FormatToPixelFormat(cli.getFormat(), &params.outputFormat);
    }
    // Decoders that build a format description from parameter sets (vtbox)
    // need SPS/PPS; extract them from the Annex-B input stream when present.
    // The same pass settles the codec when -c was not given: content first,
    // then the extension, then h264.
    DecodeStream stream;
    if (!PrepareDecodeStream(cli.getInputFile(), &params, &stream)) {
        std::cerr << "Cannot read " << cli.getInputFile() << std::endl;
        return -1;
    }
    if (params.codec.empty()) {
        params.codec = halcodec::CodecFromExtension(ExtOf(cli.getInputFile()));
    }
    if (params.codec.empty()) params.codec = "h264";
    {
        const std::string why = UnreachableOutputFormat(backend, params.codec,
                                                       cli.getFormat(),
                                                       params.outputFormat);
        if (!why.empty()) {
            std::cerr << why << std::endl;
            return -1;
        }
    }
    if (!dec->Initialize(params)) {
        std::cerr << "Fail to initialize decoder backend: " << backend << std::endl;
        return -1;
    }

    std::string input = cli.getInputFile();

    struct stat info;
    if (stat(input.c_str(), &info) != 0) {
        std::cout << "Cannot access " << input << std::endl;
        return -1;
    }
    std::vector<std::string> files;
    // The input each name was derived from, so an output that turns out to be an
    // input can be caught before anything is opened.
    std::vector<std::string> srcs;
    // A BMP format writes a BMP: the writer renames whatever it is handed to
    // `<name>.bmp`, so the extension here is the one the bytes really carry, and
    // the list the run prints is the list of files it leaves behind. The format
    // string itself still says which pixels to ask the decoder for.
    const std::string outExt =
        IsBmpFormat(cli.getFormat()) ? "bmp" : cli.getFormat();
    if (info.st_mode & S_IFDIR) {
        // -o names the output directory here, as it does for hal_enc: every
        // picture needs a file of its own, so a single name cannot be the answer.
        std::string dirPath = TrimTrailingSlashes(input);
        std::string outDir;
        if (cli.hasOutputFile()) {
            outDir = TrimTrailingSlashes(cli.getOutputFile());
        }
        if (!outDir.empty() && !EnsureDir(outDir)) {
            return -1;
        }
        std::vector<std::string> entries;
        if (!ListRegularFiles(dirPath, &entries)) {
            std::cerr << "Cannot open directory " << dirPath << std::endl;
            return -1;
        }
        for (const auto& inputFileName : entries) {
            std::string stem = outDir.empty()
                                   ? StripExt(inputFileName)
                                   : outDir + '/' +
                                         StripExt(BasenameOf(inputFileName));
            files.emplace_back(stem + "." + outExt);
            srcs.emplace_back(inputFileName);
        }
    } else {
        srcs.push_back(input);
        if (cli.hasOutputFile()) {
            files.push_back(cli.getOutputFile());
        } else {
            // The same rule the directory walk uses, so the name the run prints
            // is the file it creates -- for a BMP format that extension is
            // "bmp", which is what the writer turns its argument into anyway.
            files.push_back(StripExt(input) + "." + outExt);
        }
    }
    if (files.empty()) {
        // Reached by a directory that holds no regular file. The name list used
        // to be indexed here regardless, which read the front of an empty
        // vector; nothing is opened now, so nothing is created.
        std::cerr << "no regular files under " << input << std::endl;
        return -1;
    }
    {
        // An output that is one of the inputs gets truncated by the first write,
        // and the run then decodes an empty file and still reports success --
        // measured: `hal_dec -i s.h264 -f h264` zeroed s.h264, printed "Decode
        // total frames: 0" and exited 0. Compared by device and inode, so a
        // symlink or a `./` prefix counts as the file it points at.
        std::set<std::pair<dev_t, ino_t>> inputs;
        for (size_t i = 0; i < srcs.size(); i++) {
            struct stat st;
            if (stat(srcs[i].c_str(), &st) == 0) {
                inputs.insert(std::make_pair(st.st_dev, st.st_ino));
            }
        }
        for (size_t i = 0; i < files.size(); i++) {
            struct stat st;
            if (stat(files[i].c_str(), &st) != 0) {
                continue;
            }
            if (inputs.count(std::make_pair(st.st_dev, st.st_ino)) != 0) {
                std::cerr << files[i] << " is one of the inputs; refusing to "
                          << "write over it (pass -o, or a --format that does not "
                          << "name an input file)" << std::endl;
                return -1;
            }
        }
    }

    for (auto p : files) {
        std::cout << p << std::endl;
    }
    // The name picture `index` goes to. `files` holds one entry per input file,
    // which is one per picture for a batch of stills but not for a video stream
    // in a single file: there the list ends after its first entry while the
    // pictures keep coming, and the old code indexed past it -- on `hal_dec -i
    // cars.h264 -f bgr` that read the second entry of a one-element list, so the
    // picture was written to a garbage name (a hidden `./.bmp` in the cwd when
    // the bytes happened to parse) instead of crashing. Past the end the last
    // name is numbered, so every picture still lands in a file of its own.
    auto pictureName = [&files](size_t index) -> std::string {
        if (index < files.size()) {
            return files[index];
        }
        const std::string& last = files.back();
        return StripExt(last) + "_" + std::to_string(index + 1) + ExtOf(last);
    };

    // libc++ gives a filebuf 4096 bytes, which copies every multi-megabyte frame
    // out through that hole a thousand at a time (see kIoBufBytes). The buffer
    // has to outlive the stream, so it is declared before it, and each file gets
    // a freshly buffered stream rather than a reopened one.
    std::vector<char> out_buf(kIoBufBytes);
    // Nothing opens here. The default output name is derived from the input's,
    // so a stream opened before the input is read truncates the very file the run
    // is about to decode; the raw output opens at the first frame that needs it.
    std::ofstream fpout;
    bool outOpen = false;
    size_t outIndex = 0;
    size_t pictures = 0;

    int total_frames = 0;
    bool printedBayer = false;
    auto writeFrame = [&](halcodec::CodecFrame& frame) -> bool {
        // Device-resident frames (e.g. NvMedia zero-copy) must be materialized
        // to host memory before the file/BMP writers can touch the pixels.
        if (!halcodec::DownloadToHost(frame)) {
            std::cerr << "decoder produced a device frame this build cannot "
                         "download to host" << std::endl;
        }
        // A Bayer grid is the one output whose pixels are meaningless without the
        // metadata that travels with them, so it is the one format worth saying
        // something about here.
        if (frame.format == halcodec::PixelFormat::BAYER16LE && !printedBayer) {
            printedBayer = true;
            const char* phases = "unknown";
            switch (frame.bayer.pattern) {
                case halcodec::BayerPattern::RGGB: phases = "RGGB"; break;
                case halcodec::BayerPattern::GRBG: phases = "GRBG"; break;
                case halcodec::BayerPattern::GBRG: phases = "GBRG"; break;
                case halcodec::BayerPattern::BGGR: phases = "BGGR"; break;
                case halcodec::BayerPattern::Unknown: break;
            }
            std::cout << "raw sensel grid: " << frame.width << "x"
                      << frame.height << " " << phases
                      << " black=" << static_cast<int>(frame.bayer.blackLevel)
                      << " white=" << static_cast<int>(frame.bayer.whiteLevel)
                      << std::endl;
        }
        if (IsBmpFormat(cli.getFormat())) {
            // BMPWriter reads the picture the way --format describes it: one
            // plane of width*height bytes for y, three for rgb/bgr, and rows at
            // the picture's own width. That is only what the decoder delivers
            // when it honored the request, and CodecParams::outputFormat reaches
            // exactly one backend -- NVJPEG/layers/nvjpegdecoder.cc. VideoToolbox
            // ignores it and answers in the layout the stream carries, so an
            // NV12 frame handed over here is read 1.5x past its end (measured as
            // a heap-buffer-overflow at bmp_write.h:104 on a 320x240 frame).
            //
            // UnreachableOutputFormat above already refuses the requests a backend
            // can never satisfy; this check is the one that judges the frame that
            // actually arrived, which is what a backend answering Unknown to the
            // structured query is still held to.
            const bool interleaved = halcodec::IsInterleavedFormat(cli.getFormat());
            const halcodec::PixelFormat want =
                cli.getFormat() == "y"
                    ? halcodec::PixelFormat::GRAY
                    : (cli.getFormat() == "rgb" ? halcodec::PixelFormat::RGB
                                                : halcodec::PixelFormat::BGR);
            const size_t w = static_cast<size_t>(frame.width);
            const size_t h = static_cast<size_t>(frame.height);
            const size_t planes = cli.getFormat() == "y" ? 1 : 3;
            std::string why;
            if (interleaved) {
                why = "no backend delivers interleaved 24-bit pixels: the HAL "
                      "format table has no name for it, so -f " + cli.getFormat() +
                      " reaches the decoder as planar RGB/BGR and this writer "
                      "would read it interleaved. Use -f rgb or -f bgr.";
            } else if (frame.format != want) {
                why = "--format " + cli.getFormat() + " asks for " +
                      PixelFormatName(want) + " pixels, and the " + dec->getName() +
                      " decoder delivered " + PixelFormatName(frame.format) +
                      ". Only nvjpeg converts on request; the pixels here are the "
                      "layout the stream carries, and a BMP writer would read "
                      "past them.";
            } else {
                for (size_t p = 0; p < planes; p++) {
                    if (frame.strides[p] != 0 && frame.strides[p] != w) {
                        why = "--format " + cli.getFormat() + " writes BMP rows at " +
                              std::to_string(w) + " bytes, but plane " +
                              std::to_string(p) + " has a row stride of " +
                              std::to_string(frame.strides[p]) + ".";
                        break;
                    }
                }
            }
            if (!why.empty()) {
                std::cerr << why << std::endl;
                return false;
            }
            if (frame.size < w * h * planes) {
                std::cerr << "--format " << cli.getFormat() << " reads "
                          << w * h * planes << " bytes of pixels, and the frame "
                          << "reports " << frame.size << std::endl;
                return false;
            }
            BMPWriter writer(pictureName(pictures), cli.getFormat());
            writer.writeBMP(frame.data, frame.width, frame.height,
                            cli.getFormat() == "y" ? 1 : 3);
        } else {
            // nvjpeg answers with one still per input file, so every picture of
            // its own needs a file; the other backends decode a stream into one.
            if (!outOpen || (dec->getName() == "nvjpeg" && outIndex != pictures)) {
                fpout = OpenBuffered<std::ofstream>(
                    pictureName(pictures),
                    std::ios::out | std::ios::binary, out_buf);
                if (!fpout) {
                    std::cerr << "unable to open output file: "
                              << pictureName(pictures) << std::endl;
                    return false;
                }
                outOpen = true;
                outIndex = pictures;
            }
            fpout.write(reinterpret_cast<const char*>(frame.data), frame.size);
        }
        pictures++;
        if (frame.release) {
            frame.release();
        }
        return true;
    };

    if (dec->isAsync()) {
        // Async backends (vtbox): feed the stream chunk-by-chunk and
        // hand back whatever the feed call reports as ready, then signal EOF
        // and drain until GetFrame() returns false. A decoder that holds input
        // back when its own queue is full keeps every frame either way, but
        // interleaving feed and drain is what overlaps the two phases.
        std::ifstream fin(input, std::ios::binary);
        if (!fin) {
            std::cerr << "unable to open input file: " << input << std::endl;
            return -1;
        }
        // Small chunks keep the drain close behind the feed instead of running
        // the whole input first.
        const size_t kChunkSize = 1 << 18;  // 256 KiB
        std::vector<uint8_t> chunk(kChunkSize);
        halcodec::CodecFrame frame;
        while (fin) {
            fin.read(reinterpret_cast<char*>(chunk.data()),
                     static_cast<std::streamsize>(chunk.size()));
            std::streamsize got = fin.gcount();
            if (got <= 0) {
                break;
            }
            const size_t gotSize = static_cast<size_t>(got);
            int ready = 0;
            if (stream.ivf) {
                // Each packet goes in as its own span, container bytes left
                // behind. Only the last FillInput count matters: it is the queue
                // this chunk leaves, while an earlier one is stale by exactly the
                // frames that arrived since.
                stream.ivf->Feed(chunk.data(), gotSize,
                                 [&dec, &ready](const uint8_t* span, size_t spanSize) {
                                     ready = dec->FillInput(span, spanSize);
                                 });
            } else {
                ready = dec->FillInput(chunk.data(), gotSize);
            }
            if (ready < 0) {
                std::cerr << "decoder rejected input (FillInput failed)" << std::endl;
                return -1;
            }
            // FillInput's return value counts frames already queued, so these
            // GetFrame() calls cannot block.
            while (ready-- > 0 && dec->GetFrame(frame)) {
                total_frames++;
                if (!writeFrame(frame)) {
                    return -1;
                }
            }
        }
        dec->SignalInputComplete();
        while (dec->GetFrame(frame)) {
            total_frames++;
            if (!writeFrame(frame)) {
                return -1;
            }
        }
    } else {
        // Synchronous backends (nvdec, nvjpeg): pull a batch of frames and
        // drain it via GetFrame(), repeating until the internal source is
        // exhausted.
        int n_dec = 0;
        do {
            n_dec = dec->PullFrames();
            total_frames += n_dec;
            for (int i = 0; i < n_dec; i++) {
                halcodec::CodecFrame frame;
                if (!dec->GetFrame(frame)) {
                    break;
                }
                if (!writeFrame(frame)) {
                    return -1;
                }
            }
        } while (n_dec > 0);
    }

    // Flushing the buffered tail is what makes the last frame land, so this is
    // not left to the destructor.
    if (outOpen) {
        fpout.close();
    }
    dec->Finalize();

    std::cout << "Decode total frames: " << total_frames << std::endl;

    return 0;
}
