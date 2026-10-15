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
#include "output_paths.h"

#include "codec_config.h"
#include "codec_names.h"
#include "annexb.h"
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

// Sniff the stream's codec from its parameter-set NAL types and pack what it
// carries into AVCC extradata. The scan is shared (src/annexb.h); this app
// gated on the extension-derived codec name before calling and ignores the
// result, so the bool below just keeps the original signature.
bool ExtractParameterSets(const uint8_t* data, size_t size,
                          std::vector<uint8_t>* extradata) {
    std::vector<halcodec::AnnexBNal> nals = halcodec::SplitAnnexB(data, size);
    halcodec::CodecHint codec = halcodec::DetectCodec(nals);
    if (codec == halcodec::CodecHint::kUnknown) return false;
    halcodec::AppendParamSetsAvcc(nals, codec, extradata);
    return true;
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

    // Open N decode streams sharing the backend. We feed each stream its raw
    // Annex-B bytes ourselves (below), so backends must open in feed mode:
    // pass the codec name (derived from the input extension) rather than the
    // file path. Passing inputs[] would make NVDEC/NVDEC-style backends open
    // an internal demuxer and ignore FillInput().
    // The extension picks the codec (lowercased); anything the shared table does
    // not know falls back to h264, the sample material's usual codec.
    auto codecFromExt = [](const std::string& path) {
        // CodecFromExtension takes the extension with or without its dot and in
        // any case, so no trimming happens here.
        std::string codec = halcodec::CodecFromExtension(ExtOf(path));
        return codec.empty() ? std::string("h264") : codec;
    };
    std::vector<halcodec::CodecParams> params(N);
    for (size_t s = 0; s < N; s++) {
        params[s].codec = codecFromExt(inputs[s]);
        params[s].deviceIndex = cli.getGpuIndex();
        
        // Extract SPS/PPS (or VPS/SPS/PPS for HEVC) from the input stream for backends that need extradata.
        if (params[s].codec == "h264" || params[s].codec == "hevc") {
            ExtractParameterSets(raw[s].data(), raw[s].size(), &params[s].extradata);
        }
        
        // Converting formats only; anything else leaves outputFormat Unknown so
        // the backend hands out its native layout. Shared with hal_dec's table.
        if (halcodec::IsConverterFormat(cli.getFormat())) {
            halcodec::FormatToPixelFormat(cli.getFormat(), &params[s].outputFormat);
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
        if (!halcodec::DownloadToHost(frame)) {
            std::cerr << "decoder produced a device frame this build cannot "
                         "download to host" << std::endl;
        }
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
