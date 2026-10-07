#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "bmp_write.h"
#include "encoder.h"
#include "parse_cli.h"

#include "codec_config.h"
#include "encode_config.h"
#include "frame.h"
#include "plugin_loader.h"

namespace {

// One buffer per live stream, installed before the file is opened: libc++ gives
// a filebuf 4096 bytes otherwise, which turns a multi-megabyte read or write
// into per-chunk work. Measured on an M5 with incompressible payloads, 622 MB of
// 3 MiB writes through one held std::ofstream takes 462.9 ms at the default and
// 46.3 ms with a 4 MiB buffer (1.34 GB/s against 13.4 GB/s); the curve is flat
// from 1 MiB on and drifts slightly back the other way by 16 MiB. The SSD is
// not what separates either end of that range.
constexpr size_t kIoBufBytes = 4u << 20;

template <class Stream>
Stream OpenBuffered(const std::string& path, std::ios::openmode mode,
                    std::vector<char>& buf) {
    Stream s;
    s.rdbuf()->pubsetbuf(buf.data(), static_cast<std::streamsize>(buf.size()));
    s.open(path, mode);
    return s;
}

// One input picture and the file its codestream goes to. Directory mode names
// the output after the input, so the two belong together in one list.
struct RawImage {
    std::string inputPath;
    std::string outputPath;
};

// The default output extension comes from the codec rather than from --format.
// --format describes the raw layout of the input, so an encoded picture named
// after that extension lands on the frame it was made from -- a directory of
// .nv12 frames fed to `-c jpeg -f nv12` used to overwrite its own inputs.
std::string CodecStreamExt(const std::string& codec) {
    // Only the names that differ from the codec string need a case: h264, av1,
    // prores and vp9 already read as elementary-stream extensions.
    if (codec == "hevc" || codec == "h265") return ".h265";
    if (codec == "jpeg" || codec == "mjpeg") return ".jpg";
    if (codec == "appleprores") return ".prores";
    if (codec == "mpeg2") return ".mpg2";
    return "." + codec;
}

// Everything up to the last dot, but only when that dot is inside the final
// path component -- "in.put/f0" has no extension to strip.
std::string StripExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return path;
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && dot < slash) {
        return path;
    }
    return path.substr(0, dot);
}

