#include <cstdint>
#include <iostream>

#include "device.h"

int main() {
    halcodec::Device::ShowDevices();
    auto dev = halcodec::Device::Create("nvidia");
    if (!dev) {
        std::cout << "unable to create device" << std::endl;
        return 1;
    }
    int num = dev->getNumDevices();
    for (int i = 0; i < num; i++) {
        dev->createCtx(i);
        std::cout << dev->getDeviceIdx() << ": " << dev->getDeviceName() << std::endl;
        dev->showDecoderCapability();
        std::cout << std::endl;
        dev->showEncoderCapability();
    }
    return 0;
}