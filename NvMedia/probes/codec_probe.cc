// Minimal NvMedia codec-support probe: calls NvMediaParserCreate for every
// NvMediaVideoCodec and reports which the runtime accepts vs rejects with
// "Codec Not supported". Used to compare the nv118 system libnvmedia tree
// against the /opt/gwm/.../drive-linux SDK lib-target without touching the
// running framework.
//
// Build (cross, against the thor SDK headers):
//   aarch64-linux-gnu-g++ -static-libstdc++ -static-libgcc \
//       -I/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/include/nvmedia_6x \
//       codec_probe.cc -o codec_probe \
//       -L/opt/gwm/nv_sdk/thoru/thoru-aarch64/drive-linux/lib-target \
//       -lnvmedia_ide_parser -ldl
// Run on nv118:
//   ./codec_probe                          # against system libs
//   LD_LIBRARY_PATH=<sdk-lib-target> ./codec_probe   # against SDK libs
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "nvmedia_parser.h"
#include "nvmedia_common_encode_decode.h"

namespace {
struct Probe {
    NvMediaVideoCodec codec;
    const char* name;
};
}  // namespace

int main() {
    std::vector<Probe> probes = {
        {NVMEDIA_VIDEO_CODEC_H264,          "h264"},
        {NVMEDIA_VIDEO_CODEC_HEVC,          "hevc"},
        {NVMEDIA_VIDEO_CODEC_VC1,           "vc1"},
        {NVMEDIA_VIDEO_CODEC_VC1_ADVANCED,  "vc1-adv"},
        {NVMEDIA_VIDEO_CODEC_MPEG1,         "mpeg1"},
        {NVMEDIA_VIDEO_CODEC_MPEG2,         "mpeg2"},
        {NVMEDIA_VIDEO_CODEC_MPEG4,         "mpeg4"},
        {NVMEDIA_VIDEO_CODEC_MJPEG,         "jpeg"},
        {NVMEDIA_VIDEO_CODEC_VP8,           "vp8"},
        {NVMEDIA_VIDEO_CODEC_VP9,           "vp9"},
        {NVMEDIA_VIDEO_CODEC_AV1,           "av1"},
    };

    NvMediaParserClientCb cb;
    std::memset(&cb, 0, sizeof(cb));

    std::cout << "NvMediaParserCreate probe (" << probes.size()
              << " codecs):" << std::endl;
    for (const auto& p : probes) {
        NvMediaParserParams params;
        std::memset(&params, 0, sizeof(params));
        params.pClient = &cb;
        params.eCodec = p.codec;
        NvMediaParser* h = NvMediaParserCreate(&params);
        std::cout << "  " << p.name << ": "
                  << (h ? "OK" : "REJECTED (NULL)")
                  << std::endl;
        if (h) {
            NvMediaParserDestroy(h);
        }
    }
    return 0;
}
