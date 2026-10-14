// Feed-mode parser probe: demux an IVF container (VP9/AV1) and feed each
// frame to NvMediaParserParse as its own packet with bCompletePicture=1,
// counting the DecodePicture callbacks. This isolates whether the "0/1 frame"
// symptom for VP9/AV1 raw elementary streams is a framing problem (the parser
// cannot self-split a raw frame stream that has no start codes) vs a real
// hardware/lib limit.
//
// Also supports a raw-elementary mode ("raw" codec arg) that feeds the whole
// file in fixed-size chunks with no bCompletePicture — mirroring the
// framework's PumpNext file-mode feed — to check start-code-based codecs like
// mpeg2/h264/hevc.
//
// Compare against the framework's PumpNext(), which feeds a 1 MiB raw chunk
// per NvMediaParserParse call with no bCompletePicture — fine for Annex-B
// h264/hevc (start codes let the parser split AUs internally) but ambiguous
// for VP9/AV1 raw frame streams.
//
// Build (cross, against the thor SDK headers):
//   aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include/nvmedia_6x \
//       feed_probe.cc -o feed_probe \
//       -Wl,-rpath-link,$SDK/lib-target -L$SDK/lib-target \
//       -lnvmedia_ide_parser -lnvparser -lnvrm_surface -lnvos -lnvsocsys \
//       -lnvscibuf -lnvscisync -lnvvideo -ldl
// Run on orin:
//   LD_LIBRARY_PATH=<sdk-lib-target> ./feed_probe vp9 input.ivf
//   LD_LIBRARY_PATH=<sdk-lib-target> ./feed_probe av1 input.ivf
//   LD_LIBRARY_PATH=<sdk-lib-target> ./feed_probe raw mpeg2 input.mpg
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "nvmedia_parser.h"

