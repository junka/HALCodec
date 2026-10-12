#include "nvmediadecoder.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "nvmedia_common_decode.h"
#include "nvmedia_iep.h"
#include "registry.h"

namespace halcodec {
namespace nvmedia {

namespace {

// Map the unified codec string ("h264"/"hevc"/...) to a NvMediaVideoCodec.
// Returns static_cast<NvMediaVideoCodec>(-1) for unknown codecs.
NvMediaVideoCodec MapCodec(const std::string& codec) {
    if (codec == "h264") {
        return NVMEDIA_VIDEO_CODEC_H264;
    }
    if (codec == "hevc") {
        return NVMEDIA_VIDEO_CODEC_HEVC;
    }
    if (codec == "av1") {
        return NVMEDIA_VIDEO_CODEC_AV1;
    }
    if (codec == "jpeg") {
        return NVMEDIA_VIDEO_CODEC_MJPEG;
    }
    if (codec == "vp9") {
        return NVMEDIA_VIDEO_CODEC_VP9;
    }
    if (codec == "vp8") {
        return NVMEDIA_VIDEO_CODEC_VP8;
    }
    if (codec == "mpeg2") {
        return NVMEDIA_VIDEO_CODEC_MPEG2;
    }
    if (codec == "mpeg4") {
        return NVMEDIA_VIDEO_CODEC_MPEG4;
    }
    if (codec == "vc1") {
        return NVMEDIA_VIDEO_CODEC_VC1;
    }
    return static_cast<NvMediaVideoCodec>(-1);
}

constexpr size_t kFeedChunkSize = 1 << 20;        // 1 MiB

// Detile a decoded NvMedia IDE surface into a tightly-packed NV12 host buffer
// (Y then interleaved UV, row stride = w). Returns nullptr on failure.
// Shared by the host readout path (CopySurfaceToFrame) and the zero-copy
// DownloadToHost hook (DownloadNvSciBuf) so the detile logic exists once.
//
// NvSciBufObjGetPixels reads the BlockLinear IDE surface and detiles it into
// linear memory. The surface stores chroma semi-planar (NV12-style), but
// GetPixels on this DRIVE build only accepts a *3-plane planar* destination
// (Y, U, V as separate planes of half resolution) — a 2-plane semi-planar
// request returns BadParameter. This mirrors the nvm_ide_sci sample, which
// calls GetPixels with numSurfaces=3 and pitches/sizes
// {320,160,160}/{76800,19200,19200} for a 320x240 frame (verified via
// LD_PRELOAD interception of the sample's GetPixels call). GetPixels handles
// the CPU-cache flush internally, so no manual FlushCpuCacheRange is needed.
uint8_t* DetileNvSciBufToNV12(NvSciBufObj buf, uint32_t w, uint32_t h) {
    if (!buf || w == 0 || h == 0) {
        return nullptr;
    }
    const size_t outY = static_cast<size_t>(w) * h;
    const size_t outUV = outY / 2;
    const uint32_t halfW = w >> 1;
    const uint32_t halfH = h >> 1;
    void* planes[3] = {
        std::malloc(outY),                                  // Y:  w*h
        std::malloc(static_cast<size_t>(halfW) * halfH),    // U: halfW*halfH
        std::malloc(static_cast<size_t>(halfW) * halfH),    // V: halfW*halfH
    };
    if (!planes[0] || !planes[1] || !planes[2]) {
        std::free(planes[0]);
        std::free(planes[1]);
        std::free(planes[2]);
        return nullptr;
    }
    uint32_t sizes[3] = {w * h, halfW * halfH, halfW * halfH};
    uint32_t pitches[3] = {w, halfW, halfW};
    NvSciError gpe = NvSciBufObjGetPixels(buf, nullptr, planes, sizes, pitches);
    if (gpe != NvSciError_Success) {
        std::free(planes[0]);
        std::free(planes[1]);
        std::free(planes[2]);
        return nullptr;
    }
    // Repack planar (Y, U, V) into tightly-packed NV12 (Y, interleaved UV).
    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(outY + outUV));
    if (!buffer) {
        std::free(planes[0]);
        std::free(planes[1]);
        std::free(planes[2]);
        return nullptr;
    }
    std::memcpy(buffer, planes[0], outY);
    const uint8_t* u = static_cast<const uint8_t*>(planes[1]);
    const uint8_t* v = static_cast<const uint8_t*>(planes[2]);
    uint8_t* uv = buffer + outY;
    for (uint32_t row = 0; row < halfH; ++row) {
        uint8_t* out = uv + static_cast<size_t>(row) * w;
        const uint8_t* ur = u + static_cast<size_t>(row) * halfW;
        const uint8_t* vr = v + static_cast<size_t>(row) * halfW;
        for (uint32_t x = 0; x < halfW; ++x) {
            out[2 * x] = ur[x];
            out[2 * x + 1] = vr[x];
        }
    }
    std::free(planes[0]);
    std::free(planes[1]);
    std::free(planes[2]);
    return buffer;
}

constexpr uint64_t kFenceWaitTimeoutUs = 1000 * 1000;  // 1 s
constexpr uint32_t kSurfaceAlign = 256;

// Fill the NvSciBuf attribute list for an 8-bit NV12 surface of the given
// aligned dimensions (mirrors scibuf_utils.c PopulateNvSciBufAttrList).
bool FillNV12SurfaceAttrs(NvSciBufAttrList list, uint32_t w, uint32_t h) {
    NvSciBufType bufType = NvSciBufType_Image;
    NvSciBufAttrValAccessPerm access = NvSciBufAccessPerm_ReadWrite;
    bool needCpuAccess = true;
    NvSciBufAttrValImageLayoutType layout = NvSciBufImage_BlockLinearType;
    uint32_t planeCount = 2;
    NvSciBufAttrValColorFmt colorFormat[2] = {NvSciColor_Y8, NvSciColor_V8U8};
    NvSciBufAttrValColorStd colorStd[2] = {NvSciColorStd_REC601_ER,
                                           NvSciColorStd_REC601_ER};
    uint32_t planeWidth[2] = {w, w >> 1};
    uint32_t planeHeight[2] = {h, h >> 1};
    uint32_t baseAddrAlign[2] = {kSurfaceAlign, kSurfaceAlign};
    uint64_t padding[2] = {0, 0};
    bool vprFlag = false;
    NvSciBufAttrValImageScanType scanType = NvSciBufScan_ProgressiveType;

    NvSciBufAttrKeyValuePair attributes[] = {
        {NvSciBufGeneralAttrKey_RequiredPerm, &access, sizeof(access)},
        {NvSciBufGeneralAttrKey_Types, &bufType, sizeof(bufType)},
        {NvSciBufGeneralAttrKey_NeedCpuAccess, &needCpuAccess,
         sizeof(needCpuAccess)},
        {NvSciBufGeneralAttrKey_EnableCpuCache, &needCpuAccess,
         sizeof(needCpuAccess)},
        {NvSciBufImageAttrKey_TopPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_BottomPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_LeftPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_RightPadding, &padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_Layout, &layout, sizeof(layout)},
        {NvSciBufImageAttrKey_PlaneCount, &planeCount, sizeof(planeCount)},
        {NvSciBufImageAttrKey_PlaneColorFormat, &colorFormat,
         planeCount * sizeof(NvSciBufAttrValColorFmt)},
        {NvSciBufImageAttrKey_PlaneColorStd, &colorStd,
         planeCount * sizeof(NvSciBufAttrValColorStd)},
        {NvSciBufImageAttrKey_PlaneBaseAddrAlign, &baseAddrAlign,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneWidth, &planeWidth,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneHeight, &planeHeight,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_VprFlag, &vprFlag, sizeof(vprFlag)},
        {NvSciBufImageAttrKey_ScanType, &scanType, sizeof(scanType)},
    };

    return NvSciBufAttrListSetAttrs(
               list, attributes, sizeof(attributes) / sizeof(attributes[0])) ==
           NvSciError_Success;
}

}  // namespace

// Parser client callbacks, order must match the NvMediaParserClientCb layout
// (see nvmedia_parser.h). Each thunk forwards to the backend holding the pool.
const NvMediaParserClientCb* NvMediaDecoder::GetParserCallbacks() {
    static const NvMediaParserClientCb callbacks = {
        NvMediaDecoder::BeginSequenceCb,
        NvMediaDecoder::DecodePictureCb,
        NvMediaDecoder::DisplayPictureCb,
        NvMediaDecoder::UnhandledNALUCb,
        NvMediaDecoder::AllocPictureBufferCb,
        NvMediaDecoder::ReleaseCb,
        NvMediaDecoder::AddRefCb,
        nullptr,  // CreateDecrypter
        nullptr,  // DecryptHdr
        nullptr,  // SliceDecode
        nullptr,  // GetClearHdr
        NvMediaDecoder::GetBackwardUpdatesCb,
        nullptr,  // GetDpbInfoForMetadata
    };
    return &callbacks;
}

NvMediaDecoder::~NvMediaDecoder() {
    Finalize();
}

bool NvMediaDecoder::Initialize(const CodecParams& params) {
    codec_ = MapCodec(params.codec);
    if (codec_ == static_cast<NvMediaVideoCodec>(-1)) {
        std::cerr << "NvMediaDecoder: unsupported codec: " << params.codec
                  << std::endl;
        return false;
    }
    zeroCopy_ = params.zeroCopy;

    // JPEG has no NvMediaParser and the IDE video decoder rejects MJPEG at
    // NvMediaIDECreate. Route it to the dedicated IJPD image-JPEG engine,
    // which decodes one JPEG bitstream at a time into a YUV420 surface.
    if (codec_ == NVMEDIA_VIDEO_CODEC_MJPEG) {
        if (params.inputs.empty()) {
            std::cerr << "NvMediaDecoder: JPEG decode needs a file input"
                      << std::endl;
            return false;
        }
        inputFile_ = fopen(params.inputs[0].c_str(), "rb");
        if (!inputFile_) {
            std::cerr << "NvMediaDecoder: cannot open " << params.inputs[0]
                      << std::endl;
            return false;
        }
        // The whole file is buffered and split into JPEGs (SOI..EOI) in
        // PumpNext; the 1 MiB chunk_ buffer is reused as the read buffer.
        chunk_.resize(kFeedChunkSize);
        return true;
    }

    // With no input path the caller supplies the compressed bytes itself via
    // FillInput()/SignalInputComplete() (the same feed-mode contract NVDEC
    // uses, so a decode->encode chain can hand the same buffer to both).
    if (params.inputs.empty()) {
        feedMode_ = true;
    } else {
        inputFile_ = fopen(params.inputs[0].c_str(), "rb");
        if (!inputFile_) {
            std::cerr << "NvMediaDecoder: cannot open " << params.inputs[0]
                      << std::endl;
            return false;
        }
    }

    // VC1 Simple/Main has no start codes and no in-band sequence header, so
    // NvMediaParser cannot self-split a raw byte feed nor self-detect the
    // sequence. An RCV container carries both the out-of-band sequence header
    // (handed to NvMediaParserCreate via pExternalSeqInfo) and length-prefixed
    // per-frame records. File mode parses the RCV header here and creates the
    // parser with the external SeqInfo; feed mode is not supported for VC1
    // (the caller would have to supply the SeqInfo separately).
    if (codec_ == NVMEDIA_VIDEO_CODEC_VC1) {
        if (feedMode_) {
            std::cerr << "NvMediaDecoder: VC1 SP/MP needs an .rcv file input "
                         "(no feed-mode SeqInfo)" << std::endl;
            return false;
        }
        // The RCV header is small (≤ ~44 bytes for SP/MP); read a buffer that
        // always covers it plus the first frame record.
        uint8_t hdr[256];
        size_t got = std::fread(hdr, 1, sizeof(hdr), inputFile_);
        size_t consumed = 0;
        if (got < 8 || !ParseRcvHeader(hdr, got, consumed)) {
            std::cerr << "NvMediaDecoder: VC1 input is not a valid RCV file "
                         "(SP/MP needs .rcv; Advanced start-code streams are "
                         "not supported)" << std::endl;
            return false;
        }
        // Reposition the file just past the RCV header so PumpNext's fread
        // loop reads frame records.
        std::fseek(inputFile_, static_cast<long>(consumed), SEEK_SET);
        feedCompletePicture_ = true;
        NvMediaParserParams parserParams = {};
        parserParams.pClient =
            const_cast<NvMediaParserClientCb*>(GetParserCallbacks());
        parserParams.pClientCtx = this;
        parserParams.uReferenceClockRate = 0;
        parserParams.uErrorThreshold = 50;
        parserParams.eCodec = codec_;
        parserParams.pExternalSeqInfo = &rcvSeqInfo_;
        parser_ = NvMediaParserCreate(&parserParams);
        if (!parser_) {
            std::cerr << "NvMediaDecoder: NvMediaParserCreate(VC1) failed"
                      << std::endl;
            return false;
        }
        chunk_.resize(kFeedChunkSize);
        return true;
    }

    // VP9/AV1 raw elementary streams carry no start codes, so NvMediaParser
    // cannot split frames out of a continuous byte feed. Such inputs must be
    // packetized (one frame per NvMediaParserParse call with bCompletePicture=1).
    // For file mode this means demuxing an IVF container; for feed mode the
    // caller (hal_dec's IvfPayloadReader) already hands whole frames.
    feedCompletePicture_ = (codec_ == NVMEDIA_VIDEO_CODEC_VP9 ||
                            codec_ == NVMEDIA_VIDEO_CODEC_AV1 ||
                            codec_ == NVMEDIA_VIDEO_CODEC_VP8);

    NvMediaParserParams parserParams = {};
    parserParams.pClient =
        const_cast<NvMediaParserClientCb*>(GetParserCallbacks());
    parserParams.pClientCtx = this;
    parserParams.uReferenceClockRate = 0;
    parserParams.uErrorThreshold = 50;
    parserParams.eCodec = codec_;
    parser_ = NvMediaParserCreate(&parserParams);
    if (!parser_) {
        std::cerr << "NvMediaDecoder: NvMediaParserCreate failed" << std::endl;
        return false;
    }

    chunk_.resize(kFeedChunkSize);
    return true;
}

// The parser drives the pipeline: the IDE decoder and its surface pool are
// created here once the coded resolution is known, exactly like the
// videodemo.c cbBeginSequence. Returns the number of decode buffers required.
int32_t NvMediaDecoder::BeginSequenceCb(void* ctx,
                                        const NvMediaParserSeqInfo* seq) {
    NvMediaDecoder* self = static_cast<NvMediaDecoder*>(ctx);
    if (!self || !seq) {
        return 0;
    }
    self->decodeBuffers_ = seq->uDecodeBuffers;
    self->displayWidth_ = seq->uDisplayWidth;
    self->displayHeight_ = seq->uDisplayHeight;
    self->SetupDecoder(seq);
    return static_cast<int32_t>(self->decodeBuffers_);
}

bool NvMediaDecoder::SetupDecoder(const NvMediaParserSeqInfo* seq) {
    if (decoder_) {
        // Resolution change: reuse the existing pipeline. NvMediaIDE supports
        // dynamic resolution change via NvMediaIDEReconfigure; the re-setup
        // path is a DRIVE tuning item, so the first configuration wins here.
        std::cerr << "NvMediaDecoder: resolution change not supported yet"
                  << std::endl;
        return false;
    }
    if (!seq) {
        return false;
    }

    uint32_t w = (seq->uCodedWidth + 15) & ~15U;
    uint32_t h = (seq->uCodedHeight + 15) & ~15U;
    codedWidth_ = w;
    codedHeight_ = h;

    NvSciError serr = NvSciBufModuleOpen(&bufModule_);
    if (serr != NvSciError_Success || !bufModule_) {
        std::cerr << "NvMediaDecoder: NvSciBufModuleOpen failed" << std::endl;
        return false;
    }
    serr = NvSciSyncModuleOpen(&syncModule_);
    if (serr != NvSciError_Success || !syncModule_) {
        std::cerr << "NvMediaDecoder: NvSciSyncModuleOpen failed" << std::endl;
        return false;
    }
    serr = NvSciSyncCpuWaitContextAlloc(syncModule_, &cpuWaitContext_);
    if (serr != NvSciError_Success || !cpuWaitContext_) {
        std::cerr << "NvMediaDecoder: NvSciSyncCpuWaitContextAlloc failed"
                  << std::endl;
        return false;
    }

    // Create the IDE decoder. maxBitstreamSize is a hint; cap it when the
    // parser does not report one.
    uint16_t maxRefs = (decodeBuffers_ > 0 && decodeBuffers_ <= 17)
                           ? static_cast<uint16_t>(decodeBuffers_ - 1)
                           : 16;
    uint64_t maxBitstream = (seq->uMaxBitstreamSize)
                                ? static_cast<uint64_t>(seq->uMaxBitstreamSize)
                                : 25ull * 1024 * 1024;
    decoder_ = NvMediaIDECreate(codec_, static_cast<uint16_t>(w),
                                static_cast<uint16_t>(h), maxRefs,
                                maxBitstream, 5, 0u, instanceId_);
    if (!decoder_) {
        std::cerr << "NvMediaDecoder: NvMediaIDECreate failed" << std::endl;
        return false;
    }

    // Set up the EOF synchronization: IDE signals, CPU waits. Uses the same
    // 3-list reconcile pattern as videodemo.c (Signaler + CpuWaiter + Waiter).
    NvSciSyncAttrList ideSignaler = nullptr;
    NvSciSyncAttrList cpuWaiter = nullptr;
    NvSciSyncAttrList ideWaiter = nullptr;
    NvSciSyncAttrList reconciled = nullptr;
    NvSciSyncAttrList conflict = nullptr;

    if (NvSciSyncAttrListCreate(syncModule_, &ideSignaler) !=
            NvSciError_Success ||
        NvSciSyncAttrListCreate(syncModule_, &cpuWaiter) != NvSciError_Success ||
        NvSciSyncAttrListCreate(syncModule_, &ideWaiter) !=
            NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciSyncAttrListCreate failed"
                  << std::endl;
        return false;
    }

    if (NvMediaIDEFillNvSciSyncAttrList(decoder_, ideSignaler,
                                        NVMEDIA_SIGNALER) != NVMEDIA_STATUS_OK ||
        NvMediaIDEFillNvSciSyncAttrList(decoder_, ideWaiter, NVMEDIA_WAITER) !=
            NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: NvMediaIDEFillNvSciSyncAttrList failed"
                  << std::endl;
        return false;
    }

    bool cpuAccess = true;
    NvSciSyncAccessPerm cpuPerm = NvSciSyncAccessPerm_WaitOnly;
    NvSciSyncAttrKeyValuePair cpuAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &cpuPerm, sizeof(cpuPerm)},
    };
    if (NvSciSyncAttrListSetAttrs(cpuWaiter, cpuAttrs, 2) !=
        NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciSyncAttrListSetAttrs failed"
                  << std::endl;
        return false;
    }

    NvSciSyncAttrList unreconciled[3] = {ideSignaler, cpuWaiter, ideWaiter};
    if (NvSciSyncAttrListReconcile(unreconciled, 3, &reconciled, &conflict) !=
        NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciSyncAttrListReconcile failed"
                  << std::endl;
        return false;
    }
    if (NvSciSyncObjAlloc(reconciled, &eofSyncObj_) != NvSciError_Success ||
        !eofSyncObj_) {
        std::cerr << "NvMediaDecoder: NvSciSyncObjAlloc failed" << std::endl;
        return false;
    }
    NvSciSyncAttrListFree(reconciled);
    NvSciSyncAttrListFree(conflict);
    NvSciSyncAttrListFree(ideSignaler);
    NvSciSyncAttrListFree(cpuWaiter);
    NvSciSyncAttrListFree(ideWaiter);

    if (NvMediaIDERegisterNvSciSyncObj(decoder_, NVMEDIA_EOF_PRESYNCOBJ,
                                       eofSyncObj_) != NVMEDIA_STATUS_OK ||
        NvMediaIDESetNvSciSyncObjforEOF(decoder_, eofSyncObj_) !=
            NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: register/set EOF sync obj failed"
                  << std::endl;
        return false;
    }

    // Create the decoded surface pool (decode buffers + slack) and register
    // every NvSciBufObj with the decoder.
    //
    // The decoded NvSciBufObj may be consumed zero-copy by a downstream IEP
    // encoder (FeedDeviceFrame registers it with NvMediaIEPRegisterNvSciBufObj).
    // IEPRegisterNvSciBufObj validates the buffer against the encoder's required
    // engine-access constraints, which NvMediaIDEFillNvSciBufAttrList alone does
    // not satisfy (the IDE fill grants decoder-engine access only, so the IEP
    // rejects the surface with NVMEDIA_STATUS_ERROR). To make a surface usable
    // by both engines, reconcile the IDE-filled and IEP-filled attribute lists
    // together — the resulting reconciled list carries the union of both
    // engines' access requirements (mirrors the producer+consumer combine in
    // the e2e multicast sample's CPoolManager).
    NvSciBufAttrList ideAttrs = nullptr;
    NvSciBufAttrList iepAttrs = nullptr;
    NvSciBufAttrList bufReconciled = nullptr;
    NvSciBufAttrList bufConflict = nullptr;
    if (NvSciBufAttrListCreate(bufModule_, &ideAttrs) != NvSciError_Success ||
        NvSciBufAttrListCreate(bufModule_, &iepAttrs) != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciBufAttrListCreate failed"
                  << std::endl;
        if (ideAttrs) NvSciBufAttrListFree(ideAttrs);
        if (iepAttrs) NvSciBufAttrListFree(iepAttrs);
        return false;
    }
    if (NvMediaIDEFillNvSciBufAttrList(instanceId_, ideAttrs) !=
            NVMEDIA_STATUS_OK ||
        !FillNV12SurfaceAttrs(ideAttrs, w, h)) {
        std::cerr << "NvMediaDecoder: fill IDE surface attrs failed"
                  << std::endl;
        NvSciBufAttrListFree(ideAttrs);
        NvSciBufAttrListFree(iepAttrs);
        return false;
    }
    // IEP fill adds encoder-engine access constraints; the image attributes are
    // identical to the IDE side (same NV12 BL geometry) so the reconcile is a
    // pure union of engine permissions. IEPFill requires an encoder instance id;
    // instance 0 is always valid and only affects engine routing, not geometry.
    if (NvMediaIEPFillNvSciBufAttrList(NVMEDIA_ENCODER_INSTANCE_0, iepAttrs) !=
            NVMEDIA_STATUS_OK ||
        !FillNV12SurfaceAttrs(iepAttrs, w, h)) {
        std::cerr << "NvMediaDecoder: fill IEP surface attrs failed"
                  << std::endl;
        NvSciBufAttrListFree(ideAttrs);
        NvSciBufAttrListFree(iepAttrs);
        return false;
    }
    NvSciBufAttrList bufUnreconciled[2] = {ideAttrs, iepAttrs};
    if (NvSciBufAttrListReconcile(bufUnreconciled, 2, &bufReconciled,
                                  &bufConflict) != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciBufAttrListReconcile failed"
                  << std::endl;
        NvSciBufAttrListFree(ideAttrs);
        NvSciBufAttrListFree(iepAttrs);
        return false;
    }
    NvSciBufAttrListFree(ideAttrs);
    NvSciBufAttrListFree(iepAttrs);

    uint32_t nBuffers = decodeBuffers_ + 4;  // decoded pool plus display slack
    if (nBuffers < 8) {
        nBuffers = 8;
    }
    if (nBuffers > 32) {
        nBuffers = 32;
    }
    pool_.resize(nBuffers);
    for (uint32_t i = 0; i < nBuffers; ++i) {
        NvSciBufObj obj = nullptr;
        if (NvSciBufObjAlloc(bufReconciled, &obj) != NvSciError_Success ||
            !obj) {
            std::cerr << "NvMediaDecoder: NvSciBufObjAlloc failed" << std::endl;
            pool_.resize(i);
            break;
        }
        if (NvMediaIDERegisterNvSciBufObj(decoder_, obj) != NVMEDIA_STATUS_OK) {
            std::cerr << "NvMediaDecoder: NvMediaIDERegisterNvSciBufObj failed"
                      << std::endl;
            NvSciBufObjFree(obj);
            pool_.resize(i);
            break;
        }
        pool_[i].buf = obj;
        pool_[i].width = w;
        pool_[i].height = h;
    }
    NvSciBufAttrListFree(bufReconciled);
    if (bufConflict) {
        NvSciBufAttrListFree(bufConflict);
    }

    if (pool_.empty()) {
        std::cerr << "NvMediaDecoder: no surface pool" << std::endl;
        return false;
    }
    return true;
}

