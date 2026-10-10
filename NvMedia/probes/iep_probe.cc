// Minimal NvMedia IEP (encoder) create probe: builds a reconciled NV12
// NvSciBufAttrList and calls NvMediaIEPCreate for H264/HEVC/VP9/AV1, reporting
// which the runtime accepts. Mirrors nvmediaencoder.cc's FillNV12SurfaceAttrs +
// reconcile + create, but uses each codec's real init-params struct.
//
// Build (cross, against thor SDK):
//   aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include/nvmedia_6x \
//       iep_probe.cc -o /tmp/iep_probe \
//       -Wl,-rpath-link,/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -L/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -lnvmedia_iep_sci -lnvvideo -lnvscibuf -lnvscisync \
//       -lnvrm_surface -lnvos -lnvsocsys -ldl
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "nvscibuf.h"
#include "nvmedia_iep.h"
#include "nvmedia_common_encode.h"

namespace {
constexpr uint32_t kSurfaceAlign = 256;

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

int main() {
    NvSciBufModule mod = nullptr;
    if (NvSciBufModuleOpen(&mod) != NvSciError_Success || !mod) {
        std::cerr << "NvSciBufModuleOpen failed" << std::endl;
        return 1;
    }

    const uint32_t w = 320, h = 240;
    NvSciBufAttrList attrList = nullptr;
    if (NvSciBufAttrListCreate(mod, &attrList) != NvSciError_Success ||
        !attrList) {
        std::cerr << "NvSciBufAttrListCreate failed" << std::endl;
        return 1;
    }
    if (!FillNV12SurfaceAttrs(attrList, w, h)) {
        std::cerr << "FillNV12SurfaceAttrs failed" << std::endl;
        return 1;
    }
    NvSciBufAttrList attrArray[1] = {attrList};
    NvSciBufAttrList reconciled = nullptr, conflict = nullptr;
    if (NvSciBufAttrListReconcile(attrArray, 1, &reconciled, &conflict) !=
        NvSciError_Success) {
        std::cerr << "NvSciBufAttrListReconcile failed" << std::endl;
        return 1;
    }

    struct Probe {
        NvMediaIEPType type;
        const char* name;
    };
    std::vector<Probe> probes = {
        {NVMEDIA_IMAGE_ENCODE_H264, "h264"},
        {NVMEDIA_IMAGE_ENCODE_HEVC, "hevc"},
        {NVMEDIA_IMAGE_ENCODE_VP9,  "vp9"},
        {NVMEDIA_IMAGE_ENCODE_AV1,  "av1"},
    };

    std::cout << "NvMediaIEPCreate probe (" << probes.size()
              << " codecs, " << w << "x" << h << "):" << std::endl;
    for (const auto& p : probes) {
        // Each codec's init-params struct: zeroed, with the common leading
        // geometry/frame-rate fields set. Only whether NvMediaIEPCreate accepts
        // the IEPType matters for this probe.
        NvMediaIEP* enc = nullptr;
        if (p.type == NVMEDIA_IMAGE_ENCODE_H264) {
            NvMediaEncodeInitializeParamsH264 ip;
            std::memset(&ip, 0, sizeof(ip));
            ip.encodeWidth = w;
            ip.encodeHeight = h;
            ip.frameRateNum = 30;
            ip.frameRateDen = 1;
            enc = NvMediaIEPCreate(p.type, &ip, reconciled, 5,
                                   NVMEDIA_ENCODER_INSTANCE_0);
        } else if (p.type == NVMEDIA_IMAGE_ENCODE_HEVC) {
            NvMediaEncodeInitializeParamsH265 ip;
            std::memset(&ip, 0, sizeof(ip));
            ip.encodeWidth = w;
            ip.encodeHeight = h;
            ip.frameRateNum = 30;
            ip.frameRateDen = 1;
            enc = NvMediaIEPCreate(p.type, &ip, reconciled, 5,
                                   NVMEDIA_ENCODER_INSTANCE_0);
        } else if (p.type == NVMEDIA_IMAGE_ENCODE_VP9) {
            NvMediaEncodeInitializeParamsVP9 ip;
            std::memset(&ip, 0, sizeof(ip));
            ip.encodeWidth = w;
            ip.encodeHeight = h;
            ip.frameRateNum = 30;
            ip.frameRateDen = 1;
            enc = NvMediaIEPCreate(p.type, &ip, reconciled, 5,
                                   NVMEDIA_ENCODER_INSTANCE_0);
        } else {  // AV1
            NvMediaEncodeInitializeParamsAV1 ip;
            std::memset(&ip, 0, sizeof(ip));
            ip.encodeWidth = w;
            ip.encodeHeight = h;
            ip.frameRateNum = 30;
            ip.frameRateDen = 1;
            enc = NvMediaIEPCreate(p.type, &ip, reconciled, 5,
                                   NVMEDIA_ENCODER_INSTANCE_0);
        }
        std::cout << "  " << p.name << ": "
                  << (enc ? "OK" : "FAILED (NULL)")
                  << std::endl;
        if (enc) {
            NvMediaIEPDestroy(enc);
        }
    }

    NvSciBufAttrListFree(reconciled);
    if (conflict) NvSciBufAttrListFree(conflict);
    NvSciBufAttrListFree(attrList);
    NvSciBufModuleClose(mod);
    return 0;
}
