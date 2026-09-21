// Transcode demo: decode an elementary stream and re-encode it through the HAL.
//
// This is the app that exercises the decode->encode chain: no other app feeds
// a decoder's output into an encoder, so it is the only place the zero-copy
// paths can be verified end to end.
//
//   hal_transcode -b nvdec -B nvenc -c h264 -i in.h264 -o out.h264        (host)
//   hal_transcode -b nvdec -B nvenc -c h264 -i in.h264 -o out.h264 -z     (device)
//
// Without -z the decoder hands out host frames which the encoder uploads, as
// before. With -z the decoder keeps each frame in device memory and the frames
// are fed straight to the encoder, which copies device-to-device: the pixels
// never touch host memory. Note that the two backends must share one CUDA
// context for that to be possible, which they do because CUDAContext now
// retains the device's primary context rather than creating a private one.
//
// The encoder's geometry is taken from the first decoded frame, so width and
// height need not be given on the command line; -f selects the encoder's input
// pixel format when the decoder's is not the default NV12.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "parse_cli.h"

#include "codec_config.h"
#include "decoder.h"
#include "encode_config.h"
#include "encoder.h"
#include "frame.h"
#include "plugin_loader.h"

namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>* buf) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    buf->resize(static_cast<size_t>(size));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(buf->data()), size));
}