NvMediaStatus NvMediaDecoder::AllocPictureBufferCb(void* ctx,
                                                   NvMediaRefSurface** p) {
    NvMediaDecoder* self = static_cast<NvMediaDecoder*>(ctx);
    for (Surface& s : self->pool_) {
        // A slot is reusable only once the parser has released it (refCount
        // == 0) AND no outstanding zero-copy frame still borrows its buf.
        if (s.refCount == 0 && s.borrowed == 0) {
            s.refCount = 1;
            *p = reinterpret_cast<NvMediaRefSurface*>(&s);
            return NVMEDIA_STATUS_OK;
        }
    }
    // Should be unreachable: DisplayPictureCb falls back to a host copy once
    // borrowed slots reach the pool slack, so the parser always has free DPB
    // slots available. If we get here, the parser over-referenced beyond its
    // declared decodeBuffers_ — treat it as a fatal decode error.
    std::cerr << "NvMediaDecoder: picture pool exhausted (pool="
              << self->pool_.size() << ")" << std::endl;
    return NVMEDIA_STATUS_ERROR;
}

void NvMediaDecoder::ReleaseCb(void* ctx, NvMediaRefSurface* s) {
    // Parser releases a surface; if the reference count drops to zero it can
    // be reused for a future picture. Fences are not cleared here: the next
    // use of this slot re-renders into it, and stale fences are overwritten
    // by the fresh DecodePicture flow.
    (void)ctx;
    Surface* slot = reinterpret_cast<Surface*>(s);
    if (slot && slot->refCount > 0) {
        --slot->refCount;
    }
}

