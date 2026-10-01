// Tests for the process-wide factory registry the backends self-register into.
// The registry's storage lives in halcodec_core behind an exported accessor
// because a dlopen'd backend .so and the executable must agree on one map; these
// cases lock the behaviour that design exists for.

#include "registry.h"

#include <memory>
#include <string>
#include <vector>

#include "capability.h"
#include "decoder.h"
#include "encoder.h"
#include "test_check.h"

namespace {

class ProbeDecoder : public halcodec::Decoder {
public:
    explicit ProbeDecoder(std::string name) : name_(std::move(name)) {}
    std::string getName() const override { return name_; }

private:
    std::string name_;
};

class ProbeEncoder : public halcodec::Encoder {
public:
    std::string getName() const override { return "probe-enc"; }
};

// Registers through the macro the backends use, at static-initialisation time.
HALCODEC_CONNECT(Decoder, ctest_probe, ProbeDecoder, "ctest_probe");

bool Contains(const std::vector<std::string>& names, const std::string& needle) {
    for (const std::string& n : names) {
        if (n == needle) {
            return true;
        }
    }
    return false;
}

void UnknownNameCreatesNothing() {
    CHECK_EQ(halcodec::Decoder::Create("definitely-not-a-backend"), nullptr);
}

void MacroRegistrationIsAvailableWithoutAnySetupCall() {
    std::unique_ptr<halcodec::Decoder> dec =
        halcodec::Decoder::Create("ctest_probe");
    CHECK(dec != nullptr);
    if (dec) {
        CHECK_EQ(dec->getName(), std::string("ctest_probe"));
    }
    CHECK(Contains(halcodec::Registry<halcodec::Decoder>::Names(),
                   "ctest_probe"));
}

// Every Create runs the registered factory again: backends are per-stream
// objects, not singletons, so two streams must not share one decoder.
void CreateReturnsIndependentInstances() {
    std::unique_ptr<halcodec::Decoder> a = halcodec::Decoder::Create("ctest_probe");
    std::unique_ptr<halcodec::Decoder> b = halcodec::Decoder::Create("ctest_probe");
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(a.get() != b.get());
}

// The storage is keyed by the base class as well as the name, so a decoder and
// an encoder may both be called "same" and the same name can be reused for a
// second decoder without disturbing the encoder.
void NamesCannotCollideAcrossBases() {
    halcodec::Registry<halcodec::Decoder>::Register(
        "shared-name", []() {
            return std::make_unique<ProbeDecoder>("shared-decoder");
        });
    halcodec::Registry<halcodec::Encoder>::Register(
        "shared-name", []() { return std::make_unique<ProbeEncoder>(); });

    std::unique_ptr<halcodec::Decoder> dec =
        halcodec::Decoder::Create("shared-name");
    std::unique_ptr<halcodec::Encoder> enc =
        halcodec::Registry<halcodec::Encoder>::Create("shared-name");
    CHECK(dec != nullptr);
    CHECK(enc != nullptr);
    if (dec) {
        CHECK_EQ(dec->getName(), std::string("shared-decoder"));
    }
    if (enc) {
        CHECK_EQ(enc->getName(), std::string("probe-enc"));
    }
    // A third base type that was never registered under that name stays empty.
    CHECK_EQ(halcodec::Registry<halcodec::CapabilityProvider>::Create(
                 "shared-name"),
             nullptr);
}

// Re-registering a name replaces the factory, which is what lets a build with
// two backends of the same name resolve deterministically to the last loaded.
void LaterRegistrationReplacesTheFactory() {
    halcodec::Registry<halcodec::Decoder>::Register(
        "replaced", []() { return std::make_unique<ProbeDecoder>("first"); });
    halcodec::Registry<halcodec::Decoder>::Register(
        "replaced", []() { return std::make_unique<ProbeDecoder>("second"); });
    std::unique_ptr<halcodec::Decoder> dec =
        halcodec::Decoder::Create("replaced");
    CHECK(dec != nullptr);
    if (dec) {
        CHECK_EQ(dec->getName(), std::string("second"));
    }
}

// A backend that is only loaded later (dlopen) must show up in the same map the
// app already looked at; the accessor returning one process-wide map is what
// makes that true, so ask for it twice.
void StorageIsTheSharedProcessWideMap() {
    auto& first = halcodec::detail::registry_storage(typeid(halcodec::Decoder).name());
    auto& second = halcodec::detail::registry_storage(typeid(halcodec::Decoder).name());
    CHECK_EQ(&first, &second);
    // A different base class gets a different key, hence a different map.
    auto& other = halcodec::detail::registry_storage(typeid(halcodec::Encoder).name());
    CHECK(&first != &other);
}

} // namespace

int main() {
    UnknownNameCreatesNothing();
    MacroRegistrationIsAvailableWithoutAnySetupCall();
    CreateReturnsIndependentInstances();
    NamesCannotCollideAcrossBases();
    LaterRegistrationReplacesTheFactory();
    StorageIsTheSharedProcessWideMap();
    return haltest::finish("registry_test");
}
