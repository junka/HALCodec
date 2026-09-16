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
    std::vector<uint8_t> sps, pps;
    size_t i = 0;
    while (i + 4 <= size) {
        // Find start code 00 00 01 or 00 00 00 01.
        if (!(data[i] == 0 && data[i + 1] == 0 &&
              (data[i + 2] == 1 ||
               (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1)))) {
            ++i;
            continue;
        }
        size_t sc = (data[i + 2] == 1) ? 3 : 4;
        size_t start = i + sc;

        // Find the next start code to delimit this NALU.
        size_t j = start + 1;
        for (; j + 4 <= size; ++j) {
            if (data[j] != 0 || data[j + 1] != 0) continue;
            if (data[j + 2] == 1 ||
                (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1)) {
                break;
            }
        }
        size_t end = j;  // payload is [start, end)

        if (start < size) {
            uint8_t type = data[start] & 0x1F;
            if (type == 7 && sps.empty()) {
                sps.assign(data + start, data + end);
            } else if (type == 8 && pps.empty()) {
                pps.assign(data + start, data + end);
            }
        }
        if (!sps.empty() && !pps.empty()) break;
        i = j;
    }

    if (sps.empty() && pps.empty()) {
        return false;
    }
    if (!sps.empty()) {
        AppendAvcc(extradata, sps.data(), sps.size());
    }
    if (!pps.empty()) {
        AppendAvcc(extradata, pps.data(), pps.size());
    }
    return true;
}

#ifdef __APPLE__
const char* kDefaultDecoder = "vtbox";
#else
const char* kDefaultDecoder = ""; // resolved at runtime to first available
#endif

} // namespace

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    if (cli.getInputFile().empty()) {
        std::cerr << "missing input file (-i <file|dir>)" << std::endl;
        return 2;
    }

#ifndef __APPLE__
    // Load vendor backends lazily so the binary runs without NVIDIA libs.
    halcodec::LoadBackends();
#endif

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
        // Async backends (vtbox): feed the Annex-B stream chunk-by-chunk,
        // signal EOF, then drain frames until GetFrame() returns false.
        std::ifstream fin(input, std::ios::binary);
        if (!fin) {
            std::cerr << "unable to open input file: " << input << std::endl;
            return -1;
        }
        const size_t kChunkSize = 1 << 20;  // 1 MiB
        std::vector<uint8_t> chunk(kChunkSize);
        while (fin) {
            fin.read(reinterpret_cast<char*>(chunk.data()),
                     static_cast<std::streamsize>(chunk.size()));
            std::streamsize got = fin.gcount();
            if (got <= 0) {
                break;
            }
            if (dec->FillInput(chunk.data(), static_cast<size_t>(got)) < 0) {
                std::cerr << "decoder rejected input (FillInput failed)" << std::endl;
                return -1;
            }
        }
        dec->SignalInputComplete();
        halcodec::CodecFrame frame;
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