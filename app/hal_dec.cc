#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parse_cli.h"
#include "bmp_write.h"

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

#ifdef __APPLE__
const char* kDefaultDecoder = "vtbox";
#else
// No hard-coded default: resolved at runtime to the first available decoder
// backend. (NvMedia is aarch64's DRIVE-OS backend but its data path is still
// a stub, so it must not be the silent default — users who want it can pass
// -b nvmedia explicitly.)
const char* kDefaultDecoder = "";
#endif

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
    if (info.st_mode & S_IFDIR) {
        DIR *dir;
        struct dirent *ent;
        std::string dirPath = input;
        while (!dirPath.empty() && dirPath.back() == '/') {
            dirPath.pop_back();
        }
        if ((dir = opendir(dirPath.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    std::string inputFileName = dirPath + '/' + ent->d_name;
                    size_t lastDot = inputFileName.find_last_of('.');
                    if (lastDot != std::string::npos) {
                        inputFileName = inputFileName.substr(0, lastDot);
                    }
                    files.emplace_back(inputFileName + "." + cli.getFormat());
                }
            }
            closedir(dir);
        }
    } else {
        // getOutputFile() already carries the format extension when derived.
        files.push_back(cli.getOutputFile());
    }

    for (auto p : files) {
        std::cout << p << std::endl;
    }
    int fidx = 0;
    std::ofstream fpout(files[fidx++], std::ios::out|std::ios::binary);
    if (!fpout) {
        std::cerr << "unable to open output file" << std::endl;
        return -1;
    }

    int total_frames = 0;
    auto writeFrame = [&](halcodec::CodecFrame& frame) {
        // Device-resident frames (e.g. NvMedia zero-copy) must be materialized
        // to host memory before the file/BMP writers can touch the pixels.
        if (!halcodec::DownloadToHost(frame)) {
            std::cerr << "decoder produced a device frame this build cannot "
                         "download to host" << std::endl;
        }
        if (cli.getFormat() == "y" || cli.getFormat() == "bgr"
            || cli.getFormat() == "rgb" || cli.getFormat() == "rgbi"
            || cli.getFormat() == "bgri") {
            BMPWriter writer(files[fidx++], cli.getFormat());
            writer.writeBMP(frame.data, frame.width, frame.height,
                            cli.getFormat() == "y" ? 1 : 3);
        } else {
            fpout.write(reinterpret_cast<const char*>(frame.data), frame.size);
            if (dec->getName() == "nvjpeg" && fidx < files.size()) {
                fpout.close();
                fpout.open(files[fidx++], std::ios::out|std::ios::binary);
            }
        }
        if (frame.release) {
            frame.release();
        }
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
                writeFrame(frame);
            }
        }
        dec->SignalInputComplete();
        while (dec->GetFrame(frame)) {
            total_frames++;
            writeFrame(frame);
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
                writeFrame(frame);
            }
        } while (n_dec > 0);
    }

    if (dec->getName() != "nvjpeg") {
        fpout.close();
    }
    dec->Finalize();

    std::cout << "Decode total frames: " << total_frames << std::endl;

    return 0;
}