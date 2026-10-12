// IJPD (image JPEG decoder) end-to-end probe: decodes a JPEG file into a YUV420
// surface via NvMediaIJPDRenderYUV, reads it back with NvSciBufObjGetPixels,
// and writes the raw YUV420 to /tmp/ijpd_probe.yuv. Verifies the full NvSci
// plumbing (FillNvSciBufAttrList, FillNvSciSyncAttrList, RegisterNvSciSyncObj,
// SetNvSciSyncObjforEOF, RenderYUV, GetEOFNvSciSyncFence) that the framework's
// nvmediadecoder.cc JPEG path will use.
//
// Build (cross, against thor SDK):
//   aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include/nvmedia_6x \
//       ijpd_probe.cc -o /tmp/ijpd_probe \
//       -Wl,-rpath-link,/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -L/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -lnvmedia_ijpd_sci -lnvvideo -lnvscibuf -lnvscisync \
//       -lnvrm_surface -lnvos -lnvsocsys -ldl
// Run on nv118:
//   LD_LIBRARY_PATH=$HOME/sdk_libs /tmp/ijpd_probe input.jpg
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "nvscibuf.h"
#include "nvscisync.h"
#include "nvmedia_common_encode_decode.h"
#include "nvmedia_ijpd.h"

namespace {

constexpr uint32_t kSurfaceAlign = 256;
constexpr uint64_t kFenceWaitTimeoutUs = 1000 * 1000;

// PitchLinear 3-plane Y8/U8/V8 YUV420 output surface for IJPD.
bool FillIJPDSurfaceAttrs(NvSciBufAttrList list, uint16_t w, uint16_t h) {
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
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " input.jpg" << std::endl;
        return 2;
    }
    // Read the whole JPEG file into memory.
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::cerr << "cannot open " << argv[1] << std::endl;
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long fileBytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> jpeg(fileBytes);
    if (std::fread(jpeg.data(), 1, fileBytes, f) != static_cast<size_t>(fileBytes)) {
        std::cerr << "short read" << std::endl;
        return 1;
    }
    std::fclose(f);
    std::cout << "ijpd_probe: " << argv[1] << " (" << fileBytes << " bytes)"
              << std::endl;

    NvSciBufModule bufModule = nullptr;
    NvSciSyncModule syncModule = nullptr;
    NvSciSyncCpuWaitContext cpuWaitCtx = nullptr;
    NvSciBufModuleOpen(&bufModule);
    NvSciSyncModuleOpen(&syncModule);
    NvSciSyncCpuWaitContextAlloc(syncModule, &cpuWaitCtx);

    // Probe the stream: NvMediaIJPDGetInfo is static (no decoder handle) and
    // reports the coded width/height + whether partialAccel is needed.
    NVMEDIAJPEGDecInfo info;
    std::memset(&info, 0, sizeof(info));
    NvMediaBitstreamBuffer bsBuf;
    std::memset(&bsBuf, 0, sizeof(bsBuf));
    bsBuf.bitstream = jpeg.data();
    bsBuf.bitstreamBytes = static_cast<uint32_t>(fileBytes);
    bsBuf.bitstreamSize = static_cast<uint32_t>(fileBytes);
    NvMediaStatus st = NvMediaIJPDGetInfo(&info, 1, &bsBuf);
    if (st != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPDGetInfo failed: 0x" << std::hex << st
                  << std::dec << std::endl;
        return 1;
    }
    std::cout << "IJPDGetInfo: " << info.width << "x" << info.height
              << " partialAccel=" << static_cast<int>(info.partialAccel)
              << std::endl;

    uint16_t w = info.width;
    uint16_t h = info.height;
    // Align coded dims up to 16 (parity with the framework).
    uint16_t cw = (w + 15U) & ~15U;
    uint16_t ch = (h + 15U) & ~15U;

    NvMediaIJPD* ijpd = NvMediaIJPDCreate(cw, ch,
                                          static_cast<uint32_t>(fileBytes),
                                          /*supportPartialAccel=*/false,
                                          NVMEDIA_JPEG_INSTANCE_0);
    if (!ijpd) {
        std::cerr << "NvMediaIJPDCreate failed" << std::endl;
        return 1;
    }
    std::cout << "NvMediaIJPDCreate ok (" << cw << "x" << ch << ")" << std::endl;

    // Output surface: reconcile IJPD's buf-attr list with our PitchLinear
    // 3-plane YUV420 description.
    NvSciBufAttrList attrList = nullptr, reconciled = nullptr, conflict = nullptr;
    NvSciBufAttrListCreate(bufModule, &attrList);
    NvMediaIJPDFillNvSciBufAttrList(NVMEDIA_JPEG_INSTANCE_0, attrList);
    FillIJPDSurfaceAttrs(attrList, cw, ch);
    NvSciBufAttrList arr[1] = {attrList};
    NvSciBufAttrListReconcile(arr, 1, &reconciled, &conflict);
    NvSciBufAttrListFree(attrList);

    NvSciBufObj outBuf = nullptr;
    NvSciBufObjAlloc(reconciled, &outBuf);
    NvSciBufAttrListFree(reconciled);
    NvSciBufAttrListFree(conflict);
    if (!outBuf) {
        std::cerr << "NvSciBufObjAlloc failed" << std::endl;
        NvMediaIJPDDestroy(ijpd);
        return 1;
    }
    // NvMediaIJPDRegisterNvSciBufObj is documented "not supported, can be
    // ignored" — skip it; RenderYUV accepts the bufObj directly.

