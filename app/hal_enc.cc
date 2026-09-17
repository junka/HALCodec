#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "bmp_write.h"
#include "encoder.h"
#include "parse_cli.h"

#include "codec_config.h"
#include "frame.h"
#include "plugin_loader.h"

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    // Load vendor backends lazily so the binary runs without NVIDIA libs.
    halcodec::LoadBackends();

    std::string backend = cli.getBackend();
    if (backend.empty()) {
        // Default to the first available encoder backend instead of hard-coding
        // "nvenc", which may be absent on this machine.
        auto names = halcodec::Registry<halcodec::Encoder>::Names();
        if (names.empty()) {
            std::cerr << "No encoder backend available" << std::endl;
            return -1;
        }
        backend = names.front();
    }
    auto enc = halcodec::Encoder::Create(backend);
    if (!enc) {
        std::cerr << "Fail to create encoder backend: " << backend << std::endl;
        return -1;
    }
    std::cout << "encoder backend: " << enc->getName() << std::endl;
    std::string input = cli.getInputFile();

    std::regex pattern(R"((\d+)[xX](\d+))");
    std::smatch match;
    halcodec::CodecParams params;
    params.inputs.push_back(input);
    params.deviceIndex = cli.getGpuIndex();
    if (cli.getFormat() == "rgb" || cli.getFormat() == "rgbi") {
        params.inputFormat = halcodec::PixelFormat::RGB;
    } else if (cli.getFormat() == "bgr" || cli.getFormat() == "bgri"
               || cli.getFormat() == "bmp") {
        params.inputFormat = halcodec::PixelFormat::BGR;
    } else {
        params.inputFormat = halcodec::PixelFormat::I420; // yuv default
    }
    if (std::regex_search(input, match, pattern)) {
        params.width = std::stoi(match[1].str());
        params.height = std::stoi(match[2].str());
    }
    if (!enc->Initialize(params)) {
        std::cerr << "Fail to initialize encoder backend: " << backend << std::endl;
        return -1;
    }

    struct stat info;
    if (stat(input.c_str(), &info) != 0) {
        std::cout << "Cannot access " << input << std::endl;
        return -1;
    }
    std::vector<std::string> files;
    if (info.st_mode & S_IFDIR) {
        DIR *dir;
        struct dirent *ent;
        while (!input.empty() && input.back() == '/') {
            input.pop_back();
        }
        if ((dir = opendir(input.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    std::string inputFileName = input + '/' + ent->d_name;
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
    // The app owns input reading: raw frames are delivered one CodecFrame at
    // a time via FillFrame(); encoders must consume the parameter instead of
    // opening the file themselves.
    halcodec::CodecFrame in;
    in.width = params.width;
    in.height = params.height;
    in.format = params.inputFormat;
    // Raw input backing store for the single-file path. Kept in outer scope so
    // it outlives the final drain(): async encoders defer GetFrame() until after
    // the feed loop, and their worker thread still references the frame data
    // pointers handed to FillFrame(). Destroying this buffer before drain()
    // would leave the worker reading freed memory.
    std::vector<uint8_t> rawBuf;

    auto drain = [&]() {
        halcodec::CodecFrame out;
        while (enc->GetFrame(out)) {
            total_frames++;
            fpout.write(reinterpret_cast<const char *>(out.data), out.size);
            if (enc->getName() == "nvjpegenc" && fidx < files.size()) {
                fpout.close();
                fpout.open(files[fidx++], std::ios::out|std::ios::binary);
            }
            if (out.release) {
                out.release();
            }
        }
    };
    // Async encoders pipeline hardware submissions across frames: FillFrame
    // returns without blocking on completion, so we defer draining until the
    // stream is complete (deeper pipeline, higher throughput). Sync encoders
    // drain after every submission as before. nvjpegenc is always sync (one
    // image per output file), so per-frame draining there is unaffected.
    const bool asyncEnc = enc->isAsync();
    auto drainIfSync = [&]() { if (!asyncEnc) drain(); };

    if (info.st_mode & S_IFDIR) {
        // Feed each file in the directory as one whole image.
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir(input.c_str())) != NULL) {
            std::vector<uint8_t> buf;
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type != DT_REG) {
                    continue;
                }
                std::string path = input + '/' + ent->d_name;
                if (cli.getFormat() == "bmp") {
                    int w = 0, h = 0, n_chan = 0;
                    BMPReader reader(path);
                    in.data = reader.readBMP(&w, &h, &n_chan);
                    if (!in.data) {
                        std::cerr << "Failed to read BMP: " << path << std::endl;
                        continue;
                    }
                    int rowPadding = (4 - ((w * n_chan) % 4)) % 4;
                    in.size = (static_cast<size_t>(w) * n_chan + rowPadding) * h;
                    in.width = w;
                    in.height = h;
                    enc->FillFrame(in);
                    std::free(in.data);
                } else {
                    std::ifstream fin(path, std::ios::binary | std::ios::ate);
                    if (!fin) {
                        std::cerr << "Cannot open " << path << std::endl;
                        continue;
                    }
                    std::streamsize sz = fin.tellg();
                    fin.seekg(0, std::ios::beg);
                    buf.resize(static_cast<size_t>(sz));
                    fin.read(reinterpret_cast<char *>(buf.data()), sz);
                    // Resolve dimensions from the filename when the CLI did
                    // not carry them.
                    int w = params.width, h = params.height;
                    if (w <= 0 || h <= 0) {
                        std::smatch m;
                        if (std::regex_search(path, m, pattern)) {
                            w = std::stoi(m[1].str());
                            h = std::stoi(m[2].str());
                        }
                    }
                    if (w <= 0 || h <= 0) {
                        std::cerr << "Cannot determine dimensions for "
                                  << path << std::endl;
                        continue;
                    }
                    in.data = buf.data();
                    in.size = buf.size();
                    in.width = w;
                    in.height = h;
                    enc->FillFrame(in);
                }
                drainIfSync();
            }
            closedir(dir);
        }
    } else {
        // Single raw file. BMP input is a single whole image (header parsed by
        // BMPReader); any other input is sliced into fixed-size raw frames.
        if (cli.getFormat() == "bmp") {
            int w = 0, h = 0, n_chan = 0;
            BMPReader reader(input);
            in.data = reader.readBMP(&w, &h, &n_chan);
            if (!in.data) {
                std::cerr << "Failed to read BMP: " << input << std::endl;
                return -1;
            }
            int rowPadding = (4 - ((w * n_chan) % 4)) % 4;
            in.size = (static_cast<size_t>(w) * n_chan + rowPadding) * h;
            in.width = w;
            in.height = h;
            enc->FillFrame(in);
            drainIfSync();
            // Flush trailing packets and finish.
            halcodec::CodecFrame eos;
            eos.width = params.width;
            eos.height = params.height;
            eos.format = params.inputFormat;
            enc->FillFrame(eos);
            if (asyncEnc) enc->SignalInputComplete();
            drain();
            // The frame data is only safe to free once the async worker has
            // drained; for sync backends drainIfSync() already consumed it.
            std::free(in.data);
            fpout.close();
            enc->Finalize();
            std::cout << "Encode total frames: " << total_frames << std::endl;
            return 0;
        }
        std::ifstream fin(input, std::ios::binary | std::ios::ate);
        if (!fin) {
            std::cerr << "Cannot open input file: " << input << std::endl;
            return -1;
        }
        std::streamsize total = fin.tellg();
        fin.seekg(0, std::ios::beg);
        rawBuf.resize(static_cast<size_t>(total));
        fin.read(reinterpret_cast<char *>(rawBuf.data()), total);
        if (params.width <= 0 || params.height <= 0) {
            std::cerr << "Cannot determine frame dimensions" << std::endl;
            return -1;
        }
        int64_t w = params.width, h = params.height;
        std::string fmt = cli.getFormat();
        size_t frameBytes = 0;
        if (fmt == "y" || fmt == "gray") {
            frameBytes = w * h;
        } else if (fmt == "bgra" || fmt == "rgba") {
            frameBytes = w * h * 4;
        } else if (fmt == "yuv444" || fmt == "rgb" || fmt == "bgr"
                   || fmt == "rgbi" || fmt == "bgri") {
            frameBytes = w * h * 3;
        } else {
            frameBytes = w * h * 3 / 2; // nv12/iyuv/yuv default
        }
        in.width = params.width;
        in.height = params.height;
        for (size_t off = 0; off + frameBytes <= rawBuf.size(); off += frameBytes) {
            in.data = rawBuf.data() + off;
            in.size = frameBytes;
            enc->FillFrame(in);
            drainIfSync();
        }
    }

    // End-of-stream marker: encoders flush trailing packets, drained below.
    halcodec::CodecFrame eos;
    eos.width = params.width;
    eos.height = params.height;
    eos.format = params.inputFormat;
    enc->FillFrame(eos);
    if (asyncEnc) enc->SignalInputComplete();
    drain();

    fpout.close();
    enc->Finalize();

    std::cout << "Encode total frames: "<< total_frames << std::endl;

    return 0;
}