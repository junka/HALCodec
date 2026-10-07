#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parse_cli.h"
#include "bmp_write.h"
#include "output_paths.h"

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"
#include "plugin_loader.h"

namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>* buf) {
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

// Scan an Annex-B H.264 stream for SPS (nalu type 7) and PPS (nalu type 8)
// and pack them into AVCC extradata ([len:4][nalu]...), which is what the
// decoders' Initialize() consumes.
void AppendAvcc(std::vector<uint8_t>* avcc, const uint8_t* data, size_t n) {
    avcc->push_back(static_cast<uint8_t>((n >> 24) & 0xFF));
    avcc->push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
    avcc->push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    avcc->push_back(static_cast<uint8_t>(n & 0xFF));
    avcc->insert(avcc->end(), data, data + n);
}

bool ExtractParameterSets(const uint8_t* data, size_t size,
                          std::vector<uint8_t>* extradata) {
    std::vector<uint8_t> vps, sps, pps;
    bool isHEVC = false;
    size_t i = 0;
    
    // First pass: detect if this is HEVC by checking the first VCL/parameter NAL.
    while (i + 4 <= size) {
        if (!(data[i] == 0 && data[i + 1] == 0 &&
              (data[i + 2] == 1 ||
               (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1)))) {
            ++i;
            continue;
        }
        size_t sc = (data[i + 2] == 1) ? 3 : 4;
        size_t start = i + sc;
        if (start < size) {
            // H.264 header: 5-bit type at b0 & 0x1F (SPS=7, PPS=8).
            // HEVC header: 6-bit type at (b0 >> 1) & 0x3F (VPS/SPS/PPS=32/33/34).
            uint8_t h264Type = data[start] & 0x1F;
            uint8_t hevcType = static_cast<uint8_t>((data[start] >> 1) & 0x3F);
            if (hevcType == 32 || hevcType == 33 || hevcType == 34) {
                isHEVC = true;
                break;
            } else if (h264Type == 7 || h264Type == 8) {
                isHEVC = false;
                break;
            }
        }
        // Skip this NAL to find the next one
        size_t j = start + 1;
        for (; j + 4 <= size; ++j) {
            if (data[j] != 0 || data[j + 1] != 0) continue;
            if (data[j + 2] == 1 ||
                (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1)) {
                break;
            }
        }
        i = j;
    }
    
    // Second pass: extract parameter sets
    i = 0;
    while (i + 4 <= size) {
        if (!(data[i] == 0 && data[i + 1] == 0 &&
              (data[i + 2] == 1 ||
               (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1)))) {
            ++i;
            continue;
        }
        size_t sc = (data[i + 2] == 1) ? 3 : 4;
        size_t start = i + sc;
        
        size_t j = start + 1;
        for (; j + 4 <= size; ++j) {
            if (data[j] != 0 || data[j + 1] != 0) continue;
            if (data[j + 2] == 1 ||
                (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1)) {
                break;
            }
        }
        size_t end = j;
        
        if (start < size) {
            uint8_t type = isHEVC
                              ? static_cast<uint8_t>((data[start] >> 1) & 0x3F)
                              : static_cast<uint8_t>(data[start] & 0x1F);
            if (isHEVC) {
                if (type == 32 && vps.empty()) {
                    vps.assign(data + start, data + end);
                } else if (type == 33 && sps.empty()) {
                    sps.assign(data + start, data + end);
                } else if (type == 34 && pps.empty()) {
                    pps.assign(data + start, data + end);
                }
            } else {
                if (type == 7 && sps.empty()) {
                    sps.assign(data + start, data + end);
                } else if (type == 8 && pps.empty()) {
                    pps.assign(data + start, data + end);
                }
            }
        }
        
        bool haveEnough = isHEVC ? (!sps.empty() && !pps.empty()) 
                                  : (!sps.empty() && !pps.empty());
        if (haveEnough) break;
        i = j;
    }
    
    if ((isHEVC && sps.empty() && pps.empty()) || 
        (!isHEVC && sps.empty() && pps.empty())) {
        return false;
    }
    
    if (isHEVC) {
        if (!vps.empty()) AppendAvcc(extradata, vps.data(), vps.size());
        if (!sps.empty()) AppendAvcc(extradata, sps.data(), sps.size());
        if (!pps.empty()) AppendAvcc(extradata, pps.data(), pps.size());
    } else {
        if (!sps.empty()) AppendAvcc(extradata, sps.data(), sps.size());
        if (!pps.empty()) AppendAvcc(extradata, pps.data(), pps.size());
    }
    return true;
}

// IVF: the container AV1 test streams travel in (a 32-byte file header -- 'DKIF',
// version, header length, fourcc, width, height, timebase, frame count -- then one
// record per frame: a 4-byte little-endian payload size, an 8-byte presentation
// counter, and the packet). It carries no codec description, so the first frame's
// in-band sequence header is what a decoder has to open with.

bool IsIvf(const uint8_t* data, size_t size, size_t* headerSize, bool* isAv1) {
    if (!data || size < 32 || memcmp(data, "DKIF", 4) != 0) {
        return false;
    }
    // The header length is stated rather than fixed, so the records start where
    // the file says they do.
    *headerSize = static_cast<size_t>(data[6]) |
                  (static_cast<size_t>(data[7]) << 8);
    *isAv1 = memcmp(data + 8, "AV01", 4) == 0;
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
// bytes. They are the ones that ask the decoder for converted pixels.
bool IsBmpFormat(const std::string& format) {
    return format == "y" || format == "bgr" || format == "rgb" ||
           format == "rgbi" || format == "bgri";
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

} // namespace

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    if (cli.getInputFile().empty()) {
        std::cerr << "missing input file (-i <file|dir>)" << std::endl;
        return 2;
    }

    // Load vendor backends lazily so the binary runs without vendor libs.
    halcodec::LoadBackends();

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
    params.codec = cli.getCodec();
    // -z/--zero-copy: ask the decoder to keep frames in device memory. Backends
    // that don't support it ignore the flag (still produce host frames); the
    // output writer calls DownloadToHost on every frame regardless.
    params.zeroCopy = cli.getZeroCopy();
    if (cli.getFormat() == "rgb" || cli.getFormat() == "rgbi") {
        params.outputFormat = halcodec::PixelFormat::RGB;
    } else if (cli.getFormat() == "bgr" || cli.getFormat() == "bgri") {
        params.outputFormat = halcodec::PixelFormat::BGR;
    } else if (cli.getFormat() == "y") {
        params.outputFormat = halcodec::PixelFormat::GRAY;
    }
    // Decoders that build a format description from parameter sets (vtbox)
    // need SPS/PPS; extract them from the Annex-B input stream when present.
    std::vector<uint8_t> raw;
    std::unique_ptr<IvfPayloadReader> ivf;
    if (ReadFile(cli.getInputFile(), &raw)) {
        size_t headerSize = 0;
        bool isAv1 = false;
        const uint8_t* packet = nullptr;
        size_t packetSize = 0;
        if (IsIvf(raw.data(), raw.size(), &headerSize, &isAv1) && isAv1 &&
            FirstIvfPacket(raw.data(), raw.size(), headerSize, &packet,
                           &packetSize)) {
            // An IVF/AV01 input says which codec its bytes are, which the name
            // has to carry because the frame records make the stream look like
            // nothing to the Annex-B sniff below.
            params.codec = "av1";
            params.extradata.assign(packet, packet + packetSize);
            ivf.reset(new IvfPayloadReader(headerSize));
        } else if (IsProRes(raw.data(), raw.size())) {
            // The name has to say ProRes before the feed starts: a picture's own
            // byte count is what the decoder splits on, and no other codec's
            // bytes are delimited that way.
            params.codec = "prores";
            const size_t headerBytes = raw.size() < 20 ? raw.size() : 20;
            params.extradata.assign(raw.begin(), raw.begin() + headerBytes);
        } else if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xD8) {
            // A JPEG file is one image and says so with its own two magic bytes.
            // It has no parameter set to hand over, so the codestream itself is
            // the extradata: the decoder reads the picture size out of its frame
            // header before opening a session, and the feed below then streams
            // the same bytes as usual.
            params.codec = "jpeg";
            params.extradata = raw;
        } else {
            ExtractParameterSets(raw.data(), raw.size(), &params.extradata);
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
        DIR *dir;
        struct dirent *ent;
        std::string dirPath = input;
        while (!dirPath.empty() && dirPath.back() == '/') {
            dirPath.pop_back();
        }
        // -o names the output directory here, as it does for hal_enc: every
        // picture needs a file of its own, so a single name cannot be the answer.
        std::string outDir;
        if (cli.hasOutputFile()) {
            outDir = cli.getOutputFile();
            while (outDir.size() > 1 && outDir.back() == '/') {
                outDir.pop_back();
            }
        }
        if (!outDir.empty() && !EnsureDir(outDir)) {
            return -1;
        }
        if ((dir = opendir(dirPath.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    std::string inputFileName = dirPath + '/' + ent->d_name;
                    std::string stem = outDir.empty()
                                           ? StripExt(inputFileName)
                                           : outDir + '/' + StripExt(ent->d_name);
                    files.emplace_back(stem + "." + outExt);
                    srcs.emplace_back(inputFileName);
                }
            }
            closedir(dir);
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
            const bool interleaved =
                cli.getFormat() == "rgbi" || cli.getFormat() == "bgri";
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
            if (ivf) {
                // Each packet goes in as its own span, container bytes left
                // behind. Only the last FillInput count matters: it is the queue
                // this chunk leaves, while an earlier one is stale by exactly the
                // frames that arrived since.
                ivf->Feed(chunk.data(), gotSize,
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