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

// Read back the plane geometry the reconciled surface actually received.
//
// IEP/NvSciBuf pad the allocated surface beyond the requested encode size: the
// plane pitch gets rounded up to the allocator's alignment, so it is commonly
// larger than the width we asked for (320 -> 512 here). NvSciBufObjPutPixels
// validates srcPtrSizes against the surface it writes into and returns
// NvSciError_BadParameter when an entry is smaller, and does the same for a
// zero pitch -- deducing the stride from the requested width is exactly how
// that check trips. Mirrors ReadInput() in
// samples/nvmedia_6x/utils/scibuf_utils.c, which reads the plane attributes
// back rather than trusting the dimensions it was handed.
//
// `w`/`h` receive the luma plane width/height; `pitch`/`planeH` receive both
// planes' pitches and heights (index 0 = luma, 1 = chroma); `alignedH` receives
// the HW-aligned plane heights the allocator actually reserved (which are what
// NvSciBufObjPutPixels sizes each plane against). Returns false if the object
// refuses to report usable geometry, in which case the caller keeps its
// tightly-packed assumption.
bool GetSurfacePlaneGeometry(NvSciBufObj buf, uint32_t* w, uint32_t* h,
                             uint32_t* pitch, uint32_t* planeH,
                             uint32_t* alignedH) {
    if (!buf || !w || !h || !pitch || !planeH || !alignedH) {
        return false;
    }
    NvSciBufAttrList attrs = nullptr;
    if (NvSciBufObjGetAttrList(buf, &attrs) != NvSciError_Success || !attrs) {
        return false;
    }
    // Passing value=NULL asks NvSciBuf to point at its internal storage; a
    // caller-supplied buffer reads back zero on this build. Each key is fetched
    // in its own call: a batch fails atomically (BadParameter) if any member is
    // unset on the reconciled list.
    NvSciBufAttrKeyValuePair dims[] = {
        {NvSciBufImageAttrKey_PlaneWidth, nullptr, 0},   // [0]
        {NvSciBufImageAttrKey_PlaneHeight, nullptr, 0},  // [1]
        {NvSciBufImageAttrKey_PlanePitch, nullptr, 0},   // [2]
    };
    if (NvSciBufAttrListGetAttrs(attrs, dims, 3) != NvSciError_Success) {
        return false;
    }
    NvSciBufAttrKeyValuePair aligned = {NvSciBufImageAttrKey_PlaneAlignedHeight,
                                        nullptr, 0};
    // Aligned-height is an output key the allocator fills in; if it is absent
    // (older SDK), fall back to the unaligned plane height.
    bool haveAligned = (NvSciBufAttrListGetAttrs(attrs, &aligned, 1) ==
                            NvSciError_Success &&
                        aligned.value);
    const uint32_t* pw = static_cast<const uint32_t*>(dims[0].value);
    const uint32_t* ph = static_cast<const uint32_t*>(dims[1].value);
    const uint32_t* pp = static_cast<const uint32_t*>(dims[2].value);
    const uint32_t* pa = haveAligned
                             ? static_cast<const uint32_t*>(aligned.value)
                             : nullptr;
    if (!pw || !ph || !pp || pw[0] == 0 || ph[0] == 0 || pp[0] == 0 ||
        pp[1] == 0) {
        return false;
    }
    *w = pw[0];
    *h = ph[0];
    pitch[0] = pp[0];
    pitch[1] = pp[1];
    planeH[0] = ph[0];
    planeH[1] = ph[1];
    alignedH[0] = pa ? pa[0] : ph[0];
    alignedH[1] = pa ? pa[1] : ph[1];
    return true;
}

// Print the surface attributes that decide whether NvSciBufObjPutPixels() will
// accept a CPU upload. Kept because the failure mode is opaque: a rejected
// upload reports only NvSciError_BadParameter (0x100), and the interesting
// facts (did reconciliation keep the CPU-access/ReadWrite permissions, how big
// is each plane really) are only visible here. Each key is fetched in its own
// GetAttrs call: the NvSciBuf implementation returns NvSciError_BadParameter
// for the whole batch if any single key is unset on the reconciled list, so a
// mixed batch fails atomically and reports nothing.
void DumpOneAttr(NvSciBufObj buf, NvSciBufAttrKey key, const char* name) {
    NvSciBufAttrList attrs = nullptr;
    if (NvSciBufObjGetAttrList(buf, &attrs) != NvSciError_Success || !attrs) {
        std::cerr << "  " << name << ": no attr list" << std::endl;
        return;
    }
    NvSciBufAttrKeyValuePair k = {key, nullptr, 0};
    NvSciError e = NvSciBufAttrListGetAttrs(attrs, &k, 1);
    if (e != NvSciError_Success || !k.value) {
        std::cerr << "  " << name << ": unset (0x" << std::hex << e << std::dec
                  << ")" << std::endl;
        return;
    }
    // Print as raw uint32 words; the relevant keys are uint8/uint32/enums that
    // all fit in the first word.
    const uint32_t* v = static_cast<const uint32_t*>(k.value);
    std::cerr << "  " << name << ": " << v[0];
    if (k.len > sizeof(uint32_t)) std::cerr << "," << v[1];
    if (k.len > 2 * sizeof(uint32_t)) std::cerr << "," << v[2];
    std::cerr << " (len " << k.len << ")" << std::endl;
}

