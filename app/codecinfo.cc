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
// codecinfo -b <backend> --encode-caps <codec> [--json]
//
// The structured side of capability introspection: one backend, one codec, the
// entries a test gate matches on. JSON is one single line with a fixed field
// order per entry, which is what the cmake gate in halcodec_test_prelude.cmake
// regex-parses; keep the order stable if fields are ever added.
//
// `formatsKey` names the list field in the JSON, and `arrow` the way the human
// line reads it: outputFormats with -> for a decode, whose load-bearing answer is
// what comes out, inputFormats with <- for an encode, whose is what goes in.
template <typename Entry, typename Query>
static int ShowCaps(const std::string& backend, const std::string& codec, bool json,
                    const char* formatsKey, const char* arrow, Query query) {
    auto provider = halcodec::CapabilityProvider::Create(backend);
    if (!provider) {
        std::cerr << "codecinfo: no capability provider for backend '"
                  << backend << "'" << std::endl;
        return 2;
    }
    std::vector<Entry> entries;
    auto status = query(*provider, entries);
    const bool available =
        status == halcodec::CapabilityProvider::CapsStatus::Available;
    if (json) {
        std::cout << "{\"backend\":\"" << backend << "\",\"codec\":\"" << codec
                  << "\",\"status\":\"" << (available ? "available" : "unknown")
                  << "\",\"entries\":[";
        for (size_t i = 0; i < entries.size(); i++) {
            const auto& e = entries[i];
            std::cout << (i ? "," : "") << "{\"profile\":\"" << e.profile
                      << "\",\"bitDepth\":" << e.bitDepth << ",\"chroma\":" << e.chroma
                      << ",\"" << formatsKey << "\":[";
            for (size_t j = 0; j < e.formats().size(); j++) {
                std::cout << (j ? "," : "")
                          << "\"" << halcodec::Name(e.formats()[j]) << "\"";
            }
            std::cout << "]}";
        }
        std::cout << "]}" << std::endl;
    } else {
        std::cout << "== backend: " << backend << "  codec: " << codec << " ==" << std::endl;
        if (!available) {
            std::cout << "  (backend cannot answer for this codec)" << std::endl;
            return 0;
        }
        if (entries.empty()) {
            std::cout << "  (no support claimed)" << std::endl;
            return 0;
        }
        for (const auto& e : entries) {
            std::cout << "  " << e.profile << " (depth " << e.bitDepth
                      << ", chroma " << e.chroma << ") " << arrow << " ";
            for (size_t j = 0; j < e.formats().size(); j++) {
                std::cout << (j ? "/" : "")
                          << halcodec::Name(e.formats()[j]);
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

    // Structured single-codec queries take the whole run over.
    using Caps = halcodec::CapabilityProvider;
    if (!cli.getDecodeCapsCodec().empty() || !cli.getEncodeCapsCodec().empty()) {
        if (cli.getBackend().empty()) {
            std::cerr << "codecinfo: --decode-caps/--encode-caps needs -b <backend>"
                      << std::endl;
            return 1;
        }
        const bool encode = !cli.getEncodeCapsCodec().empty();
        const std::string& codec =
            encode ? cli.getEncodeCapsCodec() : cli.getDecodeCapsCodec();
        if (encode) {
            return ShowCaps<Caps::EncodeCapsEntry>(
                cli.getBackend(), codec, cli.getJsonOut(), "inputFormats", "<-",
                [&](Caps& p, std::vector<Caps::EncodeCapsEntry>& e) {
                    return p.queryEncoderCaps(codec, e);
                });
        }
        return ShowCaps<Caps::DecodeCapsEntry>(
            cli.getBackend(), codec, cli.getJsonOut(), "outputFormats", "->",
            [&](Caps& p, std::vector<Caps::DecodeCapsEntry>& e) {
                return p.queryDecoderCaps(codec, e);
            });
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