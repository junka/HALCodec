#ifndef PARSE_CLI_H
#define PARSE_CLI_H

#include <boost/program_options.hpp>
#include <iostream>
#include <string>

namespace po = boost::program_options;

class CommandLineParser {
public:
    CommandLineParser()
        : gpuIndex_(0), format_("yuv") {}

    void parse(int argc, char* argv[]) {
        try {
            // Define the options
            po::options_description desc("Allowed options");
            desc.add_options()
                ("help,h", "Show help message")
                ("input,i", po::value<std::vector<std::string>>(&inputFiles_),
                 "Input file (may be repeated for multi-stream apps)")
                ("output,o", po::value<std::string>(&outputFile_), "Output file (optional, defaults to input file name with format extension)")
                ("backend,b", po::value<std::string>(&backend_), "Backend name (vtbox/nvdec/nvjpeg/nvenc/amfdec/qsvdec/nvmedia); defaults per application")
                ("encoder,B", po::value<std::string>(&encoderBackend_), "Encoder backend for apps that chain two backends (e.g. hal_transcode -b nvdec -B nvenc); defaults per application")
                ("gpu", po::value<int>(&gpuIndex_)->default_value(0), "GPU index")
                ("format,f", po::value<std::string>(&format_)->default_value("yuv"),
                 "Format: yuv(i420)/nv12/yuv444/y(rgb gray)/bgr/bgri/rgbi/bgra/rgba/bmp")
                ("codec,c", po::value<std::string>(&codec_)->default_value("h264"))
                ("colorspace,c", po::value<std::string>(&cs_)->default_value("420"), "Color sapce: 420/444/410")
                ("encode-config", po::value<std::string>(&encodeConfigFile_), "JSON encoder config file (overrides defaults; CLI items below override this)")
                ("preset", po::value<std::string>(&preset_), "Encoder preset (NVENC p1..p7; QSV fast/balanced/slow/best)")
                ("tuning", po::value<std::string>(&tuning_), "NVENC tuning info (hq/ll/ull/low_latency_p)")
                ("rc", po::value<std::string>(&rateControl_), "Rate control: cbr/vbr/cqp/icq")
                ("bitrate", po::value<int>(&bitrateKbps_), "Target bitrate in kbps")
                ("maxbitrate", po::value<int>(&maxBitrateKbps_), "VBR max bitrate in kbps")
                ("qp", po::value<int>(&qp_), "Constant QP (CQP mode); overrides bitrate when >=0")
                ("gop", po::value<int>(&gopLength_), "GOP / IDR length")
                ("bframes", po::value<int>(&numBFrames_), "Number of B-frames (0 for low delay)")
                ("fps", po::value<int>(&fps_), "Frame rate (numerator; denominator=1)")
                ("profile", po::value<std::string>(&profile_), "Codec profile (baseline/main/high)")
                ("level", po::value<std::string>(&level_), "Codec level (auto/4.0/4.1...)")
                ("lowdelay", po::bool_switch(&lowDelay_), "Low-delay mode (0 B-frames, short GOP)")
                ("zero-copy,z", po::bool_switch(&zeroCopy_), "Keep decoded frames in device memory (NvMedia NvSciBuf / NVDEC CUDA) instead of copying to host; the output writer downloads on demand. Backend support is opt-in.")
                ("metrics-interval", po::value<int>(&metricsInterval_)->default_value(1000),
                 "Periodic metrics print interval in ms (0 = disable periodic; final summary always prints). Reports per-stream fps/bitrate/locality and whole-machine GPU utilization.")
                // hal_camera-only flags. Additive with sentinel defaults so the
                // other apps (which never read these getters) are unaffected.
                ("num-cameras", po::value<int>(&numCameras_)->default_value(1),
                 "hal_camera: number of SIPL sensor pipelines to start (multi-camera fan-in).")
                ("sensor-index", po::value<int>(&sensorIndex_)->default_value(0),
                 "hal_camera: starting sensor index for the first camera; subsequent cameras use index+1, +2, ...")
                ("duration-frames", po::value<int>(&durationFrames_)->default_value(0),
                 "hal_camera: stop after N frames per camera (0 = run until Ctrl-C).")
                ("sipl-platform", po::value<std::string>(&siplPlatform_),
                 "hal_camera: SIPL platform config name (empty = first from the built-in DB).")
                ("sipl-db", po::value<std::string>(&siplDb_),
                 "hal_camera: path to a SIPL platform JSON DB (empty = built-in ParseDatabase).")
                // hal_camera_v4l2-only flags. V4L2 USB/UVC camera -> QSV encode on x86.
                ("device", po::value<std::string>(&v4l2Device_)->default_value("/dev/video0"),
                 "Camera device (v4l2: device node e.g. /dev/video0; av: camera name substring filter).")
                ("input-format", po::value<std::string>(&v4l2InputFormat_)->default_value("yuyv"),
                 "hal_camera_v4l2: V4L2 pixel format to negotiate (yuyv|mjpg).")
                ("capture-width", po::value<int>(&v4l2Width_)->default_value(640),
                 "hal_camera_v4l2: capture width (must be supported by the device).")
                ("capture-height", po::value<int>(&v4l2Height_)->default_value(480),
                 "hal_camera_v4l2: capture height (must be supported by the device).")
                ("capture-fps", po::value<int>(&v4l2Fps_)->default_value(30),
                 "hal_camera_v4l2: requested capture frame rate.")
                // hal_cam-only flag. Unified camera app; selects which capture
                // source to drive (sipl|v4l2|av). Empty = platform-native
                // default (aarch64+DRIVE: sipl, Linux: v4l2, macOS: av).
                ("source", po::value<std::string>(&source_),
                 "hal_cam: camera source (sipl|v4l2|av); defaults to the platform-native source.");

            // Parse the command line
            po::variables_map vm;
            po::store(po::parse_command_line(argc, argv, desc), vm);

            // Display help if needed
            if (vm.count("help")) {
                std::cout << desc << "\n";
                exit(0);
            }
            po::notify(vm);

            // If output is not provided, derive it from input
            if (!vm.count("output") && !inputFiles_.empty()) {
                std::string inputFileName = inputFiles_.front();
                size_t lastDot = inputFileName.find_last_of('.');
                if (lastDot != std::string::npos) {
                    input_format_ = inputFileName.substr(lastDot+1);
                    inputFileName = inputFileName.substr(0, lastDot);
                }
                outputFile_ = inputFileName + "." + format_;
            }
        } catch (const po::error& e) {
            std::cerr << "Error: " << e.what() << "\n";
            exit(1);
        }
    }

