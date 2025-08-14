#include "nvdecoder.h"
#include "nvdevice.h"


namespace halcodec {
namespace nvenc {

// NVDecoder::NVDecoder() {
//     // device_ = ;
// }

void NVDecoder::Initialize() {

}

void NVDecoder::FillinFrame(const std::vector<uint8_t>& rawData) {

}

void NVDecoder::Finalize() {
}

std::string NVDecoder::getName() const {
    return "";
}

void NVDecoder::GetFrame() {
}

/* avoid drop symbol while link */
__attribute__((section(".init_array"))) void (*registered_init)() = []() {
    NVDecoder::Register();
};

} // namespace layers
} // namespace nvenc