    // EOF sync object (4-list pattern, IJPD signals EOF; CPU waits).
    NvSciSyncAttrList sigList = nullptr, cpuWaitList = nullptr;
    NvSciSyncAttrListCreate(syncModule, &sigList);
    NvSciSyncAttrListCreate(syncModule, &cpuWaitList);
    NvMediaIJPDFillNvSciSyncAttrList(ijpd, sigList, NVMEDIA_SIGNALER);
    bool cpuAccess = true;
    NvSciSyncAccessPerm waitPerm = NvSciSyncAccessPerm_WaitOnly;
    NvSciSyncAttrKeyValuePair waiterAttrs[2] = {
        {NvSciSyncAttrKey_NeedCpuAccess, &cpuAccess, sizeof(cpuAccess)},
        {NvSciSyncAttrKey_RequiredPerm, &waitPerm, sizeof(waitPerm)}};
    NvSciSyncAttrListSetAttrs(cpuWaitList, waiterAttrs, 2);
    NvSciSyncAttrList unrec[2] = {sigList, cpuWaitList};
    NvSciSyncAttrList rec = nullptr, conf = nullptr;
    NvSciSyncAttrListReconcile(unrec, 2, &rec, &conf);
    NvSciSyncObj eofObj = nullptr;
    NvSciSyncObjAlloc(rec, &eofObj);
    NvSciSyncAttrListFree(rec);
    NvSciSyncAttrListFree(conf);
    NvSciSyncAttrListFree(sigList);
    NvSciSyncAttrListFree(cpuWaitList);
    if (!eofObj) {
        std::cerr << "eof sync alloc failed" << std::endl;
        return 1;
    }
    NvMediaIJPDRegisterNvSciSyncObj(ijpd, NVMEDIA_EOFSYNCOBJ, eofObj);
    NvMediaIJPDSetNvSciSyncObjforEOF(ijpd, eofObj);

    // Render: decode the JPEG into the YUV surface.
    st = NvMediaIJPDRenderYUV(ijpd, outBuf, /*downscaleLog2=*/0, 1, &bsBuf,
                              NVMEDIA_IJPD_RENDER_FLAG_ROTATE_0,
                              NVMEDIA_JPEG_INSTANCE_0);
    if (st != NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPDRenderYUV failed: 0x" << std::hex << st
                  << std::dec << std::endl;
        return 1;
    }
    std::cout << "NvMediaIJPDRenderYUV ok" << std::endl;

    NvSciSyncFence eofFence{};
    if (NvMediaIJPDGetEOFNvSciSyncFence(ijpd, eofObj, &eofFence) !=
        NVMEDIA_STATUS_OK) {
        std::cerr << "NvMediaIJPDGetEOFNvSciSyncFence failed" << std::endl;
        return 1;
    }
    if (NvSciSyncFenceWait(&eofFence, cpuWaitCtx, kFenceWaitTimeoutUs) !=
        NvSciError_Success) {
        std::cerr << "EOF FenceWait failed" << std::endl;
        return 1;
    }
    NvSciSyncFenceClear(&eofFence);

    // Read back the decoded YUV via GetPixels (3-plane planar).
    const uint32_t halfW = w >> 1;
    const uint32_t halfH = h >> 1;
    const size_t yBytes = static_cast<size_t>(w) * h;
    const size_t cBytes = static_cast<size_t>(halfW) * halfH;
    std::vector<uint8_t> yPlane(yBytes, 0);
    std::vector<uint8_t> uPlane(cBytes, 0);
    std::vector<uint8_t> vPlane(cBytes, 0);
    void* planes[3] = {yPlane.data(), uPlane.data(), vPlane.data()};
    uint32_t sizes[3] = {static_cast<uint32_t>(yBytes),
                         static_cast<uint32_t>(cBytes),
                         static_cast<uint32_t>(cBytes)};
    uint32_t pitches[3] = {w, halfW, halfW};
    NvSciError gpe = NvSciBufObjGetPixels(outBuf, nullptr, planes, sizes,
                                          pitches);
    if (gpe != NvSciError_Success) {
        std::cerr << "NvSciBufObjGetPixels failed: 0x" << std::hex << gpe
                  << std::dec << std::endl;
        return 1;
    }
    std::cout << "GetPixels ok" << std::endl;

    // Write I420 planar (Y, U, V) — ffmpeg reads this with -pix_fmt yuv420p.
    FILE* out = std::fopen("/tmp/ijpd_probe.yuv", "wb");
    if (out) {
        std::fwrite(yPlane.data(), 1, yBytes, out);
        std::fwrite(uPlane.data(), 1, cBytes, out);
        std::fwrite(vPlane.data(), 1, cBytes, out);
        std::fclose(out);
        std::cout << "wrote /tmp/ijpd_probe.yuv (" << yBytes + 2 * cBytes
                  << " bytes, " << w << "x" << h << " I420)" << std::endl;
    }

    // Sanity: luma should not be all-zero (a real image decoded).
    uint32_t nonzero = 0;
    for (size_t i = 0; i < yBytes; ++i) if (yPlane[i]) ++nonzero;
    std::cout << "luma nonzero: " << nonzero << "/" << yBytes << std::endl;

    NvMediaIJPDUnregisterNvSciSyncObj(ijpd, eofObj);
    NvSciSyncObjFree(eofObj);
    NvSciBufObjFree(outBuf);
    NvMediaIJPDDestroy(ijpd);
    NvSciSyncCpuWaitContextFree(cpuWaitCtx);
    NvSciSyncModuleClose(syncModule);
    NvSciBufModuleClose(bufModule);
    return (nonzero > 0) ? 0 : 2;
}