void DumpSurfaceAttrs(NvSciBufObj buf) {
    std::cerr << "NvMediaEncoder: surface attr dump:" << std::endl;
    DumpOneAttr(buf, NvSciBufGeneralAttrKey_NeedCpuAccess, "NeedCpuAccess");
    DumpOneAttr(buf, NvSciBufGeneralAttrKey_RequiredPerm, "RequiredPerm");
    DumpOneAttr(buf, NvSciBufGeneralAttrKey_CpuNeedSwCacheCoherency,
                "CpuNeedSwCacheCoherency");
    DumpOneAttr(buf, NvSciBufImageAttrKey_PlanePitch, "PlanePitch");
    DumpOneAttr(buf, NvSciBufImageAttrKey_PlaneAlignedHeight,
                "PlaneAlignedHeight");
    DumpOneAttr(buf, NvSciBufImageAttrKey_Size, "Size");
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

    // Init parameters mirror SetEncoderInitParamsH264() in image_encoder.c and
    // the H264_HP_DEFAULT_PERF.cfg defaults: High profile, level 4.1, 4 ref
    // frames. Rate control / GOP are configured separately via
    // NvMediaIEPSetConfiguration in SetupSync() (FeedFrame requires it as a
    // precondition).
    NvMediaEncodeInitializeParamsH264 initParams;
    std::memset(&initParams, 0, sizeof(initParams));
    initParams.encodeHeight = height_;
    initParams.encodeWidth = width_;
    initParams.frameRateDen = frameRateDen_;
    initParams.frameRateNum = frameRateNum_;
    initParams.profile = NVMEDIA_ENCODE_PROFILE_HIGH;
    initParams.level = NVMEDIA_ENCODE_LEVEL_H264_41;
    initParams.maxNumRefFrames = 4;
    initParams.useBFramesAsRef = false;
    initParams.enableExtProfile = false;

    // Reconcile the input-surface attributes: IEP injects its internal
    // requirements, while we add the CPU-writable NV12 image description.
    // The reconciled list feeds NvMediaIEPCreate() and the surface pool.
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

    // Use NvMediaIEPCreate (the image_encoder sample's default create path),
    // not NvMediaIEPCreateCtx + NvMediaIEPInit. The split CreateCtx/Init path
    // was observed to make FeedFrame return NVMEDIA_STATUS_ERROR (0x1) on this
    // DRIVE build even with an otherwise sample-matching config; the single
    // NvMediaIEPCreate call matches the verified-working sample binary.
    encoder_ = NvMediaIEPCreate(
        iepType_, &initParams, reconciled,
        static_cast<uint8_t>(kMaxBuffering), instanceId_);
    if (!encoder_) {
        std::cerr << "NvMediaEncoder: NvMediaIEPCreate failed" << std::endl;
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

    // Learn the surfaces' real pitch/height: the allocator pads each plane, so
    // the upload cannot assume width_/height_. Fall back to the tightly packed
    // assumption (which at least keeps the failure diagnosable) if the object
    // will not report geometry.
    if (!GetSurfacePlaneGeometry(pool_[0].buf, &surfaceLumaW_, &surfaceLumaH_,
                                 planePitch_, planeHeight_, alignedHeight_)) {
        std::cerr << "NvMediaEncoder: surface geometry unreadable, assuming "
                  << width_ << "x" << height_ << " packed" << std::endl;
        planePitch_[0] = width_;
        planePitch_[1] = width_;
        planeHeight_[0] = height_;
        planeHeight_[1] = height_ >> 1;
        alignedHeight_[0] = height_;
        alignedHeight_[1] = height_ >> 1;
    } else if (surfaceLumaW_ < width_ || surfaceLumaH_ < height_ ||
               planePitch_[0] < width_ || planeHeight_[0] < height_) {        // A surface smaller than the frame we are about to copy into it means
        // the pool does not match the configured coded size.
        std::cerr << "NvMediaEncoder: surface " << surfaceLumaW_ << "x"
                  << surfaceLumaH_ << " pitch " << planePitch_[0]
                  << " too small for " << width_ << "x" << height_
                  << std::endl;
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
              << height_ << " (" << pool_.size() << " bufs), planes "
              << surfaceLumaW_ << "x" << alignedHeight_[0] << " pitch "
              << planePitch_[0] << " / " << (surfaceLumaW_ >> 1) << "x"
              << alignedHeight_[1] << " pitch " << planePitch_[1] << std::endl;
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

    // Configure the encoder's rate control / GOP. NvMediaIEPFeedFrame's
    // documented precondition requires NvMediaIEPSetConfiguration() to have
    // been called at least once; without it FeedFrame returns
    // NVMEDIA_STATUS_ERROR. Uses CBR with a modest bitrate and an
    // intra-only GOP (the simplest config that produces a valid stream). The
    // rcParams are also mirrored into each per-frame picParams (the
    // image_encoder sample populates picParams.rcParams from the config on
    // every frame; an all-zero rcParams under CBR makes FeedFrame return
    // NVMEDIA_STATUS_ERROR on this DRIVE build).
    rcParams_ = {};
    rcParams_.rateControlMode = NVMEDIA_ENCODE_PARAMS_RC_CBR;
    rcParams_.params.cbr.averageBitRate = 4 * 1000 * 1000;
    // VBV size/delay = 0 lets the driver pick the level-appropriate default
    // (the image_encoder H264_HP_DEFAULT_PERF.cfg uses RCVbvBufferSize=0 and
    // RCVbvInitialDelay=0). Nonzero values that don't fit the level were
    // suspected of making FeedFrame return NVMEDIA_STATUS_ERROR.
    rcParams_.params.cbr.vbvBufferSize = 0;
    rcParams_.params.cbr.vbvInitialDelay = 0;
    NvMediaEncodeConfigH264 configH264;
    std::memset(&configH264, 0, sizeof(configH264));
    // Match the image_encoder H264_HP_DEFAULT_PERF.cfg: gopLength=0 and
    // idrPeriod=0 (driver-selected defaults -- 0 is not INFINITE_GOPLENGTH,
    // which is 0xFFFFFFFF). repeatSPSPPS=0 in the sample (SPS/PPS not
    // repeated); encPreset=HP (0x10).
    configH264.gopLength = 0;
    configH264.idrPeriod = 0;
    configH264.repeatSPSPPS = NVMEDIA_ENCODE_SPSPPS_REPEAT_DISABLED;
    configH264.entropyCodingMode = NVMEDIA_ENCODE_H264_ENTROPY_CODING_MODE_CABAC;
    configH264.encPreset = NVMEDIA_ENC_PRESET_HP;  // sample default (0x10)
    configH264.rcParams = rcParams_;
    // The image_encoder sample always attaches a (zeroed) VUI params struct
    // pointer; mirror that to keep the config byte-identical to the working
    // sample (configH264.h264VUIParameters is otherwise NULL here).
    NvMediaEncodeConfigH264VUIParams vuiParams;
    std::memset(&vuiParams, 0, sizeof(vuiParams));
    configH264.h264VUIParameters = &vuiParams;
    if (NvMediaIEPSetConfiguration(encoder_, &configH264) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaEncoder: NvMediaIEPSetConfiguration failed"
                  << std::endl;
        return false;
    }
    return true;
}

bool NvMediaEncoder::FillFrame(const CodecFrame& in) {
    if (!initialized_ || eof_) {
        return false;
    }

    // Zero-size HOST frame marks the end of the stream: drain the remaining
    // bitstream (trailing packets surface only after the last input settles).
    // A zero-copy device frame also reports size == 0 (its bytes are
    // device-resident, host size unknown until downloaded), so gate the EOS
    // path on locality == Host to avoid swallowing real device frames.
    if (in.size == 0 && in.locality == FrameLocality::Host) {
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

    if (in.locality == FrameLocality::NvSciBufObj && in.device.nvSciBufObj) {
        // Zero-copy decoded frame: feed the producer's surface directly.
        if (FeedDeviceFrame(in)) {
            return true;
        }
        std::cerr << "NvMediaEncoder: device-frame feed failed, falling back "
                     "to host upload" << std::endl;
    }

    if (in.locality != FrameLocality::Host) {
        std::cerr << "NvMediaEncoder: unsupported frame locality (host or "
                     "NvSciBufObj only)" << std::endl;
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

    // Pre-fence flow matching image_encoder.c L2773-2792: GenerateFence ->
    // InsertPreNvSciSyncFence -> Signal -> (upload) -> FeedFrame. The Pre fence
    // is inserted BEFORE the surface upload: the IEP latches the fence at insert
    // time, and inserting it after PutPixels (our previous order) made FeedFrame
    // return NVMEDIA_STATUS_ERROR (0x1) on this DRIVE build even though every
    // other API call was byte-identical to the working sample. The fence must be
    // zero-initialized (NvSciSyncFenceInitializer / memset): passing an
    // uninitialized NvSciSyncFence to GenerateFence panics on this DRIVE build.
    NvSciSyncFence preFence{};
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
    NvSciError sigErr = NvSciSyncObjSignal(preSyncObj_);
    if (sigErr != NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciSyncObjSignal failed: 0x"
                  << std::hex << sigErr << std::dec << std::endl;
    }
    // Stage the frame as a 3-plane planar YUV420 (Y, U, V) CPU buffer. This
    // DRIVE build's NvSciBufObjPutPixels rejects a 2-plane semi-planar
    // (NV12-style) source for a V8U8 surface with BadParameter (0x100) and
    // only accepts a 3-plane planar layout -- verified directly on the board
    // with a standalone PutPixels probe (2-plane -> 0x100, 3-plane -> 0x0).
    // The decoder's GetPixels path uses the same 3-plane convention. Plane
    // sizes/pitches: Y = w*h pitch w, U = V = (w/2)*(h/2) pitch (w/2). I420
    // input is already planar; NV12 is de-interleaved on the fly.
    const uint32_t cw = w >> 1;  // chroma samples per row (each of U, V)
    const uint32_t ch = h >> 1;
    const size_t yBytes = static_cast<size_t>(w) * h;
    const size_t cBytes = static_cast<size_t>(cw) * ch;  // each of U, V
    std::vector<uint8_t> yPlane(yBytes, 0);
    std::vector<uint8_t> uPlane(cBytes, 0);
    std::vector<uint8_t> vPlane(cBytes, 0);
    std::memcpy(yPlane.data(), in.data, yBytes);
    if (in.format == PixelFormat::I420) {
        std::memcpy(uPlane.data(), in.data + yBytes, cBytes);
        std::memcpy(vPlane.data(), in.data + yBytes + cBytes, cBytes);
    } else {
        // NV12 (and Unknown: treated as semi-planar): de-interleave UV.
        const uint8_t* srcUV = in.data + yBytes;
        for (uint32_t r = 0; r < ch; ++r) {
            const uint8_t* uvRow = srcUV + static_cast<size_t>(r) * w;
            uint8_t* uRow = uPlane.data() + static_cast<size_t>(r) * cw;
            uint8_t* vRow = vPlane.data() + static_cast<size_t>(r) * cw;
            for (uint32_t c = 0; c < cw; ++c) {
                uRow[c] = uvRow[2 * c];
                vRow[c] = uvRow[2 * c + 1];
            }
        }
    }

    const void* srcPtrs[3] = {yPlane.data(), uPlane.data(), vPlane.data()};
    const uint32_t srcSizes[3] = {static_cast<uint32_t>(yBytes),
                                  static_cast<uint32_t>(cBytes),
                                  static_cast<uint32_t>(cBytes)};
    const uint32_t srcPitches[3] = {w, cw, cw};
    NvSciError putErr = NvSciBufObjPutPixels(surface.buf, nullptr, srcPtrs,
                                             srcSizes, srcPitches);
    if (putErr != NvSciError_Success) {
        std::cerr << "NvMediaEncoder: NvSciBufObjPutPixels failed: 0x"
                  << std::hex << putErr << std::dec << " (3-plane " << w << "x"
                  << h << " pitch " << w << ", " << cw << "x" << ch
                  << " pitch " << cw << ")" << std::endl;
        NvSciSyncFenceClear(&preFence);
        return false;
    }

    NvMediaEncodePicParamsH264 picParams;
    std::memset(&picParams, 0, sizeof(picParams));
    // Mirror SetEncodePicParamsH264: AUTOSELECT picture type, encodePicFlags
    // clear, and rcParams left ZERO. The sample's picParams.rcParams is in fact
    // all-zero at FeedFrame time (SetEncodeConfigRCParam dedups on rcSectionIndex
    // and skips re-population after the first call), and populating it here with
    // the CBR params made FeedFrame return NVMEDIA_STATUS_ERROR (0x1) on this
    // DRIVE build. Rate control is established by NvMediaIEPSetConfiguration,
    // not by the per-frame picParams.
    picParams.pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;

    NvMediaStatus status =
        NvMediaIEPFeedFrame(encoder_, surface.buf, &picParams, instanceId_);
    NvSciSyncFenceClear(&preFence);
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

bool NvMediaEncoder::FeedDeviceFrame(const CodecFrame& in) {
    NvSciBufObj buf = static_cast<NvSciBufObj>(in.device.nvSciBufObj);

    // Registration is per-object; keep a registry so a long-lived pooled
    // surface is registered once. Each registered object holds an IEP-internal
    // reference, keeping it alive until Finalize() unregisters it.
    bool registered = false;
    for (NvSciBufObj b : externalBufs_) {
        if (b == buf) {
            registered = true;
            break;
        }
    }
    if (!registered) {
        if (NvMediaIEPRegisterNvSciBufObj(encoder_, buf) != NVMEDIA_STATUS_OK) {
            return false;
        }
        externalBufs_.push_back(buf);
    }

    // The producer wrote the frame on another engine, so order that work before
    // the encode: generate a fence on the Pre sync object, have IEP wait on it,
    // then signal it. (A cross-engine producer would hand over its own fence;
    // for the NvMedia decoder -> encoder pipeline both engines are in this
    // process and the decoded surface is already fence-waited before handoff,
    // so a locally signaled Pre fence is sufficient.)
    NvSciSyncFence preFence{};
    if (NvSciSyncObjGenerateFence(preSyncObj_, &preFence) != NvSciError_Success) {
        return false;
    }
    if (NvMediaIEPInsertPreNvSciSyncFence(encoder_, &preFence) !=
        NVMEDIA_STATUS_OK) {
        NvSciSyncFenceClear(&preFence);
        return false;
    }
    NvSciSyncObjSignal(preSyncObj_);
    NvSciSyncFenceClear(&preFence);

    // Feed the external surface into a free pool slot's ordering context: the
    // slot's pending EOF fence is what guards reuse of the *external* buffer.
    Surface& surface = pool_[nextSurface_];
    nextSurface_ = (nextSurface_ + 1) % pool_.size();
    if (surface.pending) {
        if (NvSciSyncFenceWait(&surface.eofFence, cpuWaitContext_,
                               kFenceWaitTimeoutUs) != NvSciError_Success) {
            return false;
        }
        NvSciSyncFenceClear(&surface.eofFence);
        surface.pending = false;
    }

    NvMediaEncodePicParamsH264 picParams;
    std::memset(&picParams, 0, sizeof(picParams));
    picParams.pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;

    NvMediaStatus status =
        NvMediaIEPFeedFrame(encoder_, buf, &picParams, instanceId_);
    if (status == NVMEDIA_STATUS_INSUFFICIENT_BUFFERING) {
        if (!Drain()) {
            return false;
        }
        status = NvMediaIEPFeedFrame(encoder_, buf, &picParams, instanceId_);
    }
    if (status != NVMEDIA_STATUS_OK) {
        return false;
    }
    ++frameIndex_;

    // Wait for the encode to finish reading the external surface, then return
    // the borrowed slot to the producer (decoder Surface::borrowed--).
    if (NvMediaIEPGetEOFNvSciSyncFence(encoder_, eofSyncObj_,
                                       &surface.eofFence) != NVMEDIA_STATUS_OK) {
        return false;
    }
    surface.pending = true;
    if (NvSciSyncFenceWait(&surface.eofFence, cpuWaitContext_,
                           kFenceWaitTimeoutUs) != NvSciError_Success) {
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
    // NvMediaIEPGetBits reports the byte count through numBytes (the IEP 6x
    // API fills the out-param, not the NvMediaBitstreamBuffer.bitstreamBytes
    // field on this DRIVE build). Trust numBytes.
    if (s != NVMEDIA_STATUS_OK || numBytes == 0) {
        std::cerr << "NvMediaEncoder: NvMediaIEPGetBits failed: 0x" << std::hex
                  << s << std::dec << std::endl;
        return false;
    }

    // Hand the encoded bytes to the caller via a heap buffer owned by the
    // frame; GetFrame() transfers ownership to the app, which releases it.
    const uint32_t bytes = numBytes;
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

    // External buffers are owned by the producer (e.g. the decoder's surface
    // pool): drop IEP's registration but do not free them.
    if (encoder_) {
        for (NvSciBufObj buf : externalBufs_) {
            NvMediaIEPUnregisterNvSciBufObj(encoder_, buf);
        }
    }
    externalBufs_.clear();

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