void NvMediaDecoder::AddRefCb(void* ctx, NvMediaRefSurface* s) {
    (void)ctx;
    Surface* slot = reinterpret_cast<Surface*>(s);
    if (slot) {
        ++slot->refCount;
    }
}

void NvMediaDecoder::UnhandledNALUCb(void* ctx, const uint8_t* buf,
                                     int32_t size) {
    (void)ctx;
    (void)buf;
    (void)size;
}

NvMediaStatus NvMediaDecoder::GetBackwardUpdatesCb(
    void* ctx, NvMediaVP9BackwardUpdates* backwardUpdate) {
    NvMediaDecoder* self = static_cast<NvMediaDecoder*>(ctx);
    if (!self->decoder_) {
        return NVMEDIA_STATUS_ERROR;
    }
    return NvMediaIDEGetBackwardUpdates(self->decoder_, backwardUpdate);
}

void NvMediaDecoder::UpdateReferenceFrames(NvMediaParserPictureData* pd) {
    // Convert the pool-slot pointers carried by the parser into the real
    // NvSciBufObj handles and insert pre-fences for reference surfaces,
    // mirroring videodemo.c UpdateNvMediaSurfacePictureInfo{*}. Only the
    // codecs exercised by the HAL (h264/hevc/vp9) are wired here; others fall
    // through and decode without reference filtering (DRIVE tuning item).
    switch (codec_) {
        case NVMEDIA_VIDEO_CODEC_H264: {
            NvMediaPictureInfoH264* info =
                reinterpret_cast<NvMediaPictureInfoH264*>(
                    &pd->CodecSpecificInfo.h264);
            for (uint32_t i = 0; i < 16; ++i) {
                Surface* slot =
                    reinterpret_cast<Surface*>(info->referenceFrames[i].surface);
                if (slot) {
                    NvMediaIDEInsertPreNvSciSyncFence(decoder_, &slot->fence);
                    info->referenceFrames[i].surface = slot->buf;
                }
            }
            break;
        }
        case NVMEDIA_VIDEO_CODEC_HEVC: {
            NvMediaPictureInfoH265* info =
                reinterpret_cast<NvMediaPictureInfoH265*>(
                    &pd->CodecSpecificInfo.hevc);
            for (uint32_t i = 0; i < 16; ++i) {
                Surface* slot = reinterpret_cast<Surface*>(info->RefPics[i]);
                if (slot) {
                    NvMediaIDEInsertPreNvSciSyncFence(decoder_, &slot->fence);
                    info->RefPics[i] = slot->buf;
                }
            }
            break;
        }
        case NVMEDIA_VIDEO_CODEC_VP9: {
            NvMediaPictureInfoVP9* info =
                reinterpret_cast<NvMediaPictureInfoVP9*>(
                    &pd->CodecSpecificInfo.vp9);
            Surface* refs[3] = {
                reinterpret_cast<Surface*>(info->LastReference),
                reinterpret_cast<Surface*>(info->GoldenReference),
                reinterpret_cast<Surface*>(info->AltReference),
            };
            for (Surface* slot : refs) {
                if (slot) {
                    NvMediaIDEInsertPreNvSciSyncFence(decoder_, &slot->fence);
                }
            }
            // ref0/1/2 sizes are the coded dims; already carried by the pool.
            info->ref0_width = refs[0] ? refs[0]->width : info->width;
            info->ref0_height = refs[0] ? refs[0]->height : info->height;
            info->ref1_width = refs[1] ? refs[1]->width : info->width;
            info->ref1_height = refs[1] ? refs[1]->height : info->height;
            info->ref2_width = refs[2] ? refs[2]->width : info->width;
            info->ref2_height = refs[2] ? refs[2]->height : info->height;
            info->LastReference = refs[0] ? refs[0]->buf : nullptr;
            info->GoldenReference = refs[1] ? refs[1]->buf : nullptr;
            info->AltReference = refs[2] ? refs[2]->buf : nullptr;
            break;
        }
        case NVMEDIA_VIDEO_CODEC_VP8: {
            // VP8 mirrors VP9's three reference surfaces (Last/Golden/Alt) but
            // carries no coded-dim fields, so only the pointer translation and
            // pre-fence are needed.
            NvMediaPictureInfoVP8* info =
                reinterpret_cast<NvMediaPictureInfoVP8*>(
                    &pd->CodecSpecificInfo.vp8);
            Surface* refs[3] = {
                reinterpret_cast<Surface*>(info->LastReference),
                reinterpret_cast<Surface*>(info->GoldenReference),
                reinterpret_cast<Surface*>(info->AltReference),
            };
            for (Surface* slot : refs) {
                if (slot) {
                    NvMediaIDEInsertPreNvSciSyncFence(decoder_, &slot->fence);
                }
            }
            info->LastReference = refs[0] ? refs[0]->buf : nullptr;
            info->GoldenReference = refs[1] ? refs[1]->buf : nullptr;
            info->AltReference = refs[2] ? refs[2]->buf : nullptr;
            break;
        }
        case NVMEDIA_VIDEO_CODEC_MPEG2: {
            // MPEG-1/2 carry forward/backward reference surfaces that the
            // renderer dereferences; translate them to NvSciBufObj + pre-fence.
            NvMediaPictureInfoMPEG1Or2* info =
                reinterpret_cast<NvMediaPictureInfoMPEG1Or2*>(
                    &pd->CodecSpecificInfo.mpeg2);
            Surface* fwd = reinterpret_cast<Surface*>(info->forward_reference);
            Surface* bwd = reinterpret_cast<Surface*>(info->backward_reference);
            if (fwd) {
                NvMediaIDEInsertPreNvSciSyncFence(decoder_, &fwd->fence);
                info->forward_reference = fwd->buf;
            }
            if (bwd) {
                NvMediaIDEInsertPreNvSciSyncFence(decoder_, &bwd->fence);
                info->backward_reference = bwd->buf;
            }
            break;
        }
        case NVMEDIA_VIDEO_CODEC_MPEG4: {
            NvMediaPictureInfoMPEG4Part2* info =
                reinterpret_cast<NvMediaPictureInfoMPEG4Part2*>(
                    &pd->CodecSpecificInfo.mpeg4);
            Surface* fwd = reinterpret_cast<Surface*>(info->forward_reference);
            Surface* bwd = reinterpret_cast<Surface*>(info->backward_reference);
            if (fwd) {
                NvMediaIDEInsertPreNvSciSyncFence(decoder_, &fwd->fence);
                info->forward_reference = fwd->buf;
            }
            if (bwd) {
                NvMediaIDEInsertPreNvSciSyncFence(decoder_, &bwd->fence);
                info->backward_reference = bwd->buf;
            }
            break;
        }
        case NVMEDIA_VIDEO_CODEC_VC1: {
            // VC1 carries forward/backward references (P/B frames) plus a
            // range_mapped surface; translate all three pool-slot pointers to
            // NvSciBufObj and pre-fence the motion refs. range_mapped takes no
            // fence (mirrors videodemo.c UpdateNvMediaSurfacePictureInfoVC1).
            NvMediaPictureInfoVC1* info =
                reinterpret_cast<NvMediaPictureInfoVC1*>(
                    &pd->CodecSpecificInfo.vc1);
            Surface* fwd = reinterpret_cast<Surface*>(info->forward_reference);
            Surface* bwd = reinterpret_cast<Surface*>(info->backward_reference);
            Surface* rm = reinterpret_cast<Surface*>(info->range_mapped);
            if (fwd) {
                NvMediaIDEInsertPreNvSciSyncFence(decoder_, &fwd->fence);
                info->forward_reference = fwd->buf;
            }
            if (bwd) {
                NvMediaIDEInsertPreNvSciSyncFence(decoder_, &bwd->fence);
                info->backward_reference = bwd->buf;
            }
            if (rm) {
                info->range_mapped = rm->buf;
            }
            break;
        }
        case NVMEDIA_VIDEO_CODEC_AV1: {
            // Same pool-slot -> NvSciBufObj translation as VP9: the parser
            // carries NvMediaRefSurface* (our Surface*) in RefPics[8], and
            // NvMediaIDEDecoderRender needs the real NvSciBufObj + a pre-fence
            // on each referenced surface. Without this the renderer
            // dereferences the raw slot pointer and aborts.
            NvMediaPictureInfoAV1* info =
                reinterpret_cast<NvMediaPictureInfoAV1*>(
                    &pd->CodecSpecificInfo.av1);
            for (uint32_t i = 0; i < 8; ++i) {
                Surface* slot =
                    reinterpret_cast<Surface*>(info->RefPics[i]);
                if (slot) {
                    NvMediaIDEInsertPreNvSciSyncFence(decoder_, &slot->fence);
                    info->RefPics[i] = slot->buf;
                }
            }
            break;
        }
        default:
            break;
    }
}

