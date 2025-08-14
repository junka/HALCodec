#include <cstdint>
#include <iostream>

#include "parse_cli.h"

#include "nvdevice.h"
#include "nvdecoder.h"

#include "decoder.h"

using namespace halcodec::nvenc;

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

int main(int argc, char *argv[]) {
    
    std::string inputFile;
    std::string outputFile;
    int gpuIndex;
    std::string format;
    CommandLineParser cli;
    cli.parse(argc, argv);

    // halcodec::nvenc::NVDevice device(cli.getGpuIndex());

    // auto a = halcodec::Decoder::Create("nvdec");
    



    return 0;
}