// Multi-stream demo: drive N concurrent async decode streams (and optionally N
// encode streams) through a single halcodec::Session, draining in completion
// order via getAnyDecodeFrame. Demonstrates the HAL-level multi-stream path
// built on top of the per-stream async QSV backends.
//
// Usage:
//   hal_session -b qsvdec -i a.h264 -i b.h264 [-i c.h264 ...] -o out -f yuv
// Each -i opens one stream; outputs are written to out.<n>.<fmt>.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "parse_cli.h"

#include "codec_config.h"
#include "frame.h"
#include "plugin_loader.h"
#include "session.h"

namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>* buf) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    buf->resize(static_cast<size_t>(size));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(buf->data()), size));
}

} // namespace

int main(int argc, char* argv[]) {
    // Reuse the shared CLI for -b/-f/-c/-o; -i may repeat for multi-stream.
    CommandLineParser cli;
    cli.parse(argc, argv);

#ifndef __APPLE__
    halcodec::LoadBackends();
#endif

    std::string backend = cli.getBackend();
    if (backend.empty()) {
        auto names = halcodec::Registry<halcodec::Decoder>::Names();
        if (names.empty()) {
            std::cerr << "No decoder backend available" << std::endl;
            return -1;
        }
        backend = names.front();
    }

    // parse_cli accepts repeated -i (multitoken); one stream per input.
    const std::vector<std::string>& inputs = cli.getInputFiles();
    const size_t N = inputs.size();
    if (N == 0) {
        std::cerr << "missing input file(s) (-i <file> ...)" << std::endl;
        return 2;
    }

    // Read each input fully; the async decoders are fed chunk-by-chunk below.
    std::vector<std::vector<uint8_t>> raw(N);
    for (size_t s = 0; s < N; s++) {
        if (!ReadFile(inputs[s], &raw[s])) {
            std::cerr << "Cannot read " << inputs[s] << std::endl;
            return -1;
        }
    }

    // Open N decode streams sharing the backend.
    std::vector<halcodec::CodecParams> params(N);
    for (size_t s = 0; s < N; s++) {
        params[s].inputs.push_back(inputs[s]);
        params[s].deviceIndex = cli.getGpuIndex();
        if (cli.getFormat() == "rgb" || cli.getFormat() == "rgbi") {
            params[s].outputFormat = halcodec::PixelFormat::RGB;
        } else if (cli.getFormat() == "bgr" || cli.getFormat() == "bgri") {
            params[s].outputFormat = halcodec::PixelFormat::BGR;
        } else if (cli.getFormat() == "y") {
            params[s].outputFormat = halcodec::PixelFormat::GRAY;
        }
    }

    halcodec::Session session;
    size_t opened = session.open(halcodec::Session::Kind::Decode, backend, params);
    if (opened != N) {
        std::cerr << "Failed to open " << N << " streams (got " << opened << ")"
                  << std::endl;
        return -1;
    }
    std::cout << "opened " << opened << " decode streams on " << backend
              << std::endl;

    // Feed each stream its Annex-B input in 1 MiB chunks, then signal EOF.
    const size_t kChunk = 1 << 20;
    for (size_t s = 0; s < N; s++) {
        size_t off = 0;
        while (off < raw[s].size()) {
            size_t n = std::min(kChunk, raw[s].size() - off);
            session.feedDecode(s, raw[s].data() + off, n);
            off += n;
        }
        session.signalDecodeEOF(s);
    }

    // Per-stream output files: <out>.<s>.<fmt>
    std::vector<std::ofstream> outs(N);
    for (size_t s = 0; s < N; s++) {
        std::string path = cli.getOutputFile() + "." + std::to_string(s) + "."
                           + cli.getFormat();
        outs[s].open(path, std::ios::binary);
        std::cout << path << std::endl;
    }

    // Fan-in drain: pull frames in completion order until every stream is done.
    std::vector<int> perStream(N, 0);
    int total = 0;
    halcodec::CodecFrame frame;
    size_t which = 0;
    while (session.getAnyDecodeFrame(frame, which)) {
        outs[which].write(reinterpret_cast<const char*>(frame.data), frame.size);
        perStream[which]++;
        total++;
        if (frame.release) frame.release();
    }
    for (size_t s = 0; s < N; s++) {
        std::cout << "stream " << s << ": " << perStream[s] << " frames"
                  << std::endl;
    }
    std::cout << "Decode total frames: " << total << std::endl;

    session.close();
    return 0;
}
