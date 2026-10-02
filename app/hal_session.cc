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
    auto codecFromExt = [](const std::string& path) {
        auto dot = path.find_last_of('.');
        std::string ext = (dot == std::string::npos) ? "" : path.substr(dot + 1);
        if (ext == "h264" || ext == "264") return "h264";
        if (ext == "hevc" || ext == "h265" || ext == "265") return "hevc";
        if (ext == "av1")  return "av1";
        if (ext == "jpg" || ext == "jpeg" || ext == "mjpeg"
            || ext == "mjpg" || ext == "avi") return "mjpeg";
        if (ext == "mpg2" || ext == "m2v") return "mpeg2";
        if (ext == "mp4")  return "mpeg4";
        if (ext == "vp8")  return "vp8";
        if (ext == "vp9")  return "vp9";
        return "h264"; // default
    };
    std::vector<halcodec::CodecParams> params(N);
    for (size_t s = 0; s < N; s++) {
        params[s].codec = codecFromExt(inputs[s]);
        params[s].deviceIndex = cli.getGpuIndex();
        
        // Extract SPS/PPS (or VPS/SPS/PPS for HEVC) from the input stream for backends that need extradata.
        if (params[s].codec == "h264" || params[s].codec == "hevc") {
            ExtractParameterSets(raw[s].data(), raw[s].size(), &params[s].extradata);
        }
        
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
