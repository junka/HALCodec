#include <cstdint>
#include <fstream>
#include <iostream>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "parse_cli.h"

#include "decoder.h"

int main(int argc, char *argv[]) {
    
    std::string inputFile;
    std::string outputFile;
    int gpuIndex;
    std::string format;
    CommandLineParser cli;
    cli.parse(argc, argv);

    auto dec = halcodec::Decoder::Create("vtbox");
    if (!dec) {
        std::cerr << "Fail to create decoder" << std::endl;
        return -1;
    }
    std::string input = cli.getInputFile();
    dec->Initialize(input);

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
        files.push_back(cli.getOutputFile() + '.' + cli.getFormat());
    }

    for (auto p : files) {
        std::cout << p << std::endl;
    }
    int fidx = 0;
    std::ofstream fpout(files[fidx++], std::ios::out|std::ios::binary);
    if (!fpout) {
        std::cerr << "unable to open output file" << std::endl;
    }
    int n_dec = 0;
    int total_frames = 0;
    do {
        n_dec = dec->FillinFrame();
        total_frames += n_dec;
        for (int i = 0; i < n_dec; i++) {
            int size;
            auto data = dec->GetFrame(&size);
            fpout.write(reinterpret_cast<char *>(data), size);
            if (dec->getName() == "nvjpeg" && fidx < files.size()) {
                fpout.close();
                fpout.open(files[fidx++], std::ios::out|std::ios::binary);
            }
            dec->ReleaseFrame(&data);
        }
    } while (n_dec > 0);

    fpout.close();
    dec->Finalize();

    std::cout << "Decode total frames: "<< total_frames << std::endl;

    return 0;
}