namespace {

struct FeedState {
    int decodePictureCount = 0;
    int beginSequenceCount = 0;
    int allocPictureCount = 0;
    int displayPictureCount = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

int32_t CbBeginSequence(void* ctx, const NvMediaParserSeqInfo* seq) {
    auto* s = static_cast<FeedState*>(ctx);
    s->beginSequenceCount++;
    if (seq) {
        s->width = seq->uDisplayWidth;
        s->height = seq->uDisplayHeight;
        std::cerr << "  BeginSequence #" << s->beginSequenceCount
                  << " " << seq->uDisplayWidth << "x" << seq->uDisplayHeight
                  << std::endl;
    }
    return 8;  // decode buffer count
}

NvMediaStatus CbDecodePicture(void* ctx, NvMediaParserPictureData* pd) {
    auto* s = static_cast<FeedState*>(ctx);
    s->decodePictureCount++;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus CbDisplayPicture(void* ctx, NvMediaRefSurface* surf, int64_t pts) {
    auto* s = static_cast<FeedState*>(ctx);
    s->displayPictureCount++;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus CbAllocPictureBuffer(void* ctx, NvMediaRefSurface** surf) {
    auto* s = static_cast<FeedState*>(ctx);
    s->allocPictureCount++;
    // Return a sentinel non-null pointer so the parser proceeds; we never
    // render, only count. Use a stable dummy address.
    *surf = reinterpret_cast<NvMediaRefSurface*>(0x1);
    return NVMEDIA_STATUS_OK;
}

void CbRelease(void* ctx, NvMediaRefSurface* surf) {}
void CbAddRef(void* ctx, NvMediaRefSurface* surf) {}

// Minimal IVF demuxer: 32-byte file header, then 12-byte per-frame records
// (4-byte size + 8-byte pts, little-endian) each followed by <size> bytes.
// Returns the list of raw frame payloads.
std::vector<std::vector<uint8_t>> DemuxIvf(const char* path) {
    std::vector<std::vector<uint8_t>> frames;
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::cerr << "feed_probe: cannot open " << path << std::endl;
        return frames;
    }
    uint8_t hdr[32];
    if (std::fread(hdr, 1, 32, f) != 32 ||
        std::memcmp(hdr, "DKIF", 4) != 0) {
        std::cerr << "feed_probe: not an IVF file (no DKIF signature)" << std::endl;
        std::fclose(f);
        return frames;
    }
    while (true) {
        uint8_t rec[12];
        size_t r = std::fread(rec, 1, 12, f);
        if (r != 12) break;
        uint32_t sz = rec[0] | (rec[1] << 8) | (rec[2] << 16) | (rec[3] << 24);
        if (sz == 0 || sz > (1u << 24)) break;
        std::vector<uint8_t> frame(sz);
        if (std::fread(frame.data(), 1, sz, f) != sz) break;
        frames.push_back(std::move(frame));
    }
    std::fclose(f);
    return frames;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " <vp9|av1> <input.ivf>" << std::endl;
        std::cerr << "       " << argv[0]
                  << " raw <mpeg2|h264|hevc> <input.es>" << std::endl;
        return 2;
    }
    const char* mode = argv[1];

    NvMediaVideoCodec codec;
    bool rawMode = false;
    if (!std::strcmp(mode, "vp9")) {
        codec = NVMEDIA_VIDEO_CODEC_VP9;
    } else if (!std::strcmp(mode, "av1")) {
        codec = NVMEDIA_VIDEO_CODEC_AV1;
    } else if (!std::strcmp(mode, "vp8")) {
        codec = NVMEDIA_VIDEO_CODEC_VP8;
    } else if (!std::strcmp(mode, "vc1")) {
        codec = NVMEDIA_VIDEO_CODEC_VC1;
    } else if (!std::strcmp(mode, "raw")) {
        rawMode = true;
        if (argc < 4) {
            std::cerr << "raw mode needs <codec> <file>" << std::endl;
            return 2;
        }
        const char* cs = argv[2];
        if (!std::strcmp(cs, "mpeg2")) codec = NVMEDIA_VIDEO_CODEC_MPEG2;
        else if (!std::strcmp(cs, "h264")) codec = NVMEDIA_VIDEO_CODEC_H264;
        else if (!std::strcmp(cs, "hevc")) codec = NVMEDIA_VIDEO_CODEC_HEVC;
        else {
            std::cerr << "raw: unsupported codec " << cs << std::endl;
            return 2;
        }
    } else {
        std::cerr << "unsupported mode " << mode << std::endl;
        return 2;
    }
    const char* path = rawMode ? argv[3] : argv[2];

    std::vector<std::vector<uint8_t>> frames;
    std::vector<uint8_t> rawFile;
    if (rawMode) {
        FILE* f = std::fopen(path, "rb");
        if (!f) {
            std::cerr << "feed_probe: cannot open " << path << std::endl;
            return 1;
        }
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        rawFile.resize(sz);
        std::fread(rawFile.data(), 1, sz, f);
        std::fclose(f);
        std::cout << "raw elementary " << sz << " bytes (" << argv[2] << ")"
                  << std::endl;
    } else {
        frames = DemuxIvf(path);
        std::cout << "IVF demuxed " << frames.size() << " frames from " << path
                  << " (" << mode << ")" << std::endl;
        if (frames.empty()) return 1;
    }

    FeedState state;
    NvMediaParserClientCb cb;
    std::memset(&cb, 0, sizeof(cb));
    cb.BeginSequence = CbBeginSequence;
    cb.DecodePicture = CbDecodePicture;
    cb.DisplayPicture = CbDisplayPicture;
    cb.AllocPictureBuffer = CbAllocPictureBuffer;
    cb.Release = CbRelease;
    cb.AddRef = CbAddRef;

    NvMediaParserParams params;
    std::memset(&params, 0, sizeof(params));
    params.pClient = &cb;
    params.pClientCtx = &state;
    params.uReferenceClockRate = 0;
    params.uErrorThreshold = 50;
    params.eCodec = codec;
    NvMediaParser* parser = NvMediaParserCreate(&params);
    if (!parser) {
        std::cerr << "NvMediaParserCreate failed for " << mode << std::endl;
        return 1;
    }

    int fed = 0;
    int parseErrors = 0;
    if (rawMode) {
        // Mirror the framework PumpNext file-mode feed: fixed-size chunks, no
        // bCompletePicture. The parser must split on start codes itself.
        const size_t kChunk = 1 << 20;
        size_t pos = 0;
        while (pos < rawFile.size()) {
            size_t n = rawFile.size() - pos < kChunk ? rawFile.size() - pos
                                                     : kChunk;
            NvMediaBitStreamPkt pkt;
            std::memset(&pkt, 0, sizeof(pkt));
            pkt.pByteStream = rawFile.data() + pos;
            pkt.uDataLength = static_cast<uint32_t>(n);
            NvMediaStatus st = NvMediaParserParse(parser, &pkt);
            if (st != NVMEDIA_STATUS_OK) parseErrors++;
            else fed++;
            pos += n;
        }
    } else {
        for (size_t i = 0; i < frames.size(); ++i) {
            NvMediaBitStreamPkt pkt;
            std::memset(&pkt, 0, sizeof(pkt));
            pkt.pByteStream = frames[i].data();
            pkt.uDataLength = static_cast<uint32_t>(frames[i].size());
            pkt.llPts = static_cast<int64_t>(i);
            pkt.bPTSValid = 1;
            pkt.bCompletePicture = 1;  // one frame per packet
            NvMediaStatus st = NvMediaParserParse(parser, &pkt);
            if (st != NVMEDIA_STATUS_OK) {
                parseErrors++;
            } else {
                fed++;
            }
        }
    }
    // EOS flush
    NvMediaBitStreamPkt eos;
    std::memset(&eos, 0, sizeof(eos));
    eos.bEOS = 1;
    NvMediaParserParse(parser, &eos);
    NvMediaParserFlush(parser);

    std::cout << "fed=" << fed
              << (rawMode ? " chunks" : " packets")
              << " parseErrors=" << parseErrors
              << " BeginSequence=" << state.beginSequenceCount
              << " AllocPicture=" << state.allocPictureCount
              << " DecodePicture=" << state.decodePictureCount
              << " DisplayPicture=" << state.displayPictureCount;
    if (state.width) {
        std::cout << " " << state.width << "x" << state.height;
    }
    std::cout << std::endl;

    NvMediaParserDestroy(parser);
    return 0;
}
