#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "bmp_write.h"
#include "encoder.h"
#include "parse_cli.h"

#include "codec_config.h"
#include "frame.h"

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    std::string backend = cli.getBackend().empty() ? "nvenc"
                                                   : cli.getBackend();
    auto enc = halcodec::Encoder::Create(backend);
    if (!enc) {
        std::cerr << "Fail to create encoder backend: " << backend << std::endl;
        return -1;
    }
    std::cout << "encoder backend: " << enc->getName() << std::endl;
    std::string input = cli.getInputFile();

    std::regex pattern(R"((\d+)[xX](\d+))");
    std::smatch match;
    halcodec::CodecParams params;
    params.inputs.push_back(input);
    params.deviceIndex = cli.getGpuIndex();
    if (cli.getFormat() == "rgb" || cli.getFormat() == "rgbi") {
        params.inputFormat = halcodec::PixelFormat::RGB;
    } else if (cli.getFormat() == "bgr" || cli.getFormat() == "bgri"
               || cli.getFormat() == "bmp") {
        params.inputFormat = halcodec::PixelFormat::BGR;
    } else {
        params.inputFormat = halcodec::PixelFormat::I420; // yuv default
    }
    if (std::regex_search(input, match, pattern)) {
        params.width = std::stoi(match[1].str());
        params.height = std::stoi(match[2].str());
    }
    if (!enc->Initialize(params)) {
        std::cerr << "Fail to initialize encoder backend: " << backend << std::endl;
        return -1;
    }

    struct stat info;
    if (stat(input.c_str(), &info) != 0) {
        std::cout << "Cannot access " << input << std::endl;
        return -1;
    }
    std::vector<std::string> files;
    if (info.st_mode & S_IFDIR) {
        DIR *dir;
        struct dirent *ent;
        while (!input.empty() && input.back() == '/') {
            input.pop_back();
        }
        if ((dir = opendir(input.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    std::string inputFileName = input + '/' + ent->d_name;
                    size_t lastDot = inputFileName.find_last_of('.');
                    if (lastDot != std::string::npos) {
                        inputFileName = inputFileName.substr(0, lastDot);
                    }
                    files.emplace_back(inputFileName + "." + cli.getFormat());
                }
            }
            closedir(dir);
        }
    } else {
        files.push_back(cli.getOutputFile());
    }

    for (auto p : files) {
        std::cout << p << std::endl;
    }
    int fidx = 0;
    std::ofstream fpout(files[fidx++], std::ios::out|std::ios::binary);
    if (!fpout) {
        std::cerr << "unable to open output file" << std::endl;
        return -1;
    }

    int total_frames = 0;
    halcodec::CodecFrame frame;
    // Feed the encoder; each successful FillFrame may produce several
    // encapsulated packets which GetFrame drains one by one.
    while (enc->FillFrame(frame)) {
        while (enc->GetFrame(frame)) {
            total_frames++;
            printf("get frame size %zu, w %d, h %d\n",
                   frame.size, frame.width, frame.height);
            fpout.write(reinterpret_cast<const char *>(frame.data), frame.size);
            if (enc->getName() == "nvjpegenc" && fidx < files.size()) {
                fpout.close();
                fpout.open(files[fidx++], std::ios::out|std::ios::binary);
            }
            if (frame.release) {
                frame.release();
            }
        }
    }

    fpout.close();
    enc->Finalize();

    std::cout << "Encode total frames: "<< total_frames << std::endl;

    return 0;
}