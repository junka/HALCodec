#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "bmp_write.h"
#include "encoder.h"
#include "file_io.h"
#include "output_paths.h"
#include "parse_cli.h"
#include "session.h"

#include "codec_config.h"
#include "codec_names.h"
#include "encode_overrides.h"
#include "frame.h"
#include "plugin_loader.h"

namespace {

// One input picture and the file its codestream goes to. Directory mode names
// the output after the input, so the two belong together in one list.
struct RawImage {
    std::string inputPath;
    std::string outputPath;
};

// BMPReader -> CodecFrame: one whole image, rows padded to 4 bytes like the
// writer lays them out. Returns the pixel buffer for the caller to free, or
// null when the file does not parse.
uint8_t* ReadBmpInto(const std::string& path, halcodec::CodecFrame* in) {
    int w = 0, h = 0, n_chan = 0;
    BMPReader reader(path);
    in->data = reader.readBMP(&w, &h, &n_chan);
    if (!in->data) {
        return nullptr;
    }
    int rowPadding = (4 - ((w * n_chan) % 4)) % 4;
    in->size = (static_cast<size_t>(w) * n_chan + rowPadding) * h;
    in->width = w;
    in->height = h;
    return in->data;
}

}  // namespace

// The multi-stream encode driver: N raw inputs through one halcodec::Session,
// drained in completion order. The single-stream path below keeps the
// directory walk and the verified feed loop.
static int RunMultiStream(CommandLineParser& cli) {
    const std::vector<std::string>& inputs = cli.getInputFiles();
    const size_t n = inputs.size();

    std::string backend = cli.getBackend();
    if (backend.empty()) {
        auto names = halcodec::Registry<halcodec::Encoder>::Names();
        if (names.empty()) {
            std::cerr << "No encoder backend available" << std::endl;
            return -1;
        }
        backend = names.front();
    }
    // The Session's fan-in drains on a blocking GetFrame loop, so a backend
    // whose GetFrame only reports what FillFrame already completed has no
    // place here.
    {
        auto probe = halcodec::Encoder::Create(backend);
        if (!probe) {
            std::cerr << "Fail to create encoder backend: " << backend
                      << std::endl;
            return -1;
        }
        if (!probe->isAsync()) {
            std::cerr << "multi-stream encode needs an async backend; "
                      << backend << " is synchronous" << std::endl;
            return -1;
        }
    }
    std::cout << "encoder backend: " << backend << " (" << n << " streams)"
              << std::endl;

    std::regex pattern(R"((\d+)[xX](\d+))");
    std::smatch match;

    // One stream per -i: geometry resolved per input (CLI --width/--height
    // wins, then the WxH pattern in that input's name), everything else from
    // the CLI and therefore identical across streams.
    std::vector<halcodec::CodecParams> params(n);
    std::vector<std::pair<int64_t, int64_t>> geom(n);
    for (size_t s = 0; s < n; s++) {
        params[s].inputs.push_back(inputs[s]);
        params[s].deviceIndex = cli.getGpuIndex();
        if (!halcodec::FormatToPixelFormat(cli.getFormat(),
                                           &params[s].inputFormat)) {
            params[s].inputFormat = halcodec::PixelFormat::I420;
        }
        params[s].codec = cli.getCodec();
        halcodec::ApplyEncodeOverrides(cli, params[s].encode);
        int64_t w = cli.getWidth(), h = cli.getHeight();
        if (w <= 0 || h <= 0) {
            std::smatch m;
            if (std::regex_search(inputs[s], m, pattern)) {
                w = std::stoi(m[1].str());
                h = std::stoi(m[2].str());
            }
        }
        if (w <= 0 || h <= 0) {
            std::cerr << "Cannot determine frame dimensions for " << inputs[s]
                      << " (use --width/--height or a WxH in the name)"
                      << std::endl;
            return -1;
        }
        geom[s] = {w, h};
        params[s].width = w;
        params[s].height = h;
    }

    halcodec::Session session;
    if (session.open(halcodec::Session::Kind::Encode, backend, params) != n) {
        std::cerr << "Failed to open " << n << " encode streams on " << backend
                  << std::endl;
        return -1;
    }

    // Outputs: beside each input, or into -o when it names a directory --
    // the same rule the single-stream directory walk applies. A colliding
    // name (the same file listed twice) gets the stream index stamped in.
    const std::string outExt = halcodec::CodecStreamExtension(params[0].codec);
    std::string outDir;
    if (cli.hasOutputFile()) {
        outDir = TrimTrailingSlashes(cli.getOutputFile());
        if (!EnsureDir(outDir)) {
            return -1;
        }
    }
    // Buffers must outlive the streams that pubsetbuf into them: declared
    // before `outs`, since locals destruct in reverse order (see the note in
    // hal_dec's multi-stream path).
    std::vector<std::vector<char>> bufs(n, std::vector<char>(kIoBufBytes));
    std::vector<std::ofstream> outs(n);
    std::set<std::string> claimed;
    for (size_t s = 0; s < n; s++) {
        std::string path = outDir.empty()
                               ? StripExt(inputs[s])
                               : outDir + '/' + StripExt(BasenameOf(inputs[s]));
        path += outExt;
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

    // Feed every stream its raw frames, then its EOS. BMP inputs are one whole
    // picture; anything else is sliced on the format's frame size.
    for (size_t s = 0; s < n; s++) {
        halcodec::CodecFrame in;
        in.format = params[s].inputFormat;
        if (cli.getFormat() == "bmp") {
            in.data = ReadBmpInto(inputs[s], &in);
            if (!in.data) {
                std::cerr << "Failed to read BMP: " << inputs[s] << std::endl;
                return -1;
            }
            session.feedEncode(s, in);
            std::free(in.data);
        } else {
            std::vector<uint8_t> raw;
            if (!halcodec::ReadFile(inputs[s], &raw)) {
                std::cerr << "Cannot read " << inputs[s] << std::endl;
                return -1;
            }
            const size_t frameBytes =
                halcodec::FormatFrameBytes(cli.getFormat(), geom[s].first,
                                           geom[s].second);
            in.width = geom[s].first;
            in.height = geom[s].second;
            for (size_t off = 0; off + frameBytes <= raw.size();
                 off += frameBytes) {
                in.data = raw.data() + off;
                in.size = frameBytes;
                session.feedEncode(s, in);
            }
        }
        halcodec::CodecFrame eos;
        eos.format = params[s].inputFormat;
        session.feedEncode(s, eos);
        session.signalEncodeEOF(s);
    }

    // Fan-in drain in completion order.
    std::vector<uint64_t> perStream(n, 0);
    int total = 0;
    halcodec::CodecFrame out;
    size_t which = 0;
    while (session.getAnyEncodeFrame(out, which)) {
        halcodec::DownloadToHost(out);
        outs[which].write(reinterpret_cast<const char*>(out.data), out.size);
        perStream[which]++;
        total++;
        if (out.release) out.release();
    }
    for (size_t s = 0; s < n; s++) {
        std::cout << "stream " << s << " (" << inputs[s] << "): "
                  << perStream[s] << " frames" << std::endl;
    }
    std::cout << "Encode total frames: " << total << std::endl;

    session.close();
    return 0;
}

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    // Load vendor backends lazily so the binary runs without NVIDIA libs.
    halcodec::LoadBackends();

    // More than one -i means concurrent encode: one Session, N streams,
    // fan-in drain. The single-stream path below keeps the directory walk and
    // the streaming raw-file slicer.
    if (cli.getInputFiles().size() > 1) {
        return RunMultiStream(cli);
    }

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
    // --format -> the layout the input pixels arrive in; I420 (yuv/iyuv) is the
    // historical default for raw input.
    if (!halcodec::FormatToPixelFormat(cli.getFormat(), &params.inputFormat)) {
        params.inputFormat = halcodec::PixelFormat::I420;
    }
    if (cli.getWidth() > 0)  params.width  = cli.getWidth();
    if (cli.getHeight() > 0) params.height = cli.getHeight();
    // Without an explicit --width/--height, fall back to the WxH pattern in the
    // input filename (the historical convention for raw YUV/RGB inputs).
    if (params.width <= 0 || params.height <= 0) {
        if (std::regex_search(input, match, pattern)) {
            params.width = std::stoi(match[1].str());
            params.height = std::stoi(match[2].str());
        }
    }

    // Encoder tuning: load JSON config (if any), then apply CLI single-item
    // overrides on top.
    params.codec = cli.getCodec();
    halcodec::ApplyEncodeOverrides(cli, params.encode);

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
    const std::string outExt = halcodec::CodecStreamExtension(params.codec);
    // Where the default names go: alongside each input, or into -o when it names
    // a directory. Resolved before the scan so a bad -o is reported before
    // anything is read.
    std::string outDir;
    if (cli.hasOutputFile()) {
        outDir = TrimTrailingSlashes(cli.getOutputFile());
    }
    if (info.st_mode & S_IFDIR) {
        input = TrimTrailingSlashes(input);
        if (!outDir.empty() && !EnsureDir(outDir)) {
            return -1;
        }
        std::vector<std::string> entries;
        if (!ListRegularFiles(input, &entries)) {
            std::cerr << "Cannot open directory " << input << std::endl;
            return -1;
        }
        for (const auto& inputFileName : entries) {
            // Picture i keeps the name of the input it was made from, in
            // the output directory if one was asked for.
            std::string outputFileName =
                outDir.empty() ? StripExt(inputFileName)
                               : outDir + '/' + StripExt(BasenameOf(inputFileName));
            outputFileName += outExt;
            images.push_back({inputFileName, std::move(outputFileName)});
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

    // The BMP single-file path used to be an early return with its own copy of
    // the finalize epilogue; the flag below keeps the one epilogue at the bottom
    // and only defers the pixel-buffer free until after the final drain (an
    // async worker may still be reading in.data when FillFrame returns).
    bool bmpHeld = false;
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
                if (!ReadBmpInto(path, &in)) {
                    std::cerr << "Failed to read BMP: " << path << std::endl;
                    continue;
                }
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
    } else if (cli.getFormat() == "bmp") {
        // Single raw file holding one whole BMP image (header parsed by
        // BMPReader). The epilogue below finishes the encode.
        if (!ReadBmpInto(input, &in)) {
            std::cerr << "Failed to read BMP: " << input << std::endl;
            return -1;
        }
        enc->FillFrame(in);
        drainIfSync();
        bmpHeld = true;
    } else {
        // Single raw file: slice it into fixed-size frames of the format.
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
        const size_t frameBytes = halcodec::FormatFrameBytes(
            cli.getFormat(), params.width, params.height);
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

    // The BMP pixel buffer is only safe to free once the async worker has
    // drained; for sync backends drainIfSync() already consumed it.
    if (bmpHeld) {
        std::free(in.data);
    }
    if (outOpen) {
        fpout.close();
    }
    enc->Finalize();

    std::cout << "Encode total frames: "<< total_frames << std::endl;

    return 0;
}
