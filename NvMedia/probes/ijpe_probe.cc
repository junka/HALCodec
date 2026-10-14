// IJPE (image JPEG encoder) end-to-end probe: creates an IJPE encoder for a
// 320x240 YUV420 planar surface, uploads a synthetic gradient frame, feeds it
// with NvMediaIJPEFeedFrame(quality), and retrieves the JPEG bitstream with
// NvMediaIJPEGetBits. Verifies the full NvSci plumbing (FillNvSciBufAttrList,
// RegisterNvSciBufObj, FillNvSciSyncAttrList, RegisterNvSciSyncObj,
// SetNvSciSyncObjforEOF, SetAttributes, InsertPreNvSciSyncFence,
// GetEOFNvSciSyncFence) that the framework's nvmediaencoder.cc JPEG path uses.
//
// Build (cross, against thor SDK):
//   aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include/nvmedia_6x \
//       ijpe_probe.cc -o /tmp/ijpe_probe \
//       -Wl,-rpath-link,/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -L/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -lnvmedia_ijpe_sci -lnvvideo -lnvscibuf -lnvscisync \
//       -lnvrm_surface -lnvos -lnvsocsys -ldl
// Run on orin:
//   LD_LIBRARY_PATH=$HOME/sdk_libs /tmp/ijpe_probe [quality] [width] [height]
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "nvscibuf.h"
#include "nvscisync.h"
#include "nvmedia_ijpe.h"