NvMediaStatus NvMediaDecoder::DecodePictureCb(void* ctx,
                                              NvMediaParserPictureData* pd) {
    NvMediaDecoder* self = static_cast<NvMediaDecoder*>(ctx);
    self->OnDecodePicture(pd);
    return NVMEDIA_STATUS_OK;
}

void NvMediaDecoder::OnDecodePicture(NvMediaParserPictureData* pd) {
    if (!pd || !decoder_) {
        return;
    }

    Surface* target = reinterpret_cast<Surface*>(pd->pCurrPic);
    if (!target || !target->buf) {
        std::cerr << "NvMediaDecoder: no target surface" << std::endl;
        return;
    }

    UpdateReferenceFrames(pd);

    NvMediaBitstreamBuffer bitstream = {};
    bitstream.bitstream =
        const_cast<uint8_t*>(pd->pBitstreamData);
    bitstream.bitstreamBytes = pd->uBitstreamDataLen;

    NvMediaStatus status = NvMediaIDEDecoderRender(
        decoder_, target->buf, reinterpret_cast<NvMediaPictureInfo*>(
                                   &pd->CodecSpecificInfo),
        nullptr, 1, &bitstream, nullptr, instanceId_);
    if (status != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: NvMediaIDEDecoderRender failed: "
                  << status << std::endl;
        return;
    }

    NvMediaIDEFrameStatus frameStatus = {};
    NvMediaIDEGetFrameDecodeStatus(decoder_, 0, &frameStatus);
    if (frameStatus.decode_error) {
        std::cerr << "NvMediaDecoder: decode error on frame" << std::endl;
    }

    status = NvMediaIDEGetEOFNvSciSyncFence(decoder_, eofSyncObj_,
                                            &target->fence);
    if (status != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: NvMediaIDEGetEOFNvSciSyncFence failed"
                  << std::endl;
        return;
    }

    // Wait for the decode to complete so the surface is valid before the
    // display callback reads it. The pixel copy itself (CopySurfaceToFrame,
    // via NvSciBufObjGetPixels) runs in cbDisplayPicture — display order —
    // once the parser has released the surface, matching the nvm_ide_sci
    // sample's WriteOutput placement.
    if (NvSciSyncFenceWait(&target->fence, cpuWaitContext_,
                           kFenceWaitTimeoutUs) != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciSyncFenceWait failed" << std::endl;
        return;
    }
}

