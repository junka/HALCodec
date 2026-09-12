#include <cstdint>
#include <fstream>
#include <iostream>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parse_cli.h"
#include "bmp_write.h"

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

int main(int argc, char *argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    auto dec = halcodec::Decoder::Create("nvjpeg");
    if (!dec) {
        std::cerr << "Fail to create decoder" << std::endl;
        return -1;
    }
    std::string input = cli.getInputFile();

    halcodec::CodecParams params;
    params.inputs.push_back(input);
    params.deviceIndex = cli.getGpuIndex();
    if (cli.getFormat() == "rgb" || cli.getFormat() == "rgbi") {
        params.outputFormat = halcodec::PixelFormat::RGB;
    } else if (cli.getFormat() == "bgr" || cli.getFormat() == "bgri") {
        params.outputFormat = halcodec::PixelFormat::BGR;
    } else if (cli.getFormat() == "y") {
        params.outputFormat = halcodec::PixelFormat::GRAY;
    }
    if (!dec->Initialize(params)) {
        std::cerr << "Fail to initialize decoder" << std::endl;
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
        std::string dirPath = input;
        while (!dirPath.empty() && dirPath.back() == '/') {
            dirPath.pop_back();
        }
        if ((dir = opendir(dirPath.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    std::string inputFileName = dirPath + '/' + ent->d_name;
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

    int n_dec = 0;
    int total_frames = 0;
    do {
        n_dec = dec->FillinFrame();
        total_frames += n_dec;
        for (int i = 0; i < n_dec; i++) {
            halcodec::CodecFrame frame;
            if (!dec->GetFrame(frame)) {
                break;
            }
            if (cli.getFormat() == "y" || cli.getFormat() == "bgr" || cli.getFormat() == "rgb"
                 || cli.getFormat() == "rgbi" || cli.getFormat() == "bgri") {
                BMPWriter writer(files[fidx++], cli.getFormat());
                writer.writeBMP(frame.data, frame.width, frame.height,
                                cli.getFormat() == "y" ? 1 : 3);
            } else {
                fpout.write(reinterpret_cast<const char*>(frame.data), frame.size);
                if (dec->getName() == "nvjpeg" && fidx < files.size()) {
                    fpout.close();
                    fpout.open(files[fidx++], std::ios::out|std::ios::binary);
                }
            }
            if (frame.release) {
                frame.release();
            }
        }
    } while (n_dec > 0);

    if (dec->getName() != "nvjpeg") {
        fpout.close();
    }
    dec->Finalize();

    std::cout << "Decode total frames: " << total_frames << std::endl;

    return 0;
}