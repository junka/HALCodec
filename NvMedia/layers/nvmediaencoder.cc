#include "nvmediaencoder.h"

#include <cstring>
#include <iostream>

#include "nvscibuf.h"
#include "nvscisync.h"
#include "registry.h"

namespace halcodec {
namespace nvmedia {
namespace {

constexpr uint32_t kSurfaceAlign = 256;
constexpr uint32_t kMaxBuffering = 5;    // IEP frames outstanding before GetBits
constexpr uint32_t kSurfacePoolSize = 5; // input surface pool
constexpr uint64_t kFenceWaitTimeoutUs = 1000 * 1000;  // 1 s
constexpr uint32_t kFlushDrainIterations = 64;

// Fill the NvSciBuf attribute list for an 8-bit NV12 (semi-planar YUV 4:2:0)
// input surface of the given dimensions, mirroring the decoder's
// FillNV12SurfaceAttrs() and PopulateNvSciBufAttrList() in scibuf_utils.c:
// BlockLinear layout, 256-byte base address alignment, REC601_ER, progressive.
// Returns true on success.
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

} // namespace

bool NvMediaEncoder::Initialize(const CodecParams& params) {
    if (initialized_) {
        return true;
    }
    if (params.width <= 0 || params.height <= 0) {
        std::cerr << "NvMediaEncoder: missing width/height" << std::endl;
        return false;
    }

    // IEP takes 8-bit NV12 semi-planar input for H.264; the HAL interface
    // defaults to I420 planar, which FillFrame() converts on upload. Coded
    // dimensions are aligned up to 16 for the hardware encoder.
    width_ = (static_cast<uint32_t>(params.width) + 15U) & ~15U;
    height_ = (static_cast<uint32_t>(params.height) + 15U) & ~15U;

    // Codec selection: only H.264 is wired up so far (hal_enc leaves
    // params.codec empty, so this is the default path).
    if (!params.codec.empty() && params.codec != "h264") {
        std::cerr << "NvMediaEncoder: unsupported codec '" << params.codec
                  << "' (only h264)" << std::endl;
        return false;
    }
    iepType_ = NVMEDIA_IMAGE_ENCODE_H264;

    NvMediaVersion version{};
    if (NvMediaIEPGetVersion(&version) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPGetVersion failed" << std::endl;
        return false;
    }
    encoder_ = NvMediaIEPCreateCtx();
    if (!encoder_) {
        std::cerr << "NvMediaEncoder: NvMediaIEPCreateCtx failed" << std::endl;
        return false;
    }

    // Init parameters mirror SetEncoderInitParamsH264() in image_encoder.c.
    // Rate control and SPS/PPS repetition are left at hardware defaults
    // (NvMediaIEPSetConfiguration is not required for the data path).
    NvMediaEncodeInitializeParamsH264 initParams;
    std::memset(&initParams, 0, sizeof(initParams));
    initParams.encodeHeight = height_;
    initParams.encodeWidth = width_;
    initParams.frameRateDen = frameRateDen_;
    initParams.frameRateNum = frameRateNum_;
    initParams.maxNumRefFrames = 2;
    initParams.useBFramesAsRef = false;
    initParams.enableExtProfile = false;
    initParams.profile = 0;  // encoder picks the default profile

    // Reconcile the input-surface attributes: IEP injects its internal
    // requirements, while we add the CPU-writable NV12 image description.
    // The reconciled list feeds NvMediaIEPInit() and the surface pool.
    if (NvSciBufModuleOpen(&bufModule_) != NvSciError_Success || !bufModule_) {
        std::cerr << "NvMediaEncoder: NvSciBufModuleOpen failed" << std::endl;
        return false;
    }
    NvSciBufAttrList attrList = nullptr;
    NvSciBufAttrList reconciled = nullptr;
    NvSciBufAttrList conflict = nullptr;
    if (NvSciBufAttrListCreate(bufModule_, &attrList) != NvSciError_Success ||
        !attrList) {
        std::cerr << "NvMediaEncoder: NvSciBufAttrListCreate failed"
                  << std::endl;
        return false;
    }
    if (NvMediaIEPFillNvSciBufAttrList(instanceId_, attrList) !=
            NVMEDIA_STATUS_OK ||
        !FillNV12SurfaceAttrs(attrList, width_, height_)) {
        std::cerr << "NvMediaEncoder: fill surface attrs failed" << std::endl;
        NvSciBufAttrListFree(attrList);
        return false;
    }
    NvSciBufAttrList attrArray[1] = {attrList};
    if (NvSciBufAttrListReconcile(attrArray, 1, &reconciled, &conflict) !=
        NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciBufAttrListReconcile failed"
                  << std::endl;
        NvSciBufAttrListFree(attrList);
        return false;
    }
    NvSciBufAttrListFree(attrList);

    NvMediaStatus status = NvMediaIEPInit(
        encoder_, iepType_, &initParams, reconciled,
        static_cast<uint8_t>(kMaxBuffering), instanceId_);
    if (status != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPInit failed: 0x" << std::hex
                  << status << std::dec << std::endl;
        NvSciBufAttrListFree(reconciled);
        NvSciBufAttrListFree(conflict);
        return false;
    }

    // Surface pool: allocate each input buffer and register it with IEP.
    pool_.resize(kSurfacePoolSize);
    for (uint32_t i = 0; i < kSurfacePoolSize; ++i) {
        NvSciBufObj obj = nullptr;
        if (NvSciBufObjAlloc(reconciled, &obj) != NvSciError_Success || !obj) {
            std::cerr << "NvMediaEncoder: NvSciBufObjAlloc failed" << std::endl;
            pool_.resize(i);
            break;
        }
        if (NvMediaIEPRegisterNvSciBufObj(encoder_, obj) != NVMEDIA_STATUS_OK) {
            std::cerr << "NvMediaEncoder: NvMediaIEPRegisterNvSciBufObj failed"
                      << std::endl;
            NvSciBufObjFree(obj);
            pool_.resize(i);
            break;
        }
        pool_[i].buf = obj;
    }
    NvSciBufAttrListFree(reconciled);
    NvSciBufAttrListFree(conflict);
    if (pool_.empty()) {
        std::cerr << "NvMediaEncoder: no input surface allocated" << std::endl;
        return false;
    }

    // CPU wait context plus the EOF/Pre sync objects: IEP signals EOF, the
    // CPU signals Pre so the encoder waits for the CPU pixel write.
    if (NvSciSyncModuleOpen(&syncModule_) != NvSciError_Success ||
        !syncModule_) {
        std::cerr << "NvMediaEncoder: NvSciSyncModuleOpen failed" << std::endl;
        return false;
    }
    if (NvSciSyncCpuWaitContextAlloc(syncModule_, &cpuWaitContext_) !=
            NvSciError_Success ||
        !cpuWaitContext_) {
        std::cerr << "NvMediaEncoder: NvSciSyncCpuWaitContextAlloc failed"
                  << std::endl;
        return false;
    }
    if (!SetupSync()) {
        return false;
    }

    initialized_ = true;
    std::cout << "NvMediaEncoder: IEP ready, surface " << width_ << "x"
              << height_ << " (" << pool_.size() << " bufs)" << std::endl;
    return true;
}

bool NvMediaEncoder::SetupSync() {
    // 4-list NvSciSync setup from image_encoder.c (L2282-2426):
    //   eofSyncObj: reconcile(iepSignaler, cpuWaiter)  -- IEP signals EOF
    //   preSyncObj: reconcile(iepWaiter,  cpuSignaler) -- CPU signals "ready"
    NvSciSyncAttrList iepSignalerList = nullptr;
    NvSciSyncAttrList iepWaiterList = nullptr;
    NvSciSyncAttrList cpuWaiterList = nullptr;
    NvSciSyncAttrList cpuSignalerList = nullptr;
    if (NvSciSyncAttrListCreate(syncModule_, &iepSignalerList) !=
            NvSciError_Success ||
        NvSciSyncAttrListCreate(syncModule_, &iepWaiterList) !=
            NvSciError_Success ||
        NvSciSyncAttrListCreate(syncModule_, &cpuWaiterList) !=
            NvSciError_Success ||
        NvSciSyncAttrListCreate(syncModule_, &cpuSignalerList) !=
            NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciSyncAttrListCreate failed"
                  << std::endl;
        return false;
    }
    if (NvMediaIEPFillNvSciSyncAttrList(encoder_, iepSignalerList,
                                        NVMEDIA_SIGNALER) != NVMEDIA_STATUS_OK ||
        NvMediaIEPFillNvSciSyncAttrList(encoder_, iepWaiterList,
                                        NVMEDIA_WAITER) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPFillNvSciSyncAttrList failed"
                  << std::endl;
        return false;
    }

    // CPU waiter (EOF) permission: NeedCpuAccess + WaitOnly.
    bool cpuAccess = true;
    NvSciSyncAccessPerm waitPerm = NvSciSyncAccessPerm_WaitOnly;
    NvSciSyncAttrKeyValuePair waiterAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &waitPerm, sizeof(waitPerm)},
    };
    if (NvSciSyncAttrListSetAttrs(cpuWaiterList, waiterAttrs, 2) !=
        NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciSyncAttrListSetAttrs (waiter)"
                  << " failed" << std::endl;
        return false;
    }
    // CPU signaler (Pre) permission: NeedCpuAccess + SignalOnly.
    NvSciSyncAccessPerm signalPerm = NvSciSyncAccessPerm_SignalOnly;
    NvSciSyncAttrKeyValuePair signalerAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &signalPerm, sizeof(signalPerm)},
    };
    if (NvSciSyncAttrListSetAttrs(cpuSignalerList, signalerAttrs, 2) !=
        NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciSyncAttrListSetAttrs (signaler)"
                  << " failed" << std::endl;
        return false;
    }

    NvSciSyncAttrList unreconciled[2] = {nullptr, nullptr};
    NvSciSyncAttrList reconciled = nullptr;
    NvSciSyncAttrList conflict = nullptr;

    unreconciled[0] = iepSignalerList;
    unreconciled[1] = cpuWaiterList;
    if (NvSciSyncAttrListReconcile(unreconciled, 2, &reconciled, &conflict) !=
            NvSciError_Success ||
        NvSciSyncObjAlloc(reconciled, &eofSyncObj_) != NvSciError_Success ||
        !eofSyncObj_) {
        std::cerr << "NvMediaEncoder: eof sync reconcile/alloc failed"
                  << std::endl;
        return false;
    }
    NvSciSyncAttrListFree(reconciled);
    NvSciSyncAttrListFree(conflict);

    unreconciled[0] = iepWaiterList;
    unreconciled[1] = cpuSignalerList;
    if (NvSciSyncAttrListReconcile(unreconciled, 2, &reconciled, &conflict) !=
            NvSciError_Success ||
        NvSciSyncObjAlloc(reconciled, &preSyncObj_) != NvSciError_Success ||
        !preSyncObj_) {
        std::cerr << "NvMediaEncoder: pre sync reconcile/alloc failed"
                  << std::endl;
        return false;
    }
    NvSciSyncAttrListFree(reconciled);
    NvSciSyncAttrListFree(conflict);
    NvSciSyncAttrListFree(iepSignalerList);
    NvSciSyncAttrListFree(iepWaiterList);
    NvSciSyncAttrListFree(cpuWaiterList);
    NvSciSyncAttrListFree(cpuSignalerList);

    if (NvMediaIEPRegisterNvSciSyncObj(encoder_, NVMEDIA_EOFSYNCOBJ,
                                       eofSyncObj_) != NVMEDIA_STATUS_OK ||
        NvMediaIEPRegisterNvSciSyncObj(encoder_, NVMEDIA_PRESYNCOBJ,
                                       preSyncObj_) != NVMEDIA_STATUS_OK ||
        NvMediaIEPSetNvSciSyncObjforEOF(encoder_, eofSyncObj_) !=
            NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: register/set sync objs failed"
                  << std::endl;
        return false;
    }
    return true;
}