const char* kDefaultDecoder = "nvdec";
const char* kDefaultEncoder = "nvenc";

} // namespace

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    const std::string input = cli.getInputFile();
    if (input.empty()) {
        std::cerr << "missing input file (-i <file>)" << std::endl;
        return 2;
    }
    const std::string output = cli.getOutputFile().empty()
                                   ? std::string("out.h264")
                                   : cli.getOutputFile();

    halcodec::LoadBackends();

    std::string decBackend = cli.getBackend().empty() ? kDefaultDecoder
                                                      : cli.getBackend();
    std::string encBackend = cli.getEncoderBackend().empty()
                                 ? kDefaultEncoder
                                 : cli.getEncoderBackend();

    auto dec = halcodec::Decoder::Create(decBackend);
    if (!dec) {
        std::cerr << "Fail to create decoder backend: " << decBackend << std::endl;
        return -1;
    }
    auto enc = halcodec::Encoder::Create(encBackend);
    if (!enc) {
        std::cerr << "Fail to create encoder backend: " << encBackend << std::endl;
        return -1;
    }
    std::cout << "transcode: " << dec->getName() << " -> " << enc->getName()
              << (cli.getZeroCopy() ? " (zero-copy)" : "") << std::endl;

    std::vector<uint8_t> raw;
    if (!ReadFile(input, &raw)) {
        std::cerr << "Cannot read " << input << std::endl;
        return -1;
    }

    // The decoder is fed the compressed bytes ourselves, so it must open in
    // feed mode: pass the codec name, not the file path (passing inputs would
    // make NVDEC open its internal demuxer and ignore FillInput).
    halcodec::CodecParams dparams;
    dparams.codec = cli.getCodec();
    dparams.deviceIndex = cli.getGpuIndex();
    dparams.zeroCopy = cli.getZeroCopy();
    if (!dec->Initialize(dparams)) {
        std::cerr << "Fail to initialize decoder backend" << std::endl;
        return -1;
    }

    // Encoder tuning comes from the same JSON config + CLI overrides as
    // hal_enc, so presets and rate control can be reused verbatim.
    halcodec::CodecParams eparams;
    eparams.codec = cli.getCodec();
    eparams.deviceIndex = cli.getGpuIndex();
    // Mirror the decoder's zero-copy selection onto the encoder: a OneVPLSurface
    // decoded frame can only be fed directly (no host memcpy) to an encoder
    // initialized for video-memory input. When -z is off the encoder takes the
    // host path (IN_SYSTEM_MEMORY).
    eparams.zeroCopy = cli.getZeroCopy();
    if (cli.getFormat() == "yuv444") {
        eparams.inputFormat = halcodec::PixelFormat::YUV444P;
    } else if (cli.getFormat() == "p010" || cli.getFormat() == "p016") {
        eparams.inputFormat = halcodec::PixelFormat::P016;
    } else if (cli.getFormat() == "nv16") {
        eparams.inputFormat = halcodec::PixelFormat::NV16;
    } else {
        eparams.inputFormat = halcodec::PixelFormat::NV12;
    }
    if (!cli.getEncodeConfigFile().empty()) {
        halcodec::LoadEncodeConfig(cli.getEncodeConfigFile(), eparams.encode);
    }
    if (!cli.getPreset().empty())      eparams.encode.preset = cli.getPreset();
    if (!cli.getTuning().empty())      eparams.encode.tuningInfo = cli.getTuning();
    if (!cli.getRateControl().empty()) eparams.encode.rateControl = cli.getRateControl();
    if (cli.getBitrateKbps() >= 0)     eparams.encode.bitrateKbps = cli.getBitrateKbps();
    if (cli.getMaxBitrateKbps() >= 0)  eparams.encode.maxBitrateKbps = cli.getMaxBitrateKbps();
    if (cli.getQp() >= 0)              eparams.encode.qp = cli.getQp();
    if (cli.getGopLength() >= 0)       eparams.encode.gopLength = cli.getGopLength();
    if (cli.getNumBFrames() >= 0)      eparams.encode.numBFrames = cli.getNumBFrames();
    if (cli.getFps() > 0)              eparams.encode.frameRateNum = cli.getFps();
    if (!cli.getProfile().empty())     eparams.encode.profile = cli.getProfile();
    if (!cli.getLevel().empty())       eparams.encode.level = cli.getLevel();
    if (cli.getLowDelay())             eparams.encode.lowDelay = true;

    std::ofstream fpout(output, std::ios::binary);
    if (!fpout) {
        std::cerr << "unable to open output file: " << output << std::endl;
        return -1;
    }

    int inFrames = 0;
    int outFrames = 0;
    int hostFrames = 0;
    int deviceFrames = 0;
    bool encReady = false;

    auto drain = [&]() {
        halcodec::CodecFrame out;
        while (enc->GetFrame(out)) {
            outFrames++;
            fpout.write(reinterpret_cast<const char*>(out.data), out.size);
            if (out.release) out.release();
        }
    };

    // One decoded frame -> encoder. The encoder is initialized lazily from the
    // first frame because its geometry is only known once the decoder has
    // parsed the stream headers.
    auto feedEncoder = [&](halcodec::CodecFrame& f) -> bool {
        if (!encReady) {
            eparams.width = f.width;
            eparams.height = f.height;
            if (eparams.inputFormat == halcodec::PixelFormat::Unknown) {
                eparams.inputFormat = f.format;
            }
            // Propagate the decoder's shared device handle (QSV: VADisplay) so
            // the encoder can SetHandle it before Init — required for the
            // zero-copy ImportFrameSurface to match the imported surface's
            // vaDisplay.
            if (eparams.zeroCopy &&
                f.locality == halcodec::FrameLocality::OneVPLSurface) {
                eparams.sharedDeviceHandle = f.device.vplVaDisplay;
            }
            if (!enc->Initialize(eparams)) {
                std::cerr << "Fail to initialize encoder backend" << std::endl;
                return false;
            }
            encReady = true;
        }
        return enc->FillFrame(f);
    };

    // Feed the whole compressed stream, then drain in completion order. Feeding
    // everything first lets the async backends pipeline hardware submissions.
    const size_t kChunk = 1 << 20;
    for (size_t off = 0; off < raw.size(); off += kChunk) {
        size_t n = std::min(kChunk, raw.size() - off);
        if (dec->FillInput(raw.data() + off, n) < 0) {
            std::cerr << "decoder rejected input" << std::endl;
            return -1;
        }
    }
    dec->SignalInputComplete();

    halcodec::CodecFrame frame;
    bool failed = false;
    while (!failed && dec->GetFrame(frame)) {
        inFrames++;
        if (frame.locality == halcodec::FrameLocality::Host) {
            hostFrames++;
        } else {
            deviceFrames++;
        }
        if (!feedEncoder(frame)) {
            std::cerr << "encoder rejected frame " << inFrames << std::endl;
            failed = true;
        }
        // Hand the frame back as soon as it is queued. For host frames the
        // encoder copied the bytes already; for zero-copy device frames the
        // decoder's pool holds its own reference, so the buffer survives until
        // the worker's copy releases it too. Not releasing would starve the
        // decoder's fixed frame pool and stall the stream after a few frames.
        if (frame.release) {
            frame.release();
            frame.release = nullptr;
        }
        if (!enc->isAsync()) {
            drain();
        }
    }
    if (failed) {
        return -1;
    }

    halcodec::CodecFrame eos;
    eos.width = eparams.width;
    eos.height = eparams.height;
    eos.format = eparams.inputFormat;
    enc->FillFrame(eos);
    if (enc->isAsync()) {
        enc->SignalInputComplete();
    }
    drain();

    fpout.close();
    // Teardown order matters for zero-copy: the encoder shares the decoder's
    // VADisplay (SetHandle'd before encodeInit). The encoder must be finalized
    // first so its VA buffers/surfaces are freed while the VADisplay is still
    // live; finalizing the decoder first would vaTerminate the shared display
    // out from under the encoder's teardown and crash in vaDestroyBuffer.
    enc->Finalize();
    dec->Finalize();

    // Report the locality split: a zero-copy run that silently fell back to
    // host frames would produce byte-identical output and look like a pass.
    std::cout << "Transcode frames: " << inFrames << " in, " << outFrames
              << " out (" << hostFrames << " host, " << deviceFrames
              << " device)" << std::endl;
    return 0;
}
