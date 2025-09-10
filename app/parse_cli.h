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
                ("input,i", po::value<std::string>(&inputFile_)->required(), "Input file")
                ("output,o", po::value<std::string>(&outputFile_), "Output file (optional, defaults to input file name with format extension)")
                ("gpu", po::value<int>(&gpuIndex_)->default_value(0), "GPU index")
                ("format,f", po::value<std::string>(&format_)->default_value("yuv"),
                 "Output planar format: yuv/y/rbg/bgr/bgri/rgbi for nvjpeg decoding")
                ("colorspace,c", po::value<std::string>(&cs_)->default_value("420"), "Color sapce: 420/444/410");

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
            if (!vm.count("output") && vm.count("input")) {
                std::string inputFileName = inputFile_;
                size_t lastDot = inputFile_.find_last_of('.');
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

    const std::string& getInputFile() const { return inputFile_; }
    const std::string& getOutputFile() const { return outputFile_; }
    int getGpuIndex() const { return gpuIndex_; }
    const std::string& getFormat() const { return format_; }
    const std::string& getInputFormat() const { return input_format_; }
    const std::string& getColorSpace() const { return cs_; }

private:
    std::string inputFile_;
    std::string outputFile_;
    int gpuIndex_;
    std::string format_;
    std::string input_format_;
    std::string cs_;
};

#endif // PARSE_CLI_H
