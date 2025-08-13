#include <cstdint>
#include <iostream>

#include "nvdevice.h"

int main() {
    halcodec::nvenc::NVDevice device(0);
    device.showDecoderCapability();
    return 0;
}