namespace {

constexpr uint32_t kSurfaceAlign = 256;
constexpr uint64_t kFenceWaitTimeoutUs = 1000 * 1000;

bool FillIJPESurfaceAttrs(NvSciBufAttrList list, uint32_t w, uint32_t h) {
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

}  // namespace

int main(int argc, char** argv) {
    uint8_t quality = (argc > 1) ? static_cast<uint8_t>(std::atoi(argv[1]))
                                 : 75;
    uint32_t w = (argc > 2) ? static_cast<uint32_t>(std::atoi(argv[2])) : 320;
    uint32_t h = (argc > 3) ? static_cast<uint32_t>(std::atoi(argv[3])) : 240;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    // Align coded dims up to 16 (parity with the framework encoder).
    w = (w + 15U) & ~15U;
    h = (h + 15U) & ~15U;
    std::cout << "ijpe_probe: " << w << "x" << h << " quality "
              << static_cast<int>(quality) << std::endl;

    NvMediaVersion ver{};
    if (NvMediaIJPEGetVersion(&ver) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPEGetVersion failed" << std::endl;
        return 1;
    }

    NvSciBufModule bufModule = nullptr;
    NvSciSyncModule syncModule = nullptr;
    NvSciSyncCpuWaitContext cpuWaitCtx = nullptr;
    if (NvSciBufModuleOpen(&bufModule) != NvSciError_Success || !bufModule) {
        std::cerr << "NvSciBufModuleOpen failed" << std::endl;
        return 1;
    }
    if (NvSciSyncModuleOpen(&syncModule) != NvSciError_Success ||
        !syncModule) {
        std::cerr << "NvSciSyncModuleOpen failed" << std::endl;
        return 1;
    }
    if (NvSciSyncCpuWaitContextAlloc(syncModule, &cpuWaitCtx) !=
        NvSciError_Success) {
        std::cerr << "NvSciSyncCpuWaitContextAlloc failed" << std::endl;
        return 1;
    }

    // Reconcile IJPE's buf-attr list with our PitchLinear YUV420 description.
    NvSciBufAttrList attrList = nullptr;
    NvSciBufAttrList reconciled = nullptr;
    NvSciBufAttrList conflict = nullptr;
    if (NvSciBufAttrListCreate(bufModule, &attrList) != NvSciError_Success) {
        std::cerr << "NvSciBufAttrListCreate failed" << std::endl;
        return 1;
    }
    if (NvMediaIJPEFillNvSciBufAttrList(NVMEDIA_JPEG_INSTANCE_0, attrList) !=
            NVMEDIA_STATUS_OK ||
        !FillIJPESurfaceAttrs(attrList, w, h)) {
        std::cerr << "IJPE fill buf attrs failed" << std::endl;
        return 1;
    }
    NvSciBufAttrList attrArray[1] = {attrList};
    if (NvSciBufAttrListReconcile(attrArray, 1, &reconciled, &conflict) !=
        NvSciError_Success) {
        std::cerr << "buf attr reconcile failed" << std::endl;
        return 1;
    }
    NvSciBufAttrListFree(attrList);

    uint32_t maxBits = w * h * 3U / 2U + 4096;
    NvMediaIJPE* ijpe = NvMediaIJPECreate(reconciled, 5, maxBits,
                                          NVMEDIA_JPEG_INSTANCE_0);
    if (!ijpe) {
        std::cerr << "NvMediaIJPECreate failed" << std::endl;
        NvSciBufAttrListFree(reconciled);
        NvSciBufAttrListFree(conflict);
        return 1;
    }
    std::cout << "NvMediaIJPECreate ok" << std::endl;

    // Allocate + register the input surface. reconciled is still valid here
    // (freed only after the alloc + register succeed).
    NvSciBufObj bufObj = nullptr;
    if (NvSciBufObjAlloc(reconciled, &bufObj) != NvSciError_Success || !bufObj) {
        std::cerr << "NvSciBufObjAlloc failed" << std::endl;
        NvSciBufAttrListFree(reconciled);
        NvSciBufAttrListFree(conflict);
        NvMediaIJPEDestroy(ijpe);
        return 1;
    }
    NvSciBufAttrListFree(reconciled);
    NvSciBufAttrListFree(conflict);
    if (NvMediaIJPERegisterNvSciBufObj(ijpe, bufObj) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPERegisterNvSciBufObj failed" << std::endl;
        NvSciBufObjFree(bufObj);
        NvMediaIJPEDestroy(ijpe);
        return 1;
    }

    // EOF + Pre sync objects (4-list pattern).
    NvSciSyncAttrList sigList = nullptr, waitList = nullptr;
    NvSciSyncAttrList cpuWaitList = nullptr, cpuSigList = nullptr;
    NvSciSyncAttrListCreate(syncModule, &sigList);
    NvSciSyncAttrListCreate(syncModule, &waitList);
    NvSciSyncAttrListCreate(syncModule, &cpuWaitList);
    NvSciSyncAttrListCreate(syncModule, &cpuSigList);
    NvMediaIJPEFillNvSciSyncAttrList(ijpe, sigList, NVMEDIA_SIGNALER);
    NvMediaIJPEFillNvSciSyncAttrList(ijpe, waitList, NVMEDIA_WAITER);
    bool cpuAccess = true;
    NvSciSyncAccessPerm waitPerm = NvSciSyncAccessPerm_WaitOnly;
    NvSciSyncAccessPerm sigPerm = NvSciSyncAccessPerm_SignalOnly;
    NvSciSyncAttrKeyValuePair waiterAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &waitPerm, sizeof(waitPerm)}};
    NvSciSyncAttrKeyValuePair signalerAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &sigPerm, sizeof(sigPerm)}};
    NvSciSyncAttrListSetAttrs(cpuWaitList, waiterAttrs, 2);
    NvSciSyncAttrListSetAttrs(cpuSigList, signalerAttrs, 2);

    NvSciSyncObj eofObj = nullptr, preObj = nullptr;
    NvSciSyncAttrList unrec[2] = {nullptr, nullptr};
    NvSciSyncAttrList rec = nullptr, conf = nullptr;
    unrec[0] = sigList; unrec[1] = cpuWaitList;
    NvSciSyncAttrListReconcile(unrec, 2, &rec, &conf);
    NvSciSyncObjAlloc(rec, &eofObj);
    NvSciSyncAttrListFree(rec); NvSciSyncAttrListFree(conf);
    unrec[0] = waitList; unrec[1] = cpuSigList;
    NvSciSyncAttrListReconcile(unrec, 2, &rec, &conf);
    NvSciSyncObjAlloc(rec, &preObj);
    NvSciSyncAttrListFree(rec); NvSciSyncAttrListFree(conf);
    NvSciSyncAttrListFree(sigList); NvSciSyncAttrListFree(waitList);
    NvSciSyncAttrListFree(cpuWaitList); NvSciSyncAttrListFree(cpuSigList);
    if (!eofObj || !preObj) {
        std::cerr << "sync obj alloc failed" << std::endl;
        NvMediaIJPEDestroy(ijpe);
        return 1;
    }
    if (NvMediaIJPERegisterNvSciSyncObj(ijpe, NVMEDIA_EOFSYNCOBJ, eofObj) !=
            NVMEDIA_STATUS_OK ||
        NvMediaIJPERegisterNvSciSyncObj(ijpe, NVMEDIA_PRESYNCOBJ, preObj) !=
            NVMEDIA_STATUS_OK ||
        NvMediaIJPESetNvSciSyncObjforEOF(ijpe, eofObj) != NVMEDIA_STATUS_OK) {
        std::cerr << "IJPE register sync objs failed" << std::endl;
        return 1;
    }

    NvMediaJPEncAttributes attrs;
    std::memset(&attrs, 0, sizeof(attrs));
    attrs.quality = quality;
    if (NvMediaIJPESetAttributes(ijpe, NVMEDIA_IMAGE_JPEG_ATTRIBUTE_QUALITY,
                                 &attrs) != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPESetAttributes failed" << std::endl;
        return 1;
    }

    // Upload a synthetic 3-plane YUV420 gradient.
    const uint32_t cw = w >> 1, ch = h >> 1;
    const size_t yBytes = static_cast<size_t>(w) * h;
    const size_t cBytes = static_cast<size_t>(cw) * ch;
    std::vector<uint8_t> yPlane(yBytes), uPlane(cBytes, 128), vPlane(cBytes, 128);
    for (uint32_t r = 0; r < h; ++r)
        for (uint32_t c = 0; c < w; ++c)
            yPlane[r * w + c] = static_cast<uint8_t>((r + c) & 0xFF);
    const void* srcPtrs[3] = {yPlane.data(), uPlane.data(), vPlane.data()};
    const uint32_t srcSizes[3] = {static_cast<uint32_t>(yBytes),
                                  static_cast<uint32_t>(cBytes),
                                  static_cast<uint32_t>(cBytes)};
    const uint32_t srcPitches[3] = {w, cw, cw};
    NvSciError putErr = NvSciBufObjPutPixels(bufObj, nullptr, srcPtrs, srcSizes,
                                             srcPitches);
    if (putErr != NvSciError_Success) {
        std::cerr << "NvSciBufObjPutPixels failed: 0x" << std::hex << putErr
                  << std::dec << std::endl;
        return 1;
    }

    // Pre-fence: generate -> insert -> signal -> feed.
    NvSciSyncFence preFence{};
    NvSciSyncObjGenerateFence(preObj, &preFence);
    NvMediaIJPEInsertPreNvSciSyncFence(ijpe, &preFence);
    NvSciSyncObjSignal(preObj);

    NvMediaStatus st = NvMediaIJPEFeedFrame(ijpe, bufObj, quality,
                                            NVMEDIA_JPEG_INSTANCE_0);
    NvSciSyncFenceClear(&preFence);
    if (st != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPEFeedFrame failed: 0x" << std::hex << st
                  << std::dec << std::endl;
        return 1;
    }
    std::cout << "NvMediaIJPEFeedFrame ok" << std::endl;

    NvSciSyncFence eofFence{};
    if (NvMediaIJPEGetEOFNvSciSyncFence(ijpe, eofObj, &eofFence) !=
        NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPEGetEOFNvSciSyncFence failed" << std::endl;
        return 1;
    }
    if (NvSciSyncFenceWait(&eofFence, cpuWaitCtx, kFenceWaitTimeoutUs) !=
        NvSciError_Success) {
        std::cerr << "EOF FenceWait failed" << std::endl;
        return 1;
    }
    NvSciSyncFenceClear(&eofFence);

    uint32_t numBytes = 0;
    st = NvMediaIJPEBitsAvailable(ijpe, &numBytes,
                                  NVMEDIA_ENCODE_BLOCKING_TYPE_IF_PENDING,
                                  1000);
    if (st != NVMEDIA_STATUS_OK || numBytes == 0) {
        std::cerr << "NvMediaIJPEBitsAvailable failed: 0x" << std::hex << st
                  << std::dec << " numBytes=" << numBytes << std::endl;
        return 1;
    }

    std::vector<uint8_t> jpeg(numBytes, 0);
    uint32_t got = 0;
    st = NvMediaIJPEGetBits(ijpe, &got, jpeg.data(), NVMEDIA_JPEG_ENC_FLAG_NONE);
    if (st != NVMEDIA_STATUS_OK || got == 0) {
        std::cerr << "NvMediaIJPEGetBits failed: 0x" << std::hex << st
                  << std::dec << " got=" << got << std::endl;
        return 1;
    }

    // Validate the JPEG starts with SOI (FF D8) and ends with EOI (FF D9).
    bool soi = jpeg.size() >= 2 && jpeg[0] == 0xFF && jpeg[1] == 0xD8;
    bool eoi = got >= 2 && jpeg[got - 2] == 0xFF && jpeg[got - 1] == 0xD9;
    std::cout << "JPEG " << got << " bytes, SOI=" << soi << " EOI=" << eoi
              << std::endl;

    // Write it out for ffmpeg round-trip verification.
    FILE* f = std::fopen("/tmp/ijpe_probe.jpg", "wb");
    if (f) {
        std::fwrite(jpeg.data(), 1, got, f);
        std::fclose(f);
        std::cout << "wrote /tmp/ijpe_probe.jpg" << std::endl;
    }

    NvMediaIJPEUnregisterNvSciSyncObj(ijpe, preObj);
    NvMediaIJPEUnregisterNvSciSyncObj(ijpe, eofObj);
    NvSciSyncObjFree(preObj);
    NvSciSyncObjFree(eofObj);
    NvMediaIJPEUnregisterNvSciBufObj(ijpe, bufObj);
    NvSciBufObjFree(bufObj);
    NvMediaIJPEDestroy(ijpe);
    NvSciSyncCpuWaitContextFree(cpuWaitCtx);
    NvSciSyncModuleClose(syncModule);
    NvSciBufModuleClose(bufModule);
    return (soi && eoi) ? 0 : 2;
}
