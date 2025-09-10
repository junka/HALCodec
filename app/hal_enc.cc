#include <cstdint>
#include <fstream>
#include <iostream>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "bmp_write.h"
#include "encoder.h"
#include "parse_cli.h"

int main(int argc, char *argv[]) {
    std::string inputFile;
    std::string outputFile;
    int gpuIndex;
    std::string format;
    CommandLineParser cli;
    cli.parse(argc, argv);

    auto enc = halcodec::Encoder::Create("nvjpeg");
    if (!enc) {
        std::cerr << "Fail to create encoder" << std::endl;
        return -1;
    }
    std::string input = cli.getInputFile();

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
    }

    enc->Initialize(input, cli.getInputFormat());
    int n_enc = 0;
    int total_frames = 0;
    do {
        n_enc = enc->FillData();
        total_frames += n_enc;
        for (int i = 0; i < n_enc; i++) {
            int size;
            int width;
            int height;
            int n_chan;
            auto data = enc->GetFrame(&size, &height, &width, &n_chan);
            printf("get frame size %d, w %d, h %d\n", size, width, height);
            fpout.write(reinterpret_cast<char *>(data), size);
            if (enc->getName() == "nvjpeg" && fidx < files.size()) {
                fpout.close();
                fpout.open(files[fidx++], std::ios::out|std::ios::binary);
            }
            enc->ReleaseFrame(&data);
        }
    } while (n_enc > 0);

    fpout.close();
    enc->Finalize();

    std::cout << "Encode total frames: "<< total_frames << std::endl;

    return 0;
}