NvMediaStatus NvMediaDecoder::DisplayPictureCb(void* ctx, NvMediaRefSurface* s,
                                               int64_t pts) {
    NvMediaDecoder* self = static_cast<NvMediaDecoder*>(ctx);
    if (!self || !s) {
        return NVMEDIA_STATUS_OK;
    }
    Surface* target = reinterpret_cast<Surface*>(s);
    if (!target || !target->buf) {
        return NVMEDIA_STATUS_OK;
    }
    // The EOF fence was captured in cbDecodePicture and waited there; wait
    // again here is cheap (already signalled) before reading the surface.
    if (NvSciSyncFenceWait(&target->fence, self->cpuWaitContext_,
                           kFenceWaitTimeoutUs) != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: display fence wait failed" << std::endl;
        return NVMEDIA_STATUS_OK;
    }
    NvSciSyncFenceClear(&target->fence);

    // In zero-copy mode, hand the GPU surface directly to the consumer (no
    // detile/copy); otherwise read it out into a host NV12 buffer.
    //
    // A single NvMediaParserParse call can surface several frames at once
    // (each borrowing a slot) while the parser also keeps DPB slots ref'd.
    // Borrowing unboundedly would exhaust the picture pool and kill the
    // stream. Cap outstanding borrows to the pool slack (minus a margin for
    // the parser's transient references); when at the cap, fall back to a
    // host copy for this frame instead of borrowing. The app drains outQueue_
    // each round, so borrowed recovers and subsequent frames borrow again.
    bool borrow = self->zeroCopy_;
    if (borrow) {
        int32_t outstanding = 0;
        for (const Surface& s : self->pool_) { outstanding += s.borrowed; }
        // Reserve decodeBuffers_ (DPB) + 1 margin out of the pool for the
        // parser; borrow only what remains.
        const int32_t slack = static_cast<int32_t>(self->pool_.size())
                              - static_cast<int32_t>(self->decodeBuffers_) - 1;
        if (slack <= 0 || outstanding >= slack) {
            borrow = false;  // fall back to host copy this frame
        }
    }
    CodecFrame frame = borrow ? self->BorrowSurfaceToFrame(target)
                              : self->CopySurfaceToFrame(target, nullptr);
    bool valid = borrow ? (frame.locality != FrameLocality::Host)
                        : (frame.data != nullptr);
    if (valid) {
        frame.pts = pts;
        self->outQueue_.push_back(std::move(frame));
    }
    return NVMEDIA_STATUS_OK;
}

CodecFrame NvMediaDecoder::BorrowSurfaceToFrame(Surface* s) {
    // Hand the decoded NvSciBufObj to the consumer without detiling/copying.
    // The surface is borrowed: borrowed++ keeps AllocPictureBufferCb from
    // reusing this slot until the consumer drops the frame (release decrements
    // it). The fence was already waited in DisplayPictureCb, so the surface
    // contents are valid at handoff. The consumer detiles on demand via
    // DownloadToHost -> RegisterNvSciBufDownload (see DownloadNvSciBuf below).
    CodecFrame frame;
    if (!s || !s->buf) {
        return frame;
    }
    ++s->borrowed;
    NvSciBufObj buf = s->buf;
    Surface* slot = s;
    frame.locality = FrameLocality::NvSciBufObj;
    frame.device.nvSciBufObj = buf;
    frame.width = static_cast<int>(displayWidth_ ? displayWidth_ : s->width);
    frame.height = static_cast<int>(displayHeight_ ? displayHeight_ : s->height);
    frame.format = PixelFormat::NV12;
    frame.size = 0;  // device-resident; host size unknown until downloaded
    frame.release = [slot]() {
        if (slot->borrowed > 0) {
            --slot->borrowed;
        }
    };
    return frame;
}

CodecFrame NvMediaDecoder::CopySurfaceToFrame(Surface* s,
                                              NvMediaParserPictureData*) {
    CodecFrame frame;
    if (!s || !s->buf) {
        return frame;
    }

    const uint32_t w = s->width;
    const uint32_t h = s->height;
    uint8_t* buffer = DetileNvSciBufToNV12(s->buf, w, h);
    if (!buffer) {
        std::cerr << "NvMediaDecoder: NvSciBufObjGetPixels/detile failed"
                  << std::endl;
        return frame;
    }

    frame.data = buffer;
    frame.size = static_cast<size_t>(w) * h * 3 / 2;
    frame.width = static_cast<int>(displayWidth_ ? displayWidth_ : w);
    frame.height = static_cast<int>(displayHeight_ ? displayHeight_ : h);
    frame.format = PixelFormat::NV12;
    frame.strides[0] = w;
    frame.strides[1] = w;
    frame.release = [buffer]() { std::free(buffer); };
    return frame;
}

int NvMediaDecoder::FillInput(const uint8_t* data, size_t size) {
    if (error_ || finalized_) {
        return -1;
    }
    if (!feedMode_) {
        // Not opened for explicit feeding: the internal file source drives
        // parsing instead (see PullFrames).
        return -1;
    }
    NvMediaBitStreamPkt packet = {};
    packet.pByteStream = data;
    packet.uDataLength = static_cast<uint32_t>(size);
    // VP9/AV1 have no start codes: each FillInput span must be a whole frame
    // (hal_dec's IvfPayloadReader guarantees this for IVF input), so tell the
    // parser the packet is a complete picture.
    packet.bCompletePicture = feedCompletePicture_ ? 1 : 0;
    if (NvMediaParserParse(parser_, &packet) != NVMEDIA_STATUS_OK) {
        error_ = true;
        return -1;
    }
    return static_cast<int>(outQueue_.size());
}

bool NvMediaDecoder::SignalInputComplete() {
    if (error_ || finalized_) {
        return false;
    }
    inputEof_ = true;
    return true;
}

int NvMediaDecoder::PullFrames() {
    if (error_ || finalized_) {
        return 0;
    }
    return PumpNext();
}

int NvMediaDecoder::PumpNext() {
    // JPEG (IJPD) path: no parser. Read the whole file once, split it into
    // SOI(FF D8)..EOI(FF D9) segments, and decode each as one frame.
    if (codec_ == NVMEDIA_VIDEO_CODEC_MJPEG) {
        if (jpegFileBuffer_.empty() && !inputEof_) {
            // Slurp the entire input file (a .jpg is one image; an .mjpeg
            // stream is many concatenated JPEGs).
            std::fseek(inputFile_, 0, SEEK_END);
            long sz = std::ftell(inputFile_);
            std::fseek(inputFile_, 0, SEEK_SET);
            if (sz <= 0) {
                inputEof_ = true;
                streamDone_ = true;
                return 0;
            }
            jpegFileBuffer_.resize(sz);
            if (std::fread(jpegFileBuffer_.data(), 1, sz, inputFile_) !=
                static_cast<size_t>(sz)) {
                std::cerr << "NvMediaDecoder: JPEG file read failed"
                          << std::endl;
                error_ = true;
                return 0;
            }
            inputEof_ = true;
            // Split into JPEG segments on SOI (FF D8) / EOI (FF D9).
            const uint8_t* p = jpegFileBuffer_.data();
            const size_t n = jpegFileBuffer_.size();
            size_t i = 0;
            while (i + 1 < n) {
                if (p[i] != 0xFF || p[i + 1] != 0xD8) { ++i; continue; }
                size_t start = i;
                // Find the matching EOI.
                size_t j = i + 2;
                while (j + 1 < n) {
                    if (p[j] == 0xFF && p[j + 1] == 0xD9) { j += 2; break; }
                    ++j;
                }
                if (j + 1 >= n && !(p[j - 2] == 0xFF && p[j - 1] == 0xD9)) {
                    // No EOI found; take the rest of the buffer.
                    j = n;
                }
                ijpdPending_.emplace_back(p + start, p + j);
                i = j;
            }
            if (ijpdPending_.empty()) {
                std::cerr << "NvMediaDecoder: no JPEG SOI found in input"
                          << std::endl;
                error_ = true;
                return 0;
            }
        }
        // Decode every framed JPEG currently pending.
        while (!ijpdPending_.empty() && !error_) {
            auto pkt = std::move(ijpdPending_.front());
            ijpdPending_.pop_front();
            if (!DecodeJpeg(pkt.data(), pkt.size())) {
                error_ = true;
                return 0;
            }
        }
        if (inputEof_ && ijpdPending_.empty()) {
            streamDone_ = true;
        }
        return static_cast<int>(outQueue_.size());
    }

    // Keep consuming the input stream until at least one frame is queued or
    // the stream is fully drained (input exhausted + parser flush sent).
    while (outQueue_.empty() && !streamDone_) {
        if (!inputEof_) {
            if (feedMode_) {
                // The caller owns the byte stream in feed mode: bytes arrive
                // through FillInput(), so there is nothing to read here. Wait
                // for SignalInputComplete() before flushing.
                break;
            }
            size_t got = std::fread(chunk_.data(), 1, chunk_.size(),
                                    inputFile_);
            if (got == 0) {
                inputEof_ = true;
            } else if (!FeedFileChunk(chunk_.data(), got)) {
                std::cerr << "NvMediaDecoder: NvMediaParserParse failed"
                          << std::endl;
                error_ = true;
                return 0;
            }
            continue;
        }
        if (!flushSent_) {
            // End-of-stream: signal the parser to flush the trailing data.
            NvMediaBitStreamPkt eos = {};
            eos.bEOS = 1;
            NvMediaParserParse(parser_, &eos);
            NvMediaParserFlush(parser_);
            flushSent_ = true;
            continue;
        }
        streamDone_ = true;
    }
    return static_cast<int>(outQueue_.size());
}

