#include "nvmediadecoder.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "nvmedia_common_decode.h"
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
    if (codec == "mpeg2") {
        return NVMEDIA_VIDEO_CODEC_MPEG2;
    }
    return static_cast<NvMediaVideoCodec>(-1);
}

constexpr size_t kFeedChunkSize = 1 << 20;        // 1 MiB
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
    if (params.inputs.empty()) {
        std::cerr << "NvMediaDecoder: no input file given" << std::endl;
        return false;
    }

    codec_ = MapCodec(params.codec);
    if (codec_ == static_cast<NvMediaVideoCodec>(-1)) {
        std::cerr << "NvMediaDecoder: unsupported codec: " << params.codec
                  << std::endl;
        return false;
    }

    inputFile_ = fopen(params.inputs[0].c_str(), "rb");
    if (!inputFile_) {
        std::cerr << "NvMediaDecoder: cannot open " << params.inputs[0]
                  << std::endl;
        return false;
    }

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
    NvSciBufAttrList bufAttrs = nullptr;
    NvSciBufAttrList bufReconciled = nullptr;
    NvSciBufAttrList bufConflict = nullptr;
    if (NvSciBufAttrListCreate(bufModule_, &bufAttrs) != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciBufAttrListCreate failed"
                  << std::endl;
        return false;
    }
    if (NvMediaIDEFillNvSciBufAttrList(instanceId_, bufAttrs) !=
            NVMEDIA_STATUS_OK ||
        !FillNV12SurfaceAttrs(bufAttrs, w, h)) {
        std::cerr << "NvMediaDecoder: fill surface attrs failed" << std::endl;
        NvSciBufAttrListFree(bufAttrs);
        return false;
    }
    if (NvSciBufAttrListReconcile(&bufAttrs, 1, &bufReconciled, &bufConflict) !=
        NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciBufAttrListReconcile failed"
                  << std::endl;
        NvSciBufAttrListFree(bufAttrs);
        return false;
    }
    NvSciBufAttrListFree(bufAttrs);

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
        if (s.refCount == 0) {
            s.refCount = 1;
            *p = reinterpret_cast<NvMediaRefSurface*>(&s);
            return NVMEDIA_STATUS_OK;
        }
    }
    std::cerr << "NvMediaDecoder: picture pool exhausted" << std::endl;
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

NvMediaStatus NvMediaDecoder::DisplayPictureCb(void* ctx, NvMediaRefSurface* s,
                                               int64_t pts) {
    (void)ctx;
    (void)s;
    (void)pts;
    // Frames are copied out in DecodePictureCb (sync path), so display-order
    // callbacks are no-ops here. A B-frame stream may reorder display vs.
    // decode order; documenting this as a DRIVE tuning item.
    return NVMEDIA_STATUS_OK;
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

    // Wait for the decode to complete, then copy the surface out so the slot
    // can be reused once the parser releases it.
    if (NvSciSyncFenceWait(&target->fence, cpuWaitContext_,
                           kFenceWaitTimeoutUs) != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciSyncFenceWait failed" << std::endl;
        return;
    }
    NvSciSyncFenceClear(&target->fence);

    CodecFrame frame = CopySurfaceToFrame(target, pd);
    if (frame.data) {
        outQueue_.push_back(std::move(frame));
    }
}

CodecFrame NvMediaDecoder::CopySurfaceToFrame(Surface* s,
                                              NvMediaParserPictureData*) {
    CodecFrame frame;
    if (!s || !s->buf) {
        return frame;
    }

    const uint32_t w = s->width;
    const uint32_t h = s->height;
    const size_t yBytes = static_cast<size_t>(w) * h;
    const size_t uvBytes = yBytes / 2;  // NV12: one interleaved chroma plane

    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(yBytes + uvBytes));
    if (!buffer) {
        return frame;
    }

    void* planes[2] = {buffer, buffer + yBytes};
    uint32_t sizes[2] = {static_cast<uint32_t>(yBytes),
                         static_cast<uint32_t>(uvBytes)};
    uint32_t pitches[2] = {w, w};

    NvSciError err = NvSciBufObjGetPixels(s->buf, nullptr, planes, sizes,
                                          pitches);
    if (err != NvSciError_Success) {
        std::cerr << "NvMediaDecoder: NvSciBufObjGetPixels failed" << std::endl;
        std::free(buffer);
        return frame;
    }

    frame.data = buffer;
    frame.size = yBytes + uvBytes;
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
    NvMediaBitStreamPkt packet = {};
    packet.pByteStream = data;
    packet.uDataLength = static_cast<uint32_t>(size);
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
    // Keep consuming the input stream until at least one frame is queued or
    // the stream is fully drained (input exhausted + parser flush sent).
    while (outQueue_.empty() && !streamDone_) {
        if (!inputEof_) {
            size_t got = std::fread(chunk_.data(), 1, chunk_.size(),
                                    inputFile_);
            if (got == 0) {
                inputEof_ = true;
            } else {
                NvMediaBitStreamPkt packet = {};
                packet.pByteStream = chunk_.data();
                packet.uDataLength = static_cast<uint32_t>(got);
                if (NvMediaParserParse(parser_, &packet) != NVMEDIA_STATUS_OK) {
                    std::cerr << "NvMediaDecoder: NvMediaParserParse failed"
                              << std::endl;
                    error_ = true;
                    return 0;
                }
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

bool NvMediaDecoder::GetFrame(CodecFrame& out) {
    if (outQueue_.empty()) {
        return false;
    }
    out = std::move(outQueue_.front());
    outQueue_.pop_front();
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

HALCODEC_CONNECT(Decoder, nvmedia, NvMediaDecoder);

} // namespace nvmedia
} // namespace halcodec