    const std::string& getInputFile() const {
        // Single-file apps read the first -i; multi-stream apps use getInputFiles().
        static const std::string empty;
        return inputFiles_.empty() ? empty : inputFiles_.front();
    }
    const std::vector<std::string>& getInputFiles() const { return inputFiles_; }
    const std::string& getOutputFile() const { return outputFile_; }
    const std::string& getBackend() const { return backend_; }
    const std::string& getEncoderBackend() const { return encoderBackend_; }
    int getGpuIndex() const { return gpuIndex_; }
    const std::string& getFormat() const { return format_; }
    const std::string& getInputFormat() const { return input_format_; }
    const std::string& getColorSpace() const { return cs_; }
    const std::string& getCodec() const { return codec_; }

    // Encoder config: returns the JSON file path (may be empty) plus the
    // single-item CLI overrides. The caller (hal_enc) is responsible for
    // loading the JSON first, then applying these overrides on top.
    const std::string& getEncodeConfigFile() const { return encodeConfigFile_; }
    const std::string& getPreset() const { return preset_; }
    const std::string& getTuning() const { return tuning_; }
    const std::string& getRateControl() const { return rateControl_; }
    int getBitrateKbps() const { return bitrateKbps_; }
    int getMaxBitrateKbps() const { return maxBitrateKbps_; }
    int getQp() const { return qp_; }
    int getGopLength() const { return gopLength_; }
    int getNumBFrames() const { return numBFrames_; }
    int getFps() const { return fps_; }
    const std::string& getProfile() const { return profile_; }
    const std::string& getLevel() const { return level_; }
    bool getLowDelay() const { return lowDelay_; }
    bool getZeroCopy() const { return zeroCopy_; }
    // Metrics print interval in ms (0 = no periodic printing; the final
    // summary always prints). Used by hal_transcode / hal_session.
    int getMetricsInterval() const { return metricsInterval_; }

    // hal_camera-only options. Other apps ignore these.
    int getNumCameras() const { return numCameras_; }
    int getSensorIndex() const { return sensorIndex_; }
    int getDurationFrames() const { return durationFrames_; }
    const std::string& getSiplPlatform() const { return siplPlatform_; }
    const std::string& getSiplDb() const { return siplDb_; }

    // hal_camera_v4l2-only options.
    const std::string& getV4l2Device() const { return v4l2Device_; }
    const std::string& getV4l2InputFormat() const { return v4l2InputFormat_; }
    int getV4l2Width() const { return v4l2Width_; }
    int getV4l2Height() const { return v4l2Height_; }
    int getV4l2Fps() const { return v4l2Fps_; }

    // hal_cam-only.
    const std::string& getSource() const { return source_; }

private:
    std::vector<std::string> inputFiles_;
    std::string outputFile_;
    std::string backend_;
    std::string encoderBackend_;
    int gpuIndex_;
    std::string format_;
    std::string input_format_;
    std::string cs_;
    std::string codec_;

    // Encoder tuning overrides (sentinel defaults mean "not set on CLI").
    std::string encodeConfigFile_;
    std::string preset_;
    std::string tuning_;
    std::string rateControl_;
    int bitrateKbps_ = -1;
    int maxBitrateKbps_ = -1;
    int qp_ = -1;
    int gopLength_ = -1;
    int numBFrames_ = -1;
    int fps_ = -1;
    std::string profile_;
    std::string level_;
    bool lowDelay_ = false;
    bool zeroCopy_ = false;
    int metricsInterval_ = 1000;  // ms; 0 disables periodic metrics

    // hal_camera-only (sentinel defaults; other apps never read these).
    int numCameras_ = 1;
    int sensorIndex_ = 0;
    int durationFrames_ = 0;
    std::string siplPlatform_;
    std::string siplDb_;

    // hal_camera_v4l2-only.
    std::string v4l2Device_ = "/dev/video0";
    std::string v4l2InputFormat_ = "yuyv";
    int v4l2Width_ = 640;
    int v4l2Height_ = 480;
    int v4l2Fps_ = 30;

    // hal_cam-only.
    std::string source_;
};

#endif // PARSE_CLI_H
