#ifndef NVENC_LAYERS_NVIDIA_CAPS_H
#define NVENC_LAYERS_NVIDIA_CAPS_H

#include <cuviddec.h>

namespace halcodec {
namespace nvenc {

// Query whether this GPU's NVDEC engine supports decoding the given codec at
// all (codec-level only: 8-bit, 4:2:0). Mirrors the cuvidGetDecoderCaps probe
// used by showDecoderCapability(), exposed as a programmable check so a
// backend can refuse Initialize() up front instead of failing deep inside
// NvDecoder construction.
//
// `deviceIndex` selects the CUDA device to query. Returns true if the driver
// reports bIsSupported for the codec. Creates and destroys a transient CUDA
// context of its own, so the caller need not have one current.
bool nvdecSupportsCodec(int deviceIndex, cudaVideoCodec codec);

// Human-readable name for a cudaVideoCodec, for error messages.
const char* nvdecCodecName(cudaVideoCodec codec);

} // namespace nvenc
} // namespace halcodec

#endif // NVENC_LAYERS_NVIDIA_CAPS_H
