// VC1 Simple/Main RCV decode probe: parses an .rcv file header, builds a
// NvMediaParserSeqInfo from it, creates an NvMediaParser with that sequence
// info supplied out-of-band via pExternalSeqInfo, feeds each RCV frame record
// as a complete-picture packet, and counts BeginSequence / DecodePicture
// callbacks. Verifies the RCV-framing + external-SeqInfo path the framework's
// nvmediadecoder.cc VC1 branch uses (VC1 SP/MP carries no start codes and no
// in-band sequence header, so NvMediaParser cannot self-detect the sequence).
//
// Build (cross, against thor SDK):
//   aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include/nvmedia_6x \
//       vc1_probe.cc -o /tmp/vc1_probe \
//       -Wl,-rpath-link,/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -L/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -lnvmedia_ide_parser -lnvvideo -lnvscibuf -lnvscisync \
//       -lnvrm_surface -lnvos -lnvsocsys -ldl
// Run on nv118:
//   LD_LIBRARY_PATH=$HOME/sdk_libs /tmp/vc1_probe input.rcv [maxFrames]
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "nvmedia_parser.h"

namespace {

constexpr uint8_t kRcvVc1Type = 0x85;
constexpr uint8_t kRcvV2Mask = 0x40;

uint32_t Rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Minimal RCV header parse (mirrors NVIDIA ParseRCVHeader). Fills seq and
// returns the header byte count, or 0 on rejection.
size_t ParseRcvHeader(const uint8_t* p, size_t n, NvMediaParserSeqInfo& seq) {
    if (n < 20) return 0;
    uint32_t numFrames = p[0] | (p[1] << 8) | (p[2] << 16);
    if (numFrames == 0) return 0;
    uint8_t type = p[3];
    if ((type & ~kRcvV2Mask) != kRcvVc1Type) return 0;
    size_t cur = 4;
    uint32_t cbSeqHdr = Rd32(p + cur);
    cur += 4;
    if (cbSeqHdr == 0 || cbSeqHdr > 32 || cur + cbSeqHdr > n) return 0;
    uint8_t profile = p[cur] >> 6;
    if (profile >= 2) {
        std::cerr << "RCV is VC1 Advanced (profile " << static_cast<int>(profile)
                  << "); only Simple/Main supported" << std::endl;
        return 0;
    }
    std::memset(&seq, 0, sizeof(seq));
    seq.eCodec = NVMEDIA_VIDEO_CODEC_VC1;
    seq.bProgSeq = true;
    seq.uSequenceHeaderSize = cbSeqHdr;
    std::memcpy(seq.SequenceHeaderData, p + cur, cbSeqHdr);
    cur += cbSeqHdr;
    if (cur + 12 > n) return 0;
    uint32_t height = Rd32(p + cur);
    uint32_t width = Rd32(p + cur + 4);
    uint32_t sentinel = Rd32(p + cur + 8);
    cur += 12;
    if (sentinel != 0x0000000C || height <= 31 || height > 2048 - 32 ||
        width <= 15 || width > 4096 - 16) {
        return 0;
    }
    if (cur + 12 > n) return 0;
    uint32_t structB = Rd32(p + cur);
    uint32_t level = structB >> 29;
    if (level > 4) return 0;
    uint32_t bitrate = Rd32(p + cur + 4);
    uint32_t fps = Rd32(p + cur + 8);
    cur += 12;

    seq.uDisplayWidth = static_cast<uint16_t>(width);
    seq.uDisplayHeight = static_cast<uint16_t>(height);
    seq.uCodedWidth = static_cast<uint16_t>((width + 15) & ~15U);
    seq.uCodedHeight = static_cast<uint16_t>((height + 15) & ~15U);
    seq.eChromaFormat = NvMediaParserChromaFormat_420;
    seq.uBitrate = bitrate;
    seq.fFrameRate = (fps && fps != 0xFFFFFFFF) ? static_cast<float>(fps) : 0.0f;
    seq.uDARWidth = static_cast<uint16_t>(width);
    seq.uDARHeight = static_cast<uint16_t>(height);
    seq.eVideoFormat = NvMVideoFormat_Unspecified;
    seq.eColorPrimaries = NvMColorPrimaries_Unspecified;
    seq.eTransferCharacteristics = NvMTransferCharacteristics_Unspecified;
    seq.eMatrixCoefficients = NvMMatrixCoeffs_Unspecified;
    return cur;
}

struct Ctx {
    int beginSeq = 0;
    int decodePic = 0;
};

int32_t BeginSequenceCb(void* ctx, const NvMediaParserSeqInfo* seq) {
    static_cast<Ctx*>(ctx)->beginSeq++;
    std::cout << "BeginSequence: " << seq->uDisplayWidth << "x"
              << seq->uDisplayHeight << " coded " << seq->uCodedWidth << "x"
              << seq->uCodedHeight << " chroma=" << static_cast<int>(seq->eChromaFormat)
              << " decodeBuffers=" << seq->uDecodeBuffers << std::endl;
    return static_cast<int32_t>(seq->uDecodeBuffers);
}

NvMediaStatus DecodePictureCb(void* ctx, NvMediaParserPictureData* pd) {
    static_cast<Ctx*>(ctx)->decodePic++;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus DisplayPictureCb(void*, NvMediaRefSurface*, int64_t) {
    return NVMEDIA_STATUS_OK;
}

void UnhandledNALUCb(void*, const uint8_t*, int32_t) {}

NvMediaStatus AllocPictureBufferCb(void*, NvMediaRefSurface** p) {
    *p = nullptr;  // no IDE decoder in this probe; the parser tolerates null
    return NVMEDIA_STATUS_OK;
}

void ReleaseCb(void*, NvMediaRefSurface*) {}
void AddRefCb(void*, NvMediaRefSurface*) {}
NvMediaStatus GetBackwardUpdatesCb(void*, NvMediaVP9BackwardUpdates*) {
    return NVMEDIA_STATUS_OK;
}

const NvMediaParserClientCb* GetCallbacks() {
    static NvMediaParserClientCb cb;
    cb.BeginSequence = BeginSequenceCb;
    cb.DecodePicture = DecodePictureCb;
    cb.DisplayPicture = DisplayPictureCb;
    cb.UnhandledNALU = UnhandledNALUCb;
    cb.AllocPictureBuffer = AllocPictureBufferCb;
    cb.Release = ReleaseCb;
    cb.AddRef = AddRefCb;
    cb.GetBackwardUpdates = GetBackwardUpdatesCb;
    return &cb;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " input.rcv [maxFrames]" << std::endl;
        return 2;
    }
    int maxFrames = (argc >= 3) ? std::atoi(argv[2]) : 1 << 30;

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::cerr << "cannot open " << argv[1] << std::endl;
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long fileBytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(fileBytes);
    if (std::fread(buf.data(), 1, fileBytes, f) != static_cast<size_t>(fileBytes)) {
        std::cerr << "short read" << std::endl;
        return 1;
    }
    std::fclose(f);
    std::cout << "vc1_probe: " << argv[1] << " (" << fileBytes << " bytes)"
              << std::endl;

    NvMediaParserSeqInfo seq;
    size_t hdrLen = ParseRcvHeader(buf.data(), buf.size(), seq);
    if (hdrLen == 0) {
        std::cerr << "not a valid VC1 SP/MP RCV file" << std::endl;
        return 1;
    }
    std::cout << "RCV header: " << hdrLen << " bytes, " << seq.uDisplayWidth
              << "x" << seq.uDisplayHeight << " seqhdr=" << seq.uSequenceHeaderSize
              << "B fps=" << seq.fFrameRate << std::endl;

    Ctx ctx;
    NvMediaParserParams pp = {};
    pp.pClient = const_cast<NvMediaParserClientCb*>(GetCallbacks());
    pp.pClientCtx = &ctx;
    pp.uReferenceClockRate = 0;
    pp.uErrorThreshold = 50;
    pp.eCodec = NVMEDIA_VIDEO_CODEC_VC1;
    pp.pExternalSeqInfo = &seq;
    NvMediaParser* parser = NvMediaParserCreate(&pp);
    if (!parser) {
        std::cerr << "NvMediaParserCreate(VC1) failed" << std::endl;
        return 1;
    }
    std::cout << "NvMediaParserCreate(VC1) ok (external SeqInfo)" << std::endl;

    // Walk frame records: 4-byte LE size + 4-byte pts + payload.
    size_t pos = hdrLen;
    int fed = 0;
    int parseFails = 0;
    while (pos + 8 <= buf.size() && fed < maxFrames) {
        uint32_t frameSize = Rd32(buf.data() + pos) & 0x00FFFFFF;
        pos += 8;  // size + pts
        if (pos + frameSize > buf.size()) break;
        NvMediaBitStreamPkt pkt = {};
        pkt.pByteStream = (frameSize == 0) ? buf.data() : buf.data() + pos;
        pkt.uDataLength = (frameSize == 0) ? 1 : frameSize;
        pkt.bCompletePicture = 1;
        if (NvMediaParserParse(parser, &pkt) != NVMEDIA_STATUS_OK) {
            ++parseFails;
        }
        if (frameSize) pos += frameSize;
        ++fed;
    }
    // Flush.
    NvMediaBitStreamPkt eos = {};
    eos.bEOS = 1;
    NvMediaParserParse(parser, &eos);
    NvMediaParserFlush(parser);

    std::cout << "fed " << fed << " frames, parseFails=" << parseFails
              << ", BeginSequence=" << ctx.beginSeq
              << ", DecodePicture=" << ctx.decodePic << std::endl;
    NvMediaParserDestroy(parser);
    return (ctx.decodePic > 0) ? 0 : 2;
}