bool NvMediaEncoder::FillFrame(const CodecFrame& in) {
    if (!initialized_ || eof_) {
        return false;
    }

    // Zero-size frame marks the end of the stream: drain the remaining
    // bitstream (trailing packets surface only after the last input settles).
    if (in.size == 0) {
        eof_ = true;
        for (uint32_t i = 0; i < kFlushDrainIterations; ++i) {
            uint32_t bytesAvailable = 0;
            NvMediaStatus s = NvMediaIEPBitsAvailable(
                encoder_, &bytesAvailable, NVMEDIA_ENCODE_BLOCKING_TYPE_NEVER,
                0);
            if (s != NVMEDIA_STATUS_OK || bytesAvailable == 0) {
                break;
            }
            if (!Drain()) {
                return false;
            }
        }
        return true;
    }

    if (in.format != PixelFormat::NV12 && in.format != PixelFormat::I420 &&
        in.format != PixelFormat::Unknown) {
        std::cerr << "NvMediaEncoder: unsupported input format (NV12/I420"
                  << " only)" << std::endl;
        return false;
    }
    const uint32_t w = static_cast<uint32_t>(in.width);
    const uint32_t h = static_cast<uint32_t>(in.height);
    if (w == 0 || h == 0 || w > width_ || h > height_) {
        std::cerr << "NvMediaEncoder: bad frame dims " << in.width << "x"
                  << in.height << std::endl;
        return false;
    }

    Surface& surface = pool_[nextSurface_];
    nextSurface_ = (nextSurface_ + 1) % pool_.size();

    // Reuse safety: only write the surface once the encode that previously
    // used it has finished (its EOF fence was signaled and consumed).
    if (surface.pending) {
        if (NvSciSyncFenceWait(&surface.eofFence, cpuWaitContext_,
                               kFenceWaitTimeoutUs) != NvSciError_Success) {
            std::cerr << "NvMediaEncoder: FenceWait (surface reuse) failed"
                      << std::endl;
            return false;
        }
        NvSciSyncFenceClear(&surface.eofFence);
        surface.pending = false;
    }

    // Stage the frame into a tightly packed NV12 CPU buffer sized to the
    // aligned surface, converting I420 planar input to NV12 and zero-padding
    // the right/bottom edges. The CPU write is ordered against the encoder
    // via the Pre sync object below.
    const size_t yBytes = static_cast<size_t>(width_) * height_;
    const size_t uvBytes = yBytes / 2;
    std::vector<uint8_t> stage(yBytes + uvBytes, 0);
    const uint8_t* srcY = in.data;
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(stage.data() + static_cast<size_t>(y) * width_,
                    srcY + static_cast<size_t>(y) * w, w);
    }
    const uint32_t cw = w >> 1;  // chroma plane width in bytes per row
    const uint32_t ch = h >> 1;
    uint8_t* dstUV = stage.data() + yBytes;
    if (in.format == PixelFormat::I420) {
        // Planar U/V -> interleaved NV12 chroma plane.
        const uint8_t* srcU = in.data + static_cast<size_t>(w) * h;
        const uint8_t* srcV = in.data + static_cast<size_t>(w) * h * 5 / 4;
        for (uint32_t r = 0; r < ch; ++r) {
            uint8_t* row = dstUV + static_cast<size_t>(r) * width_;
            const uint8_t* u = srcU + static_cast<size_t>(r) * cw;
            const uint8_t* v = srcV + static_cast<size_t>(r) * cw;
            for (uint32_t c = 0; c < cw; ++c) {
                row[2 * c] = u[c];
                row[2 * c + 1] = v[c];
            }
        }
    } else {
        // NV12 (and Unknown: treated as semi-planar): copy chroma as-is.
        const uint8_t* srcUV = in.data + static_cast<size_t>(w) * h;
        for (uint32_t r = 0; r < ch; ++r) {
            std::memcpy(dstUV + static_cast<size_t>(r) * width_,
                        srcUV + static_cast<size_t>(r) * w, w);
        }
    }

    const void* srcPtrs[2] = {stage.data(), stage.data() + yBytes};
    const uint32_t srcSizes[2] = {static_cast<uint32_t>(yBytes),
                                  static_cast<uint32_t>(uvBytes)};
    const uint32_t srcPitches[2] = {width_, width_};
    if (NvSciBufObjPutPixels(surface.buf, nullptr, srcPtrs, srcSizes,
                             srcPitches) != NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciBufObjPutPixels failed" << std::endl;
        return false;
    }

    // Order the CPU write before the encode: generate a fence on the Pre sync
    // object, insert it for IEP to wait on, then signal it (image_encoder.c).
    NvSciSyncFence preFence;
    if (NvSciSyncObjGenerateFence(preSyncObj_, &preFence) != NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciSyncObjGenerateFence failed"
                  << std::endl;
        return false;
    }
    if (NvMediaIEPInsertPreNvSciSyncFence(encoder_, &preFence) !=
        NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPInsertPreNvSciSyncFence failed"
                  << std::endl;
        NvSciSyncFenceClear(&preFence);
        return false;
    }
    NvSciSyncObjSignal(preSyncObj_);
    NvSciSyncFenceClear(&preFence);

    NvMediaEncodePicParamsH264 picParams;
    std::memset(&picParams, 0, sizeof(picParams));
    picParams.pictureType =
        (frameIndex_ == 0) ? NVMEDIA_ENCODE_PIC_TYPE_IDR
                           : NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;

    NvMediaStatus status =
        NvMediaIEPFeedFrame(encoder_, surface.buf, &picParams, instanceId_);
    if (status == NVMEDIA_STATUS_INSUFFICIENT_BUFFERING) {
        // The encoder's output queue is full; drain it and retry submission.
        if (!Drain()) {
            return false;
        }
        status =
            NvMediaIEPFeedFrame(encoder_, surface.buf, &picParams, instanceId_);
    }
    if (status != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPFeedFrame failed: 0x"
                  << std::hex << status << std::dec << std::endl;
        return false;
    }
    ++frameIndex_;

    // Wait for this frame's encode to complete, then pull its bitstream.
    if (NvMediaIEPGetEOFNvSciSyncFence(encoder_, eofSyncObj_,
                                       &surface.eofFence) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPGetEOFNvSciSyncFence failed"
                  << std::endl;
        return false;
    }
    surface.pending = true;
    if (NvSciSyncFenceWait(&surface.eofFence, cpuWaitContext_,
                           kFenceWaitTimeoutUs) != NvSciError_Success) {
        std::cerr << "NvMediaEncoder: FenceWait (frame EOF) failed" << std::endl;
        return false;
    }
    NvSciSyncFenceClear(&surface.eofFence);
    surface.pending = false;

    return Drain();
}