// Parse a VC1 Simple/Main RCV file header (the format NVIDIA's videodemo.c
// ParseRCVHeader consumes) into rcvSeqInfo_, so NvMediaParserCreate can be
// given the sequence header out-of-band via pExternalSeqInfo. VC1 SP/MP raw
// frame bitstreams carry no start codes and no in-band sequence header, so
// the parser cannot self-detect the sequence — RCV supplies both. Sets
// rcvHeaderSize_ and rcvExternalSeq_ on success. `consumed` reports the header
// byte count to skip before the per-frame records begin.
bool NvMediaDecoder::ParseRcvHeader(const uint8_t* data, size_t size,
                                    size_t& consumed) {
    rcvHeaderSize_ = 0;  // default: not RCV
    if (size < 20) {
        return false;
    }
    // 3-byte LE numFrames, 1-byte type (0x85 V1, 0xC5 V2 extended).
    uint32_t numFrames = static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16);
    if (numFrames == 0) {
        return false;
    }
    uint8_t type = data[3];
    constexpr uint8_t kRcvVc1Type = 0x85;
    constexpr uint8_t kRcvV2Mask = 0x40;
    if ((type & ~kRcvV2Mask) != kRcvVc1Type) {
        return false;
    }
    size_t cur = 4;
    // 4-byte LE cbSeqHdr (SP/MP is 4; the sequence header is the ASF extradata).
    uint32_t cbSeqHdr = static_cast<uint32_t>(data[cur]) |
        (static_cast<uint32_t>(data[cur + 1]) << 8) |
        (static_cast<uint32_t>(data[cur + 2]) << 16) |
        (static_cast<uint32_t>(data[cur + 3]) << 24);
    cur += 4;
    if (cbSeqHdr == 0 || cbSeqHdr > 32 || cur + cbSeqHdr > size) {
        return false;
    }
    // VC1 profile lives in the top 2 bits of the sequence header's first byte.
    uint8_t profile = data[cur] >> 6;
    if (profile >= 2) {
        // High profile RCV is not supported (Advanced needs the elementary
        // stream path, not RCV).
        std::cerr << "NvMediaDecoder: RCV is VC1 Advanced (profile " << profile
                  << "); only Simple/Main supported" << std::endl;
        return false;
    }
    std::memset(&rcvSeqInfo_, 0, sizeof(rcvSeqInfo_));
    rcvSeqInfo_.eCodec = NVMEDIA_VIDEO_CODEC_VC1;
    rcvSeqInfo_.bProgSeq = true;
    rcvSeqInfo_.uSequenceHeaderSize = cbSeqHdr;
    std::memcpy(rcvSeqInfo_.SequenceHeaderData, data + cur, cbSeqHdr);
    cur += cbSeqHdr;
    // STRUCT_A: 4-byte LE height, 4-byte LE width, 4-byte 0x0000000C sentinel.
    if (cur + 12 > size) {
        return false;
    }
    auto rd32 = [&](size_t off) {
        return static_cast<uint32_t>(data[off]) |
            (static_cast<uint32_t>(data[off + 1]) << 8) |
            (static_cast<uint32_t>(data[off + 2]) << 16) |
            (static_cast<uint32_t>(data[off + 3]) << 24);
    };
    uint32_t height = rd32(cur);
    uint32_t width = rd32(cur + 4);
    uint32_t sentinel = rd32(cur + 8);
    cur += 12;
    if (sentinel != 0x0000000C || height <= 31 || height > 2048 - 32 ||
        width <= 15 || width > 4096 - 16) {
        return false;
    }
    // STRUCT_B: 4-byte (level<<29 | cbr<<28 | hrd), 4-byte bitrate, 4-byte fps.
    if (cur + 12 > size) {
        return false;
    }
    uint32_t structB = rd32(cur);
    uint32_t level = structB >> 29;
    if (level > 0x4) {
        return false;
    }
    uint32_t bitrate = rd32(cur + 4);
    uint32_t fps = rd32(cur + 8);
    cur += 12;

    rcvSeqInfo_.uDisplayWidth = static_cast<uint16_t>(width);
    rcvSeqInfo_.uDisplayHeight = static_cast<uint16_t>(height);
    rcvSeqInfo_.uCodedWidth =
        static_cast<uint16_t>((width + 15) & ~15U);
    rcvSeqInfo_.uCodedHeight =
        static_cast<uint16_t>((height + 15) & ~15U);
    rcvSeqInfo_.eChromaFormat = NvMediaParserChromaFormat_420;  // 4:2:0
    rcvSeqInfo_.uBitrate = bitrate;
    rcvSeqInfo_.fFrameRate = (fps && fps != 0xFFFFFFFF) ? static_cast<float>(fps)
                                                        : 0.0;
    rcvSeqInfo_.uDARWidth = static_cast<uint16_t>(width);
    rcvSeqInfo_.uDARHeight = static_cast<uint16_t>(height);
    rcvSeqInfo_.eVideoFormat = NvMVideoFormat_Unspecified;
    rcvSeqInfo_.eColorPrimaries = NvMColorPrimaries_Unspecified;
    rcvSeqInfo_.eTransferCharacteristics = NvMTransferCharacteristics_Unspecified;
    rcvSeqInfo_.eMatrixCoefficients = NvMMatrixCoeffs_Unspecified;

    rcvHeaderSize_ = static_cast<int>(cur);
    rcvExternalSeq_ = true;
    consumed = cur;
    return true;
}

// Feed one read buffer of file input to the parser. For Annex-B streams
// (h264/hevc/mpeg2) the whole chunk goes in as one packet — the parser splits
// on start codes. For IVF-wrapped VP9/AV1 the container is demuxed here and
// each frame is fed as its own packet with bCompletePicture=1, because those
// codecs carry no frame boundary the parser can recover from a raw byte feed.
// Returns false on a parse error.
bool NvMediaDecoder::FeedFileChunk(const uint8_t* data, size_t size) {
    // VC1 SP/MP RCV demux: the file header was parsed in Initialize and the
    // file repositioned past it, so `data` here is a stream of frame records
    // (4-byte LE size + 4-byte LE pts + payload). Feed each frame as its own
    // complete-picture packet — VC1 has no start codes the parser can split.
    // A frameSize of 0 is a skipped P-frame: feed a single 0x00 byte (per
    // videodemo.c StreamVC1SimpleProfile).
    if (codec_ == NVMEDIA_VIDEO_CODEC_VC1 && rcvHeaderSize_ > 0) {
        size_t pos = 0;
        while (pos < size) {
            if (!rcvFrameHeaderRead_) {
                // Accumulate the 8-byte frame record.
                while (pos < size && rcvRecordLen_ < sizeof(rcvRecord_)) {
                    rcvRecord_[rcvRecordLen_++] = data[pos++];
                }
                if (rcvRecordLen_ < sizeof(rcvRecord_)) {
                    return true;  // record straddles the next read
                }
                uint32_t frameSize = static_cast<uint32_t>(rcvRecord_[0]) |
                    (static_cast<uint32_t>(rcvRecord_[1]) << 8) |
                    (static_cast<uint32_t>(rcvRecord_[2]) << 16);
                // pts (rcvRecord_[4..7]) is not consumed — the parser stamps
                // its own PTS from the default frame rate.
                rcvRecordLen_ = 0;
                rcvFrameHeaderRead_ = true;
                if (frameSize == 0) {
                    // Skipped P-frame: the decoder wants a single 0x00 byte.
                    rcvFrameLeft_ = 1;
                    rcvSkippedFrame_ = true;
                } else {
                    rcvFrameLeft_ = frameSize & 0x00FFFFFF;
                    rcvSkippedFrame_ = false;
                }
            }
            if (rcvFrameLeft_ == 0) {
                rcvFrameHeaderRead_ = false;
                continue;
            }
            const size_t n = rcvFrameLeft_ < size - pos ? rcvFrameLeft_
                                                        : size - pos;
            uint8_t zero = 0;
            NvMediaBitStreamPkt packet = {};
            packet.pByteStream = rcvSkippedFrame_ ? &zero : data + pos;
            packet.uDataLength = static_cast<uint32_t>(n);
            packet.bCompletePicture = 1;
            if (NvMediaParserParse(parser_, &packet) != NVMEDIA_STATUS_OK) {
                return false;
            }
            if (!rcvSkippedFrame_) {
                pos += n;
            }
            rcvFrameLeft_ -= static_cast<uint32_t>(n);
            if (rcvFrameLeft_ == 0) {
                rcvFrameHeaderRead_ = false;
            }
        }
        return true;
    }

    // Sniff IVF once on the first chunk (DKIF signature + declared header
    // length). A non-IVF input leaves ivfHeaderSize_ at 0 and falls through to
    // the raw chunk feed.
    if (ivfHeaderSize_ < 0) {
        ivfHeaderSize_ = 0;
        if (size >= 32 && std::memcmp(data, "DKIF", 4) == 0) {
            const int hs = static_cast<int>(data[6]) |
                           (static_cast<int>(data[7]) << 8);
            if (hs >= 32 && hs <= static_cast<int>(size)) {
                ivfHeaderSize_ = hs;
            }
        }
    }

    if (ivfHeaderSize_ == 0) {
        // Raw Annex-B / elementary: feed the whole chunk at once.
        NvMediaBitStreamPkt packet = {};
        packet.pByteStream = data;
        packet.uDataLength = static_cast<uint32_t>(size);
        return NvMediaParserParse(parser_, &packet) == NVMEDIA_STATUS_OK;
    }

    // IVF demux: skip the file header on the first chunk, then walk frame
    // records (12-byte: 4-byte LE payload size + 8-byte pts) feeding each
    // frame payload as one complete-picture packet. Both header and records
    // can straddle read boundaries, which the carried state is for.
    size_t pos = 0;
    if (ivfHeaderSize_ > 0) {
        const int drop = ivfHeaderSize_ < static_cast<int>(size)
                             ? ivfHeaderSize_
                             : static_cast<int>(size);
        ivfHeaderSize_ -= drop;
        pos = static_cast<size_t>(drop);
    }
    while (pos < size) {
        if (ivfPayloadLeft_ == 0) {
            // Accumulate the 12-byte frame record.
            while (pos < size && ivfRecordLen_ < sizeof(ivfRecord_)) {
                ivfRecord_[ivfRecordLen_++] = data[pos++];
            }
            if (ivfRecordLen_ < sizeof(ivfRecord_)) {
                return true;  // record straddles the next read
            }
            ivfPayloadLeft_ = static_cast<uint32_t>(ivfRecord_[0]) |
                (static_cast<uint32_t>(ivfRecord_[1]) << 8) |
                (static_cast<uint32_t>(ivfRecord_[2]) << 16) |
                (static_cast<uint32_t>(ivfRecord_[3]) << 24);
            ivfRecordLen_ = 0;
            if (ivfPayloadLeft_ == 0) {
                continue;
            }
        }
        const size_t n = ivfPayloadLeft_ < size - pos ? ivfPayloadLeft_
                                                      : size - pos;
        NvMediaBitStreamPkt packet = {};
        packet.pByteStream = data + pos;
        packet.uDataLength = static_cast<uint32_t>(n);
        packet.bCompletePicture = 1;
        if (NvMediaParserParse(parser_, &packet) != NVMEDIA_STATUS_OK) {
            return false;
        }
        pos += n;
        ivfPayloadLeft_ -= static_cast<uint32_t>(n);
    }
    return true;
}

