#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "parse_cli.h"

#include "capability.h"
#include "plugin_loader.h"
#include "registry.h"

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    // Load vendor backends (nvenc_layers, qsv_layers, ...) via dlopen. Backends
    // whose driver libs are missing are skipped, so this runs on any machine.
    halcodec::LoadBackends();

    // Resolve providers: a single one from -b, or every registered one.
    std::vector<std::string> backends;
    if (!cli.getBackend().empty()) {
        backends.push_back(cli.getBackend());
    } else {
        backends = halcodec::Registry<halcodec::CapabilityProvider>::Names();
    }

    if (backends.empty()) {
        std::cout << "no capability providers registered" << std::endl;
        return 0;
    }

    for (const auto& backend : backends) {
        std::cout << "== backend: " << backend << " ==" << std::endl;
        auto provider = halcodec::CapabilityProvider::Create(backend);
        if (!provider) {
            std::cout << "  unable to create capability provider" << std::endl;
            continue;
        }

        auto devices = provider->getDeviceNames();
        if (devices.empty()) {
            std::cout << "  (no device enumeration available)" << std::endl;
        } else {
            for (size_t i = 0; i < devices.size(); i++) {
                std::cout << "  device " << i << ": " << devices[i] << std::endl;
            }
        }

        std::cout << "  decoder capabilities:" << std::endl;
        provider->showDecoderCapability();
        std::cout << "  encoder capabilities:" << std::endl;
        provider->showEncoderCapability();
        std::cout << std::endl;
    }
    return 0;
}