bool NvMediaEncoder::Drain() {
    uint32_t bytesAvailable = 0;
    NvMediaStatus s = NvMediaIEPBitsAvailable(
        encoder_, &bytesAvailable, NVMEDIA_ENCODE_BLOCKING_TYPE_NEVER, 0);
    if (s == NVMEDIA_STATUS_NONE_PENDING || s == NVMEDIA_STATUS_PENDING) {
        return true;  // nothing ready to read back
    }
    if (s != NVMEDIA_STATUS_OK || bytesAvailable == 0) {
        return true;
    }

    NvMediaBitstreamBuffer bitstreams;
    std::memset(&bitstreams, 0, sizeof(bitstreams));
    std::vector<uint8_t> storage(bytesAvailable);
    bitstreams.bitstream = storage.data();
    bitstreams.bitstreamSize = bytesAvailable;

    uint32_t numBytes = 0;
    s = NvMediaIEPGetBits(encoder_, &numBytes, 1, &bitstreams, nullptr);
    if (s == NVMEDIA_STATUS_NONE_PENDING) {
        return true;
    }
    if (s != NVMEDIA_STATUS_OK || bitstreams.bitstreamBytes == 0) {
        std::cerr << "NvMediaEncoder: NvMediaIEPGetBits failed: 0x" << std::hex
                  << s << std::dec << std::endl;
        return false;
    }

    // Hand the encoded bytes to the caller via a heap buffer owned by the
    // frame; GetFrame() transfers ownership to the app, which releases it.
    const uint32_t bytes = bitstreams.bitstreamBytes;
    uint8_t* data = new uint8_t[bytes];
    std::memcpy(data, storage.data(), bytes);
    CodecFrame packet;
    packet.data = data;
    packet.size = bytes;
    packet.width = static_cast<int>(width_);
    packet.height = static_cast<int>(height_);
    packet.format = PixelFormat::Unknown;  // encoded bitstream, not a surface
    packet.release = [data] { delete[] data; };
    outQueue_.push_back(std::move(packet));
    return true;
}

