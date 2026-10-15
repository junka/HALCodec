#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "parse_cli.h"

#include "capability.h"
#include "frame.h"
#include "plugin_loader.h"
#include "registry.h"

// codecinfo -b <backend> --decode-caps <codec> [--json]
//
// The structured side of capability introspection: one backend, one codec, the
// entries a test gate matches on. JSON is one single line with a fixed field
// order per entry, which is what the cmake gate in halcodec_test_prelude.cmake
// regex-parses; keep the order stable if fields are ever added.
static int ShowDecodeCaps(const std::string& backend, const std::string& codec,
                          bool json) {
    auto provider = halcodec::CapabilityProvider::Create(backend);
    if (!provider) {
        std::cerr << "codecinfo: no capability provider for backend '"
                  << backend << "'" << std::endl;
        return 2;
    }
    std::vector<halcodec::CapabilityProvider::DecodeCapsEntry> entries;
    auto status = provider->queryDecoderCaps(codec, entries);
    if (json) {
        std::cout << "{\"backend\":\"" << backend << "\",\"codec\":\"" << codec
                  << "\",\"status\":\""
                  << (status == halcodec::CapabilityProvider::CapsStatus::Available
                          ? "available"
                          : "unknown")
                  << "\",\"entries\":[";
        for (size_t i = 0; i < entries.size(); i++) {
            const auto& e = entries[i];
            std::cout << (i ? "," : "") << "{\"profile\":\"" << e.profile
                      << "\",\"bitDepth\":" << e.bitDepth
                      << ",\"chroma\":" << e.chroma << ",\"outputFormats\":[";
            for (size_t j = 0; j < e.outputFormats.size(); j++) {
                std::cout << (j ? "," : "")
                          << "\"" << halcodec::Name(e.outputFormats[j]) << "\"";
            }
            std::cout << "]}";
        }
        std::cout << "]}" << std::endl;
    } else {
        std::cout << "== backend: " << backend << "  codec: " << codec << " ==" << std::endl;
        if (status != halcodec::CapabilityProvider::CapsStatus::Available) {
            std::cout << "  (backend cannot answer for this codec)" << std::endl;
            return 0;
        }
        if (entries.empty()) {
            std::cout << "  (no decode support claimed)" << std::endl;
            return 0;
        }
        for (const auto& e : entries) {
            std::cout << "  " << e.profile << " (depth " << e.bitDepth
                      << ", chroma " << e.chroma << ") -> ";
            for (size_t j = 0; j < e.outputFormats.size(); j++) {
                std::cout << (j ? "/" : "")
                          << halcodec::Name(e.outputFormats[j]);
            }
            std::cout << std::endl;
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    CommandLineParser cli;
    cli.parse(argc, argv);

    // Load vendor backends (nvenc_layers, qsv_layers, ...) via dlopen. Backends
    // whose driver libs are missing are skipped, so this runs on any machine.
    // Both the structured query and the dump below need the registry filled.
    halcodec::LoadBackends();

    // Structured single-codec query takes the whole run over.
    if (!cli.getDecodeCapsCodec().empty()) {
        if (cli.getBackend().empty()) {
            std::cerr << "codecinfo: --decode-caps needs -b <backend>" << std::endl;
            return 1;
        }
        return ShowDecodeCaps(cli.getBackend(), cli.getDecodeCapsCodec(),
                              cli.getJsonOut());
    }

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