bool NvMediaDecoder::GetFrame(CodecFrame& out) {
    // In feed mode there is no file pump behind PullFrames, and NvMediaParserParse
    // only surfaces a frame once its data is complete. Drain whatever the last
    // parse produced, and once the caller has signaled EOF, flush the parser to
    // release the frames still held in the DPB. JPEG (IJPD) has no parser and
    // is never feed-mode, so this branch is skipped for it.
    if (feedMode_ && codec_ != NVMEDIA_VIDEO_CODEC_MJPEG) {
        if (outQueue_.empty() && inputEof_ && !streamDone_ && !flushSent_) {
            NvMediaBitStreamPkt eos = {};
            eos.bEOS = 1;
            NvMediaParserParse(parser_, &eos);
            NvMediaParserFlush(parser_);
            flushSent_ = true;
        }
        if (outQueue_.empty() && flushSent_) {
            streamDone_ = true;
        }
    }
    if (outQueue_.empty()) {
        return false;
    }
    out = std::move(outQueue_.front());
    outQueue_.pop_front();
    return true;
}

// Fill a PitchLinear 3-plane Y8/U8/V8 YUV420 attribute list for the IJPD
// output surface. IJPD requires PitchLinear (not BlockLinear), 8-bit, plane
// base align 256.
bool FillIJPDSurfaceAttrs(NvSciBufAttrList list, uint32_t w, uint32_t h) {
    NvSciBufType bufType = NvSciBufType_Image;
    NvSciBufAttrValAccessPerm access = NvSciBufAccessPerm_ReadWrite;
    bool needCpuAccess = true;
    NvSciBufAttrValImageLayoutType layout = NvSciBufImage_PitchLinearType;
    uint32_t planeCount = 3;
    NvSciBufAttrValColorFmt colorFormat[3] = {NvSciColor_Y8, NvSciColor_U8,
                                              NvSciColor_V8};
    NvSciBufAttrValColorStd colorStd[3] = {NvSciColorStd_REC601_ER,
                                           NvSciColorStd_REC601_ER,
                                           NvSciColorStd_REC601_ER};
    uint32_t planeWidth[3] = {w, w >> 1, w >> 1};
    uint32_t planeHeight[3] = {h, h >> 1, h >> 1};
    uint32_t baseAddrAlign[3] = {kSurfaceAlign, kSurfaceAlign, kSurfaceAlign};
    uint64_t padding[3] = {0, 0, 0};
    bool vprFlag = false;
    NvSciBufAttrValImageScanType scanType = NvSciBufScan_ProgressiveType;

    NvSciBufAttrKeyValuePair attributes[] = {
        {NvSciBufGeneralAttrKey_RequiredPerm, &access, sizeof(access)},
        {NvSciBufGeneralAttrKey_Types, &bufType, sizeof(bufType)},
        {NvSciBufGeneralAttrKey_NeedCpuAccess, &needCpuAccess,
         sizeof(needCpuAccess)},
        {NvSciBufGeneralAttrKey_EnableCpuCache, &needCpuAccess,
         sizeof(needCpuAccess)},
        {NvSciBufImageAttrKey_TopPadding, padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_BottomPadding, padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_LeftPadding, padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_RightPadding, padding, planeCount * sizeof(padding[0])},
        {NvSciBufImageAttrKey_Layout, &layout, sizeof(layout)},
        {NvSciBufImageAttrKey_PlaneCount, &planeCount, sizeof(planeCount)},
        {NvSciBufImageAttrKey_PlaneColorFormat, colorFormat,
         planeCount * sizeof(NvSciBufAttrValColorFmt)},
        {NvSciBufImageAttrKey_PlaneColorStd, colorStd,
         planeCount * sizeof(NvSciBufAttrValColorStd)},
        {NvSciBufImageAttrKey_PlaneBaseAddrAlign, baseAddrAlign,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneWidth, planeWidth,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_PlaneHeight, planeHeight,
         planeCount * sizeof(uint32_t)},
        {NvSciBufImageAttrKey_VprFlag, &vprFlag, sizeof(vprFlag)},
        {NvSciBufImageAttrKey_ScanType, &scanType, sizeof(scanType)},
    };
    return NvSciBufAttrListSetAttrs(
               list, attributes, sizeof(attributes) / sizeof(attributes[0])) ==
           NvSciError_Success;
}

bool NvMediaDecoder::SetupJpegDecoder() {
    if (ijpdReady_) {
        return true;
    }
    if (NvSciBufModuleOpen(&bufModule_) != NvSciError_Success || !bufModule_) {
        std::cerr << "NvMediaDecoder: IJPD NvSciBufModuleOpen failed"
                  << std::endl;
        return false;
    }
    if (NvSciSyncModuleOpen(&syncModule_) != NvSciError_Success ||
        !syncModule_) {
        std::cerr << "NvMediaDecoder: IJPD NvSciSyncModuleOpen failed"
                  << std::endl;
        return false;
    }
    if (NvSciSyncCpuWaitContextAlloc(syncModule_, &cpuWaitContext_) !=
            NvSciError_Success ||
        !cpuWaitContext_) {
        std::cerr << "NvMediaDecoder: IJPD CpuWaitContextAlloc failed"
                  << std::endl;
        return false;
    }
    // EOF sync object: IJPD signals EOF, CPU waits. (No pre-fence needed for
    // file/host input — there is no upstream producer to gate on.)
    NvSciSyncAttrList sigList = nullptr, cpuWaitList = nullptr;
    NvSciSyncAttrListCreate(syncModule_, &sigList);
    NvSciSyncAttrListCreate(syncModule_, &cpuWaitList);
    NvMediaIJPDFillNvSciSyncAttrList(ijpd_, sigList, NVMEDIA_SIGNALER);
    bool cpuAccess = true;
    NvSciSyncAccessPerm waitPerm = NvSciSyncAccessPerm_WaitOnly;
    NvSciSyncAttrKeyValuePair waiterAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &waitPerm, sizeof(waitPerm)}};
    NvSciSyncAttrListSetAttrs(cpuWaitList, waiterAttrs, 2);
    NvSciSyncAttrList unrec[2] = {sigList, cpuWaitList};
    NvSciSyncAttrList rec = nullptr, conf = nullptr;
    NvSciSyncAttrListReconcile(unrec, 2, &rec, &conf);
    NvSciSyncObjAlloc(rec, &eofSyncObj_);
    NvSciSyncAttrListFree(rec);
    NvSciSyncAttrListFree(conf);
    NvSciSyncAttrListFree(sigList);
    NvSciSyncAttrListFree(cpuWaitList);
    if (!eofSyncObj_) {
        std::cerr << "NvMediaDecoder: IJPD eof sync alloc failed" << std::endl;
        return false;
    }
    if (NvMediaIJPDRegisterNvSciSyncObj(ijpd_, NVMEDIA_EOFSYNCOBJ,
                                        eofSyncObj_) != NVMEDIA_STATUS_OK ||
        NvMediaIJPDSetNvSciSyncObjforEOF(ijpd_, eofSyncObj_) !=
            NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: IJPD register/set sync objs failed"
                  << std::endl;
        return false;
    }
    ijpdReady_ = true;
    return true;
}