// -o names a directory in directory mode, because each picture needs a file of
// its own. Create it if it is missing so `-o out/` works on a clean tree.
bool EnsureDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (st.st_mode & S_IFDIR) {
            return true;
        }
        std::cerr << "-o " << path << " is a file; with a directory input it "
                  << "names the output directory" << std::endl;
        return false;
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        if (!EnsureDir(path.substr(0, slash))) {
            return false;
        }
    }
    if (mkdir(path.c_str(), 0755) != 0) {
        std::cerr << "Cannot create output directory " << path << std::endl;
        return false;
    }
    return true;
}

}  // namespace

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
    } else if (cli.getFormat() == "nv12") {
        params.inputFormat = halcodec::PixelFormat::NV12;
    } else if (cli.getFormat() == "p010") {
        params.inputFormat = halcodec::PixelFormat::P010;
    } else if (cli.getFormat() == "p210") {
        params.inputFormat = halcodec::PixelFormat::P210;
    } else if (cli.getFormat() == "p016") {
        params.inputFormat = halcodec::PixelFormat::P016;
    } else if (cli.getFormat() == "yuv444") {
        params.inputFormat = halcodec::PixelFormat::YUV444P;
    } else {
        params.inputFormat = halcodec::PixelFormat::I420; // yuv/iyuv default
    }
    if (std::regex_search(input, match, pattern)) {
        params.width = std::stoi(match[1].str());
        params.height = std::stoi(match[2].str());
    }

    // Encoder tuning: load JSON config (if any), then apply CLI single-item
    // overrides on top. CLI items left at their sentinels are skipped so they
    // don't clobber values that came from the JSON file.
    params.codec = cli.getCodec();
    if (!cli.getEncodeConfigFile().empty()) {
        halcodec::LoadEncodeConfig(cli.getEncodeConfigFile(), params.encode);
    }
    if (!cli.getPreset().empty())       params.encode.preset = cli.getPreset();
    if (!cli.getTuning().empty())       params.encode.tuningInfo = cli.getTuning();
    if (!cli.getRateControl().empty())  params.encode.rateControl = cli.getRateControl();
    if (cli.getBitrateKbps() >= 0)      params.encode.bitrateKbps = cli.getBitrateKbps();
    if (cli.getMaxBitrateKbps() >= 0)   params.encode.maxBitrateKbps = cli.getMaxBitrateKbps();
    if (cli.getQp() >= 0)               params.encode.qp = cli.getQp();
    if (cli.getQuality() >= 0)          params.encode.quality = cli.getQuality();
    if (cli.getGopLength() >= 0)        params.encode.gopLength = cli.getGopLength();
    if (cli.getNumBFrames() >= 0)       params.encode.numBFrames = cli.getNumBFrames();
    if (cli.getFps() > 0)               params.encode.frameRateNum = cli.getFps();
    if (!cli.getProfile().empty())      params.encode.profile = cli.getProfile();
    if (!cli.getLevel().empty())        params.encode.level = cli.getLevel();
    if (cli.getLowDelay())              params.encode.lowDelay = true;

    if (!enc->Initialize(params)) {
        std::cerr << "Fail to initialize encoder backend: " << backend << std::endl;
        return -1;
    }

    struct stat info;
    if (stat(input.c_str(), &info) != 0) {
        std::cout << "Cannot access " << input << std::endl;
        return -1;
    }
    // The directory is walked once, here, and the encoder is fed from the list
    // this produces. The feed loop used to walk it a second time, by which point
    // the outputs this run creates are regular files in the same directory, so a
    // codestream could be read back as raw pixels.
    std::vector<RawImage> images;
    const std::string outExt = CodecStreamExt(params.codec);
    // Where the default names go: alongside each input, or into -o when it names
    // a directory. Resolved before the scan so a bad -o is reported before
    // anything is read.
    std::string outDir;
    if (cli.hasOutputFile()) {
        outDir = cli.getOutputFile();
        while (outDir.size() > 1 && outDir.back() == '/') {
            outDir.pop_back();
        }
    }
    if (info.st_mode & S_IFDIR) {
        DIR *dir;
        struct dirent *ent;
        while (!input.empty() && input.back() == '/') {
            input.pop_back();
        }
        if (!outDir.empty() && !EnsureDir(outDir)) {
            return -1;
        }
        if ((dir = opendir(input.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    std::string inputFileName = input + '/' + ent->d_name;
                    // Picture i keeps the name of the input it was made from, in
                    // the output directory if one was asked for.
                    std::string outputFileName =
                        outDir.empty() ? StripExt(inputFileName)
                                       : outDir + '/' + StripExt(ent->d_name);
                    outputFileName += outExt;
                    images.push_back({std::move(inputFileName),
                                      std::move(outputFileName)});
                }
            }
            closedir(dir);
        }
    } else {
        std::string outputFileName = cli.getOutputFile();
        if (!cli.hasOutputFile()) {
            // The same rule as directory mode: the input keeps its raw name and
            // the codestream gets the codec's, so a single `-i f.nv12 -f nv12` no
            // longer truncates the frame it is reading.
            outputFileName = StripExt(input) + outExt;
        }
        images.push_back({input, std::move(outputFileName)});
    }
    if (images.empty()) {
        std::cerr << "No regular files to encode in " << input << std::endl;
        return -1;
    }

    // Naming itself cannot collide any more, so reaching this point means it was
    // asked for: -o pointed the codestream at an input, or the inputs already
    // carry the codec's own extension. Worth saying before anything is written.
    for (const auto& im : images) {
        if (im.inputPath == im.outputPath) {
            std::cerr << "Note: " << im.outputPath
                      << " is both an input and an output; it is being replaced "
                         "by its encoded picture" << std::endl;
            break;
        }
    }

    for (const auto& im : images) {
        std::cout << im.outputPath << std::endl;
    }
    std::vector<char> out_buf(kIoBufBytes);
    std::vector<char> in_buf(kIoBufBytes);

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

    // An image coder emits a whole codestream per frame, so each one has to
    // land in its own file instead of being appended to the previous one.
    const bool perFrameFile = enc->oneOutputFilePerFrame();
    // Every output opens on the write that fills it rather than up front. The
    // first one used to be opened before any input was read, and directory mode
    // names its outputs after its inputs, so that truncate could land on a frame
    // that had not been read yet -- which then read back as an empty picture.
    std::ofstream fpout;
    bool outOpen = false;
    size_t openIdx = 0;
    size_t pictures = 0;
    bool outFailed = false;
    auto openFor = [&](size_t idx) {
        // A fresh stream for the next file rather than close() plus open() on
        // the one already held. On this libc++ a filebuf that has been closed and
        // reopened loses the buffered write path for good: every byte then goes
        // through the per-character sputn loop instead of one bulk copy, and it is
        // user time rather than syscalls (156 G instructions and 98.7 ms against
        // 5.0 ms in the kernel for 315 MB). pubsetbuf does not bring the fast path
        // back; only a filebuf that has not been closed yet does. Measured on the
        // shipped path -- 99 JPEG pictures of 158 KB each -- that costs 2.81 ms a
        // picture, the difference between 532 ms and 254 ms for the whole run.
        fpout = OpenBuffered<std::ofstream>(
            images[idx].outputPath, std::ios::out | std::ios::binary, out_buf);
        if (!fpout) {
            std::cerr << "unable to open output file " << images[idx].outputPath
                      << std::endl;
            outFailed = true;
            return false;
        }
        outOpen = true;
        openIdx = idx;
        return true;
    };
    auto drain = [&]() {
        halcodec::CodecFrame out;
        while (enc->GetFrame(out)) {
            total_frames++;
            // Encoded packets are always host memory today, but download
            // defensively in case a future encoder yields a device buffer.
            halcodec::DownloadToHost(out);
            // One file per picture for an image coder, one file for the whole
            // stream otherwise. Anything past the last name appends to it rather
            // than inventing a file no input was read from.
            size_t idx = perFrameFile ? pictures : 0;
            if (idx >= images.size()) {
                idx = images.size() - 1;
            }
            if (!outOpen || openIdx != idx) {
                if (!openFor(idx)) {
                    if (out.release) {
                        out.release();
                    }
                    return;
                }
            }
            fpout.write(reinterpret_cast<const char *>(out.data), out.size);
            ++pictures;
            if (out.release) {
                out.release();
            }
        }
    };
    // Async encoders pipeline hardware submissions across frames: FillFrame
    // returns without blocking on completion, so we defer draining until the
    // stream is complete (deeper pipeline, higher throughput). Sync encoders
    // drain after every submission as before. Image coders are covered by
    // perFrameFile above: one image per output file, however the backend runs.
    const bool asyncEnc = enc->isAsync();
    auto drainIfSync = [&]() { if (!asyncEnc) drain(); };

    if (info.st_mode & S_IFDIR) {
        // Feed each file in the directory as one whole image, in the order the
        // single scan produced -- which is also the order the pictures are named
        // in, so picture i can only land in the file derived from input i.
        std::vector<uint8_t> buf;
        for (const auto& im : images) {
            if (outFailed) {
                break;
            }
            const std::string& path = im.inputPath;
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
                std::ifstream fin =
                    OpenBuffered<std::ifstream>(path,
                                               std::ios::binary |
                                                   std::ios::ate,
                                               in_buf);
                if (!fin) {
                    std::cerr << "Cannot open " << path << std::endl;
                    continue;
                }
                std::streamsize sz = fin.tellg();
                if (sz <= 0) {
                    // Not a picture. CodecFrame::size == 0 is the end-of-stream
                    // marker, so submitting an empty file would end the encode
                    // rather than skip one input.
                    std::cerr << "Skipping empty input " << path << std::endl;
                    continue;
                }
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
                    std::cerr << "Cannot determine dimensions for " << path
                              << std::endl;
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
            if (outOpen) {
                fpout.close();
            }
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
        } else if (fmt == "p010" || fmt == "p016") {
            frameBytes = w * h * 3; // 16-bit storage, 4:2:0: 2 bytes * 1.5 samples
        } else if (fmt == "p210") {
            frameBytes = w * h * 4; // 16-bit storage, 4:2:2: 2 bytes * 2 samples
        } else if (fmt == "yuv444" || fmt == "rgb" || fmt == "bgr"
                   || fmt == "rgbi" || fmt == "bgri") {
            frameBytes = w * h * 3;
        } else {
            frameBytes = w * h * 3 / 2; // nv12/iyuv/yuv default
        }
        in.width = params.width;
        in.height = params.height;
        for (size_t off = 0; off + frameBytes <= rawBuf.size();
             off += frameBytes) {
            in.data = rawBuf.data() + off;
            in.size = frameBytes;
            enc->FillFrame(in);
            drainIfSync();
            if (outFailed) {
                break;
            }
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

    if (outOpen) {
        fpout.close();
    }
    enc->Finalize();

    std::cout << "Encode total frames: "<< total_frames << std::endl;

    return 0;
}