bool NvMediaEncoder::GetFrame(CodecFrame& out) {
    if (outQueue_.empty()) {
        return false;
    }
    out = std::move(outQueue_.front());
    outQueue_.pop_front();
    return true;
}

void NvMediaEncoder::Finalize() {
    // Unregister/free the input surfaces (inverse of Initialize).
    for (auto& surface : pool_) {
        if (surface.pending) {
            NvSciSyncFenceClear(&surface.eofFence);
            surface.pending = false;
        }
        if (surface.buf) {
            if (encoder_) {
                NvMediaIEPUnregisterNvSciBufObj(encoder_, surface.buf);
            }
            NvSciBufObjFree(surface.buf);
            surface.buf = nullptr;
        }
    }
    pool_.clear();

    if (encoder_) {
        if (preSyncObj_) {
            NvMediaIEPUnregisterNvSciSyncObj(encoder_, preSyncObj_);
            NvSciSyncObjFree(preSyncObj_);
            preSyncObj_ = nullptr;
        }
        if (eofSyncObj_) {
            NvMediaIEPUnregisterNvSciSyncObj(encoder_, eofSyncObj_);
            NvSciSyncObjFree(eofSyncObj_);
            eofSyncObj_ = nullptr;
        }
        NvMediaIEPDestroy(encoder_);
        encoder_ = nullptr;
    }
    if (cpuWaitContext_) {
        NvSciSyncCpuWaitContextFree(cpuWaitContext_);
        cpuWaitContext_ = nullptr;
    }
    if (syncModule_) {
        NvSciSyncModuleClose(syncModule_);
        syncModule_ = nullptr;
    }
    if (bufModule_) {
        NvSciBufModuleClose(bufModule_);
        bufModule_ = nullptr;
    }

    // Free any packets not yet consumed by the caller.
    for (auto& packet : outQueue_) {
        if (packet.release) {
            packet.release();
        }
    }
    outQueue_.clear();
    initialized_ = false;
}

std::string NvMediaEncoder::getName() const {
    return "nvmedia";
}

HALCODEC_CONNECT(Encoder, nvmedia, NvMediaEncoder);

} // namespace nvmedia
} // namespace halcodec