bool NvMediaDecoder::DecodeJpeg(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return false;
    }
    // Probe the stream for its coded dimensions (static — no decoder needed).
    NVMEDIAJPEGDecInfo info;
    std::memset(&info, 0, sizeof(info));
    NvMediaBitstreamBuffer bsBuf;
    std::memset(&bsBuf, 0, sizeof(bsBuf));
    bsBuf.bitstream = const_cast<uint8_t*>(data);
    bsBuf.bitstreamBytes = static_cast<uint32_t>(size);
    bsBuf.bitstreamSize = static_cast<uint32_t>(size);
    if (NvMediaIJPDGetInfo(&info, 1, &bsBuf) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: NvMediaIJPDGetInfo failed" << std::endl;
        return false;
    }
    const uint16_t w = info.width;
    const uint16_t h = info.height;
    const uint16_t cw = (w + 15U) & ~15U;
    const uint16_t ch = (h + 15U) & ~15U;

    // Create (or resize) the IJPD decoder if the coded dimensions grew.
    if (!ijpd_) {
        ijpd_ = NvMediaIJPDCreate(cw, ch, static_cast<uint32_t>(size),
                                  /*supportPartialAccel=*/info.partialAccel,
                                  NVMEDIA_JPEG_INSTANCE_0);
        if (!ijpd_) {
            std::cerr << "NvMediaDecoder: NvMediaIJPDCreate failed" << std::endl;
            return false;
        }
        if (!SetupJpegDecoder()) {
            return false;
        }
    } else if (cw > codedWidth_ || ch > codedHeight_ ||
               static_cast<uint32_t>(size) > ijpdMaxBits_) {
        if (NvMediaIJPDResize(ijpd_, cw, ch,
                              static_cast<uint32_t>(size)) != NVMEDIA_STATUS_OK) {
            std::cerr << "NvMediaDecoder: NvMediaIJPDResize failed"
                      << std::endl;
            return false;
        }
    }
    codedWidth_ = cw;
    codedHeight_ = ch;
    ijpdMaxBits_ = static_cast<uint32_t>(size);

    // (Re)allocate the output surface if the coded size changed. IJPD's
    // RegisterNvSciBufObj is documented "not supported, can be ignored", so
    // the surface is fed directly to RenderYUV without registration.
    if (!ijpdOutBuf_) {
        NvSciBufAttrList attrList = nullptr, reconciled = nullptr, conflict = nullptr;
        NvSciBufAttrListCreate(bufModule_, &attrList);
        NvMediaIJPDFillNvSciBufAttrList(NVMEDIA_JPEG_INSTANCE_0, attrList);
        FillIJPDSurfaceAttrs(attrList, cw, ch);
        NvSciBufAttrList arr[1] = {attrList};
        NvSciBufAttrListReconcile(arr, 1, &reconciled, &conflict);
        NvSciBufObjAlloc(reconciled, &ijpdOutBuf_);
        NvSciBufAttrListFree(attrList);
        NvSciBufAttrListFree(reconciled);
        NvSciBufAttrListFree(conflict);
        if (!ijpdOutBuf_) {
            std::cerr << "NvMediaDecoder: IJPD NvSciBufObjAlloc failed"
                      << std::endl;
            return false;
        }
    }

    if (NvMediaIJPDRenderYUV(ijpd_, ijpdOutBuf_, /*downscaleLog2=*/0, 1,
                             &bsBuf, NVMEDIA_IJPD_RENDER_FLAG_ROTATE_0,
                             NVMEDIA_JPEG_INSTANCE_0) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: NvMediaIJPDRenderYUV failed"
                  << std::endl;
        return false;
    }
    NvSciSyncFence eofFence{};
    if (NvMediaIJPDGetEOFNvSciSyncFence(ijpd_, eofSyncObj_, &eofFence) !=
        NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaDecoder: IJPD GetEOFNvSciSyncFence failed"
                  << std::endl;
        return false;
    }
    if (NvSciSyncFenceWait(&eofFence, cpuWaitContext_, kFenceWaitTimeoutUs) !=
        NvSciError_Success) {
        std::cerr << "NvMediaDecoder: IJPD EOF FenceWait failed" << std::endl;
        return false;
    }
    NvSciSyncFenceClear(&eofFence);

    // Read back the decoded YUV (3-plane planar via GetPixels, then repack to
    // NV12 — same convention as the IDE host readout path). The PitchLinear
    // IJPD surface is read with the same 3-plane GetPixels call that handles
    // the BlockLinear IDE surface.
    displayWidth_ = w;
    displayHeight_ = h;
    uint8_t* buffer = DetileNvSciBufToNV12(ijpdOutBuf_, cw, ch);
    if (!buffer) {
        std::cerr << "NvMediaDecoder: IJPD GetPixels/detile failed"
                  << std::endl;
        return false;
    }
    CodecFrame frame;
    frame.data = buffer;
    frame.size = static_cast<size_t>(w) * h * 3 / 2;
    frame.width = static_cast<int>(w);
    frame.height = static_cast<int>(h);
    frame.format = PixelFormat::NV12;
    frame.strides[0] = w;
    frame.strides[1] = w;
    frame.release = [buffer]() { std::free(buffer); };
    outQueue_.push_back(std::move(frame));
    return true;
}

void NvMediaDecoder::Finalize() {
    if (finalized_) {
        return;
    }
    finalized_ = true;


    if (parser_) {
        NvMediaParserDestroy(parser_);
        parser_ = nullptr;
    }
    if (inputFile_) {
        std::fclose(inputFile_);
        inputFile_ = nullptr;
    }

    if (decoder_) {
        for (Surface& s : pool_) {
            if (s.buf) {
                NvMediaIDEUnregisterNvSciBufObj(decoder_, s.buf);
                NvSciBufObjFree(s.buf);
                s.buf = nullptr;
            }
        }
        if (eofSyncObj_) {
            NvMediaIDEUnregisterNvSciSyncObj(decoder_, eofSyncObj_);
            NvSciSyncObjFree(eofSyncObj_);
            eofSyncObj_ = nullptr;
        }
        NvMediaIDEDestroy(decoder_);
        decoder_ = nullptr;
    }
    pool_.clear();

    if (ijpd_) {
        if (ijpdOutBuf_) {
            NvSciBufObjFree(ijpdOutBuf_);
            ijpdOutBuf_ = nullptr;
        }
        if (eofSyncObj_) {
            NvMediaIJPDUnregisterNvSciSyncObj(ijpd_, eofSyncObj_);
            NvSciSyncObjFree(eofSyncObj_);
            eofSyncObj_ = nullptr;
        }
        NvMediaIJPDDestroy(ijpd_);
        ijpd_ = nullptr;
    }

    if (cpuWaitContext_) {
        NvSciSyncCpuWaitContextFree(cpuWaitContext_);
        cpuWaitContext_ = nullptr;
    }
    if (bufModule_) {
        NvSciBufModuleClose(bufModule_);
        bufModule_ = nullptr;
    }
    if (syncModule_) {
        NvSciSyncModuleClose(syncModule_);
        syncModule_ = nullptr;
    }

    // Release any frames the caller has not consumed.
    while (!outQueue_.empty()) {
        CodecFrame& f = outQueue_.front();
        if (f.release) {
            f.release();
        }
        outQueue_.pop_front();
    }
}

std::string NvMediaDecoder::getName() const {
    return "nvmedia";
}

// DownloadToHost hook for zero-copy NvSciBufObj frames. Detiles the borrowed
// GPU surface into a tightly-packed NV12 host buffer, installs it as the
// frame's `data`, and flips locality to Host. The frame's `release` (which
// returns the borrowed slot to the pool) is invoked first, because the
// detile via NvSciBufObjGetPixels reads the surface one last time and the
// surface can be recycled as soon as that read completes — we no longer need
// to hold the slot. The new `release` frees the host buffer.
namespace {
bool DownloadNvSciBuf(CodecFrame& f) {
    if (f.locality != FrameLocality::NvSciBufObj || !f.device.nvSciBufObj) {
        return false;
    }
    const uint32_t w = static_cast<uint32_t>(f.width);
    const uint32_t h = static_cast<uint32_t>(f.height);
    uint8_t* buffer = DetileNvSciBufToNV12(
        static_cast<NvSciBufObj>(f.device.nvSciBufObj), w, h);
    // The borrowed surface is no longer needed after the detile read.
    if (f.release) {
        f.release();
        f.release = nullptr;
    }
    if (!buffer) {
        return false;
    }
    f.data = buffer;
    f.size = static_cast<size_t>(w) * h * 3 / 2;
    f.strides[0] = w;
    f.strides[1] = w;
    f.locality = FrameLocality::Host;
    f.device = {};
    f.release = [buffer]() { std::free(buffer); };
    return true;
}

const bool g_nvsciDownloadRegistered = [] {
    RegisterNvSciBufDownload(&DownloadNvSciBuf);
    return true;
}();
}  // namespace

HALCODEC_CONNECT(Decoder, nvmedia, NvMediaDecoder);

} // namespace nvmedia
} // namespace halcodec