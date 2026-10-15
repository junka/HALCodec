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
//
// Repeating -i transcodes several streams concurrently through one
// halcodec::Session: N independent decoders feed N independent encoders on the
// same backend pair, and the outputs land beside the inputs (or in -o, which
// then names a directory) named after each input. Zero-copy is a
// single-stream option: the -z plumbing (shared VADisplay/CUDA context across
// the pair) is verified for one stream and is rejected for many.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "parse_cli.h"
#include "file_io.h"
#include "output_paths.h"

#include "codec_config.h"
#include "codec_names.h"
#include "annexb.h"
#include "decoder.h"
#include "encode_overrides.h"
#include "encoder.h"
#include "frame.h"
#include "metrics.h"
#include "plugin_loader.h"
#include "session.h"

namespace {

const char* kDefaultDecoder = "nvdec";
const char* kDefaultEncoder = "nvenc";

// Per-stream encoder params: the CLI-derived template, plus the geometry that
// only the first decoded frame can supply. One per -i.
struct TranscodeStream {
    std::vector<uint8_t> raw;       // whole compressed input
    halcodec::CodecParams eparams;  // CLI template; geometry filled lazily
    bool encOpen = false;
    size_t encIdx = 0;
    uint64_t inFrames = 0;
    uint64_t outFrames = 0;
};

// The multi-stream transcode driver: N inputs through one mixed
// halcodec::Session (N decoders opened up front, N encoders opened lazily as
// each stream's first frame reveals its geometry). The single-stream path
// below keeps the verified zero-copy chain; this path is host-frames only.
static int RunMultiStream(CommandLineParser& cli) {
    const std::vector<std::string>& inputs = cli.getInputFiles();
    const size_t n = inputs.size();

    if (cli.getZeroCopy()) {
        std::cerr << "-z is a single-stream option: multi-stream transcode "
                     "runs on host frames" << std::endl;
        return -1;
    }

    std::string decBackend = cli.getBackend().empty() ? kDefaultDecoder
                                                      : cli.getBackend();
    std::string encBackend = cli.getEncoderBackend().empty()
                                 ? kDefaultEncoder
                                 : cli.getEncoderBackend();

    // The Session's fan-in drains on blocking GetFrame loops, so both sides
    // have to be async backends; a synchronous encoder would also stall the
    // per-stream drainer between packets.
    {
        auto dec = halcodec::Decoder::Create(decBackend);
        if (!dec) {
            std::cerr << "Fail to create decoder backend: " << decBackend
                      << std::endl;
            return -1;
        }
        if (!dec->isAsync()) {
            std::cerr << "multi-stream transcode needs async backends; "
                      << decBackend << " is synchronous" << std::endl;
            return -1;
        }
        auto enc = halcodec::Encoder::Create(encBackend);
        if (!enc) {
            std::cerr << "Fail to create encoder backend: " << encBackend
                      << std::endl;
            return -1;
        }
        if (!enc->isAsync()) {
            std::cerr << "multi-stream transcode needs async backends; "
                      << encBackend << " is synchronous" << std::endl;
            return -1;
        }
    }
    std::cout << "transcode: " << decBackend << " -> " << encBackend << " ("
              << n << " streams)" << std::endl;

    // One entry per -i: the input bytes, the encoder template from the CLI,
    // and the per-stream counters.
    std::vector<TranscodeStream> streams(n);
    std::vector<halcodec::CodecParams> dparams(n);
    for (size_t s = 0; s < n; s++) {
        if (!halcodec::ReadFile(inputs[s], &streams[s].raw)) {
            std::cerr << "Cannot read " << inputs[s] << std::endl;
            return -1;
        }
        // Feed mode: codec name, never the path (an inputs path would send
        // NVDEC into its internal demuxer and FillInput would be ignored).
        dparams[s].codec = cli.getCodec();
        if (dparams[s].codec.empty()) {
            dparams[s].codec = halcodec::CodecFromExtension(ExtOf(inputs[s]));
        }
        dparams[s].deviceIndex = cli.getGpuIndex();
        // Extradata for the backends that open on parameter sets (vtbox), as
        // the single-stream path does.
        if (dparams[s].codec == "h264" || dparams[s].codec == "hevc" ||
            dparams[s].codec.empty()) {
            halcodec::ExtractParameterSets(streams[s].raw.data(),
                                           streams[s].raw.size(),
                                           &dparams[s].extradata, nullptr,
                                           /*onlyWithSpsPps=*/true);
        }

        // Encoder template: the same CLI-derived settings the single-stream
        // path builds, minus geometry (per-stream, from the first frame).
        streams[s].eparams.codec = dparams[s].codec;
        streams[s].eparams.deviceIndex = cli.getGpuIndex();
        streams[s].eparams.inputFormat = halcodec::PixelFormat::NV12;
        if (cli.getFormat() != "yuv" && cli.getFormat() != "iyuv" &&
            cli.getFormat() != "i420") {
            halcodec::FormatToPixelFormat(cli.getFormat(),
                                          &streams[s].eparams.inputFormat);
        }
        // The p010 -> P016 remap is deliberate and stays: this app's encoders
        // read the 16-bit samples right-aligned, so a left-aligned P010 frame
        // must be declared as P016.
        if (cli.getFormat() == "p010") {
            streams[s].eparams.inputFormat = halcodec::PixelFormat::P016;
        }
        halcodec::ApplyEncodeOverrides(cli, streams[s].eparams.encode);
    }

    halcodec::Session session;
    if (session.open(halcodec::Session::Kind::Decode, decBackend, dparams) !=
        n) {
        std::cerr << "Failed to open " << n << " decode streams on "
                  << decBackend << std::endl;
        return -1;
    }

    // Outputs: beside each input, or in -o when it names a directory. The
    // extension follows each stream's codec, so a mixed batch of h264/hevc
    // inputs does not get one format stamped on all outputs.
    std::string outDir;
    if (!cli.getOutputFile().empty()) {
        outDir = TrimTrailingSlashes(cli.getOutputFile());
        if (!EnsureDir(outDir)) {
            return -1;
        }
    }
    // Buffers must outlive the streams that pubsetbuf into them (see the
    // note in hal_dec's multi-stream path for what happens otherwise).
    std::vector<std::vector<char>> bufs(n, std::vector<char>(kIoBufBytes));
    std::vector<std::ofstream> outs(n);
    std::set<std::string> claimed;
    for (size_t s = 0; s < n; s++) {
        std::string codec = streams[s].eparams.codec.empty()
                                ? "h264"
                                : streams[s].eparams.codec;
        std::string name = BasenameOf(inputs[s]);
        std::string path = outDir.empty()
                               ? StripExt(inputs[s])
                               : outDir + '/' + StripExt(name);
        path += "_transcoded" + halcodec::CodecStreamExtension(codec);
        // Two inputs can derive the same name (the same file listed twice);
        // stamp the stream index into the second one so the ofstreams do not
        // truncate each other's files mid-run.
        if (!claimed.insert(path).second) {
            path = StripExt(path) + "." + std::to_string(s) + ExtOf(path);
            claimed.insert(path);
        }
        outs[s] = OpenBuffered<std::ofstream>(
            path, std::ios::out | std::ios::binary, bufs[s]);
        if (!outs[s]) {
            std::cerr << "unable to open output file: " << path << std::endl;
            return -1;
        }
        std::cout << path << std::endl;
    }

    // Feed every decoder its whole stream, then signal EOF. Feeding everything
    // first lets the async backends pipeline hardware submissions, as the
    // single-stream path does.
    const size_t kChunk = 1 << 20;
    for (size_t s = 0; s < n; s++) {
        for (size_t off = 0; off < streams[s].raw.size(); off += kChunk) {
            size_t len = std::min(kChunk, streams[s].raw.size() - off);
            if (session.feedDecode(s, streams[s].raw.data() + off, len) < 0) {
                std::cerr << "decoder rejected input for stream " << s
                          << std::endl;
                return -1;
            }
        }
        session.signalDecodeEOF(s);
    }

    // Decode fan-in; each stream's encoder opens lazily on its first frame.
    // getAnyEncodeFrame must wait until every encoder is open (the fan-in
    // drainer is fixed to the encoders present at its first call), so the
    // encoded packets queue here until the decode side is exhausted.
    int encodersOpened = 0;
    halcodec::CodecFrame frame;
    size_t s = 0;
    while (session.getAnyDecodeFrame(frame, s)) {
        TranscodeStream& st = streams[s];
        st.inFrames++;
        if (!st.encOpen) {
            // Geometry comes from this stream's first decoded frame;
            // --width/--height, when given, override it (same rule as the
            // single-stream path).
            st.eparams.width =
                cli.getWidth() > 0 ? cli.getWidth() : frame.width;
            st.eparams.height =
                cli.getHeight() > 0 ? cli.getHeight() : frame.height;
            if (st.eparams.inputFormat == halcodec::PixelFormat::Unknown) {
                st.eparams.inputFormat = frame.format;
            }
            // One open call per stream appends exactly one encoder; its index
            // is the number opened so far minus one.
            if (session.open(halcodec::Session::Kind::Encode, encBackend,
                             {st.eparams}) != 1) {
                std::cerr << "Failed to open encoder for stream " << s
                          << std::endl;
                return -1;
            }
            st.encIdx = static_cast<size_t>(encodersOpened++);
            st.encOpen = true;
        }
        if (!session.feedEncode(st.encIdx, frame)) {
            std::cerr << "encoder rejected a frame on stream " << s
                      << std::endl;
            return -1;
        }
        // The encoder copied the host bytes already (same contract the
        // single-stream path relies on when it releases right after FillFrame).
        if (frame.release) {
            frame.release();
        }
    }

    // The decode side is done: every stream either produced frames (encoder
    // open) or produced none (no encoder to close). Signal EOS on the ones
    // that encode, then start the fan-in drain -- safe now that all encoders
    // exist.
    for (size_t i = 0; i < n; i++) {
        if (!streams[i].encOpen) {
            std::cout << "stream " << i << " (" << inputs[i]
                      << "): no frames decoded" << std::endl;
            continue;
        }
        halcodec::CodecFrame eos;
        eos.width = streams[i].eparams.width;
        eos.height = streams[i].eparams.height;
        eos.format = streams[i].eparams.inputFormat;
        session.feedEncode(streams[i].encIdx, eos);
        session.signalEncodeEOF(streams[i].encIdx);
    }
    halcodec::CodecFrame packet;
    size_t which = 0;
    while (session.getAnyEncodeFrame(packet, which)) {
        outs[which].write(reinterpret_cast<const char*>(packet.data),
                          packet.size);
        streams[which].outFrames++;
        if (packet.release) packet.release();
    }

    uint64_t totalIn = 0, totalOut = 0;
    for (size_t i = 0; i < n; i++) {
        std::cout << "stream " << i << " (" << inputs[i] << "): "
                  << streams[i].inFrames << " in, " << streams[i].outFrames
                  << " out" << std::endl;
        totalIn += streams[i].inFrames;
        totalOut += streams[i].outFrames;
    }
    std::cout << "Transcode frames: " << totalIn << " in, " << totalOut
              << " out" << std::endl;

    // Host frames only on this path, so no shared VADisplay/CUDA-context
    // teardown order to preserve: close() resets all backends in one go.
    session.close();
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    const std::string input = cli.getInputFile();
    if (input.empty()) {
        std::cerr << "missing input file (-i <file>)" << std::endl;
        return 2;
    }

    halcodec::LoadBackends();

    // More than one -i means concurrent transcode: one mixed Session, N
    // decoders -> N lazily-opened encoders. The single-stream path below
    // keeps the zero-copy chain.
    if (cli.getInputFiles().size() > 1) {
        return RunMultiStream(cli);
    }

    const std::string output = cli.getOutputFile().empty()
                                   ? std::string("out.h264")
                                   : cli.getOutputFile();

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
    if (!halcodec::ReadFile(input, &raw)) {
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

    // Extract SPS/PPS (or VPS/SPS/PPS for HEVC) from the input stream for backends
    // that need extradata (e.g. vtbox on macOS). This mirrors what hal_dec does.
    if (dparams.codec == "h264" || dparams.codec == "hevc" || dparams.codec.empty()) {
        halcodec::ExtractParameterSets(raw.data(), raw.size(), &dparams.extradata,
                                       nullptr, /*onlyWithSpsPps=*/true);
    }

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
    // --format names the layout the encoder should accept. NV12 is the default
    // here and -f yuv (the CLI default) must keep meaning it: the decoders hand
    // NV12 to the encoder, so unlike hal_enc -- where the same sentinel selects
    // I420 for raw input -- the yuv spellings are left at NV12.
    eparams.inputFormat = halcodec::PixelFormat::NV12;
    if (cli.getFormat() != "yuv" && cli.getFormat() != "iyuv" &&
        cli.getFormat() != "i420") {
        halcodec::FormatToPixelFormat(cli.getFormat(), &eparams.inputFormat);
    }
    // The p010 -> P016 remap is deliberate and stays: this app's encoders read
    // the 16-bit samples right-aligned, so a left-aligned P010 frame must be
    // declared as P016.
    if (cli.getFormat() == "p010") {
        eparams.inputFormat = halcodec::PixelFormat::P016;
    }
    halcodec::ApplyEncodeOverrides(cli, eparams.encode);

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
            // Geometry comes from the first decoded frame (the decoder parsed
            // it from the stream headers). --width/--height, when given,
            // override that — useful when the decoder reports 16-aligned coded
            // size but the encoder should target the display size, or to force
            // a geometry the stream header got wrong. Note a mismatch with the
            // actual frame bytes is backend-defined (host path may misread;
            // zero-copy device frames will likely reject the import).
            eparams.width = cli.getWidth() > 0 ? cli.getWidth() : f.width;
            eparams.height = cli.getHeight() > 0 ? cli.getHeight() : f.height;
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
    // Periodic metrics: print a snapshot every metricsInterval ms (0 disables).
    // The snapshot is read-only across the registry; safe to call mid-stream.
    const int metricsMs = cli.getMetricsInterval();
    auto nextMetrics = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(metricsMs > 0 ? metricsMs : 60000);
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
        // Periodic metrics snapshot.
        if (metricsMs > 0 &&
            std::chrono::steady_clock::now() >= nextMetrics) {
            halcodec::printStats(std::cout);
            nextMetrics = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(metricsMs);
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
    // Final metrics summary: per-stream fps/bitrate/locality + GPU util. The
    // streams are still registered here (Finalize is below), so this captures
    // the final cumulative counters before teardown.
    std::cout << "--- metrics summary ---\n";
    halcodec::printStats(std::cout);
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
