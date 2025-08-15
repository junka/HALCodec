#include <cstdint>
#include <fstream>
#include <iostream>

#include "parse_cli.h"

#include "decoder.h"

int main(int argc, char *argv[]) {
    
    std::string inputFile;
    std::string outputFile;
    int gpuIndex;
    std::string format;
    CommandLineParser cli;
    cli.parse(argc, argv);

    auto a = halcodec::Decoder::Create("nvdec");
    if (!a) {
        std::cerr << "Fail to create decoder" << std::endl;
        return -1;
    }

    a->Initialize(cli.getInputFile());

    std::ofstream fpout(cli.getOutputFile(), std::ios::out|std::ios::binary);
    if (!fpout) {
        std::cerr << "unable to open output file" << std::endl;
    }
    int n_dec = 0;
    int total_frames = 0;
    do {
        n_dec = a->FillinFrame();
        total_frames += n_dec;

        for (int i = 0; i < n_dec; i++) {
            int size;
            auto data = a->GetFrame(&size);
            fpout.write(reinterpret_cast<char *>(data), size);
            a->ReleaseFrame(&data);
        }

    } while (n_dec > 0);

    fpout.close();
    a->Finalize();

    std::cout << "Decode total frames: "<< total_frames << std::endl;

    return 0;
}