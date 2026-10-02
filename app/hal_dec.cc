#include <cstdint>
#include <fstream>
#include <iostream>
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
    if (ReadFile(cli.getInputFile(), &raw)) {
        ExtractParameterSets(raw.data(), raw.size(), &params.extradata);
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
        // Async backends (vtbox): feed the Annex-B stream chunk-by-chunk and
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
            int ready = dec->FillInput(chunk.data(),
                                       static_cast<size_t>(got));
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